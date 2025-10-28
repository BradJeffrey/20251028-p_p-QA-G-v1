#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static std::string trim(std::string s) {
  auto issp=[](unsigned char c){return std::isspace(c);};
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){return !issp(c);} ));
  s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){return !issp(c);} ).base(), s.end());
  return s;
}
static std::vector<std::string> split(const std::string& s, char d) {
  std::vector<std::string> out; std::stringstream ss(s); std::string t;
  while (std::getline(ss,t,d)) out.push_back(t);
  return out;
}

static std::vector<std::string> metrics_from_conf(const char* conf) {
  std::vector<std::string> m;
  std::ifstream in(conf); std::string line;
  while (std::getline(in,line)) {
    line=trim(line); if (line.empty()||line[0]=='#') continue;
    auto p=line.find(','); if (p==std::string::npos) continue;
    m.push_back(line.substr(0,p));
  }
  return m;
}

static bool read_perrun(const std::string& path, std::map<int,double>& run2val) {
  std::ifstream in(path); if(!in) return false;
  std::string s; bool header=true; bool ok=false;
  while (std::getline(in,s)) {
    if (header){ header=false; continue; }
    if (s.empty()) continue;
    auto toks = split(s,','); if (toks.size()<2) continue;
    int run = std::stoi(toks[0]); double val= std::stod(toks[1]);
    run2val[run] = val; ok=true;
  }
  return ok;
}

// Usage: .x macros/merge_per_run.C("metrics.conf","out/metrics_perrun_wide.csv")
void merge_per_run(const char* conf="metrics.conf", const char* outcsv="out/metrics_perrun_wide.csv")
{
  auto metrics = metrics_from_conf(conf);
  std::map<int,std::map<std::string,double>> table;
  std::set<int> runs;

  for (auto& m: metrics) {
    std::map<int,double> r2v;
    if (!read_perrun("out/metrics_"+m+"_perrun.csv", r2v)) continue;
    for (auto& kv: r2v) { table[kv.first][m] = kv.second; runs.insert(kv.first); }
  }

  std::ofstream out(outcsv);
  out<<"run";
  for (auto& m: metrics) out<<","<<m;
  out<<"\n";
  for (int r: runs) {
    out<<r;
    for (auto& m: metrics) {
      auto it = table[r].find(m);
      if (it==table[r].end()) out<<",";
      else out<<","<<it->second;
    }
    out<<"\n";
  }
  std::cout<<"[DONE] wrote "<<outcsv<<"\n";
}
