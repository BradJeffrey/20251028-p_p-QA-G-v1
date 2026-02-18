///////////////////////////////////////////////////////////////////////////////
// pca_multimetric.C — Multi-Metric Principal Component Analysis
//
// Reads the wide-format per-run CSV and performs PCA via SVD.
// Produces:
//   1. PC1 vs PC2 scatter plot with run labels
//   2. Scree plot (variance explained per PC, cumulative)
//   3. Loadings heatmap (which metrics drive each PC)
//   4. Mahalanobis distance outlier detection in PC1-PC2 space
//
// Outputs:
//   out/qa_pca_pc12.{png,pdf}      — PC1 vs PC2 scatter
//   out/qa_pca_scree.{png,pdf}     — scree plot
//   out/qa_pca_loadings.{png,pdf}  — loadings heatmap
//   out/pca_outliers.csv           — per-run Mahalanobis distance + outlier flag
//
// Usage:
//   root -l -b -q 'macros/pca_multimetric.C("out/metrics_perrun_wide.csv")'
///////////////////////////////////////////////////////////////////////////////

#include <TCanvas.h>
#include <TDecompSVD.h>
#include <TGraph.h>
#include <TH2D.h>
#include <TMatrixD.h>
#include <TStyle.h>
#include <TText.h>
#include <TVectorD.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

static bool ReadWideCSV(const std::string& path, std::vector<int>& runs, TMatrixD& X, std::vector<std::string>& cols){
  std::ifstream in(path); if(!in) return false;
  std::string line; if(!std::getline(in,line)) return false;
  std::stringstream hs(line); std::string cell; std::vector<std::string> header;
  while(std::getline(hs,cell,',')) header.push_back(cell);
  if(header.size()<3) return false;
  cols.assign(header.begin()+1, header.end());
  std::vector<std::vector<double>> rows;
  while(std::getline(in,line)){
    if(line.empty()) continue;
    std::stringstream ss(line);
    std::getline(ss,cell,','); runs.push_back(std::stoi(cell));
    std::vector<double> row; while(std::getline(ss,cell,',')){
      if(cell=="NaN"||cell=="nan"||cell=="") row.push_back(std::numeric_limits<double>::quiet_NaN());
      else row.push_back(std::stod(cell));
    } rows.push_back(row);
  }
  std::vector<std::vector<double>> clean; std::vector<int> cleanRuns;
  for(size_t i=0;i<rows.size();++i){ bool ok=true; for(double v:rows[i]) if(!(v==v)){ ok=false; break;} if(ok){ clean.push_back(rows[i]); cleanRuns.push_back(runs[i]); } }
  runs.swap(cleanRuns);
  int N=(int)clean.size(), P=(int)cols.size(); if(N<3||P<2) return false;
  X.ResizeTo(N,P);
  for(int i=0;i<N;++i) for(int j=0;j<P;++j) X(i,j)=clean[i][j];
  // Standardize columns (zero-mean, unit-variance)
  for(int j=0;j<P;++j){ double mu=0; for(int i=0;i<N;++i) mu+=X(i,j); mu/=N;
    double s2=0; for(int i=0;i<N;++i){ double d=X(i,j)-mu; s2+=d*d; } s2=(N>1)?s2/(N-1):0;
    double sd=(s2>0)?std::sqrt(s2):1.0; for(int i=0;i<N;++i) X(i,j)=(X(i,j)-mu)/sd; }
  return true;
}

void pca_multimetric(const char* wide_csv="out/metrics_perrun_wide.csv"){
  std::vector<int> runs; std::vector<std::string> cols; TMatrixD X;
  if(!ReadWideCSV(wide_csv,runs,X,cols)){ printf("[ERR] cannot read %s or not enough data\n",wide_csv); return; }
  int N=X.GetNrows(), P=X.GetNcols();
  TDecompSVD svd(X); if(!svd.Decompose()){ printf("[ERR] SVD failed\n"); return; }
  TMatrixD V=svd.GetV(); TVectorD S=svd.GetSig(); double vare=0; int K=std::min(P,N);
  for(int j=0;j<K;++j) vare+=S(j)*S(j);
  double var1=(K>0)?S(0)*S(0):0, var2=(K>1)?S(1)*S(1):0;
  double ev1=(vare>0)?100.0*var1/vare:0, ev2=(vare>0)?100.0*var2/vare:0;

  // Compute all scores (NxK)
  TMatrixD scores(N,K);
  for(int i=0;i<N;++i) for(int k=0;k<K;++k){ double s=0; for(int j=0;j<P;++j) s+=X(i,j)*V(j,k); scores(i,k)=s; }

  // Variance explained per component
  std::vector<double> var_explained(K);
  for(int k=0;k<K;++k) var_explained[k]=(vare>0)?100.0*S(k)*S(k)/vare:0;

  // ==========================================
  // 1. PC1 vs PC2 scatter plot
  // ==========================================
  {
    auto c=new TCanvas("c_pca","PC1 vs PC2",1000,800); auto g=new TGraph(N);
    for(int i=0;i<N;++i) g->SetPoint(i,scores(i,0),scores(i,1));
    g->SetTitle(Form("PCA on %d metrics (N=%d);PC1 (%.1f%%);PC2 (%.1f%%)",P,N,ev1,ev2));
    g->SetMarkerStyle(20); g->Draw("AP");
    auto txt=new TText(); txt->SetTextSize(0.02);
    for(int i=0;i<N;++i) if(i<3 || i>=N-3) txt->DrawText(scores(i,0),scores(i,1),Form("%d",runs[i]));
    c->Print("out/qa_pca_pc12.png"); c->Print("out/qa_pca_pc12.pdf");
    printf("[DONE] PCA scatter written to out/qa_pca_pc12.(png|pdf)\n");
  }

  // ==========================================
  // 2. Scree plot (variance explained)
  // ==========================================
  {
    auto c=new TCanvas("c_scree","Scree Plot",900,600);
    int nshow = std::min(K, 10);
    auto g=new TGraph(nshow);
    auto gcum=new TGraph(nshow);
    double cum=0;
    for(int k=0;k<nshow;++k){
      g->SetPoint(k, k+1, var_explained[k]);
      cum+=var_explained[k];
      gcum->SetPoint(k, k+1, cum);
    }
    g->SetTitle(Form("PCA Scree Plot (%d metrics);Principal Component;Variance Explained (%%)",P));
    g->SetMarkerStyle(20);
    g->SetMinimum(0);
    g->SetMaximum(105);
    g->Draw("APL");
    gcum->SetMarkerStyle(24);
    gcum->SetLineStyle(2);
    gcum->SetLineColor(kRed);
    gcum->SetMarkerColor(kRed);
    gcum->Draw("PL same");
    // Add labels
    auto txt=new TText(); txt->SetTextSize(0.025);
    for(int k=0;k<nshow;++k)
      txt->DrawText(k+1.1, var_explained[k]+2, Form("%.1f%%",var_explained[k]));
    auto txt2=new TText(); txt2->SetTextSize(0.025); txt2->SetTextColor(kRed);
    cum=0;
    for(int k=0;k<nshow;++k) {
      cum+=var_explained[k];
      if(k==0 || k==nshow-1 || k==1) txt2->DrawText(k+1.1, cum-3, Form("%.1f%%",cum));
    }
    c->Print("out/qa_pca_scree.png"); c->Print("out/qa_pca_scree.pdf");
    printf("[DONE] Scree plot written to out/qa_pca_scree.(png|pdf)\n");
  }

  // ==========================================
  // 3. Loadings heatmap
  // ==========================================
  {
    int npc = std::min(K, 5);
    gStyle->SetOptStat(0);
    auto h2 = new TH2D("h_loadings","PCA Loadings;Metric;Principal Component",
                         P,0,P, npc,0,npc);
    for(int j=0;j<P;++j) h2->GetXaxis()->SetBinLabel(j+1, cols[j].c_str());
    for(int k=0;k<npc;++k) h2->GetYaxis()->SetBinLabel(k+1, Form("PC%d (%.0f%%)",k+1,var_explained[k]));
    for(int j=0;j<P;++j) for(int k=0;k<npc;++k) h2->SetBinContent(j+1,k+1, V(j,k));
    h2->SetMinimum(-1); h2->SetMaximum(1);
    h2->GetXaxis()->SetLabelSize(0.02);
    h2->GetXaxis()->LabelsOption("v");

    int cw = std::max(800, P*35+200);
    auto c=new TCanvas("c_loadings","Loadings",cw,500);
    c->SetLeftMargin(0.15);
    c->SetBottomMargin(0.25);
    c->SetRightMargin(0.12);
    gStyle->SetPalette(kRedBlue);
    h2->Draw("COLZ");

    // Numeric labels for strong loadings
    auto txt=new TText(); txt->SetTextSize(0.018); txt->SetTextAlign(22);
    for(int j=0;j<P;++j) for(int k=0;k<npc;++k){
      double v=V(j,k);
      if(std::fabs(v)>0.3) txt->DrawText(j+0.5,k+0.5,Form("%.2f",v));
    }
    c->Print("out/qa_pca_loadings.png"); c->Print("out/qa_pca_loadings.pdf");
    printf("[DONE] Loadings heatmap written to out/qa_pca_loadings.(png|pdf)\n");
  }

  // ==========================================
  // 4. Mahalanobis distance outlier detection
  // ==========================================
  {
    double mu1=0, mu2=0;
    for(int i=0;i<N;++i){ mu1+=scores(i,0); mu2+=scores(i,1); }
    mu1/=N; mu2/=N;
    double s11=0,s12=0,s22=0;
    for(int i=0;i<N;++i){
      double d1=scores(i,0)-mu1, d2=scores(i,1)-mu2;
      s11+=d1*d1; s12+=d1*d2; s22+=d2*d2;
    }
    s11/=(N-1); s12/=(N-1); s22/=(N-1);

    double det = s11*s22 - s12*s12;
    if(det<=0){ printf("[WARN] Singular covariance in PC space; skipping outlier detection\n"); return; }
    double inv11 = s22/det, inv12 = -s12/det, inv22 = s11/det;

    std::ofstream f("out/pca_outliers.csv");
    f << "run,pc1,pc2,mahalanobis_d,outlier\n";
    int n_outliers = 0;
    double threshold = 3.0;
    for(int i=0;i<N;++i){
      double d1=scores(i,0)-mu1, d2=scores(i,1)-mu2;
      double md = std::sqrt(d1*d1*inv11 + 2*d1*d2*inv12 + d2*d2*inv22);
      bool is_outlier = (md > threshold);
      if(is_outlier) n_outliers++;
      f << runs[i] << ","
        << std::fixed << std::setprecision(4) << scores(i,0) << ","
        << scores(i,1) << "," << md << ","
        << (is_outlier ? 1 : 0) << "\n";
    }
    printf("[DONE] PCA outliers: out/pca_outliers.csv (%d outliers of %d runs)\n", n_outliers, N);
  }
}
