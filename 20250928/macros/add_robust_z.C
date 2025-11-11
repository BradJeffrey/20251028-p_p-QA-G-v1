// add_robust_z.C — Append robust local z columns to per-run CSVs for all metrics in metrics.conf.
// Usage: root -l -b -q 'macros/add_robust_z.C("metrics.conf",5)'

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include <tuple>

namespace {
  inline bool isFinite(double x){ return std::isfinite(x); }

  double median(std::vector<double> v){
    if(v.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(v.begin(), v.end());
    size_t n=v.size();
    return (n%2) ? v[n/2] : 0.5*(v[n/2-1]+v[n/2]);
  }

  struct Row { int run; double value, stat_err, entries; };

  bool parseRow(const std::string& line, Row& r){
    // Expect CSV: run,value,stat_err,entries
    std::stringstream ss(line);
    std::string f0,f1,f2,f3;
    if(!std::getline(ss,f0,',')) return false;
    // Skip header lines
    if(!f0.empty() && !std::isdigit(static_cast<unsigned char>(f0[0])) && f0!="0") return false;
    if(!std::getline(ss,f1,',')) return false;
    if(!std::getline(ss,f2,',')) return false;
    if(!std::getline(ss,f3,',')) return false;
    r.run     = std::stoi(f0);
    r.value   = std::stod(f1);
    r.stat_err= std::stod(f2);
    r.entries = std::stod(f3);
    return true;
  }

  void append_z_to_csv(const std::string& path, int W){
    std::ifstream in(path);
    if(!in.good()){
      printf("[add_robust_z] WARN: missing per-run CSV: %s\n", path.c_str());
      return;
    }
    std::vector<Row> rows; rows.reserve(1024);
    std::string line; bool saw_header=false;
    // Preserve header if present
    std::string header = "run,value,stat_err,entries,neighbors_median,neighbors_mad,z_local,is_outlier_weak,is_outlier_strong";
    {
      std::streampos pos = in.tellg();
      if(std::getline(in,line)){
        // If first token non-numeric, treat as header
        if(!line.empty() && !std::isdigit(static_cast<unsigned char>(line[0])) && line[0]!='-'){
          saw_header = true;
        } else {
          in.seekg(pos);
        }
      }
    }
    while(std::getline(in,line)){
      Row r; if(parseRow(line,r)) rows.push_back(r);
    }
    in.close();
    const size_t N = rows.size();
    if(N==0){
      // Re-write trivial header only
      std::ofstream out(path);
      out << header << "\n";
      out.close();
      return;
    }

    // Prepare vectors of finite indices to respect entries>0
    std::vector<int> good(N,0);
    for(size_t i=0;i<N;++i){
      good[i] = (rows[i].entries>0 && isFinite(rows[i].value)) ? 1 : 0;
    }

    // Compute neighbors medians/MAD and z
    std::vector<double> med(N, std::numeric_limits<double>::quiet_NaN());
    std::vector<double> mad(N, std::numeric_limits<double>::quiet_NaN());
    std::vector<double> z  (N, std::numeric_limits<double>::quiet_NaN());
    std::vector<int> weak(N,0), strong(N,0);

    for(int i=0;i<(int)N;++i){
      int i0 = std::max(0, i-W);
      int i1 = std::min((int)N-1, i+W);
      std::vector<double> nb;
      nb.reserve(2*W);
      for(int j=i0;j<=i1;++j){
        if(j==i) continue;
        if(good[j]) nb.push_back(rows[j].value);
      }
      if((int)nb.size() < 3){ // Not enough support
        med[i]=mad[i]=z[i]=std::numeric_limits<double>::quiet_NaN();
        weak[i]=strong[i]=0;
        continue;
      }
      med[i] = median(nb);
      std::vector<double> dev(nb.size());
      for(size_t k=0;k<nb.size();++k) dev[k] = std::fabs(nb[k]-med[i]);
      mad[i] = median(dev);
      const double eps = 1e-6;
      if(good[i]){
        z[i] = 0.6745 * (rows[i].value - med[i]) / (mad[i] + eps);
        const double az = std::fabs(z[i]);
        strong[i] = (az >= 3.0) ? 1 : 0;
        weak[i]   = (!strong[i] && az >= 2.0) ? 1 : 0;
      } else {
        z[i]=std::numeric_limits<double>::quiet_NaN();
        weak[i]=strong[i]=0;
      }
    }

    // Write back (overwrite) with appended columns
    std::ofstream out(path);
    out << header << "\n";
    out << std::setprecision(8);
    for(size_t i=0;i<N;++i){
      out << rows[i].run << ","
          << rows[i].value << ","
          << rows[i].stat_err << ","
          << rows[i].entries << ","
          << med[i] << ","
          << mad[i] << ","
          << z[i] << ","
          << weak[i] << ","
          << strong[i] << "\n";
    }
    out.close();
    printf("[add_robust_z] augmented %s (W=%d)\n", path.c_str(), W);
  }

  std::vector<std::string> read_metrics(const std::string& conf_path){
    std::ifstream in(conf_path);
    std::vector<std::string> metrics;
    std::string line;
    while(std::getline(in,line)){
      // trim
      auto p = line.find('#'); if(p!=std::string::npos) line=line.substr(0,p);
      for(char& c: line) if(c=='\t') c=' ';
      // format: metric_name, histogram_name, method
      std::stringstream ss(line);
      std::string name; if(!std::getline(ss, name, ',')) continue;
      // strip spaces
      auto ltrim=[&](std::string& s){ while(!s.empty()&&std::isspace((unsigned char)s.front())) s.erase(s.begin()); };
      auto rtrim=[&](std::string& s){ while(!s.empty()&&std::isspace((unsigned char)s.back()))  s.pop_back(); };
      ltrim(name); rtrim(name);
      if(name.empty()) continue;
      metrics.push_back(name);
    }
    return metrics;
  }
} // namespace

void add_robust_z(const char* metrics_conf_path="metrics.conf", int W=5){
  std::vector<std::string> metrics = read_metrics(metrics_conf_path);
  for(const auto& m : metrics){
    std::string csv = "out/metrics_" + m + "_perrun.csv";
    append_z_to_csv(csv, W);
  }
}
