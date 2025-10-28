#include <TFile.h>
#include <TH2.h>
#include <TH1.h>
#include <TCanvas.h>
#include <TSystem.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <tuple>
#include <vector>

static double median(std::vector<double> v){
  if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
  size_t n=v.size(); std::nth_element(v.begin(), v.begin()+n/2, v.end());
  double m=v[n/2]; if(n%2==0){ std::nth_element(v.begin(), v.begin()+n/2-1, v.end()); m=0.5*(m+v[n/2-1]); }
  return m;
}
static int ladder_index(int chip, int lad){ return chip*14 + lad; } // 0..111

// Usage: .x macros/intt_ladder_health.C("lists/files.txt", 0.05, 5.0)
void intt_ladder_health(const char* filelist="lists/files.txt", double dead_frac=0.05, double hot_mult=5.0)
{
  gSystem->mkdir("out", true);
  std::ifstream in(filelist);
  if(!in){ std::cerr<<"[ERR] cannot open "<<filelist<<"\n"; return; }

  std::ofstream summary("out/intt_ladder_health.csv");
  summary<<"run,dead_count,hot_count,median,total_ladders\n";

  std::string path;
  while (std::getline(in,path)) {
    if (path.empty()) continue;

    // parse run from basename "runNNN..." pattern
    size_t p=path.find_last_of('/'); std::string base=(p==std::string::npos)?path:path.substr(p+1);
    int run=-1; {
      size_t pos=base.find("run"); if(pos!=std::string::npos){
        size_t i = pos+3; std::string digits;
        while (i<base.size() && isdigit(base[i])) { digits.push_back(base[i]); ++i; }
        if (!digits.empty()) run=std::stoi(digits);
      }
    }

    std::unique_ptr<TFile> f(TFile::Open(path.c_str(),"READ"));
    if (!f || f->IsZombie()) { std::cerr<<"[WARN] cannot open "<<path<<"\n"; continue; }

    std::vector<double> counts(8*14, 0.0);
    int found=0;
    for (int chip=0; chip<8; ++chip){
      for (int lad=0; lad<14; ++lad){
        std::string hname = "h_InttRawHitQA_intt"+std::to_string(chip)+"_"+std::to_string(lad);
        auto h = dynamic_cast<TH2*>(f->Get(hname.c_str()));
        if (!h) continue;
        counts[ladder_index(chip,lad)] = h->Integral(1,h->GetNbinsX(),1,h->GetNbinsY());
        ++found;
      }
    }
    if (found==0) { std::cerr<<"[INFO] no ladder histos in "<<path<<"\n"; continue; }

    double med = median(counts);
    int dead=0, hot=0;
    for (double c: counts){
      if (!std::isfinite(c)) continue;
      if (c < dead_frac*med) dead++;
      if (c > hot_mult*med)  hot++;
    }

    summary<<run<<","<<dead<<","<<hot<<","<<med<<","<<counts.size()<<"\n";

    // quick “heatmap”: draw counts as 1D ladder index
    auto h1 = new TH1D(("h_counts_"+std::to_string(run)).c_str(),"INTT ladder counts;ladder index (0..111);counts", 8*14, -0.5, 8*14-0.5);
    for (int i=0;i<(int)counts.size();++i) h1->SetBinContent(i+1, counts[i]);
    TCanvas c(("c_ladder_"+std::to_string(run)).c_str(),"intt ladder", 1100, 400);
    h1->Draw("hist");
    c.SaveAs((std::string("out/intt_ladder_counts_run")+std::to_string(run)+".png").c_str());

    // detailed CSV per run (optional quick peek)
    std::ofstream per((std::string("out/intt_ladder_counts_run")+std::to_string(run)+".csv").c_str());
    per<<"chip,ladder,count\n";
    for (int chip=0; chip<8; ++chip)
      for (int lad=0; lad<14; ++lad)
        per<<chip<<","<<lad<<","<<counts[ladder_index(chip,lad)]<<"\n";
    per.close();
  }

  summary.close();
  std::cout<<"[DONE] wrote out/intt_ladder_health.csv and per-run ladder plots/CSVs.\n";
}
