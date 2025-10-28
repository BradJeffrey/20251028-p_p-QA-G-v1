#include <TCanvas.h>
#include <TImage.h>
#include <TSystem.h>
#include <TSystemDirectory.h>
#include <TSystemFile.h>
#include <TList.h>
#include <TLatex.h>
#include <TDatime.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static std::string read_stamp(const char* stamp, int& rmin, int& rmax, std::string& date)
{
  rmin=-1; rmax=-1; date="";
  std::ifstream in(stamp);
  std::string s, list="out/_stamp.txt";
  while (std::getline(in,s)){
    if (s.rfind("date=",0)==0) date=s.substr(5);
    else if (s.rfind("run_min=",0)==0) rmin=std::stoi(s.substr(8));
    else if (s.rfind("run_max=",0)==0) rmax=std::stoi(s.substr(8));
  }
  if (date.empty()){
    TDatime now;
    char buf[32]; snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d",
      now.GetYear(), now.GetMonth(), now.GetDay(),
      now.GetHour(), now.GetMinute(), now.GetSecond());
    date = buf;
  }
  return date;
}

static void add_title_page(const char* pdffile, const std::string& date, int rmin, int rmax)
{
  TCanvas c("c_title","QA Report",1000,700);
  c.cd();
  TLatex t; t.SetNDC();
  t.SetTextSize(0.055);
  t.DrawLatex(0.15,0.85,"sPHENIX QA Report");
  t.SetTextSize(0.04);
  t.DrawLatex(0.15,0.74,Form("Stamp: %s", date.c_str()));
  t.DrawLatex(0.15,0.69,Form("Run range: %d .. %d", rmin, rmax));
  t.DrawLatex(0.15,0.64,Form("Generated: %s", TDatime().AsString()));
  c.Print((std::string(pdffile)+"(").c_str()); // open PDF
}

static void add_image(const char* pdffile, const std::string& img, bool close=false)
{
  TImage* im = TImage::Open(img.c_str());
  if (!im) return;
  int w = im->GetWidth(), h = im->GetHeight();
  double ar = (h>0)? (double)w/h : 1.5;
  int cw = (ar>1.4)? 1400 : 1000;
  int ch = (int)(cw/ar);
  TCanvas c("c_page","",cw,ch);
  im->Draw("X");
  if (close) c.Print((std::string(pdffile)+")").c_str());
  else       c.Print(pdffile);
  delete im;
}

void make_report(const char* stamp="out/_stamp.txt")
{
  gSystem->mkdir("out", true);
  int rmin=-1, rmax=-1; std::string date = read_stamp(stamp, rmin, rmax, date);
  std::string pdf = std::string("out/QA_report_")+date+Form("_run%d-%d.pdf",rmin,rmax);

  // Gather candidate images (order: annotated per-run, control, PCA, INTT ladder)
  std::vector<std::string> imgs;

  // 1) Annotated per-run plots
  {
    TSystemDirectory d("out","out");
    TList* fl = d.GetListOfFiles();
    if (fl){
      TIter it(fl); TSystemFile* f;
      while ((f=(TSystemFile*)it())){
        TString n = f->GetName();
        if (f->IsDirectory()) continue;
        if (n.BeginsWith("metric_") && n.EndsWith("_perrun_annot.png")) imgs.push_back(std::string("out/")+n.Data());
      }
    }
  }
  std::sort(imgs.begin(), imgs.end());

  // 2) Control charts
  {
    TSystemDirectory d("out","out");
    TList* fl = d.GetListOfFiles();
    if (fl){
      TIter it(fl); TSystemFile* f;
      while ((f=(TSystemFile*)it())){
        TString n = f->GetName();
        if (f->IsDirectory()) continue;
        if (n.BeginsWith("metric_") && n.EndsWith("_control.png")) imgs.push_back(std::string("out/")+n.Data());
      }
    }
  }

  // 3) PCA map
  if (!gSystem->AccessPathName("out/qa_pca_pc12.png", kReadPermission))
    imgs.push_back("out/qa_pca_pc12.png");

  // 4) INTT ladder pages
  {
    TSystemDirectory d("out","out");
    TList* fl = d.GetListOfFiles();
    if (fl){
      TIter it(fl); TSystemFile* f;
      while ((f=(TSystemFile*)it())){
        TString n = f->GetName();
        if (f->IsDirectory()) continue;
        if (n.BeginsWith("intt_ladder_counts_run") && n.EndsWith(".png")) imgs.push_back(std::string("out/")+n.Data());
      }
    }
  }

  if (imgs.empty()){
    std::cout<<"[WARN] no plots found in out/; creating an empty title-only report.\n";
  }

  // Write PDF
  add_title_page(pdf.c_str(), date, rmin, rmax);
  for (size_t i=0;i<imgs.size();++i){
    bool last = (i+1==imgs.size());
    add_image(pdf.c_str(), imgs[i], last);
  }
  std::cout<<"[DONE] report: "<<pdf<<"\n";
}
