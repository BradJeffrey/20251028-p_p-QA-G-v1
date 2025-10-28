#include <TFile.h>
#include <TTree.h>
#include <TDirectory.h>
#include <TGraphErrors.h>
#include <TSystem.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <memory>

static std::string trim(std::string s) {
  auto issp=[](unsigned char c){return std::isspace(c);};
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){return !issp(c);}));
  s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){return !issp(c);}).base(), s.end());
  return s;
}

static std::vector<std::string> load_metric_names(const char* conf) {
  std::vector<std::string> names;
  std::ifstream in(conf);
  if(!in){ std::cerr<<"[ERROR] cannot open "<<conf<<"\n"; return names; }
  std::string line;
  while (std::getline(in,line)) {
    line = trim(line);
    if (line.empty() || line[0]=='#') continue;
    std::stringstream ss(line);
    std::string name; if (!std::getline(ss,name,',')) continue;
    names.push_back(trim(name));
  }
  return names;
}

struct FileRow { int run, seg; std::string file; double y, ey; };
struct RunRow  { int run; double y, ey; };

static bool read_file_csv(const std::string& path, std::vector<FileRow>& rows) {
  std::ifstream in(path);
  if (!in) return false;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header) { header=false; continue; }
    if (s.empty()) continue;
    // run,segment,file,value,error
    std::stringstream ss(s);
    std::string a,b,c,d,e;
    if (!std::getline(ss,a,',')) continue;
    if (!std::getline(ss,b,',')) continue;
    if (!std::getline(ss,c,',')) continue;
    if (!std::getline(ss,d,',')) continue;
    if (!std::getline(ss,e,',')) continue;
    FileRow r; r.run = std::stoi(a); r.seg = std::stoi(b); r.file = c; r.y = std::stod(d); r.ey = std::stod(e);
    rows.push_back(r);
  }
  return true;
}
static bool read_run_csv(const std::string& path, std::vector<RunRow>& rows) {
  std::ifstream in(path);
  if (!in) return false;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header) { header=false; continue; }
    if (s.empty()) continue;
    // run,value,error
    std::stringstream ss(s);
    std::string a,b,c;
    if (!std::getline(ss,a,',')) continue;
    if (!std::getline(ss,b,',')) continue;
    if (!std::getline(ss,c,',')) continue;
    RunRow r; r.run = std::stoi(a); r.y = std::stod(b); r.ey = std::stod(c);
    rows.push_back(r);
  }
  return true;
}

// Usage: .x macros/build_summary_root.C("metrics.conf","out/summary.root")
void build_summary_root(const char* conf="metrics.conf", const char* outf="out/summary.root")
{
  auto metrics = load_metric_names(conf);
  if (metrics.empty()) { std::cerr<<"[ERROR] no metrics found in "<<conf<<"\n"; return; }

  gSystem->mkdir("out", kTRUE);
  std::unique_ptr<TFile> fout(TFile::Open(outf,"RECREATE"));
  if (!fout || fout->IsZombie()) { std::cerr<<"[ERROR] cannot create "<<outf<<"\n"; return; }

  // Trees
  int run, segment; double value, error;
  std::string metric, file;
  TTree tfile("file_metrics","metrics per file");
  tfile.Branch("run",&run);
  tfile.Branch("segment",&segment);
  tfile.Branch("metric",&metric);
  tfile.Branch("value",&value);
  tfile.Branch("error",&error);
  tfile.Branch("file",&file);

  TTree trun("run_metrics","metrics per run (aggregated)");
  trun.Branch("run",&run);
  trun.Branch("metric",&metric);
  trun.Branch("value",&value);
  trun.Branch("error",&error);

  // Graph directory
  auto gdir = fout->mkdir("Graphs");

  // Fill from CSVs
  for (const auto& m : metrics) {
    // per-file
    {
      std::vector<FileRow> rows;
      std::string inpf = std::string("out/metrics_")+m+".csv";
      if (read_file_csv(inpf, rows)) {
        for (const auto& r : rows) {
          run = r.run; segment = r.seg; metric = m; value = r.y; error = r.ey; file = r.file;
          tfile.Fill();
        }
      } else {
        std::cerr<<"[WARN] missing "<<inpf<<"\n";
      }
    }
    // per-run
    {
      std::vector<RunRow> rows;
      std::string inpf = std::string("out/metrics_")+m+"_perrun.csv";
      if (read_run_csv(inpf, rows) && !rows.empty()) {
        std::unique_ptr<TGraphErrors> gr(new TGraphErrors());
        gr->SetName((std::string("gr_")+m+"_perrun").c_str());
        int i=0;
        for (const auto& r : rows) {
          run = r.run; metric = m; value = r.y; error = r.ey;
          trun.Fill();
          gr->SetPoint(i, run, value);
          gr->SetPointError(i, 0.0, error);
          ++i;
        }
        gdir->cd();
        gr->Write();
        fout->cd();
      } else {
        std::cerr<<"[INFO] no per-run CSV for "<<m<<" (skip graphs)\n";
      }
    }
  }

  fout->cd();
  tfile.Write();
  trun.Write();
  fout->Write();
  std::cout<<"[DONE] wrote "<<outf<<" with file_metrics, run_metrics and Graphs/*\n";
}
