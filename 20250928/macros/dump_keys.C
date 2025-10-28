#include <TFile.h>
#include <TKey.h>
#include <TClass.h>
#include <TH1.h>
#include <TDirectory.h>
#include <TSystem.h>
#include <fstream>
#include <iostream>
#include <string>

static void walkDir(TDirectory* dir, std::ofstream& out, const std::string& prefix) {
  TIter next(dir->GetListOfKeys());
  while (TKey* k = (TKey*)next()) {
    TObject* obj = k->ReadObj();
    if (!obj) continue;
    std::string name = prefix + obj->GetName();
    const char* cls = obj->ClassName();

    // Print line for any object
    out << name << "  [" << cls << "]";

    // If it's a TH1, include basic stats
    if (obj->InheritsFrom(TH1::Class())) {
      TH1* h = (TH1*)obj;
      out << "  entries=" << h->GetEntries()
          << "  mean="    << h->GetMean()
          << "  rms="     << h->GetRMS();
    }
    out << "\n";

    // Recurse into subdirectories
    if (obj->InheritsFrom(TDirectory::Class())) {
      TDirectory* sub = (TDirectory*)obj;
      std::string newPrefix = name + "/";
      walkDir(sub, out, newPrefix);
    }
    delete obj;
  }
}

// Usage from shell:
//   root -l -b -q 'macros/dump_keys.C("../run66522.root","out/hist_list_run66522.txt")'
void dump_keys(const char* infile="../run66522.root",
               const char* outfile="out/hist_list_run66522.txt")
{
  gSystem->mkdir("out", kTRUE);
  std::unique_ptr<TFile> f(TFile::Open(infile,"READ"));
  if (!f || f->IsZombie()) { std::cerr << "[ERROR] cannot open " << infile << "\n"; return; }
  std::ofstream out(outfile);
  if (!out) { std::cerr << "[ERROR] cannot open output file " << outfile << "\n"; return; }
  out << "# file: " << infile << "\n";
  out << "# format: <object_path>  [ClassName] (plus stats for TH1)\n";
  walkDir(f.get(), out, "");
  std::cout << "[DONE] wrote " << outfile << "\n";
}
