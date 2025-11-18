#include <TSystem.h>
#include <TSystemDirectory.h>
#include <TList.h>
#include <TObjString.h>

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

static bool has_wildcard(const std::string& s) {
  return s.find('*')!=std::string::npos || s.find('?')!=std::string::npos || s.find('[')!=std::string::npos;
}
static std::string dirname_of(const std::string& p){
  auto pos = p.find_last_of("/"); if (pos==std::string::npos) return ".";
  return p.substr(0,pos);
}
static std::string basename_of(const std::string& p){
  auto pos = p.find_last_of("/"); if (pos==std::string::npos) return p;
  return p.substr(pos+1);
}
static bool simple_match(const std::string& pat, const std::string& name) {
  // only supports '*'
  if (pat=="*") return true;
  size_t p=0, n=0, star=std::string::npos, mark=0;
  while (n<name.size()){
    if (p<pat.size() && (pat[p]=='?' || pat[p]==name[n])) { ++p; ++n; }
    else if (p<pat.size() && pat[p]=='*'){ star=p++; mark=n; }
    else if (star!=std::string::npos){ p=star+1; n=++mark; }
    else return false;
  }
  while (p<pat.size() && pat[p]=='*') ++p;
  return p==pat.size();
}
static void list_matching(const std::string& dir, const std::string& pat, std::vector<std::string>& out) {
  TSystemDirectory d(dir.c_str(), dir.c_str());
  TList* fl = d.GetListOfFiles();
  if (!fl) return;
  for (int i=0;i<fl->GetSize();++i){
    TSystemFile* f = (TSystemFile*)fl->At(i);
    if (!f) continue;
    std::string nm = f->GetName();
    if (f->IsDirectory()) continue;
    if (simple_match(pat, nm)) out.push_back(dir + "/" + nm);
  }
  std::sort(out.begin(), out.end());
}

void aggregate_per_run_v2(const char* pattern_or_dir="out/metrics_*.csv",
                          const char* outcsv="out/agg_runs_v2.csv")
{
  std::string in(pattern_or_dir);
  std::string dir = in, pat = "metrics_*.csv";
  std::vector<std::string> files;

  if (has_wildcard(in)) {
    dir = dirname_of(in);
    pat = basename_of(in);
    list_matching(dir, pat, files);
  } else {
    // treat as directory
    pat = "metrics_*.csv";
    list_matching(in, pat, files);
  }

  if (files.empty()) {
    std::cerr << "[ERROR] aggregate_per_run_v2: no inputs found under '" << in
              << "'. Expected per-metric CSVs named metrics_*.csv\n";
    gSystem->Exit(1);
    return;
  }

  gSystem->mkdir("out", true);
  std::ofstream out(outcsv);
  out << "metric,run,segment,file,value,error,weight\n";

  size_t total_rows = 0;
  for (const auto& path : files) {
    // derive metric name from filename metrics_<metric>.csv
    std::string base = basename_of(path);
    std::string metric = base;
    if (metric.rfind("metrics_",0)==0) metric = metric.substr(8);
    if (metric.size()>4 && metric.substr(metric.size()-4)==".csv")
      metric = metric.substr(0, metric.size()-4);

    std::ifstream fin(path);
    if (!fin) { std::cerr << "[WARN] cannot open " << path << ", skipping\n"; continue; }
    std::string line; bool first=true;
    while (std::getline(fin, line)) {
      if (line.empty()) continue;
      if (first) {
        first=false;
        // skip a header line that starts with run,segment,...
        if (line.find("run,segment,file,value,error,weight")==0) continue;
      }
      out << metric << "," << line << "\n";
      ++total_rows;
    }
  }
  out.close();
  std::cout << "[OK] wrote " << outcsv << " with " << total_rows << " rows from " << files.size() << " metrics files\n";
}
