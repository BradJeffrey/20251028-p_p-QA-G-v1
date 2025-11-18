void run_v2_verbose() {
  gErrorIgnoreLevel = kInfo;   // print INFO+ messages
  gSystem->mkdir("out", true); // ensure out/ exists
  gROOT->ProcessLine(".L macros/extract_metrics_v2.C+");
  extract_metrics_v2("lists/files.txt","metrics.conf");
}
