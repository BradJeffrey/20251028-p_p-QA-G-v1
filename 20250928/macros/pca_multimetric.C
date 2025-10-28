#include <TCanvas.h>
#include <TPrincipal.h>
#include <TGraph.h>
#include <TSystem.h>
#include <TLatex.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Read CSV into header + rows
static bool read_csv(const std::string& path, std::vector<std::string>& cols,
                     std::vector<int>& runs, std::vector<std::vector<double>>& X)
{
  std::ifstream in(path); if(!in) return false;
  std::string s; if(!std::getline(in,s)) return false; // header
  std::stringstream sh(s); std::string t;
  while (std::getline(sh,t,',')) cols.push_back(t);

  int runcol = 0;
  while (std::getline(in,s)) {
    if (s.empty()) continue;
    std::stringstream ss(s); std::vector<std::string> toks; std::string u;
    while (std::getline(ss,u,',')) toks.push_back(u);
    if (toks.size()!=cols.size()) continue;
    runs.push_back(std::stoi(toks[runcol]));
    std::vector<double> row;
    for (size_t j=1;j<toks.size();++j) row.push_back( toks[j].empty()? std::numeric_limits<double>::quiet_NaN() : std::stod(toks[j]) );
    X.push_back(row);
  }
  return !X.empty();
}

// Usage: .x macros/pca_multimetric.C("out/metrics_perrun_wide.csv")
void pca_multimetric(const char* wide="out/metrics_perrun_wide.csv")
{
  std::vector<std::string> cols; std::vector<int> runs; std::vector<std::vector<double>> X;
  if (!read_csv(wide, cols, runs, X)) { std::cerr<<"[ERR] missing "<<wide<<"\n"; return; }
  // choose a subset of columns (skip run)
  std::set<std::string> keep = {
    "cluster_size_intt_mean",
    "cluster_phi_intt_rms",
    "intt_adc_peak",
    "intt_hits_asym",
    "intt_phi_uniform_r1",
    "intt_adc_median_p50",
    "mvtx_cluster_size_mean",
    "mvtx_phi_uniform_r1"
  };
  // build matrix (rows = runs, cols = chosen metrics)
  std::vector<int> useIdx;
  for (size_t j=1;j<cols.size();++j) if (keep.count(cols[j])) useIdx.push_back(j-1);
  if (useIdx.empty()) { std::cerr<<"[INFO] no matching columns found\n"; return; }

  int R = (int)X.size(), C=(int)useIdx.size();
  TPrincipal pca(C,"ND"); // normalize & demean
  std::vector<double> row(C);
  for (int i=0;i<R;++i){
    bool ok=true;
    for (int j=0;j<C;++j){
      double v = X[i][useIdx[j]];
      if (!std::isfinite(v)) { ok=false; break; }
      row[j]=v;
    }
    if (ok) pca.AddRow(row.data());
  }
  pca.MakePrincipals();

  // Project each row (skip NaNs rows)
  std::vector<double> xs, ys; xs.reserve(R); ys.reserve(R);
  std::vector<int> rr; rr.reserve(R);
  for (int i=0;i<R;++i){
    bool ok=true;
    for (int j=0;j<C;++j) if (!std::isfinite(X[i][useIdx[j]])) { ok=false; break; }
    if (!ok) continue;
    TVectorD v(C);
    for (int j=0;j<C;++j) v[j]=X[i][useIdx[j]];
    TVectorD y = pca.Evaluate(v);
    xs.push_back(y[0]); ys.push_back(y[1]); rr.push_back(runs[i]);
  }

  // plot scatter
  auto gr = new TGraph(xs.size());
  for (size_t i=0;i<xs.size();++i) gr->SetPoint(i,xs[i],ys[i]);
  double var1 = pca.GetEigenValues()[0];
  double var2 = pca.GetEigenValues()[1];
  double vare = 0; for (int j=0;j<C;++j) vare += pca.GetEigenValues()[j];

  TCanvas c("c_pca","PCA QA map",900,700);
  gr->SetTitle(Form("PCA of QA metrics;PC1 (%.1f%%);PC2 (%.1f%%)", 100*var1/vare, 100*var2/vare));
  gr->Draw("AP");
  gSystem->mkdir("out", true);
  c.SaveAs("out/qa_pca_pc12.pdf");
  c.SaveAs("out/qa_pca_pc12.png");

  std::cout<<"[DONE] PCA map saved. PC1 explains "<<100*var1/vare<<"%.\n";
}
