#include <TCanvas.h>
#include <TGraphErrors.h>
#include <TF1.h>
#include <TLine.h>
#include <TSystem.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

struct Row { int run; double y; double ey; };

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
    if (!std::isfinite(r.ey) || r.ey<=0) r.ey = 1.0; // safety
    if (std::isfinite(r.y)) rows.push_back(r);
  }
  return !rows.empty();
}

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
  // Fit y = a + b*x (x=run), weights w=1/ey^2. Return slope b, err_b, pval (Z-test).
  double Sw=0,Sx=0,Sy=0,Sxx=0,Sxy=0;
  for (auto&r: rows) {
    double w = (r.ey>0 && std::isfinite(r.ey)) ? 1.0/(r.ey*r.ey) : 1.0;
    Sw += w; Sx += w*r.run; Sy += w*r.y; Sxx += w*r.run*r.run; Sxy += w*r.run*r.y;
  }
  double D = Sw*Sxx - Sx*Sx;
  if (D<=0) return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), 1.0};
  double b = (Sw*Sxy - Sx*Sy)/D;
  // Estimate variance via residuals:
  double a = (Sy - b*Sx)/Sw;
  double rss=0, dof=0;
  for (auto&r: rows) {
    double w = (r.ey>0 && std::isfinite(r.ey)) ? 1.0/(r.ey*r.ey) : 1.0;
    double res = r.y - (a + b*r.run);
    rss += w*res*res;
    dof += 1.0;
  }
  dof = std::max(1.0, dof - 2.0);
  double sigma2 = rss/dof; // reduced
  double var_b = sigma2 * Sw / D;
  double eb = std::sqrt(std::max(0.0, var_b));
  double Z = (eb>0) ? b/eb : 0.0;
  // two-sided p-value from normal approx
  double p = std::erfc(std::fabs(Z)/std::sqrt(2.0));
  return {b, eb, p};
}

static std::tuple<int,double> changepoint_bic_shift(const std::vector<Row>& rows) {
  // Single change-point model: two weighted means vs one global mean. Return (run_at_change, deltaBIC)
  const int n = (int)rows.size();
  if (n < 6) return {-1, 0.0};
  auto sse_const = [&]() {
    double sw=0, swy=0;
    for (auto&r: rows){ double w=1.0/(r.ey*r.ey); sw+=w; swy+=w*r.y; }
    double mu = swy/sw;
    double sse=0; for (auto&r: rows){ double w=1.0/(r.ey*r.ey); double d=r.y-mu; sse += w*d*d; }
    return sse;
  }();
  double best_bic = std::numeric_limits<double>::infinity();
  int best_k = -1;
  // enforce minimum points per side
  int min_side = std::max(3, n/10);
  for (int k=min_side; k<=n-min_side; ++k) {
    double sw1=0,swy1=0, sw2=0,swy2=0;
    for (int i=0;i<k;i++){ double w=1.0/(rows[i].ey*rows[i].ey); sw1+=w; swy1+=w*rows[i].y; }
    for (int i=k;i<n;i++){ double w=1.0/(rows[i].ey*rows[i].ey); sw2+=w; swy2+=w*rows[i].y; }
    if (sw1<=0 || sw2<=0) continue;
    double m1=swy1/sw1, m2=swy2/sw2;
    double sse=0;
    for (int i=0;i<k;i++){ double w=1.0/(rows[i].ey*rows[i].ey); double d=rows[i].y-m1; sse+=w*d*d; }
    for (int i=k;i<n;i++){ double w=1.0/(rows[i].ey*rows[i].ey); double d=rows[i].y-m2; sse+=w*d*d; }
    // BIC ≈ SSE + k_params * ln(n), k_params: 2 means -> 2
    double bic = sse + 2.0*std::log(n);
    if (bic < best_bic) { best_bic=bic; best_k=k; }
  }
  // baseline model params: 1 mean
  double bic0 = sse_const + 1.0*std::log(n);
  double dBIC = bic0 - best_bic; // >10 strong evidence for change
  int run_at_change = (best_k>0 && best_k<n) ? rows[best_k].run : -1;
  return {run_at_change, dBIC};
}

static std::vector<Row> ewma(const std::vector<Row>& rows, double lambda=0.3) {
  std::vector<Row> s; s.reserve(rows.size());
  double m = rows[0].y;
  for (size_t i=0;i<rows.size();++i){
    m = lambda*rows[i].y + (1.0-lambda)*m;
    s.push_back({rows[i].run, m, rows[i].ey});
  }
  return s;
}

static void write_report(const std::string& metric,
                         const std::vector<Row>& rows,
                         double slope, double eslope, double pval,
                         int cp_run, double dBIC,
                         double med, double robust_sigma,
                         const std::string& txtpath, const std::string& csvsum)
{
  // text
  std::ofstream t(txtpath);
  t<<std::fixed<<std::setprecision(6);
  t<<"metric: "<<metric<<"\n";
  t<<"N: "<<rows.size()<<"\n";
  t<<"median: "<<med<<"  robust_sigma (1.4826*MAD): "<<robust_sigma<<"\n";
  t<<"trend slope: "<<slope<<" +/- "<<eslope<<"  (two-sided p="<<pval<<")\n";
  t<<"change-point: "<<(cp_run>=0?std::to_string(cp_run):"none")
   <<"  dBIC="<<dBIC<<"  (>=10 strong)\n";
  t.close();
  // CSV summary (append)
  std::ofstream c(csvsum, std::ios::app);
  c<<metric<<","<<rows.size()<<","<<med<<","<<robust_sigma<<","<<slope<<","<<eslope<<","<<pval<<","<<cp_run<<","<<dBIC<<"\n";
}

// Usage: .x macros/analyze_consistency.C("metrics.conf")
void analyze_consistency(const char* conf="metrics.conf")
{
  // which metrics to analyze? derive from conf
  std::vector<std::string> metrics;
  {
    std::ifstream in(conf); std::string line;
    while (std::getline(in,line)) {
      if (line.empty() || line[0]=='#') continue;
      size_t p=line.find(','); if (p==std::string::npos) continue;
      metrics.push_back(std::string(line.begin(), line.begin()+p));
    }
  }
  if (metrics.empty()) { std::cerr<<"[ERROR] no metrics in "<<conf<<"\n"; return; }

  gSystem->mkdir("out", kTRUE);
  std::string summary_csv = "out/consistency_summary.csv";
  std::ofstream(summary_csv)<<"metric,N,median,robust_sigma,slope,eslope,pval,cp_run,dBIC\n";

  for (auto& m : metrics) {
    std::string perrun = "out/metrics_"+m+"_perrun.csv";
    std::vector<Row> rows;
    if (!read_perrun_csv(perrun, rows) || rows.size()<3) {
      std::cerr<<"[INFO] skip "<<m<<" (insufficient per-run points)\n"; continue;
    }
    // summary stats
    std::vector<double> vals; vals.reserve(rows.size());
    for (auto&r: rows) vals.push_back(r.y);
    double med = median(vals);
    double rsig = 1.4826 * mad(vals, med); // robust sigma

    // trend
    auto [slope, eslope, pval] = weighted_linfit(rows);

    // change-point (BIC)
    auto [cp_run, dBIC] = changepoint_bic_shift(rows);

    // EWMA for plotting
    auto sm = ewma(rows, 0.3);

    // write report
    std::string txt = "out/consistency_"+m+"_analysis.txt";
    write_report(m, rows, slope, eslope, pval, cp_run, dBIC, med, rsig, txt, summary_csv);

    // annotated plot
    auto gr = new TGraphErrors(rows.size());
    for (size_t i=0;i<rows.size();++i){
      gr->SetPoint(i, rows[i].run, rows[i].y);
      gr->SetPointError(i, 0.0, rows[i].ey);
    }
    gr->SetName(("gr_"+m+"_annot").c_str());
    gr->SetTitle((m+";Run;"+m).c_str());

    auto grs = new TGraph(sm.size());
    grs->SetName(("gr_"+m+"_ewma").c_str());
    for (size_t i=0;i<sm.size();++i) grs->SetPoint(i, sm[i].run, sm[i].y);

    TCanvas c(("c_"+m+"_annot").c_str(), (m+" analysis").c_str(), 1000, 700);
    gr->Draw("AP");
    grs->SetLineStyle(2);
    grs->Draw("L SAME");

    // vertical line for change-point if strong
    if (cp_run>=0 && dBIC>=10.0) {
      double ymin = TMath::MinElement(gr->GetN(), gr->GetY());
      double ymax = TMath::MaxElement(gr->GetN(), gr->GetY());
      TLine L(cp_run, ymin, cp_run, ymax);
      L.SetLineColor(kRed); L.SetLineStyle(7); L.SetLineWidth(2);
      L.Draw("SAME");
    }

    c.SaveAs((std::string("out/metric_")+m+"_perrun_annot.pdf").c_str());
    c.SaveAs((std::string("out/metric_")+m+"_perrun_annot.png").c_str());
  }
  std::cout<<"[DONE] wrote "<<summary_csv<<" and per-metric analyses in out/.\n";
}
