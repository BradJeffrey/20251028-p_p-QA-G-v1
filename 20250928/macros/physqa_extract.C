#include <TFile.h>
#include <TH1.h>
#include <TH2.h>
#include <TF1.h>
#include <TMath.h>
#include <TCanvas.h>
#include <TSystem.h>
#include <TGraphErrors.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// ------------------------ small utilities ------------------------
struct FileMeta { int run=-1, seg=-1; std::string base; };
static FileMeta parse_meta_simple(const std::string& path) {
  FileMeta m; size_t p = path.find_last_of('/'); m.base = (p==std::string::npos)? path : path.substr(p+1);
  // run number after "run"
  size_t rpos = m.base.find("run");
  if (rpos != std::string::npos) {
    size_t i = rpos + 3;
    std::string digs;
    while (i<m.base.size() && std::isdigit(m.base[i])) { digs.push_back(m.base[i]); ++i; }
    if (!digs.empty()) m.run = std::stoi(digs);
  }
  // segment: trailing _### or -###
  for (int i=(int)m.base.size()-1; i>=0; --i) {
    if (std::isdigit(m.base[i])) {
      int j=i; while (j>=0 && std::isdigit(m.base[j])) --j;
      if (j>=0 && (m.base[j]=='_' || m.base[j]=='-')) { m.seg = std::stoi(m.base.substr(j+1, i-j)); break; }
    }
  }
  return m;
}

static TH1* H1(TFile* f, const std::string& n){ return dynamic_cast<TH1*>(f->Get(n.c_str())); }
static TH2* H2(TFile* f, const std::string& n){ return dynamic_cast<TH2*>(f->Get(n.c_str())); }

static double hcounts(TH1* h){ return h? h->Integral(1,h->GetNbinsX()) : 0.0; }

static double quantile_x(TH1* h, double p){
  if (!h) return std::numeric_limits<double>::quiet_NaN();
  p = std::clamp(p, 0.0, 1.0);
  double tot = hcounts(h); if (tot<=0) return std::numeric_limits<double>::quiet_NaN();
  double acc=0.0;
  for (int i=1;i<=h->GetNbinsX();++i){
    acc += h->GetBinContent(i);
    if (acc >= p*tot) return h->GetXaxis()->GetBinCenter(i);
  }
  return h->GetXaxis()->GetBinCenter(h->GetNbinsX());
}

// ------------------------ physics helpers ------------------------
// Landau MPV fit in a robust window [q10,q90]
static std::pair<double,double> landau_mpv(TH1* h){
  if (!h || hcounts(h)<=0) return {NAN,0};
  double x10 = quantile_x(h, 0.10);
  double x90 = quantile_x(h, 0.90);
  if (!std::isfinite(x10) || !std::isfinite(x90) || x90<=x10) { x10=h->GetXaxis()->GetXmin(); x90=h->GetXaxis()->GetXmax(); }
  int ib = h->GetMaximumBin();
  double xpk = h->GetXaxis()->GetBinCenter(ib);
  double sigma_guess = (x90-x10)/6.0;
  TF1 f("f_land","landau", x10, x90);
  f.SetParameters(h->GetMaximum(), xpk, std::max(1e-3, sigma_guess));
  int fr = h->Fit(&f,"QS0");
  if (fr!=0) return {NAN,0};
  return {f.GetParameter(1), f.GetParError(1)}; // MPV, error
}

// First harmonic amplitude R1 on a periodic axis: 0 → uniform
static std::pair<double,double> fourier_R1(TH1* h){
  if (!h || hcounts(h)<=0) return {NAN,0};
  double xmin = h->GetXaxis()->GetXmin();
  double xmax = h->GetXaxis()->GetXmax();
  double sumw=0, c=0, s=0;
  for (int i=1;i<=h->GetNbinsX();++i){
    double w = h->GetBinContent(i); if (w<=0) continue;
    double x = h->GetXaxis()->GetBinCenter(i);
    double phi = 2*TMath::Pi() * (x - xmin) / (xmax - xmin + 1e-12);
    sumw += w; c += w*std::cos(phi); s += w*std::sin(phi);
  }
  if (sumw<=0) return {NAN,0};
  double R1 = std::sqrt(c*c + s*s) / sumw;
  double err = std::sqrt(std::max(0.0, 1.0 - R1*R1) / sumw); // rough
  return {R1, err};
}

// MVTX per-layer dead/hot chip fractions from TH2 nhits_stave_chip_layer{L}
static std::tuple<double,double,double,double> mvtx_chip_health(TH2* h2, double dead_frac=0.05, double hot_mult=5.0){
  if (!h2) return {NAN,NAN,0,0};
  int nx = h2->GetNbinsX(), ny = h2->GetNbinsY();
  std::vector<double> occ; occ.reserve(nx*ny);
  double total_counts=0;
  for (int ix=1; ix<=nx; ++ix){
    for (int iy=1; iy<=ny; ++iy){
      double v = h2->GetBinContent(ix,iy);
      occ.push_back(v); total_counts += v;
    }
  }
  if (occ.empty()) return {NAN,NAN,0,0};
  // median
  size_t n=occ.size(); std::nth_element(occ.begin(), occ.begin()+n/2, occ.end());
  double med = occ[n/2]; if (n%2==0){ std::nth_element(occ.begin(), occ.begin()+n/2-1, occ.end()); med=0.5*(med+occ[n/2-1]); }
  if (!(med>0)) return {NAN,NAN,(double)n,total_counts};
  int dead=0, hot=0;
  for (double v : occ){ if (!std::isfinite(v)) continue; if (v < dead_frac*med) dead++; if (v > hot_mult*med) hot++; }
  double deadfrac = (double)dead / n;
  double hotfrac  = (double)hot  / n;
  return {deadfrac, hotfrac, (double)n, total_counts};
}

// Sector ADC uniformity chi2/dof by summing sec0..23, rings 0..2
static double tpc_sector_adc_chi2red(TFile* f){
  std::vector<double> sec(24,0.0);
  int filled=0;
  for (int isec=0; isec<24; ++isec){
    double sum=0;
    for (int r=0; r<3; ++r){
      std::string hn = "h_TpcRawHitQA_adc_sec"+std::to_string(isec)+"_R"+std::to_string(r);
      TH1* h = H1(f, hn);
      if (h) sum += h->Integral(1,h->GetNbinsX());
    }
    if (sum>0) { sec[isec]=sum; filled++; }
  }
  if (filled<8) return std::numeric_limits<double>::quiet_NaN();
  double tot=0; for (double v:sec) tot+=v;
  double exp = tot / 24.0;
  double chi2=0; for (double v: sec){ if (exp>0) chi2 += (v-exp)*(v-exp)/exp; }
  return chi2 / 23.0; // dof = 24-1
}

// Average Gaussian mean of laser time-sample lines (R1+R2) for a side
static std::tuple<double,double,double> tpc_laser_side_mu(TFile* f, const char* side){
  // patterns: h_TpcLaserQA_sample_R{1,2}_{North|South}_{0..11}
  double num=0, den=0, wsum=0;
  for (int R=1; R<=2; ++R){
    for (int i=0;i<12;++i){
      std::string hn = std::string("h_TpcLaserQA_sample_R")+std::to_string(R)+"_"+side+"_"+std::to_string(i);
      TH1* h = H1(f, hn);
      if (!h || hcounts(h)<=0) continue;
      double x10 = quantile_x(h, 0.10), x90 = quantile_x(h, 0.90);
      if (!std::isfinite(x10) || !std::isfinite(x90) || x90<=x10){ x10=h->GetXaxis()->GetXmin(); x90=h->GetXaxis()->GetXmax(); }
      TF1 g("g","gaus", x10, x90);
      int fr = h->Fit(&g,"QS0");
      if (fr!=0) continue;
      double w = hcounts(h);
      num += w * g.GetParameter(1);
      den += w;
      wsum += w * g.GetParError(1) * g.GetParError(1);
    }
  }
  if (den<=0) return {NAN,0,0};
  double mu = num/den;
  double emu = std::sqrt(std::max(0.0, wsum)) / std::max(1.0, den);
  return {mu, emu, den};
}

// Ring-slope (0..2) averaged across sides for either phisize_ or zsize_
static std::tuple<double,double> tpc_size_ring_slope_avg(TFile* f, const char* base){ // base = "phisize" or "zsize"
  auto slope_for_side = [&](int side)->std::pair<double,double>{
    std::vector<double> y; std::vector<double> x; y.reserve(3); x.reserve(3);
    for (int r=0;r<3;++r){
      std::string hn = std::string("h_TpcClusterQA_")+base+"_side"+std::to_string(side)+"_"+std::to_string(r);
      TH1* h=H1(f, hn);
      if (!h || hcounts(h)<=0) return std::make_pair(NAN,0.0);
      x.push_back(r); y.push_back(h->GetMean());
    }
    // simple unweighted linear fit (tiny sample)
    double Sx=0,Sy=0,Sxx=0,Sxy=0; int n=(int)x.size();
    for (int i=0;i<n;++i){ Sx+=x[i]; Sy+=y[i]; Sxx+=x[i]*x[i]; Sxy+=x[i]*y[i]; }
    double D = n*Sxx - Sx*Sx; if (D<=0) return std::make_pair(NAN,0.0);
    double b = (n*Sxy - Sx*Sy) / D;
    return std::make_pair(b, (double)n);
  };
  auto a = slope_for_side(0), b = slope_for_side(1);
  if (!std::isfinite(a.first) && !std::isfinite(b.first)) return {NAN,0};
  double n=0, s=0;
  if (std::isfinite(a.first)) { s+=a.first; n++; }
  if (std::isfinite(b.first)) { s+=b.first; n++; }
  return { (n>0? s/n : NAN), n };
}

// Mean of rphi_error_* or z_error_* over rings 0..2
static std::tuple<double,double> tpc_error_mean(TFile* f, const char* which){ // "rphi_error" or "z_error"
  double sum=0; int n=0;
  for (int r=0;r<3;++r){
    std::string hn = std::string("h_TpcClusterQA_")+which+"_"+std::to_string(r);
    TH1* h=H1(f, hn);
    if (!h || hcounts(h)<=0) continue;
    sum += h->GetMean(); n++;
  }
  if (n==0) return {NAN,0};
  return {sum/n, n};
}

// ------------------------ main extractor ------------------------
struct Out { std::ofstream csv; std::unique_ptr<TGraphErrors> gr; };

void physqa_extract(const char* filelist="lists/files.txt",
                    double mvtx_dead_frac=0.05, double mvtx_hot_mult=5.0)
{
  gSystem->mkdir("out", kTRUE);

  // Prepare outputs
  std::map<std::string, Out> outs;
  auto openOut = [&](const std::string& name){
    if (outs.count(name)) return;
    outs[name].csv.open(std::string("out/metrics_")+name+".csv");
    outs[name].csv<<"run,segment,file,value,error,weight\n";
    outs[name].gr.reset(new TGraphErrors());
    outs[name].gr->SetName((std::string("gr_")+name).c_str());
  };

  // Register all physics metrics
  const char* METRICS[] = {
    "intt_adc_landau_mpv",
    "intt_bco_mod_r1",
    "intt_sensor_occupancy_median",
    "mvtx_deadchip_frac_l0","mvtx_hotchip_frac_l0",
    "mvtx_deadchip_frac_l1","mvtx_hotchip_frac_l1",
    "mvtx_deadchip_frac_l2","mvtx_hotchip_frac_l2",
    "tpc_laser_time_mean_north","tpc_laser_time_mean_south","tpc_laser_time_delta_NS",
    "tpc_phisize_ring_slope_avg","tpc_zsize_ring_slope_avg",
    "tpc_resolution_rphi_mean","tpc_resolution_z_mean",
    "tpc_sector_adc_uniform_chi2"
  };
  for (auto* m : METRICS) openOut(m);

  // Read file list
  std::ifstream in(filelist);
  if (!in){ std::cerr<<"[ERROR] cannot open "<<filelist<<"\n"; return; }

  std::string path;
  while (std::getline(in, path)) {
    if (path.empty()) continue;
    auto meta = parse_meta_simple(path);
    std::unique_ptr<TFile> f(TFile::Open(path.c_str(),"READ"));
    if (!f || f->IsZombie()){ std::cerr<<"[WARN] cannot open "<<path<<"\n"; continue; }

    // ---------- INTT ----------
    { // ADC Landau MPV
      TH1* h = H1(f.get(),"h_InttRawHitQA_adc");
      auto pr = landau_mpv(h);
      double val=pr.first, err=pr.second, w=hcounts(h);
      outs["intt_adc_landau_mpv"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<val<<","<<err<<","<<w<<"\n";
      int n=outs["intt_adc_landau_mpv"].gr->GetN(); outs["intt_adc_landau_mpv"].gr->SetPoint(n, meta.run, val); outs["intt_adc_landau_mpv"].gr->SetPointError(n, 0, err);
    }
    { // BCO R1 amplitude (periodic non-uniformity)
      TH1* h = H1(f.get(),"h_InttRawHitQA_bco");
      auto pr = fourier_R1(h);
      double val=pr.first, err=pr.second, w=hcounts(h);
      outs["intt_bco_mod_r1"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<val<<","<<err<<","<<w<<"\n";
      int n=outs["intt_bco_mod_r1"].gr->GetN(); outs["intt_bco_mod_r1"].gr->SetPoint(n, meta.run, val); outs["intt_bco_mod_r1"].gr->SetPointError(n,0,err);
    }
    { // sensor occupancy (median across sensors)
      TH1* h = H1(f.get(),"h_InttClusterQA_sensorOccupancy");
      double med = quantile_x(h, 0.50);
      double w = hcounts(h);
      outs["intt_sensor_occupancy_median"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<med<<",0,"<<w<<"\n";
      int n=outs["intt_sensor_occupancy_median"].gr->GetN(); outs["intt_sensor_occupancy_median"].gr->SetPoint(n, meta.run, med);
    }

    // ---------- MVTX ----------
    for (int L=0; L<=2; ++L){
      std::string hn = "h_MvtxRawHitQA_nhits_stave_chip_layer"+std::to_string(L);
      TH2* h2 = H2(f.get(), hn);
      auto tt = mvtx_chip_health(h2, mvtx_dead_frac, mvtx_hot_mult);
      double deadfrac=std::get<0>(tt), hotfrac=std::get<1>(tt), nchips=std::get<2>(tt), w=std::get<3>(tt);
      // binomial error approx
      double ed = std::isfinite(deadfrac) ? std::sqrt(std::max(0.0, deadfrac*(1-deadfrac)/std::max(1.0,nchips))) : 0;
      double eh = std::isfinite(hotfrac)  ? std::sqrt(std::max(0.0, hotfrac *(1-hotfrac) /std::max(1.0,nchips))) : 0;
      std::string dname = "mvtx_deadchip_frac_l"+std::to_string(L);
      std::string hname = "mvtx_hotchip_frac_l"+std::to_string(L);
      outs[dname].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<deadfrac<<","<<ed<<","<<w<<"\n";
      outs[hname].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<hotfrac<<","<<eh<<","<<w<<"\n";
      int n1=outs[dname].gr->GetN(); outs[dname].gr->SetPoint(n1, meta.run, deadfrac); outs[dname].gr->SetPointError(n1,0,ed);
      int n2=outs[hname].gr->GetN(); outs[hname].gr->SetPoint(n2, meta.run, hotfrac);  outs[hname].gr->SetPointError(n2,0,eh);
    }

    // ---------- TPC laser ----------
    auto nmu = tpc_laser_side_mu(f.get(),"North");
    auto smu = tpc_laser_side_mu(f.get(),"South");
    if (std::isfinite(std::get<0>(nmu))) {
      outs["tpc_laser_time_mean_north"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<std::get<0>(nmu)<<","<<std::get<1>(nmu)<<","<<std::get<2>(nmu)<<"\n";
      int n=outs["tpc_laser_time_mean_north"].gr->GetN(); outs["tpc_laser_time_mean_north"].gr->SetPoint(n, meta.run, std::get<0>(nmu)); outs["tpc_laser_time_mean_north"].gr->SetPointError(n,0,std::get<1>(nmu));
    }
    if (std::isfinite(std::get<0>(smu))) {
      outs["tpc_laser_time_mean_south"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<std::get<0>(smu)<<","<<std::get<1>(smu)<<","<<std::get<2>(smu)<<"\n";
      int n=outs["tpc_laser_time_mean_south"].gr->GetN(); outs["tpc_laser_time_mean_south"].gr->SetPoint(n, meta.run, std::get<0>(smu)); outs["tpc_laser_time_mean_south"].gr->SetPointError(n,0,std::get<1>(smu));
    }
    if (std::isfinite(std::get<0>(nmu)) && std::isfinite(std::get<0>(smu))) {
      double d = std::get<0>(smu) - std::get<0>(nmu);
      double ed = std::hypot(std::get<1>(smu), std::get<1>(nmu));
      double w  = std::get<2>(smu) + std::get<2>(nmu);
      outs["tpc_laser_time_delta_NS"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<d<<","<<ed<<","<<w<<"\n";
      int n=outs["tpc_laser_time_delta_NS"].gr->GetN(); outs["tpc_laser_time_delta_NS"].gr->SetPoint(n, meta.run, d); outs["tpc_laser_time_delta_NS"].gr->SetPointError(n,0,ed);
    }

    // ---------- TPC size slopes & resolution ----------
    {
      auto sphi = tpc_size_ring_slope_avg(f.get(),"phisize");
      auto sz   = tpc_size_ring_slope_avg(f.get(),"zsize");
      if (std::isfinite(std::get<0>(sphi))) {
        outs["tpc_phisize_ring_slope_avg"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<std::get<0>(sphi)<<",0,"<<std::get<1>(sphi)<<"\n";
        int n=outs["tpc_phisize_ring_slope_avg"].gr->GetN(); outs["tpc_phisize_ring_slope_avg"].gr->SetPoint(n, meta.run, std::get<0>(sphi));
      }
      if (std::isfinite(std::get<0>(sz))) {
        outs["tpc_zsize_ring_slope_avg"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<std::get<0>(sz)<<",0,"<<std::get<1>(sz)<<"\n";
        int n=outs["tpc_zsize_ring_slope_avg"].gr->GetN(); outs["tpc_zsize_ring_slope_avg"].gr->SetPoint(n, meta.run, std::get<0>(sz));
      }
    }
    {
      auto er = tpc_error_mean(f.get(),"rphi_error");
      auto ez = tpc_error_mean(f.get(),"z_error");
      if (std::isfinite(std::get<0>(er))) {
        outs["tpc_resolution_rphi_mean"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<std::get<0>(er)<<",0,"<<std::get<1>(er)<<"\n";
        int n=outs["tpc_resolution_rphi_mean"].gr->GetN(); outs["tpc_resolution_rphi_mean"].gr->SetPoint(n, meta.run, std::get<0>(er));
      }
      if (std::isfinite(std::get<0>(ez))) {
        outs["tpc_resolution_z_mean"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<std::get<0>(ez)<<",0,"<<std::get<1>(ez)<<"\n";
        int n=outs["tpc_resolution_z_mean"].gr->GetN(); outs["tpc_resolution_z_mean"].gr->SetPoint(n, meta.run, std::get<0>(ez));
      }
    }

    // ---------- TPC sector uniformity ----------
    {
      double chi2r = tpc_sector_adc_chi2red(f.get());
      double w = 1.0; // placeholder
      outs["tpc_sector_adc_uniform_chi2"].csv<<meta.run<<","<<meta.seg<<","<<path<<","<<chi2r<<",0,"<<w<<"\n";
      int n=outs["tpc_sector_adc_uniform_chi2"].gr->GetN(); outs["tpc_sector_adc_uniform_chi2"].gr->SetPoint(n, meta.run, chi2r);
    }
  }

  // quick one‑plot per metric (optional, like your other extractors)
  for (auto& kv : outs) {
    auto& name = kv.first; auto& gr = kv.second.gr;
    TCanvas c(("c_phys_"+name).c_str(), name.c_str(), 900, 600);
    gr->SetTitle((name+";Run;"+name).c_str());
    gr->Draw("AP");
    c.SaveAs((std::string("out/metric_")+name+".png").c_str());
    c.SaveAs((std::string("out/metric_")+name+".pdf").c_str());
  }
  std::cout<<"[DONE] physics metrics written to out/metrics_*.csv and plots.\n";
}
