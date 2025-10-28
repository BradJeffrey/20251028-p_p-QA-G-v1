#include <TCanvas.h>
#include <TGraphErrors.h>
#include <TF1.h>
#include <TLine.h>
#include <TBox.h>
#include <TLatex.h>
#include <TSystem.h>
#include <TMath.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

struct Row { int run; double y; double ey; };

// ---------- IO ----------
static std::string trim(std::string s) {
  auto issp=[](unsigned char c){return std::isspace(c);};
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){return !issp(c);} ));
  s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){return !issp(c);} ).base(), s.end());
  return s;
}
static bool read_perrun_csv(const std::string& path, std::vector<Row>& rows) {
  std::ifstream in(path);
  if (!in) return false;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header) { header=false; continue; }
    if (s.empty()) continue;
    size_t p1=s.find(','); if(p1==std::string::npos) continue;
    size_t p2=s.find(',',p1+1); if(p2==std::string::npos) continue;
    Row r; r.run = std::stoi(s.substr(0,p1));
    r.y   = std::stod(s.substr(p1+1,p2-p1-1));
    r.ey  = std::stod(s.substr(p2+1));
    if (!std::isfinite(r.ey) || r.ey<=0) r.ey = 1.0;
    if (std::isfinite(r.y)) rows.push_back(r);
  }
  return !rows.empty();
}
static std::vector<std::string> metrics_from_conf(const char* conf) {
  std::vector<std::string> m;
  std::ifstream in(conf); std::string line;
  while (std::getline(in,line)) {
    line=trim(line); if (line.empty()||line[0]=='#') continue;
    size_t p=line.find(','); if (p==std::string::npos) continue;
    m.push_back(line.substr(0,p));
  }
  return m;
}

// ---------- stats ----------
static double median(std::vector<double> v) {
  if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
  size_t n=v.size(); std::nth_element(v.begin(), v.begin()+n/2, v.end());
  double m=v[n/2];
  if (n%2==0) { std::nth_element(v.begin(), v.begin()+n/2-1, v.end()); m=0.5*(m+v[n/2-1]); }
  return m;
}
static double mad(std::vector<double> v, double med) {
  for (auto& x: v) x = std::fabs(x - med);
  return median(v);
}
static std::tuple<double,double,double> weighted_linfit(const std::vector<Row>& rows) {
  double Sw=0,Sx=0,Sy=0,Sxx=0,Sxy=0;
  for (auto&r: rows) { double w=1.0/(r.ey*r.ey); Sw+=w; Sx+=w*r.run; Sy+=w*r.y; Sxx+=w*r.run*r.run; Sxy+=w*r.run*r.y; }
  double D = Sw*Sxx - Sx*Sx;
  if (D<=0) return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), 1.0};
  double b = (Sw*Sxy - Sx*Sy)/D; double a = (Sy - b*Sx)/Sw;
  double rss=0, dof=0; for (auto&r: rows){ double w=1.0/(r.ey*r.ey); double res=r.y-(a+b*r.run); rss+=w*res*res; dof+=1; }
  dof = std::max(1.0, dof-2.0); double sigma2=rss/dof; double var_b = sigma2 * Sw / D; double eb=std::sqrt(std::max(0.0,var_b));
  double Z = (eb>0)? b/eb : 0.0; double p = std::erfc(std::fabs(Z)/std::sqrt(2.0));
  return {b,eb,p};
}
static std::tuple<int,double> changepoint_bic_shift(const std::vector<Row>& rows) {
  const int n=(int)rows.size(); if (n<6) return {-1,0.0};
  auto sse_const = [&](){
    double sw=0,swy=0; for (auto&r: rows){ double w=1.0/(r.ey*r.ey); sw+=w; swy+=w*r.y; }
    double mu=swy/sw; double sse=0; for (auto&r: rows){ double w=1.0/(r.ey*r.ey); double d=r.y-mu; sse+=w*d*d; }
    return sse;
  }();
  double best=std::numeric_limits<double>::infinity(); int bestk=-1;
  int min_side=std::max(3,n/10);
  for (int k=min_side;k<=n-min_side;++k){
    double sw1=0,swy1=0,sw2=0,swy2=0;
    for (int i=0;i<k;i++){ double w=1.0/(rows[i].ey*rows[i].ey); sw1+=w; swy1+=w*rows[i].y; }
    for (int i=k;i<n;i++){ double w=1.0/(rows[i].ey*rows[i].ey); sw2+=w; swy2+=w*rows[i].y; }
    if (sw1<=0||sw2<=0) continue; double m1=swy1/sw1, m2=swy2/sw2;
    double sse=0;
    for (int i=0;i<k;i++){ double w=1.0/(rows[i].ey*rows[i].ey); double d=rows[i].y-m1; sse+=w*d*d; }
    for (int i=k;i<n;i++){ double w=1.0/(rows[i].ey*rows[i].ey); double d=rows[i].y-m2; sse+=w*d*d; }
    double bic = sse + 2.0*std::log(n);
    if (bic<best) { best=bic; bestk=k; }
  }
  double bic0 = sse_const + 1.0*std::log(n);
  double dBIC = bic0 - best; int run_at = (bestk>0 && bestk<n) ? rows[bestk].run : -1;
  return {run_at, dBIC};
}
static std::vector<Row> ewma(const std::vector<Row>& rows, double lambda=0.3) {
  std::vector<Row> s; s.reserve(rows.size());
  double m = rows[0].y;
  for (size_t i=0;i<rows.size();++i){ m = lambda*rows[i].y + (1.0-lambda)*m; s.push_back({rows[i].run, m, rows[i].ey}); }
  return s;
}

// ---------- markers ----------
struct Marker { std::string type; std::string label; int start; int end; };
static std::vector<Marker> read_markers(const char* path) {
  std::vector<Marker> v; if (!path || !*path) return v;
  std::ifstream in(path); if (!in) return v;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header){ header=false; continue; }
    if (s.empty()) continue;
    std::stringstream ss(s); std::string t,l,a,b;
    if (!std::getline(ss,t,',')) continue;
    if (!std::getline(ss,l,',')) l="";
    if (!std::getline(ss,a,',')) a="-1";
    if (!std::getline(ss,b,',')) b="-1";
    Marker m{t,l, std::stoi(a), std::stoi(b)};
    v.push_back(m);
  }
  return v;
}
static void draw_markers(const std::vector<Marker>& ms, double ymin, double ymax) {
  for (auto&m: ms) {
    if (m.type=="line") {
      TLine L(m.start, ymin, m.start, ymax); L.SetLineColor(kBlue+1); L.SetLineStyle(7); L.Draw("SAME");
    } else if (m.type=="band") {
      TBox B(m.start, ymin, m.end, ymax); B.SetFillColorAlpha(kOrange, 0.15); B.SetLineColor(kOrange+2); B.Draw("SAME");
    }
  }
}

// ---------- thresholds (optional) ----------
struct Thresh { double lo; double hi; };
static std::map<std::string,Thresh> read_thresholds(const char* path) {
  std::map<std::string,Thresh> T; if (!path||!*path) return T;
  std::ifstream in(path); if(!in) return T;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header){ header=false; continue; }
    if (s.empty()) continue;
    std::stringstream ss(s); std::string m,lo,hi;
    if (!std::getline(ss,m,',')) continue;
    if (!std::getline(ss,lo,',')) lo="";
    if (!std::getline(ss,hi,',')) hi="";
    Thresh th{ std::istringstream(lo).good()? std::stod(lo): -std::numeric_limits<double>::infinity(),
               std::istringstream(hi).good()? std::stod(hi):  std::numeric_limits<double>::infinity() };
    T[m]=th;
  }
  return T;
}

static void write_report(const std::string& metric,
                         const std::vector<Row>& rows,
                         double med, double rsig,
                         double slope, double eslope, double pval,
                         int cp_run, double dBIC,
                         const std::string& txtpath, const std::string& csvsum)
{
  std::ofstream t(txtpath);
  t<<std::fixed<<std::setprecision(6);
  t<<"metric,"<<metric<<"\n";
  t<<"N,"<<rows.size()<<"\n";
  t<<"median,"<<med<<"\n";
  t<<"robust_sigma,"<<rsig<<"\n";
  t<<"slope,"<<slope<<"\n";
  t<<"eslope,"<<eslope<<"\n";
  t<<"pval,"<<pval<<"\n";
  t<<"changepoint_run,"<<(cp_run>=0?std::to_string(cp_run):"none")<<"\n";
  t<<"deltaBIC,"<<dBIC<<"\n";
  t.close();
  std::ofstream c(csvsum, std::ios::app);
  c<<metric<<","<<rows.size()<<","<<med<<","<<rsig<<","<<slope<<","<<eslope<<","<<pval<<","<<cp_run<<","<<dBIC<<"\n";
}

// Usage: .x macros/analyze_consistency_v2.C("metrics.conf","markers.csv","thresholds.csv")
void analyze_consistency_v2(const char* conf="metrics.conf",
                            const char* markers_csv="",
                            const char* thresholds_csv="")
{
  auto metrics = metrics_from_conf(conf);
  if (metrics.empty()) { std::cerr<<"[ERROR] no metrics in "<<conf<<"\n"; return; }

  auto markers = read_markers(markers_csv);
  auto ths     = read_thresholds(thresholds_csv);

  gSystem->mkdir("out", kTRUE);
  std::string summary_csv = "out/consistency_summary.csv";
  std::ofstream(summary_csv)<<"metric,N,median,robust_sigma,slope,eslope,pval,cp_run,dBIC\n";

  for (auto& m : metrics) {
    std::string perrun = "out/metrics_"+m+"_perrun.csv";
    std::vector<Row> rows;
    if (!read_perrun_csv(perrun, rows) || rows.size()<3) { std::cerr<<"[INFO] skip "<<m<<"\n"; continue; }

    std::vector<double> vals; vals.reserve(rows.size());
    for (auto&r: rows) vals.push_back(r.y);
    double med = median(vals);
    double rsig = 1.4826 * mad(vals, med);

    auto [slope, eslope, pval] = weighted_linfit(rows);
    auto [cp_run, dBIC]        = changepoint_bic_shift(rows);
    auto sm = [&](){ std::vector<Row> s; s.reserve(rows.size()); double mm=rows[0].y; for (auto&r: rows){ mm=0.3*r.y+0.7*mm; s.push_back({r.run,mm,r.ey}); } return s; }();

    std::string txt = "out/consistency_"+m+"_analysis.txt";
    write_report(m, rows, med, rsig, slope, eslope, pval, cp_run, dBIC, txt, summary_csv);

    // QC status per run
    std::ofstream qc(("out/qc_status_"+m+".csv").c_str());
    qc<<"run,value,status,reason\n";
    double tolZ=3.5;
    bool haveRange = ths.find(m)!=ths.end();
    for (auto&r: rows) {
      std::string st="PASS", reason="";
      if (haveRange) {
        auto th=ths[m];
        if (r.y < th.lo || r.y > th.hi) { st="FAIL"; reason="threshold"; }
      }
      double z = (rsig>0)? std::fabs(r.y - med)/rsig : 0.0;
      if (z>tolZ) { st = (st=="PASS"?"WARN":st); if (reason.size()) reason += "+"; reason += "robust_z"; }
      qc<<r.run<<","<<r.y<<","<<st<<","<<reason<<"\n";
    }
    qc.close();

    // Annotated plot
    auto gr = new TGraphErrors(rows.size());
    for (size_t i=0;i<rows.size();++i){ gr->SetPoint(i, rows[i].run, rows[i].y); gr->SetPointError(i, 0.0, rows[i].ey); }
    gr->SetName(("gr_"+m+"_annot").c_str()); gr->SetTitle((m+";Run;"+m).c_str());
    auto grs = new TGraph(sm.size()); for (size_t i=0;i<sm.size();++i) grs->SetPoint(i, sm[i].run, sm[i].y);

    TCanvas c(("c_"+m+"_annot").c_str(), (m+" analysis").c_str(), 1100, 750);
    gr->Draw("AP");
    grs->SetLineStyle(2); grs->Draw("L SAME");

    // thresholds as horizontal bands/lines
    if (haveRange) {
      double ymin = TMath::MinElement(gr->GetN(), gr->GetY());
      double ymax = TMath::MaxElement(gr->GetN(), gr->GetY());
      auto th=ths[m];
      double lo = std::isfinite(th.lo)? th.lo : ymin;
      double hi = std::isfinite(th.hi)? th.hi : ymax;
      TBox band(gr->GetX()[0], lo, gr->GetX()[gr->GetN()-1], hi);
      band.SetFillColorAlpha(kGreen+1, 0.06); band.SetLineColor(kGreen+2); band.Draw("SAME");
    }

    // change-point line if strong
    if (cp_run>=0 && dBIC>=10.0) {
      double ymin = TMath::MinElement(gr->GetN(), gr->GetY());
      double ymax = TMath::MaxElement(gr->GetN(), gr->GetY());
      TLine L(cp_run, ymin, cp_run, ymax); L.SetLineColor(kRed); L.SetLineStyle(7); L.SetLineWidth(2); L.Draw("SAME");
    }

    // markers (lines/bands)
    if (!markers.empty()) {
      double ymin = TMath::MinElement(gr->GetN(), gr->GetY());
      double ymax = TMath::MaxElement(gr->GetN(), gr->GetY());
      draw_markers(markers, ymin, ymax);
    }

    c.SaveAs((std::string("out/metric_")+m+"_perrun_annot.pdf").c_str());
    c.SaveAs((std::string("out/metric_")+m+"_perrun_annot.png").c_str());
  }
  std::cout<<"[DONE] wrote "<<summary_csv<<" and QC/status files in out/.\n";
}
