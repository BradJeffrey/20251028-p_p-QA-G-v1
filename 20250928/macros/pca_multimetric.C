#include <TCanvas.h>
#include <TDecompSVD.h>
#include <TGraph.h>
#include <TMatrixD.h>
#include <TText.h>
#include <TVectorD.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <limits>

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

  TMatrixD scores(N,2);
  for(int i=0;i<N;++i) for(int k=0;k<2;++k){ double s=0; for(int j=0;j<P;++j) s+=X(i,j)*V(j,k); scores(i,k)=s; }

  auto c=new TCanvas("c_pca","PC1 vs PC2",1000,800); auto g=new TGraph(N);
  for(int i=0;i<N;++i) g->SetPoint(i,scores(i,0),scores(i,1));
  g->SetTitle(Form("PCA on %d metrics (N=%d);PC1 (%.1f%%);PC2 (%.1f%%)",P,N,ev1,ev2));
  g->SetMarkerStyle(20); g->Draw("AP");
  auto txt=new TText(); txt->SetTextSize(0.02);
  for(int i=0;i<N;++i) if(i<3 || i>=N-3) txt->DrawText(scores(i,0),scores(i,1),Form("%d",runs[i]));
  c->Print("out/qa_pca_pc12.png"); c->Print("out/qa_pca_pc12.pdf");
  printf("[DONE] PCA written to out/qa_pca_pc12.(png|pdf)\n");
}
