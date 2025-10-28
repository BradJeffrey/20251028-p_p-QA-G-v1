#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

static std::vector<double> get_col(const std::string& path, int& nrows, std::vector<int>& runs) {
  std::ifstream in(path);
  std::string line; bool first=true;
  std::vector<double> v; runs.clear(); nrows=0;
  while (std::getline(in,line)) {
    if (first) { first=false; continue; }
    if (line.empty()) continue;
    // run,value,error
    size_t p1 = line.find(','); if (p1==std::string::npos) continue;
    size_t p2 = line.find(',', p1+1); if (p2==std::string::npos) continue;
    int run = std::stoi(line.substr(0,p1));
    double val = std::stod(line.substr(p1+1, p2-p1-1));
    runs.push_back(run); v.push_back(val); ++nrows;
  }
  return v;
}

static double median(std::vector<double> x) {
  if (x.empty()) return std::nan("");
  size_t n=x.size(); std::nth_element(x.begin(), x.begin()+n/2, x.end());
  double m=x[n/2];
  if (n%2==0) { std::nth_element(x.begin(), x.begin()+n/2-1, x.end()); m = 0.5*(m + x[n/2-1]); }
  return m;
}

void flag_outliers(const char* perrun_csv="out/metrics_cluster_size_intt_mean_perrun.csv",
                   double k=3.5,
                   const char* outcsv="out/outliers.csv")
{
  int nrows=0; std::vector<int> runs;
  auto vals = get_col(perrun_csv, nrows, runs);
  if (vals.size()<5) { std::cerr<<"[INFO] too few points for outlier stats\n"; return; }

  double med = median(vals);
  std::vector<double> absdev(vals.size());
  for (size_t i=0;i<vals.size();++i) absdev[i]=std::fabs(vals[i]-med);
  double mad = median(absdev);
  double sigma = 1.4826 * mad;

  std::ofstream out(outcsv, std::ios::app); // append across metrics
  out<<"# "<<perrun_csv<<"\n";
  out<<"run,value,med,robust_sigma,z_robust\n";
  for (size_t i=0;i<vals.size();++i) {
    double z = (sigma>0) ? std::fabs(vals[i]-med)/sigma : 0;
    if (z > k) out<<runs[i]<<","<<vals[i]<<","<<med<<","<<sigma<<","<<z<<"\n";
  }
  std::cout<<"[DONE] outlier scan on "<<perrun_csv<<"\n";
}
