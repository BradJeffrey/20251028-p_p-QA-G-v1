#include <TCanvas.h>
#include <TGraphErrors.h>
#include <TF1.h>

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct R { int run; double y; double ey; };
static bool read_csv(const std::string& p, std::vector<R>& v){
  std::ifstream in(p); if(!in) return false;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header){ header=false; continue; }
    if (s.empty()) continue;
    size_t p1=s.find(','), p2=s.find(',',p1+1);
    if (p1==std::string::npos||p2==std::string::npos) continue;
    R r; r.run=std::stoi(s.substr(0,p1));
    r.y  =std::stod(s.substr(p1+1,p2-p1-1));
    r.ey =std::stod(s.substr(p2+1));
    v.push_back(r);
  }
  return !v.empty();
}

// Usage: .x macros/correlate_metrics.C("intt_nhit_mean","mvtx_nhits_l0_mean")
void correlate_metrics(const char* m1="intt_nhit_mean", const char* m2="mvtx_nhits_l0_mean")
{
  std::string f1 = std::string("out/metrics_")+m1+"_perrun.csv";
  std::string f2 = std::string("out/metrics_")+m2+"_perrun.csv";
  std::vector<R> a,b; if(!read_csv(f1,a)||!read_csv(f2,b)){ std::cerr<<"missing data\n"; return; }

  // join by run
  std::map<int,std::pair<double,double>> J;
  for (auto& r: a) J[r.run].first  = r.y;
  for (auto& r: b) J[r.run].second = r.y;

  std::vector<double> xs, ys; xs.reserve(J.size()); ys.reserve(J.size());
  for (auto& kv: J) if (std::isfinite(kv.second.first) && std::isfinite(kv.second.second)) {
    xs.push_back(kv.second.first); ys.push_back(kv.second.second);
  }
  if (xs.size()<3) { std::cerr<<"too few points\n"; return; }

  auto gr = new TGraph(xs.size());
  for (size_t i=0;i<xs.size();++i) gr->SetPoint(i,xs[i],ys[i]);
  gr->SetTitle((std::string(m2)+" vs "+m1+";"+m1+";"+m2).c_str());

  TCanvas c("c_corr","correlation",800,700);
  gr->Draw("AP");
  TF1 f("lin","pol1"); gr->Fit(&f,"Q");
  c.SaveAs((std::string("out/corr_")+m2+"_vs_"+m1+".png").c_str());

  // Pearson R
  double mx=0,my=0; for (size_t i=0;i<xs.size();++i){ mx+=xs[i]; my+=ys[i]; }
     mx/=xs.size(); my/=ys.size();

  for (size_t i=0;i<xs.size();++i){ double dx=xs[i]-mx, dy=ys[i]-my; sx+=dx*dx; sy+=dy*dy; sxy+=dx*dy; }
  double R = sxy/std::sqrt(sx*sy);
  std::ofstream out("out/corr_summary.txt", std::ios::app);
  out<<m2<<" vs "<<m1<<": N="<<xs.size()<<"  PearsonR="<<R<<"  slope="<<f.GetParameter(1)<<"\n";
  std::cout<<"[DONE] corr "<<m2<<" vs "<<m1<<"  R="<<R<<"\n";
}
