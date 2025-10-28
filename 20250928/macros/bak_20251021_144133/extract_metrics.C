#include <TFile.h>
#include <TH1.h>
#include <TSystem.h>
#include <TCanvas.h>
#include <TGraphErrors.h>
#include <TF1.h>
#include <TAxis.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// ---- Helpers for advanced histogram metrics ----
static double clamp01(double p){ return p<0?0:(p>1?1:p); }

static double hist_entries(TH1* h) {
  // safer than GetEntries() when only bin contents matter
  return h->Integral(1, h->GetNbinsX());
}

static double hist_quantile(TH1* h, double p) {
  p = clamp01(p);
  const int nb = h->GetNbinsX();
  double tot = hist_entries(h);
  if (tot <= 0) return std::numeric_limits<double>::quiet_NaN();
  const double target = p * tot;
  double acc = 0.0;
  for (int i=1;i<=nb;i++){
    acc += h->GetBinContent(i);
    if (acc >= target) {
      // simple bin-center; (optional: interpolate with neighboring bins)
      return h->GetXaxis()->GetBinCenter(i);
    }
  }
  return h->GetXaxis()->GetBinCenter(nb);
}

static std::pair<double,double> hist_trunc_range_by_quantiles(TH1* h, double qlo, double qhi) {
  double xlo = hist_quantile(h, clamp01(qlo));
  double xhi = hist_quantile(h, clamp01(qhi));
  if (xhi < xlo) std::swap(xlo, xhi);
  return {xlo, xhi};
}

static double hist_mean_in_window(TH1* h, double xlo, double xhi) {
  int b1 = h->GetXaxis()->FindBin(xlo);
  int b2 = h->GetXaxis()->FindBin(xhi);
  b1 = std::max(1,b1); b2 = std::min(h->GetNbinsX(), b2);
  double num=0, den=0;
  for (int i=b1;i<=b2;++i) {
    double w = h->GetBinContent(i);
    double x = h->GetXaxis()->GetBinCenter(i);
    num += w*x; den += w;
  }
  if (den<=0) return std::numeric_limits<double>::quiet_NaN();
  return num/den;
}

static std::pair<double,double> phi_uniform_r1(TH1* h) {
  // assumes phi in radians ~ uniform on [-pi,pi] when stable
  const int nb = h->GetNbinsX();
  double sumw=0, csum=0, ssum=0;
  for (int i=1;i<=nb;i++){
    double w = h->GetBinContent(i);
    double x = h->GetXaxis()->GetBinCenter(i); // radians
    sumw += w;
    csum += w * std::cos(x);
    ssum += w * std::sin(x);
  }
  if (sumw<=0) return {std::numeric_limits<double>::quiet_NaN(), 0.0};
  double a = csum/sumw, b = ssum/sumw;
  double R1 = std::sqrt(a*a + b*b); // 0 → uniform
  double err = std::sqrt(std::max(0.0, 1.0 - R1*R1) / sumw); // rough
  return {R1, err};
}

static double chi2_uniform_reduced(TH1* h) {
  const int nb = h->GetNbinsX();
  if (nb<=1) return std::numeric_limits<double>::quiet_NaN();
  double tot = hist_entries(h);
  if (tot<=0) return std::numeric_limits<double>::quiet_NaN();
  double exp = tot / nb;
  if (exp<=0) return std::numeric_limits<double>::quiet_NaN();
  double chi2=0;
  for (int i=1;i<=nb;i++){
    double o = h->GetBinContent(i);
    chi2 += (o-exp)*(o-exp)/exp;
  }
  double dof = nb - 1.0;
  return chi2/dof;
}


struct FileMeta {
  int run=-1, segment=-1;
  std::string base;
};

static FileMeta parse_meta(const std::string& path) {
  FileMeta m;

  // Use only the basename to avoid picking up digits from parent directories (e.g., "20250928")
  size_t p = path.find_last_of('/');
  m.base = (p==std::string::npos)? path : path.substr(p+1);

  // Prefer "run<digits>" in the basename (case-insensitive)
  {
    std::regex r_run("run[_-]?(\\d{5,7})", std::regex_constants::icase);
    std::smatch sm;
    if (std::regex_search(m.base, sm, r_run)) {
      m.run = std::stoi(sm[1]);
    }
  }

  // If still not found, fall back to the LAST 5–7 digit group in the basename
  if (m.run < 0) {
    std::regex r_digits("(\\d{5,7})");
    std::smatch sm;
    std::string s = m.base;
    while (std::regex_search(s, sm, r_digits)) {
      m.run = std::stoi(sm[1]);      // keep last match
      s = sm.suffix().str();
    }
  }

  // Segment patterns in the basename
  {
    // e.g., run66522_003.root  or run66522-12.root
    std::regex r_seg_suffix("[-_](\\d{1,4})\\.[Rr][Oo][Oo][Tt]$");
    std::smatch sm;
    if (std::regex_search(m.base, sm, r_seg_suffix)) {
      m.segment = std::stoi(sm[1]);
    } else {
      // e.g., run66522_seg3 or run66522-Seg12
      std::regex r_seg_tag("[-_][Ss]eg(\\d+)");
      if (std::regex_search(m.base, sm, r_seg_tag)) {
        m.segment = std::stoi(sm[1]);
      }
    }
  }

  return m;
}


struct MetricDef {
  std::string name;
  std::vector<std::string> hnames;  // 1 or 2 names (for asym)
  std::string method;               // mean|rms|sum|maxbin|gauspeak|asym
  double xlow = std::numeric_limits<double>::quiet_NaN();
  double xhigh= std::numeric_limits<double>::quiet_NaN();
  bool hasRange=false;
};

static std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> out; std::stringstream ss(s); std::string item;
  while (std::getline(ss, item, delim)) out.push_back(item);
  return out;
}
static std::string trim(std::string s) {
  auto issp=[](unsigned char c){return std::isspace(c);};
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){return !issp(c);}));
  s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){return !issp(c);}).base(), s.end());
  return s;
}

static bool parse_metrics(const char* conf, std::vector<MetricDef>& defs) {
  std::ifstream in(conf);
  if (!in) { std::cerr << "[ERROR] cannot open metrics conf: " << conf << "\n"; return false; }
  std::string line; int ln=0;
  while (std::getline(in,line)) {
    ++ln; line=trim(line);
    if (line.empty() || line[0]=='#') continue;
    auto toks = split(line, ',');
    for (auto& t: toks) t=trim(t);
    if (toks.size() < 3) { std::cerr<<"[WARN] bad line "<<ln<<": "<<line<<"\n"; continue; }
    MetricDef m;
    m.name   = toks[0];
    auto pair = split(toks[1], '|');
    for (auto& p: pair) m.hnames.push_back(trim(p));
    m.method = toks[2];
    if (toks.size() >= 5) { m.xlow = std::stod(toks[3]); m.xhigh = std::stod(toks[4]); m.hasRange=true; }
    defs.push_back(m);
  }
  return true;
}

static double integral_range(TH1* h, double xlow, double xhigh) {
  int b1 = (xlow<=xhigh)? h->GetXaxis()->FindBin(xlow) : 1;
  int b2 = (xlow<=xhigh)? h->GetXaxis()->FindBin(xhigh) : h->GetNbinsX();
  b1 = std::max(1,b1); b2 = std::min(h->GetNbinsX(), b2);
  return h->Integral(b1,b2);
}

static std::pair<double,double> compute_metric(const MetricDef& d, TFile* f) {
  auto getH = [&](const std::string& n)->TH1*{ return (TH1*)f->Get(n.c_str()); };

  if (d.method=="asym") {
    if (d.hnames.size()!=2) return {std::numeric_limits<double>::quiet_NaN(), 0.0};
    TH1* a=getH(d.hnames[0]); TH1* b=getH(d.hnames[1]);
    if (!a || !b) return {std::numeric_limits<double>::quiet_NaN(), 0.0};
    double A = d.hasRange? integral_range(a,d.xlow,d.xhigh) : a->Integral(1,a->GetNbinsX());
    double B = d.hasRange? integral_range(b,d.xlow,d.xhigh) : b->Integral(1,b->GetNbinsX());
    if (A+B<=0) return {std::numeric_limits<double>::quiet_NaN(), 0.0};
    double y  = (A-B)/(A+B);
    double ey = std::sqrt(std::max(0.0, 1.0 - y*y) / std::max(1.0, A+B));
    return {y, ey};
  }

  if (d.hnames.empty()) return {std::numeric_limits<double>::quiet_NaN(), 0.0};
  TH1* h = getH(d.hnames[0]); if (!h || h->GetEntries()==0) return {std::numeric_limits<double>::quiet_NaN(), 0.0};

  if (d.method=="mean") {
    double y=h->GetMean(); double N=h->GetEntries(); double rms=h->GetRMS();
    double ey = (N>0 && rms==rms) ? rms/std::sqrt(N) : 0.0;
    return {y,ey};
  } else if (d.method=="rms") {
    return {h->GetRMS(), 0.0};
  } else if (d.method=="sum") {
    double y = h->Integral(1,h->GetNbinsX());
    return {y, std::sqrt(std::max(0.0,y))};
  } else if (d.method=="maxbin") {
    int ib = h->GetMaximumBin();
    double y = h->GetXaxis()->GetBinCenter(ib);
    return {y, 0.0};
  } else if (d.method=="gauspeak") {
    double xl = d.hasRange? d.xlow : h->GetXaxis()->GetXmin();
    double xh = d.hasRange? d.xhigh: h->GetXaxis()->GetXmax();
    TF1 g("g","gaus", xl, xh);
    auto fr = h->Fit(&g,"QS0");
      // --- NEW METHODS ---
      else if (d.method=="median") { // alias of quantile p=0.5
        double y = hist_quantile(h, 0.5);
        return {y, 0.0};
      } else if (d.method=="quantilep") {
        // pass p in xlow (0..1)
        double p = d.hasRange ? d.xlow : 0.5;
        double y = hist_quantile(h, p);
        return {y, 0.0};
      } else if (d.method=="truncmean") {
        // pass qlo,qhi in xlow,xhigh (fractions)
        double qlo = d.hasRange ? d.xlow : 0.05;
        double qhi = d.hasRange ? d.xhigh: 0.95;
        auto rng = hist_trunc_range_by_quantiles(h, qlo, qhi);
        double y = hist_mean_in_window(h, rng.first, rng.second);
        // approx error by RMS/sqrt(N) restricted to window (optional)
        return {y, 0.0};
      } else if (d.method=="mean_win") {
        // mean in a fixed x window [xlow,xhigh]
        if (!d.hasRange) return {std::numeric_limits<double>::quiet_NaN(), 0.0};
        double y = hist_mean_in_window(h, d.xlow, d.xhigh);
        return {y, 0.0};
      } else if (d.method=="uniform_r1") {
        auto pr = phi_uniform_r1(h);
        return {pr.first, pr.second};
      } else if (d.method=="chi2_uniform") {
        double y = chi2_uniform_reduced(h);
        return {y, 0.0};
      }

    if (fr != 0) return {std::numeric_limits<double>::quiet_NaN(), 0.0};
    double mu = g.GetParameter(1);
    double emu= g.GetParError(1);
    return {mu, emu};
  }
  return {std::numeric_limits<double>::quiet_NaN(), 0.0};
}

void extract_metrics(const char* filelist="lists/files.txt",
                     const char* conf="metrics.conf")
{
  std::vector<MetricDef> defs;
  if (!parse_metrics(conf, defs) || defs.empty()) {
    std::cerr<<"[ERROR] no metrics parsed; edit "<<conf<<" first.\n"; return;
  }
  gSystem->mkdir("out", kTRUE);

  struct Out { std::ofstream csv; std::unique_ptr<TGraphErrors> gr; };
  std::map<std::string, Out> outs;
  for (auto& d: defs) {
    std::string csvp = std::string("out/metrics_")+d.name+".csv";
    outs[d.name].csv.open(csvp);
    outs[d.name].csv<<"run,segment,file,value,error\n";
    outs[d.name].gr.reset(new TGraphErrors());
    outs[d.name].gr->SetName((std::string("gr_")+d.name).c_str());
  }

  std::ifstream in(filelist);
  if (!in) { std::cerr << "[ERROR] cannot open file list: " << filelist << "\n"; return; }
  std::vector<std::tuple<int,int,std::string>> files; // run,seg,path
  std::string path;
  while (std::getline(in,path)) {
    if (path.empty()) continue;
    auto m = parse_meta(path);
    files.emplace_back(m.run, m.segment, path);
  }
  std::sort(files.begin(), files.end(), [](auto&a, auto&b){
    if (std::get<0>(a)!=std::get<0>(b)) return std::get<0>(a)<std::get<0>(b);
    return std::get<1>(a)<std::get<1>(b);
  });

  for (auto& tup: files) {
    int run = std::get<0>(tup), seg = std::get<1>(tup);
    const std::string& fpath = std::get<2>(tup);
    std::unique_ptr<TFile> f(TFile::Open(fpath.c_str(),"READ"));
    if (!f || f->IsZombie()) { std::cerr<<"[WARN] cannot open "<<fpath<<"\n"; continue; }

    for (auto& d: defs) {
      auto val = compute_metric(d, f.get());
      double y = val.first, ey = val.second;
      outs[d.name].csv << run << "," << seg << "," << fpath << "," << y << "," << ey << "\n";
      int n = outs[d.name].gr->GetN();
      outs[d.name].gr->SetPoint(n, run, y);
      outs[d.name].gr->SetPointError(n, 0.0, ey);
    }
  }

  for (auto& kv : outs) {
    const std::string& name = kv.first;
    auto& gr = kv.second.gr;
    TCanvas c(("c_"+name).c_str(), ("metric: "+name).c_str(), 900, 600);
    gr->SetTitle((name+";Run;"+name).c_str());
    gr->Draw("AP");
    c.SaveAs((std::string("out/metric_")+name+".pdf").c_str());
    c.SaveAs((std::string("out/metric_")+name+".png").c_str());
  }
  std::cout<<"[DONE] metrics written to out/, plots saved.\n";
}
