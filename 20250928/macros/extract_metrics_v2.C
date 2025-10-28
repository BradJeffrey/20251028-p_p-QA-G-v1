// [001] // Robust metric extractor with safe "peak", proper phi-uniformity tests, and 'weight' column
// [002] #include <TFile.h>
// [003] #include <TKey.h>
// [004] #include <TDirectory.h>
// [005] #include <TH1.h>
// [006] #include <TMath.h>
// [007] #include <TCanvas.h>
// [008] #include <TGraphErrors.h>
// [009] #include <TF1.h>
// [010] #include <TSystem.h>
// [011] #include <TString.h>
// [012] #include <iostream>
// [013] #include <fstream>
// [014] #include <sstream>
// [015] #include <vector>
// [016] #include <map>
// [017] #include <regex>
// [018] #include <memory>
// [019] using std::string;
// [020] 
// [021] struct MetricSpec {
// [022]   string name;                 // metric name
// [023]   std::vector<string> h;       // 1 or 2 histogram names (2 for 'asym')
// [024]   string method;               // mean|rms|sum|maxbin|median|quantilepXX|landau_mpv|asym|chi2_uniform_red|ks_uniform_p
// [025]   double xlow = NAN;           // optional fit/range lower bound
// [026]   double xhigh = NAN;          // optional fit/range upper bound
// [027] };
// [028] 
// [029] struct OutSeries {
// [030]   std::unique_ptr<TGraphErrors> gr;
// [031]   std::ofstream csv;
// [032] };
// [033] 
// [034] static bool parse_line(const string& line, MetricSpec& m) {
// [035]   string s=line;
// [036]   auto hash = s.find('#'); if (hash!=string::npos) s = s.substr(0,hash);
// [037]   auto l = s.find_first_not_of(" \t\r\n"); if (l==string::npos) return false;
// [038]   auto r = s.find_last_not_of(" \t\r\n");  s = s.substr(l,r-l+1);
// [039]   std::vector<string> tok; tok.reserve(5);
// [040]   std::stringstream ss(s); string part;
// [041]   while (std::getline(ss, part, ',')) {
// [042]     auto l2 = part.find_first_not_of(" \t\r\n");
// [043]     auto r2 = part.find_last_not_of(" \t\r\n");
// [044]     tok.push_back(l2==string::npos ? "" : part.substr(l2, r2-l2+1));
// [045]   }
// [046]   if (tok.size()<3) return false;
// [047]   m.name   = tok[0];
// [048]   m.h.clear(); { std::stringstream hs(tok[1]); string h; while (std::getline(hs,h,'|')) if(!h.empty()) m.h.push_back(h); }
// [049]   m.method = tok[2];
// [050]   m.xlow   = (tok.size()>=4 && !tok[3].empty()) ? atof(tok[3].c_str()) : NAN;
// [051]   m.xhigh  = (tok.size()>=5 && !tok[4].empty()) ? atof(tok[4].c_str()) : NAN;
// [052]   return true;
// [053] }
// [054] 
// [055] static bool open_file(const string& path, std::unique_ptr<TFile>& f) {
// [056]   f.reset(TFile::Open(path.c_str(),"READ"));
// [057]   return (bool)f && !f->IsZombie();
// [058] }
// [059] 
// [060] static bool get_run_seg_from_path(const string& fpath, long& run, int& seg) {
// [061]   std::regex r1("run(\\d{5,6})(?:[_-](\\d+))?\\.root");
// [062]   std::smatch m;
// [063]   if (std::regex_search(fpath, m, r1)) { run = std::stol(m[1]); seg = m[2].matched ? std::stoi(m[2]) : -1; return true; }
// [064]   std::regex r2("(\\d{5,6})");
// [065]   if (std::regex_search(fpath, m, r2)) { run = std::stol(m[1]); seg=-1; return true; }
// [066]   return false;
// [067] }
// [068] 
// [069] static double h_median(const TH1* h) {
// [070]   if (!h || h->GetEntries()<=0) return NAN;
// [071]   int nb=h->GetNbinsX(); double total=h->Integral(1,nb); if (total<=0) return NAN;
// [072]   double cum=0; for (int i=1;i<=nb;++i){ cum+=h->GetBinContent(i); if (cum>=0.5*total) return h->GetXaxis()->GetBinCenter(i); }
// [073]   return h->GetXaxis()->GetBinCenter(nb);
// [074] }
// [075] 
// [076] static double h_quantile(const TH1* h, double p) {
// [077]   if (!h || h->GetEntries()<=0) return NAN;
// [078]   double qq[1]; double pp[1]={p}; const_cast<TH1*>(h)->GetQuantiles(1,qq,pp); return qq[0];
// [079] }
// [080] 
// [081] static double h_maxbin_center(const TH1* h) {
// [082]   if (!h || h->GetEntries()<=0) return NAN;
// [083]   int ib = h->GetMaximumBin(); if (ib<1) ib=1; if (ib>h->GetNbinsX()) ib=h->GetNbinsX();
// [084]   return h->GetXaxis()->GetBinCenter(ib);
// [085] }
// [086] 
// [087] static double landau_mpv(TH1* h, double xlo, double xhi) {
// [088]   if (!h || h->GetEntries()<=0) return NAN;
// [089]   double lo = std::isfinite(xlo)?xlo:h->GetXaxis()->GetXmin();
// [090]   double hi = std::isfinite(xhi)?xhi:h->GetXaxis()->GetXmax();
// [091]   TF1 f("fl","landau",lo,hi);
// [092]   int st = h->Fit(&f,"QNR","",lo,hi); // Quiet, NoDraw, Range
// [093]   if (st!=0) return h_maxbin_center(h); // fallback
// [094]   return f.GetParameter(1); // MPV
// [095] }
// [096] 
// [097] static double chi2_uniform_reduced(const TH1* hin, int& ndof) {
// [098]   ndof = 0;
// [099]   if (!hin || hin->GetEntries()<=0) return NAN;
// [100]   std::unique_ptr<TH1> h((TH1*)hin->Clone("h_tmp_uniform"));
// [101]   h->SetDirectory(nullptr);
// [102]   const int nb0 = h->GetNbinsX();
// [103]   double total = h->Integral(1,nb0); if (total<=0) return NAN;
// [104]   int rebin = 1; double mu = total/nb0;
// [105]   while (mu<5.0 && (nb0/rebin)>=2) { rebin*=2; h->Rebin(2); int nb=h->GetNbinsX(); total=h->Integral(1,nb); mu=total/nb; }
// [106]   const int nb = h->GetNbinsX(); if (nb<2) return NAN;
// [107]   mu = h->Integral(1,nb)/nb; if (mu<=0) return NAN;
// [108]   double chi2=0.0; for (int i=1;i<=nb;++i) { double ni=h->GetBinContent(i); chi2 += (ni-mu)*(ni-mu)/mu; }
// [109]   ndof = nb-1; return chi2/(double)ndof;
// [110] }
// [111] 
// [112] static double ks_uniform_pvalue(const TH1* hin) {
// [113]   if (!hin || hin->GetEntries()<=0) return NAN;
// [114]   int nb = ((TH1*)hin)->GetNbinsX();
// [115]   auto hU = (TH1*)hin->Clone("hU"); hU->SetDirectory(nullptr);
// [116]   double total = ((TH1*)hin)->Integral(1,nb), mu = (nb>0)?total/nb:0.0;
// [117]   for (int i=1;i<=nb;++i) hU->SetBinContent(i, mu);
// [118]   double p = ((TH1*)hin)->KolmogorovTest(hU,""); delete hU; return p;
// [119] }
// [120] 
// [121] static double compute_value(TFile* f, const MetricSpec& m, double& ey, double& wt) {
// [122]   ey = 0.0; wt = 0.0;
// [123]   auto need = [&](size_t n){ if(m.h.size()!=n){ std::cerr<<"[WARN] "<<m.name<<": expected "<<n<<" hists, got "<<m.h.size()<<"\n"; } };
// [124]   auto getH = [&](const string& hname)->TH1*{ TH1* h=nullptr; f->GetObject(hname.c_str(),h); return h; };
// [125]   auto entries = [&](TH1* h)->double{ return h ? h->GetEntries() : 0.0; };
// [126]   auto integral = [&](TH1* h)->double{ return h ? h->Integral(1,h->GetNbinsX()) : 0.0; };
// [127]   if (m.method=="mean") {
// [128]     need(1); TH1* h=getH(m.h[0]); if(!h||entries(h)<=0) return NAN; wt=entries(h); return h->GetMean();
// [129]   } else if (m.method=="rms") {
// [130]     need(1); TH1* h=getH(m.h[0]); if(!h||entries(h)<=0) return NAN; wt=entries(h); return h->GetRMS();
// [131]   } else if (m.method=="sum") {
// [132]     need(1); TH1* h=getH(m.h[0]); if(!h) return NAN; wt=integral(h); return integral(h);
// [133]   } else if (m.method=="maxbin") {
// [134]     need(1); TH1* h=getH(m.h[0]); if(!h||entries(h)<=0) return NAN; wt=entries(h); return h_maxbin_center(h);
// [135]   } else if (m.method=="median") {
// [136]     need(1); TH1* h=getH(m.h[0]); if(!h||entries(h)<=0) return NAN; wt=entries(h); return h_median(h);
// [137]   } else if (m.method.rfind("quantilep",0)==0) {
// [138]     need(1); TH1* h=getH(m.h[0]); if(!h||entries(h)<=0) return NAN; wt=entries(h); double p=atof(m.method.substr(9).c_str())/100.0; return h_quantile(h,p);
// [139]   } else if (m.method=="landau_mpv") {
// [140]     need(1); TH1* h=getH(m.h[0]); if(!h||entries(h)<=0) return NAN; wt=entries(h); return landau_mpv(h,m.xlow,m.xhigh);
// [141]   } else if (m.method=="asym") {
// [142]     need(2); TH1* hN=getH(m.h[0]); TH1* hS=getH(m.h[1]); if(!hN||!hS) return NAN;
// [143]     double n=integral(hN), s=integral(hS); if (n+s<=0) return NAN; wt=n+s; return (n-s)/(n+s);
// [144]   } else if (m.method=="chi2_uniform_red") {
// [145]     need(1); TH1* h=getH(m.h[0]); if(!h||entries(h)<=0) return NAN; int ndof=0; wt=entries(h); return chi2_uniform_reduced(h,ndof);
// [146]   } else if (m.method=="ks_uniform_p") {
// [147]     need(1); TH1* h=getH(m.h[0]); if(!h||entries(h)<=0) return NAN; wt=entries(h); return ks_uniform_pvalue(h);
// [148]   } else {
// [149]     std::cerr<<"[WARN] Unknown method '"<<m.method<<"' for "<<m.name<<"\n"; return NAN;
// [150]   }
// [151] }
// [152] 
// [153] void extract_metrics_v2(const char* listFile="lists/files.txt", const char* confFile="metrics.conf") {
// [154]   gSystem->mkdir("out", kTRUE);
// [155]   std::ifstream cfg(confFile);
// [156]   if(!cfg){ std::cerr<<"[ERROR] cannot open "<<confFile<<"\n"; return; }
// [157]   std::vector<MetricSpec> specs; string line;
// [158]   while (std::getline(cfg,line)) { MetricSpec m; if (parse_line(line,m)) specs.push_back(m); }
// [159]   if (specs.empty()) { std::cerr<<"[ERROR] no metrics in "<<confFile<<"\n"; return; }
// [160]   std::map<string,OutSeries> outs;
// [161]   for (auto& s: specs) {
// [162]     OutSeries o; o.gr.reset(new TGraphErrors());
// [163]     string csvPath = string("out/metrics_")+s.name+".csv";
// [164]    o.csv.open(csvPath); o.csv<<"run,segment,file,value,error,weight\n";
// [165]     outs.emplace(s.name, std::move(o));
// [166]   }
// [167]   std::ifstream fl(listFile); if(!fl){ std::cerr<<"[ERROR] cannot open "<<listFile<<"\n"; return; }
// [168]   std::vector<string> files; string fline; while(std::getline(fl,fline)){ if(fline.empty()) continue; files.push_back(fline); }
// [169]   for (auto& fpath: files) {
// [170]     std::unique_ptr<TFile> f; if(!open_file(fpath,f)){ std::cerr<<"[WARN] cannot open "<<fpath<<"\n"; continue; }
// [171]     long run=0; int seg=-1; get_run_seg_from_path(fpath,run,seg);
// [172]     for (auto& s: specs) {
// [173]       double ey=0.0, wt=0.0; double y = compute_value(f.get(), s, ey, wt);
// [174]       auto& o = outs[s.name];
// [175]       o.csv << run << "," << seg << "," << fpath << "," << y << "," << 0.0 << "," << wt << "\n";
// [176]       int n=o.gr->GetN(); o.gr->SetPoint(n, (double)run, y); o.gr->SetPointError(n, 0.0, 0.0);
// [177]     }
// [178]   }
// [179]   for (auto& kv: outs) {
// [180]     const string& name = kv.first; auto& gr = kv.second.gr;
// [181]     TCanvas c(("c_"+name).c_str(), name.c_str(), 900, 600);
// [182]     gr->SetTitle((name+";Run;"+name).c_str()); gr->Draw("AP");
// [183]     c.SaveAs((string("out/metric_")+name+".png").c_str());
// [184]     c.SaveAs((string("out/metric_")+name+".pdf").c_str());
// [185]   }
// [186]   std::cout<<"[DONE] metrics written to out/, plots saved.\n";
// [187] }
// [188] 
