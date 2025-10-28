// [001] // Wrapper macro: include extractor and call it in the same interpreter session
// [002] #include "macros/extract_metrics_v2.C"
// [003] 
// [004] void run_extract_v2()
// [005] {
// [006]   // edit paths here only if you move files.txt or metrics.conf
// [007]   const char* listFile = "lists/files.txt";
// [008]   const char* confFile = "metrics.conf";
// [009]   extract_metrics_v2(listFile, confFile);
// [010] }
// [011] 
// [012] // Call it at top-level so `root -q macros/run_extract_v2.C` runs immediately
// [013] run_extract_v2();
