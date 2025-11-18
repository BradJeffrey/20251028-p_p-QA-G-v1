#include <TFile.h>
#include <TH1.h>
#include <TSystem.h>
#include <TROOT.h>
#include <TMath.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include <iomanip>
#include <cctype>
#include <algorithm>
#include <memory>

namespace qa {

// ---------- small utils ----------
static inline std::string ltrim(std::string s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){return !std::isspace(ch);})); return s;
}
static inline std::string rtrim(std::string s) {
  s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){return !std::isspace(ch);}).base(), s.end()); return s;
}
static inline std::string trim(std::string s) { return rtrim(ltrim(s)); }

static std::string tolower_copy(std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){return std::tolower(c);}); return s; }

// parse "runNNNNN[-SSS]" from any portion of path
static bool parse_run_segment(const std::string& path, long& run, long& seg) {
  run = 0; seg = -1;
  for (size_t i=0; i+3<path.size(); ++i) {
    if (path[i]=='r' && path[i+1]=='u' && path[i+2]=='n' && std::isdigit((unsigned char)path[i+3])) {
      size_t j=i+3;
      size_t nstart=j;
      while (j<path.size() && std::isdigit((unsigned char)path[j])) ++j;
      if (j>nstart) {
        run = std::strtol(path.substr(nstart, j-nstart).c_str(), nullptr, 10);
        if (j<path.size() && path[j]=='-') {
          size_t k=j+1, kstart=k;
          while (k<path.size() && std::isdigit((unsigned char)path[k])) ++k;
          if (k>kstart) seg = std::strtol(path.substr(kstart, k-kstart).c_str(), nullptr, 10);
        }
        return true;
      }
    }
  }
  return false;
}

static void ensure_out_dir() { gSystem->mkdir("out", true); }

static void ensure_csv_header(const std::string& outcsv) {
  if (!gSystem->AccessPathName(outcsv.c_str())) return;
  std::ofstream o(outcsv.c_str(), std::ios::out);
  o << "run,segment,file,value,error,weight\n";
  o.close();
}

static void append_row(const std::string& outcsv, long run, long seg,
                       const std::string& file, double value, double error, double weight) {
  std::ofstream o(outcsv.c_str(), std::ios::app);
  o << run << "," << seg << ",";
  bool need_quotes = (file.find(',') != std::string::npos);
  if (need_quotes) o << '"' << file << '"';
  else o << file;
  o << ",";
  if (std::isfinite(value)) o << std::setprecision(15) << value; else o << "nan";
  o << "," << std::setprecision(15) << error << "," << std::setprecision(15) << weight << "\n";
}

static double h_maxbin_center(TH1* h) {
  int ib = h->GetMaximumBin();
  return h->GetXaxis()->GetBinCenter(ib);
}
static double h_quantile(TH1* h, double q) {
  double x=std::numeric_limits<double>::quiet_NaN(); double qq=q;
  if (h->GetEntries()<=0) return std::numeric_limits<double>::quiet_NaN();
  h->GetQuantiles(1,&x,&qq);
  return x;
}
static TH1* make_uniform_like(const TH1* h, const char* name) {
  int nb = h->GetXaxis()->GetNbins();
  TH1* u = (TH1*)h->Clone(name);
  u->Reset("ICESM");
  double tot = h->Integral(1, nb);
  double per = (nb>0) ? (tot/nb) : 0.0;
  for (int i=1;i<=nb;i++) u->SetBinContent(i, per);
  return u;
}
static double h_ks_uniform_p(TH1* h) {
  if (h->GetEntries()<=0) return std::numeric_limits<double>::quiet_NaN();
  std::unique_ptr<TH1> u(make_uniform_like(h,"__qa_u_ks"));
  return h->KolmogorovTest(u.get(), "N");
}
static double h_chi2_uniform_red(TH1* h) {
  if (h->GetEntries()<=0) return std::numeric_limits<double>::quiet_NaN();
  std::unique_ptr<TH1> u(make_uniform_like(h,"__qa_u_chi"));
  return h->Chi2Test(u.get(), "CHI2/NDF");
}

struct MetricDef { std::string metric, hist, method; };

static std::string normalize_method(std::string m) {
  m = tolower_copy(trim(m));
  if (m=="p50") m="median";
  if (m=="quantilep90") m="p90";
  return m;
}

static bool load_conf(const char* confpath, std::vector<MetricDef>& defs) {
  std::ifstream in(confpath);
  if (!in) { std::cerr << "[ERROR] cannot open metrics.conf: " << confpath << "\n"; return false; }
  std::string line;
  while (std::getline(in,line)) {
    line = trim(line);
    if (line.empty()) continue;
    if (line[0]=='#') continue;
    std::vector<std::string> toks;
    std::string cur; std::istringstream ss(line);
    while (std::getline(ss, cur, ',')) toks.push_back(trim(cur));
    if (toks.size()<3) {
      std::cerr << "[WARN] skipping malformed config line: " << line << "\n";
      continue;
    }
    MetricDef d; d.metric=toks[0]; d.hist=toks[1]; d.method=normalize_method(toks[2]);
    defs.push_back(d);
  }
  return true;
}

} // namespace qa

void extract_metrics_v2(const char* listspath="lists/files.txt", const char* confpath="metrics.conf") {
  using namespace qa;
  ensure_out_dir();
  std::vector<MetricDef> defs;
  if (!load_conf(confpath, defs) || defs.empty()) {
    std::cerr << "[ERROR] no metrics loaded from " << confpath << "\n";
    gSystem->Exit(1);
    return;
  }
  std::cout << "[INFO] metrics in scope: " << defs.size() << "\n";
  for (auto& d : defs) {
    std::string outcsv = std::string("out/metrics_") + d.metric + ".csv";
    ensure_csv_header(outcsv);
  }
  std::ifstream lf(listspath);
  if (!lf) {
    std::cerr << "[ERROR] cannot open lists file: " << listspath << "\n";
    gSystem->Exit(1);
    return;
  }
  std::vector<std::string> files;
  std::string line;
  while (std::getline(lf, line)) {
    line = trim(line);
    if (line.empty()) continue;
    if (line[0]=='#') continue;
    files.push_back(line);
  }
  std::cout << "[INFO] files in list: " << files.size() << "\n";
  for (const auto& fpath : files) {
    long run=0, seg=-1;
    parse_run_segment(fpath, run, seg);
    std::unique_ptr<TFile> f(TFile::Open(fpath.c_str(), "READ"));
    if (!f || f->IsZombie()) {
      std::cerr << "[WARN] cannot open file: " << fpath << " (writing NaN rows)\n";
      for (const auto& d : defs) {
        std::string outcsv = std::string("out/metrics_") + d.metric + ".csv";
        append_row(outcsv, run, seg, fpath, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
      }
      continue;
    }
    for (const auto& d : defs) {
      TH1* h=nullptr;
      f->GetObject(d.hist.c_str(), h);
      double value = std::numeric_limits<double>::quiet_NaN();
      double error = 0.0;
      double weight = 0.0;
      if (h) {
        weight = h->GetEntries();
        const std::string m = d.method;
        if      (m=="maxbin")           value = h_maxbin_center(h);
        else if (m=="median")           value = h_quantile(h, 0.50);
        else if (m=="p90")              value = h_quantile(h, 0.90);
        else if (m=="ks_uniform_p")     value = h_ks_uniform_p(h);
 else if (m=="mean") { value = h->GetMean(); error = h->GetMeanError(); }
            else if (m=="rms")  { value = h->GetRMS();  error = h->GetRMSError(); }

        else if (m=="chi2_uniform_red") value = h_chi2_uniform_red(h);
        else {
          std::cerr << "[INFO] unknown method '" << m << "' for metric " << d.metric << " — writing NaN/0 row\n";
          value = std::numeric_limits<double>::quiet_NaN();
          weight = (h ? h->GetEntries() : 0.0);
        }
        std::cout << "[INFO] " << d.metric << "
else if (m=="mean") { value = h->GetMean(); error = h->GetMeanError(); }
        else if (m=="rms")  { value = h->GetRMS();  error = h->GetRMSError(); } run=" << run << " seg=" << seg
                  << " value=" << (std::isfinite(value)?std::to_string(value):"NaN")
                  << " w=" << weight << "\n";
      } else {
        std::cerr << "[INFO] missing hist '" << d.hist << "' in file: " << fpath << " — writing NaN/0 row\n";
      }
      std::string outcsv = std::string("out/metrics_") + d.metric + ".csv";
      append_row(outcsv, run, seg, fpath, value, error, weight);
    }
  }
  std::cout << "[OK] extract_metrics_v2 completed.\n";
}
