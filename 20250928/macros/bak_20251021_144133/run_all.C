#include <TROOT.h>
#include <TSystem.h>
#include <TDatime.h>
#include <TString.h>

#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include <cctype>

static void parse_runs_from_list(const char* list, int& rmin, int& rmax){
  std::ifstream in(list); std::string s; std::set<int> runs;
  while (std::getline(in,s)){
    auto p=s.find("run");
    if (p==std::string::npos) continue;
    p+=3; std::string digs;
    while (p<s.size() && std::isdigit(s[p])) { digs.push_back(s[p]); ++p; }
    if (!digs.empty()) runs.insert(std::stoi(digs));
  }
  if (!runs.empty()){ rmin=*runs.begin(); rmax=*runs.rbegin(); } else { rmin=-1; rmax=-1; }
}

static void call(const TString& cmd){
  std::cout << "[RUN] " << cmd << std::endl;
  gROOT->ProcessLine(cmd);
}

void run_all(const char* list="lists/files.txt",
             const char* conf="metrics.conf",
             const char* markers="config/markers.csv",
             const char* thresholds="config/thresholds.csv",
             const char* weighting="ivar")
{
  gSystem->mkdir("out", kTRUE);

  // Stamp (date + run range)
  int rmin=-1, rmax=-1; parse_runs_from_list(list, rmin, rmax);
  TDatime now; TString tag = Form("%04d%02d%02d_%02d%02d%02d_run%d-%d",
                                  now.GetYear(), now.GetMonth(), now.GetDay(),
                                  now.GetHour(), now.GetMinute(), now.GetSecond(),
                                  rmin, rmax);
  std::ofstream st("out/_stamp.txt");
  st << "date="<<tag.Data()<<"\n";
  st << "run_min="<<rmin<<"\n";
  st << "run_max="<<rmax<<"\n";
  st << "list="<<list<<"\n";
  st << "conf="<<conf<<"\n";
  st << "weighting="<<weighting<<"\n";
  st.close();
  std::cout << "[STAMP] " << tag << std::endl;

  // Core
  call(Form(".x macros/extract_metrics_v2.C(\"%s\",\"%s\")", list, conf));
  call(Form(".x macros/physqa_extract.C(\"%s\")", list));
  call(Form(".x macros/aggregate_per_run_v2.C(\"%s\",\"%s\")", conf, weighting));
  call(Form(".x macros/merge_per_run.C(\"%s\",\"%s\")", conf, "out/metrics_perrun_wide.csv"));

  // Deep analysis if present
  if (!gSystem->AccessPathName("macros/analyze_consistency_v2.C", kReadPermission))
    call(Form(".x macros/analyze_consistency_v2.C(\"%s\",\"%s\",\"%s\")", conf, markers, thresholds));
  else
    std::cout<<"[INFO] analyze_consistency_v2.C not found; skipping\n";

  // Optional extras (best-effort; ignore failures)
  if (!gSystem->AccessPathName("macros/derive_metric_pair.C", kReadPermission)) {
    call(".x macros/derive_metric_pair.C(\"intt_bco_full_peak\",\"mvtx_bco_peak\",\"diff\",\"delta_bco_full\")");
    call(".x macros/derive_metric_pair.C(\"intt_nhit_mean\",\"mvtx_nhits_l0_mean\",\"ratio\",\"ratio_nhits_intt_to_mvtx\")");
  }
  if (!gSystem->AccessPathName("macros/segment_consistency.C", kReadPermission)) {
    call(".x macros/segment_consistency.C(\"cluster_size_intt_mean\")");
    call(".x macros/segment_consistency.C(\"intt_adc_peak\")");
  }
  if (!gSystem->AccessPathName("macros/intt_ladder_health.C", kReadPermission))
    call(Form(".x macros/intt_ladder_health.C(\"%s\",0.05,5.0)", list));
  if (!gSystem->AccessPathName("macros/control_charts.C", kReadPermission)) {
    call(".x macros/control_charts.C(\"intt_adc_landau_mpv\",3.0,0.5,5.0)");
    call(".x macros/control_charts.C(\"tpc_sector_adc_uniform_chi2\",3.0,0.5,5.0)");
  }
  if (!gSystem->AccessPathName("macros/pca_multimetric.C", kReadPermission))
    call(".x macros/pca_multimetric.C(\"out/metrics_perrun_wide.csv\")");

  // Report if available
  if (!gSystem->AccessPathName("macros/make_report.C", kReadPermission))
    call(".x macros/make_report.C(\"out/_stamp.txt\")");

  std::cout << "[DONE] run_all complete.\n";
}
