#include <TFile.h>
#include <TH1.h>
#include <TString.h>
#include <TSystem.h>
#include <fstream>
#include <iostream>
void debug_one_metric(const char* list="lists/files.txt") {
  gSystem->mkdir("out", true);
  std::ifstream in(list);
  if(!in){ std::cerr << "[ERR] cannot open lists/files.txt\n"; return; }
  std::string fpath;
  if(!std::getline(in, fpath)){ std::cerr << "[ERR] empty lists/files.txt\n"; return; }
  TFile f(fpath.c_str(), "READ");
  if(f.IsZombie()){ std::cerr << "[ERR] cannot open file: " << fpath << "\n"; return; }
  TH1* h = (TH1*) f.Get("h_InttRawHitQA_adc");
  if(!h){ std::cerr << "[ERR] hist not found in file: h_InttRawHitQA_adc\n"; return; }
  int ibin = h->GetMaximumBin();
  double maxx = h->GetXaxis()->GetBinCenter(ibin);
  // write a minimal CSV
  std::ofstream out("out/metrics_debug_intt_adc_peak.csv");
  out << "run,segment,file,value,error,weight\n";
  out << "0,0," << fpath << "," << maxx << ",0,1\n";
  out.close();
  std::cout << "[OK] wrote out/metrics_debug_intt_adc_peak.csv with value=" << maxx << "\n";
}
