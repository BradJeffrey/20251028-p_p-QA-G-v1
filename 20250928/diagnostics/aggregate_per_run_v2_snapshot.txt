#include <TCanvas.h>
#include <TGraphErrors.h>
#include <TSystem.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

struct MetricDef { std::string name; std::string method; };

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

static std::map<std::string, MetricDef> load_conf(const char* conf) {
  std::map<std::string, MetricDef> defs;
  std::ifstream in(conf);
  std::string line; int ln=0;
  while (std::getline(in,line)) {
    ++ln; line=trim(line);
    if (line.empty() || line[0]=='#') continue;
    auto toks = split(line, ',');
    for (auto& t: toks) t=trim(t);
    if (toks.size() < 3) { std::cerr<<"[WARN] bad conf line "<<ln<<": "<<line<<"\n"; continue; }
    MetricDef m; m.name = toks[0]; m.method = toks[2];
    defs[m.name] = m;
  }
  return defs;
}

struct Row { int run; int seg; std::string file; double y; double ey; double w; };

static bool read_metric_csv(const std::string& path, std::vector<Row>& rows) {
  std::ifstream in(path);
  if (!in) return false;
  std::string s; bool first=true;
  while (std::getline(in,s)) {
    if (first) { first=false; continue; }
    if (s.empty()) continue;
    auto toks = split(s, ',');
    if (toks.size() < 5) continue;
    Row r;
    try {
      r.run = std::stoi(toks[0]);
      r.seg = std::stoi(toks[1]);
      r.file= toks[2];
      r.y   = std::stod(toks[3]);
      r.ey  = std::stod(toks[4]);
      r.w   = (toks.size()>=6)? std::stod(toks[5]) : 1.0;
    } catch (...) { continue; }
    rows.push_back(r);
  }
  return true;
}

struct Agg { double y=std::numeric_limits<double>::quiet_NaN(); double ey=0; };

static Agg agg_sum(const std::vector<Row>& v) {
  double sum=0, e2=0; int n=0;
  for (auto& r: v) { if (std::isnan(r.y)) continue; sum += r.y; e2 += r.ey*r.ey; ++n; }
  Agg a; if (n>0) { a.y=sum; a.ey=std::sqrt(e2); } return a;
}
static Agg agg_mean(const std::vector<Row>& v) {
  double s=0; int n=0; for (auto&r: v) if (std::isfinite(r.y)) { s+=r.y; ++n; }
  Agg a; if (n>0) { a.y=s/n; a.ey=0; } return a;
}
static Agg agg_wmean_ivar(const std::vector<Row>& v) {
  double sw=0, swy=0;
  for (auto&r: v) {
    double w = (r.ey>0 && std::isfinite(r.ey)) ? 1.0/(r.ey*r.ey) : 0.0;
    if (w>0) { sw += w; swy += w*r.y; }
  }
  Agg a; if (sw>0) { a.y=swy/sw; a.ey=std::sqrt(1.0/sw); } return a;
}
static Agg agg_wmean_entries(const std::vector<Row>& v) {
  double sw=0, swy=0;
  for (auto&r: v) if (std::isfinite(r.y)) { double w=std::max(0.0,r.w); sw+=w; swy+=w*r.y; }
  Agg a; if (sw>0) { a.y=swy/sw; a.ey=0; } return a;
}

static void write_and_plot(const std::string& metric, const std::map<int,Agg>& byrun) {
  gSystem->mkdir("out", kTRUE);
  std::ofstream out("out/metrics_"+metric+"_perrun.csv");
  out<<"run,value,error\n";
  auto gr = std::make_unique<TGraphErrors>();
  gr->SetName(("gr_"+metric+"_perrun").c_str());
  int i=0;
  for (auto& kv : byrun) {
    out<<kv.first<<","<<kv.second.y<<","<<kv.second.ey<<"\n";
    gr->SetPoint(i, kv.first, kv.second.y);
    gr->SetPointError(i, 0.0, kv.second.ey);
    ++i;
  }
  TCanvas c(("c_"+metric+"_perrun").c_str(), ("per-run: "+metric).c_str(), 900, 600);
  gr->SetTitle((metric+" (per run);Run;"+metric).c_str());
  gr->Draw("AP");
  c.SaveAs(("out/metric_"+metric+"_perrun.pdf").c_str());
  c.SaveAs(("out/metric_"+metric+"_perrun.png").c_str());
}

// Usage: .x macros/aggregate_per_run_v2.C("metrics.conf","ivar")
// weighting: "ivar" (default) | "entries" | "mean"
void aggregate_per_run_v2(const char* conf="metrics.conf", const char* weighting="ivar")
{
  auto defs = load_conf(conf);
  if (defs.empty()) { std::cerr<<"[ERROR] no metrics in "<<conf<<"\n"; return; }
  std::string W = weighting;

  for (auto& kv : defs) {
    const auto& mname  = kv.first;
    const auto& method = kv.second.method;

    std::string inpath = "out/metrics_"+mname+".csv";
    std::vector<Row> rows;
    if (!read_metric_csv(inpath, rows) || rows.empty()) {
      std::cerr<<"[WARN] no rows in "<<inpath<<"\n";
      continue;
    }

    std::map<int, std::vector<Row>> g;
    for (auto& r: rows) g[r.run].push_back(r);

    std::map<int,Agg> byrun;
    for (auto& runvec : g) {
      const auto& vec = runvec.second;
      Agg a;
      if (method=="sum") a = agg_sum(vec);
      else if (W=="mean") a = agg_mean(vec);
      else if (W=="entries") a = agg_wmean_entries(vec);
      else a = agg_wmean_ivar(vec); // default
      byrun[runvec.first] = a;
    }
    write_and_plot(mname, byrun);
    std::cout<<"[AGG] per-run "<<mname<<" using weighting="<<W<<"\n";
  }
  std::cout<<"[DONE] per-run aggregation.\n";
}
