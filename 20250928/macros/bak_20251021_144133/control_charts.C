#include <TCanvas.h>
#include <TGraph.h>
#include <TLine.h>
#include <TSystem.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

struct Row { int run; double y; double ey; };

static bool read_csv(const std::string& p, std::vector<Row>& v){
  std::ifstream in(p); if(!in) return false;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header){ header=false; continue; }
    if (s.empty()) continue;
    size_t p1=s.find(','), p2=s.find(',',p1+1);
    if (p1==std::string::npos||p2==std::string::npos) continue;
    Row r; r.run=std::stoi(s.substr(0,p1));
    r.y  =std::stod(s.substr(p1+1,p2-p1-1));
    r.ey =std::stod(s.substr(p2+1));
    v.push_back(r);
  }
  return !v.empty();
}
static double median(std::vector<double> v){
  if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
  size_t n=v.size(); std::nth_element(v.begin(), v.begin()+n/2, v.end());
  double m=v[n/2];
  if (n%2==0){ std::nth_element(v.begin(), v.begin()+n/2-1, v.end()); m=0.5*(m+v[n/2-1]); }
  return m;
}
static double mad(std::vector<double> v, double med){
  for (auto& x: v) x=std::fabs(x-med);
  return median(v);
}

// Usage: .x macros/control_charts.C("cluster_size_intt_mean",3.0,0.5,5.0)
void control_charts(const char* metric="cluster_size_intt_mean",
                    double zShewhart=3.0, double kCUSUM=0.5, double HCUSUM=5.0)
{
  std::string f = std::string("out/metrics_")+metric+"_perrun.csv";
  std::vector<Row> r; if(!read_csv(f,r)||r.size()<3){ std::cerr<<"[ERR] need >=3 points\n"; return; }

  std::vector<double> v; v.reserve(r.size());
  for (auto& e: r) v.push_back(e.y);
  double med = median(v);
  double rsig = 1.4826 * mad(v, med);
  if (!(rsig>0)) rsig = 1.0;

  // Shewhart flags
  std::ofstream out((std::string("out/qc_control_")+metric+".csv").c_str());
  out<<"run,value,Zrobust,Shewhart_OOC,CUSUM_pos,CUSUM_neg,flag\n";

  // CUSUM (one-sided, positive & negative)
  double Cp=0.0, Cn=0.0; // accumulate deviations beyond k
  for (auto& e : r){
    double z = (e.y - med)/rsig;
    bool shew = std::fabs(z) > zShewhart;
    double dev = (e.y - med)/rsig;
    Cp = std::max(0.0, Cp + (dev - kCUSUM));
    Cn = std::max(0.0, Cn + (-dev - kCUSUM));
    bool cusum = (Cp>HCUSUM || Cn>HCUSUM);
    std::string flag = (shew||cusum) ? "WARN" : "PASS";
    out<<e.run<<","<<e.y<<","<<z<<","<<(shew?1:0)<<","<<Cp<<","<<Cn<<","<<flag<<"\n";
  }
  out.close();

  // Plot value + control limits
  std::vector<double> xs, ys;
  for (auto& e: r){ xs.push_back(e.run); ys.push_back(e.y); }
  auto gr = new TGraph(xs.size());
  for (size_t i=0;i<xs.size();++i) gr->SetPoint(i,xs[i],ys[i]);

  TCanvas c(("c_ctrl_"+std::string(metric)).c_str(),"control",1000,700);
  gr->SetTitle((std::string(metric)+" control chart;Run;"+metric).c_str());
  gr->Draw("AP");
  double xmin=xs.front(), xmax=xs.back();
  auto drawH = [&](double y, int col, int sty){
    TLine L(xmin,y,xmax,y); L.SetLineColor(col); L.SetLineStyle(sty); L.Draw("SAME");
  };
  drawH(med, kBlack, 1);
  drawH(med + zShewhart*rsig, kRed+1, 7);
  drawH(med - zShewhart*rsig, kRed+1, 7);
  gSystem->mkdir("out", true);
  c.SaveAs((std::string("out/metric_")+metric+"_control.pdf").c_str());
  c.SaveAs((std::string("out/metric_")+metric+"_control.png").c_str());
  std::cout<<"[DONE] control charts for "<<metric<<"\n";
}
