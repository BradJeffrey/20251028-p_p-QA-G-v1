// [001] // Robust metric extractor with safe "peak" and proper uniformity tests
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
// [037]   // trim
// [038]   auto l = s.find_first_not_of(" \t\r\n"); if (l==string::npos) return false;
// [039]   auto r = s.find_last_not_of(" \t\r\n");  s = s.substr(l,r-l+1);
// [040]   std::vector<string> tok; tok.reserve(5);
// [041]   std::stringstream ss(s); string part;
// [042]   while (std::getline(ss, part, ',')) { // CSV-ish (no quoted commas expected)
// [043]     // trim each
// [044]     auto l2 = part.find_first_not_of(" \t\r\n");
// [045]     auto r2 = part.find_last_not_of(" \t\r\n");
// [046]     tok.push_back(l2==string::npos ? "" : part.substr(l2, r2-l2+1));
// [047]   }
// [048]   if (tok.size()<3) return false;
// [049]   m.name   = tok[0];
// [050]   // split hists by |
// [051]   m.h.clear();
// [052]   { std::stringstream hs(tok[1]); string h; while (std::getline(hs,h,'|')) { if(!h.empty()) m.h.push_back(h); } }
// [053]   m.method = tok[2];
// [054]   m.xlow   = (tok.size()>=4 && !tok[3].empty()) ? atof(tok[3].c_str()) : NAN;
// [055]   m.xhigh  = (tok.size()>=5 && !tok[4].empty()) ? atof(tok[4].c_str()) : NAN;
// [056]   return true;
// [057] }
// [058] 
// [059] static bool open_file(const string& path, std::unique_ptr<TFile>& f) {
// [060]   f.reset(TFile::Open(path.c_str(),"READ"));
// [061]   return (bool)f && !f->IsZombie();
// [062] }
// [063] 
// [064] static bool get_run_seg_from_path(const string& fpath, long& run, int& seg) {
// [065]   // Try runNNNNN[_seg]* first
// [066]   std::regex r1("run(\\d{5,6})(?:[_-](\\d+))?\\.root");
// [067]   std::smatch m;
// [068]   if (std::regex_search(fpath, m, r1)) {
// [069]     run = std::stol(m[1]);
// [070]     seg = m[2].matched ? std::stoi(m[2]) : -1;
// [071]     return true;
// [072]   }
// [073]   // Fallback: any 5–6 digit number in name
// [074]   std::regex r2("(\\d{5,6})");
// [075]   if (std::regex_search(fpath, m, r2)) { run = std::stol(m[1]); seg=-1; return true; }
// [076]   return false;
// [077] }
// [078] 
// [079] static double h_median(const TH1* h) {
// [080]   if (!h || h->GetEntries()<=0) return NAN;
// [081]   const int nb=h->GetNbinsX(); double total=h->Integral(1,nb); if (total<=0) return NAN;
// [082]   double cum=0; for (int i=1;i<=nb;++i){ cum+=h->GetBinContent(i); if (cum>=0.5*total) return h->GetXaxis()->GetBinCenter(i); }
// [083]   return h->GetXaxis()->GetBinCenter(nb);
// [084] }
// [085] 
// [086] static double h_quantile(const TH1* h, double p) {
// [087]   if (!h || h->GetEntries()<=0) return NAN;
// [088]   double q; const double pp[1]={p}; double qq[1]; const_cast<TH1*>(h)->GetQuantiles(1,qq,pp);
// [089]   q = qq[0]; return q;
// [090] }
// [091] 
// [092] static double h_maxbin_center(const TH1* h) {
// [093]   if (!h || h->GetEntries()<=0) return NAN;
// [094]   int ib = h->GetMaximumBin(); if (ib<1) ib=1; if (ib>h->GetNbinsX()) ib=h->GetNbinsX();
// [095]   return h->GetXaxis()->GetBinCenter(ib);
// [096] }
// [097] 
// [098] static double landau_mpv(TH1* h, double xlo, double xhi) {
// [099]   if (!h || h->GetEntries()<=0) return NAN;
// [100]   double lo = std::isfinite(xlo)?xlo:h->GetXaxis()->GetXmin();
// [101]   double hi = std::isfinite(xhi)?xhi:h->GetXaxis()->GetXmax();
// [102]   TF1 f("fl","landau",lo,hi);
// [103]   int st = h->Fit(&f,"QNR","",lo,hi); // Quiet, NoDraw, Range
// [104]   if (st!=0) return h_maxbin_center(h); // fallback is robust
// [105]   return f.GetParameter(1); // MPV
// [106] }
// [107] 
// [108] static double chi2_uniform_reduced(const TH1* hin, int& ndof) {
// [109]   ndof = 0;
// [110]   if (!hin || hin->GetEntries()<=0) return NAN;
// [111]   // Work on a copy we can rebin if needed
// [112]   std::unique_ptr<TH1> h((TH1*)hin->Clone("h_tmp_uniform"));
// [113]   h->SetDirectory(nullptr);
// [114]   const int nb0 = h->GetNbinsX();
// [115]   // Ensure expected counts per bin are not too small
// [116]   double total = h->Integral(1,nb0);
// [117]   if (total<=0) return NAN;
// [118]   int rebin = 1;
// [119]   double mu = total/nb0;
// [120]   while (mu<5.0 && (nb0/rebin)>=2) { rebin*=2; h->Rebin(2); int nb=h->GetNbinsX(); total=h->Integral(1,nb); mu=total/nb; }
// [121]   const int nb = h->GetNbinsX();
// [122]   if (nb<2) return NAN;
// [123]   mu = h->Integral(1,nb)/nb;
// [124]   if (mu<=0) return NAN;
// [125]   double chi2=0.0;
// [126]   for (int i=1;i<=nb;++i) {
// [127]     double ni = h->GetBinContent(i);
// [128]     chi2 += (ni-mu)*(ni-mu)/mu;
// [129]   }
// [130]   ndof = nb-1;
// [131]   return chi2/(double)ndof;
// [132] }
// [133] 
// [134] static double ks_uniform_pvalue(const TH1* hin) {
// [135]   if (!hin || hin->GetEntries()<=0) return NAN;
// [136]   // Build a uniform template with same binning & total
// [137]   auto ax = hin->GetXaxis();
// [138]   int nb = ax->GetNbins();
// [139]   auto hU = (TH1*)hin->Clone("hU");
// [140]   hU->SetDirectory(nullptr);
// [141]   double total = ((TH1*)hin)->Integral(1,nb);
// [142]   double mu = (nb>0)?total/nb:0.0;
// [143]   for (int i=1;i<=nb;++i) hU->SetBinContent(i, mu);
// [144]   // ROOT returns p-value by default (unless "D" option)
// [145]   double p = ((TH1*)hin)->KolmogorovTest(hU,""); // empty option -> probability
// [146]   delete hU;
// [147]   return p;
// [148] }
// [149] 
// [150] static double compute_value(TFile* f, const MetricSpec& m, double& errY) {
// [151]   errY = 0.0;
// [152]   auto need = [&](size_t n){ if(m.h.size()!=n){ std::cerr<<"[WARN] "<<m.name<<": expected "<<n<<" hists, got "<<m.h.size()<<"\n"; } };
// [153]   auto getH = [&](const string& hname)->TH1*{ TH1* h=nullptr; f->GetObject(hname.c_str(),h); return h; };
// [154]   if (m.method=="mean") {
// [155]     need(1); TH1* h=getH(m.h[0]); if(!h||h->GetEntries()<=0) return NAN; errY = 0.0; return h->GetMean();
// [156]   } else if (m.method=="rms") {
// [157]     need(1); TH1* h=getH(m.h[0]); if(!h||h->GetEntries()<=0) return NAN; errY = 0.0; return h->GetRMS();
// [158]   } else if (m.method=="sum") {
// [159]     need(1); TH1* h=getH(m.h[0]); if(!h) return NAN; errY = 0.0; return h->Integral(1,h->GetNbinsX());
// [160]   } else if (m.method=="maxbin") {
// [161]     need(1); TH1* h=getH(m.h[0]); if(!h) return NAN; errY = 0.0; return h_maxbin_center(h);
// [162]   } else if (m.method=="median") {
// [163]     need(1); TH1* h=getH(m.h[0]); if(!h) return NAN; errY = 0.0; return h_median(h);
// [164]   } else if (m.method.rfind("quantilep",0)==0) {
// [165]     need(1); TH1* h=getH(m.h[0]); if(!h) return NAN; double p=atof(m.method.substr(9).c_str())/100.0; errY=0.0; return h_quantile(h,p);
// [166]   } else if (m.method=="landau_mpv") {
// [167]     need(1); TH1* h=getH(m.h[0]); if(!h) return NAN; errY = 0.0; return landau_mpv(h,m.xlow,m.xhigh);
// [168]   } else if (m.method=="asym") {
// [169]     need(2); TH1* hN=getH(m.h[0]); TH1* hS=getH(m.h[1]); if(!hN||!hS) return NAN;
// [170]     double n=hN->Integral(1,hN->GetNbinsX()), s=hS->Integral(1,hS->GetNbinsX()); if (n+s<=0) return NAN;
// [171]     errY = 0.0; return (n-s)/(n+s);
// [172]   } else if (m.method=="chi2_uniform_red") {
// [173]     need(1); TH1* h=getH(m.h[0]); if(!h) return NAN; int ndof=0; double v=chi2_uniform_reduced(h,ndof); errY=0.0; return v;
// [174]   } else if (m.method=="ks_uniform_p") {
// [175]     need(1); TH1* h=getH(m.h[0]); if(!h) return NAN; errY=0.0; return ks_uniform_pvalue(h);
// [176]   } else {
// [177]     std::cerr<<"[WARN] Unknown method '"<<m.method<<"' for "<<m.name<<"\n"; return NAN;
// [178]   }
// [179] }
// [180] 
// [181] void extract_metrics_v2(const char* listFile="lists/files.txt", const char* confFile="metrics.conf") {
// [182]   gSystem->mkdir("out", kTRUE);
// [183]   // Parse config
// [184]   std::ifstream cfg(confFile);
// [185]   if(!cfg){ std::cerr<<"[ERROR] cannot open "<<confFile<<"\n"; return; }
// [186]   std::vector<MetricSpec> specs; string line;
// [187]   while (std::getline(cfg,line)) { MetricSpec m; if (parse_line(line,m)) specs.push_back(m); }
// [188]   if (specs.empty()) { std::cerr<<"[ERROR] no metrics in "<<confFile<<"\n"; return; }
// [189]   // Open output streams
// [190]   std::map<string,OutSeries> outs;
// [191]   for (auto& s: specs) {
// [192]     OutSeries o;
// [193]     o.gr.reset(new TGraphErrors());
// [194]     string csvPath = string("out/metrics_")+s.name+".csv";
// [195]     o.csv.open(csvPath);
// [196]     o.csv<<"run,segment,file,value,error\n";
// [197]     outs.emplace(s.name, std::move(o));
// [198]   }
// [199]   // Read file list
// [200]   std::ifstream fl(listFile); if(!fl){ std::cerr<<"[ERROR] cannot open "<<listFile<<"\n"; return; }
// [201]   std::vector<string> files; while(std::getline(fl,line)){ if(line.empty()) continue; files.push_back(line); }
// [202]   // Loop files
// [203]   for (auto& fpath: files) {
// [204]     std::unique_ptr<TFile> f; if(!open_file(fpath,f)){ std::cerr<<"[WARN] cannot open "<<fpath<<"\n"; continue; }
// [205]     long run=0; int seg=-1; if(!get_run_seg_from_path(fpath,run,seg)){ std::cerr<<"[WARN] cannot parse run from "<<fpath<<"\n"; }
// [206]     for (auto& s: specs) {
// [207]       double ey=0.0; double y = compute_value(f.get(), s, ey);
// [208]       // For single-point per run, don't draw huge error bars
// [209]       ey = 0.0;
// [210]       auto& o = outs[s.name];
// [211]       o.csv << run << "," << seg << "," << fpath << "," << y << "," << ey << "\n";
// [212]       int n=o.gr->GetN(); o.gr->SetPoint(n, (double)run, y); o.gr->SetPointError(n, 0.0, ey);
// [213]     }
// [214]   }
// [215]   // Plot
// [216]   for (auto& kv: outs) {
// [217]     const string& name = kv.first;
// [218]     auto& gr = kv.second.gr;
// [219]     TCanvas c(("c_"+name).c_str(), name.c_str(), 900, 600);
// [220]     gr->SetTitle((name+";Run;"+name).c_str());
// [221]     gr->Draw("AP");
// [222]     c.SaveAs((string("out/metric_")+name+".png").c_str());
// [223]     c.SaveAs((string("out/metric_")+name+".pdf").c_str());
// [224]   }
// [225]   std::cout<<"[DONE] metrics written to out/, plots saved.\n";
// [226] }
// [227] 
