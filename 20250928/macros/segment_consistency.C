#include <TCanvas.h>
#include <TGraph.h>
#include <TSystem.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <tuple>
#include <vector>

struct Row { int run; int seg; double y; double ey; };

static bool read_perfile(const std::string& path, std::vector<Row>& rows) {
  std::ifstream in(path); if(!in) return false;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header){ header=false; continue; }
    if (s.empty()) continue;
    // run,segment,file,value,error,weight?
    size_t p1=s.find(','), p2=s.find(',',p1+1), p3=s.find(',',p2+1), p4=s.find(',',p3+1);
    if (p4==std::string::npos) continue;
    Row r; r.run=std::stoi(s.substr(0,p1)); r.seg=std::stoi(s.substr(p1+1,p2-p1-1));
    r.y  =std::stod(s.substr(p3+1,p4-p3-1)); r.ey=std::stod(s.substr(p4+1));
    rows.push_back(r);
  }
  return !rows.empty();
}

static std::tuple<double,double,int> seg_cv(const std::vector<Row>& segs) {
  std::vector<double> v; v.reserve(segs.size());
  for (auto&s: segs) if (std::isfinite(s.y)) v.push_back(s.y);
  int n=v.size(); if (n<2) return {std::numeric_limits<double>::quiet_NaN(),0.0,0};
  double mean = std::accumulate(v.begin(), v.end(), 0.0)/n;
  double ss = 0.0; for (auto x: v){ double d=x-mean; ss+=d*d; }
  double sd = std::sqrt(ss/(n-1));
  double cv = (mean!=0)? sd/std::fabs(mean) : std::numeric_limits<double>::quiet_NaN();
  return {cv, mean, n};
}

// Usage: .x macros/segment_consistency.C("cluster_size_intt_mean")
void segment_consistency(const char* metric="cluster_size_intt_mean")
{
  std::string f=std::string("out/metrics_")+metric+".csv";
  std::vector<Row> rows;
  if(!read_perfile(f, rows)){ std::cerr<<"[ERR] missing "<<f<<"\n"; return; }

  std::map<int,std::vector<Row>> byrun;
  for (auto&r: rows) byrun[r.run].push_back(r);

  std::vector<double> xs, ys;
  std::ofstream out(std::string("out/metrics_")+metric+"_segcv_perrun.csv");
  out<<"run,value,error\n";
  for (auto& kv: byrun) {
    auto [cv, mean, n] = seg_cv(kv.second);
    out<<kv.first<<","<<cv<<",0\n";
    xs.push_back(kv.first); ys.push_back(cv);
  }
  out.close();

  auto gr = new TGraph(xs.size());
  for (size_t i=0;i<xs.size();++i) gr->SetPoint(i,xs[i],ys[i]);
  gr->SetTitle((std::string(metric)+" segment CV;Run;segment CV").c_str());
  TCanvas c(("c_"+std::string(metric)+"_segcv").c_str(),"segcv",900,600);
  gr->Draw("AP");
  gSystem->mkdir("out", true);
  c.SaveAs((std::string("out/metric_")+metric+"_segcv_perrun.pdf").c_str());
  c.SaveAs((std::string("out/metric_")+metric+"_segcv_perrun.png").c_str());
  std::cout<<"[DONE] wrote per-run segment CV for "<<metric<<"\n";
}
