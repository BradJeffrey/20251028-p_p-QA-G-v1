#include <TCanvas.h>
#include <TGraph.h>
#include <TSystem.h>

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct Row { int run; double y; double ey; };

static bool read_perrun(const std::string& path, std::map<int,Row>& m) {
  std::ifstream in(path); if(!in) return false;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header){ header=false; continue; }
    if (s.empty()) continue;
    size_t p1=s.find(','), p2=s.find(',',p1+1);
    if (p1==std::string::npos || p2==std::string::npos) continue;
    Row r; r.run=std::stoi(s.substr(0,p1));
    r.y  =std::stod(s.substr(p1+1,p2-p1-1));
    r.ey =std::stod(s.substr(p2+1));
    m[r.run]=r;
  }
  return !m.empty();
}

// Usage: .x macros/derive_metric_pair.C("A","B","diff","outname")
//   op: "diff" or "ratio"
void derive_metric_pair(const char* mA, const char* mB, const char* op, const char* outname)
{
  std::string fA=std::string("out/metrics_")+mA+"_perrun.csv";
  std::string fB=std::string("out/metrics_")+mB+"_perrun.csv";
  std::map<int,Row> A,B;
  if(!read_perrun(fA,A)||!read_perrun(fB,B)){ std::cerr<<"[ERR] missing per-run data\n"; return; }

  std::vector<double> xs, ys;
  std::ofstream out(std::string("out/metrics_")+outname+"_perrun.csv");
  out<<"run,value,error\n";
  for (auto& kv: A) {
    int run=kv.first;
    if (!B.count(run)) continue;
    double y = 0.0, e=0.0;
    if (std::string(op)=="diff") {
      y = A[run].y - B[run].y;
      e = std::hypot(A[run].ey, B[run].ey);
    } else { // ratio
      if (B[run].y==0 || !std::isfinite(B[run].y)) continue;
      y = A[run].y / B[run].y;
      // uncorrelated error propagation
      double r = y;
      double rel2 = std::pow(A[run].ey/std::max(1e-12,std::fabs(A[run].y)),2)
                  + std::pow(B[run].ey/std::max(1e-12,std::fabs(B[run].y)),2);
      e = std::fabs(r) * std::sqrt(std::max(0.0, rel2));
    }
    out<<run<<","<<y<<","<<e<<"\n";
    xs.push_back(run); ys.push_back(y);
  }
  out.close();

  auto gr = new TGraph(xs.size());
  for (size_t i=0;i<xs.size();++i) gr->SetPoint(i, xs[i], ys[i]);
  gr->SetTitle((std::string(outname)+";Run;"+outname).c_str());
  TCanvas c(("c_"+std::string(outname)).c_str(), outname, 900, 600);
  gr->Draw("AP");
  gSystem->mkdir("out", true);
  c.SaveAs((std::string("out/metric_")+outname+"_perrun.pdf").c_str());
  c.SaveAs((std::string("out/metric_")+outname+"_perrun.png").c_str());
  std::cout<<"[DONE] wrote out/metrics_"<<outname<<"_perrun.csv and plot\n";
}
