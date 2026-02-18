#include <TSystem.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <ctime>

// ----------------------------------------------------------------
// generate_report_md.C
// Reads per-run CSVs for all metrics in metrics.conf and produces
// out/REPORT.md with coverage stats, NaN rates, mean +/- std,
// and outlier counts per metric.
// ----------------------------------------------------------------

static std::string trim(std::string s) {
  auto f=[](unsigned char c){return !std::isspace(c);};
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), f));
  s.erase(std::find_if(s.rbegin(), s.rend(), f).base(), s.end());
  return s;
}

static std::vector<std::string> metrics_from_conf(const char* conf) {
  std::vector<std::string> m;
  std::ifstream in(conf); std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto p = line.find(',');
    if (p == std::string::npos) continue;
    std::string name = trim(line.substr(0, p));
    // avoid duplicates
    if (std::find(m.begin(), m.end(), name) == m.end())
      m.push_back(name);
  }
  return m;
}

struct PerRunRow {
  int run;
  double value;
  double stat_err;
  double entries;
  int weak;
  int strong;
};

static bool read_perrun(const std::string& metric, std::vector<PerRunRow>& rows) {
  std::string path = "out/metrics_" + metric + "_perrun.csv";
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (header) { header = false; continue; }
    if (line.empty()) continue;
    std::stringstream ss(line);
    std::string field;
    std::vector<std::string> fields;
    while (std::getline(ss, field, ',')) fields.push_back(field);
    if (fields.size() < 2) continue;
    PerRunRow r;
    r.run = std::stoi(fields[0]);
    try { r.value = std::stod(fields[1]); } catch (...) { r.value = NAN; }
    r.stat_err = (fields.size() > 2) ? std::stod(fields[2]) : 0.0;
    r.entries  = (fields.size() > 3) ? std::stod(fields[3]) : 0.0;
    r.weak     = (fields.size() > 7) ? std::stoi(fields[7]) : 0;
    r.strong   = (fields.size() > 8) ? std::stoi(fields[8]) : 0;
    rows.push_back(r);
  }
  return !rows.empty();
}

void generate_report_md(const char* conf = "metrics.conf") {
  gSystem->mkdir("out", true);

  auto metrics = metrics_from_conf(conf);
  if (metrics.empty()) {
    std::cerr << "[ERROR] no metrics loaded from " << conf << "\n";
    return;
  }

  // collect all run numbers across metrics
  std::set<int> all_runs;
  struct MetricStats {
    std::string name;
    int total_runs;
    int finite_runs;
    int nan_runs;
    double mean;
    double stddev;
    int weak_outliers;
    int strong_outliers;
    double min_val;
    double max_val;
  };
  std::vector<MetricStats> stats;

  for (const auto& m : metrics) {
    std::vector<PerRunRow> rows;
    MetricStats ms;
    ms.name = m;
    ms.total_runs = 0;
    ms.finite_runs = 0;
    ms.nan_runs = 0;
    ms.mean = 0;
    ms.stddev = 0;
    ms.weak_outliers = 0;
    ms.strong_outliers = 0;
    ms.min_val = 1e100;
    ms.max_val = -1e100;

    if (!read_perrun(m, rows)) {
      ms.total_runs = 0;
      ms.nan_runs = 0;
      stats.push_back(ms);
      continue;
    }

    ms.total_runs = (int)rows.size();
    double sum = 0;
    double sum2 = 0;
    for (const auto& r : rows) {
      all_runs.insert(r.run);
      if (std::isfinite(r.value)) {
        ms.finite_runs++;
        sum += r.value;
        sum2 += r.value * r.value;
        if (r.value < ms.min_val) ms.min_val = r.value;
        if (r.value > ms.max_val) ms.max_val = r.value;
      } else {
        ms.nan_runs++;
      }
      if (r.strong) ms.strong_outliers++;
      else if (r.weak) ms.weak_outliers++;
    }
    if (ms.finite_runs > 0) {
      ms.mean = sum / ms.finite_runs;
      double var = (sum2 / ms.finite_runs) - (ms.mean * ms.mean);
      ms.stddev = (var > 0) ? std::sqrt(var) : 0.0;
    }
    if (ms.finite_runs == 0) {
      ms.min_val = NAN;
      ms.max_val = NAN;
    }
    stats.push_back(ms);
  }

  // read stamp if available
  std::string stamp_date, stamp_rmin, stamp_rmax;
  {
    std::ifstream sf("out/_stamp.txt");
    std::string line;
    while (std::getline(sf, line)) {
      if (line.rfind("date=", 0) == 0)    stamp_date = line.substr(5);
      if (line.rfind("run_min=", 0) == 0)  stamp_rmin = line.substr(8);
      if (line.rfind("run_max=", 0) == 0)  stamp_rmax = line.substr(8);
    }
  }

  // write report
  std::ofstream out("out/REPORT.md");

  out << "# QA Pipeline Summary Report\n\n";
  if (!stamp_date.empty()) {
    out << "**Generated:** " << stamp_date << "  \n";
  }
  if (!stamp_rmin.empty() && !stamp_rmax.empty()) {
    out << "**Run range:** " << stamp_rmin << " -- " << stamp_rmax << "  \n";
  }
  out << "**Total runs:** " << all_runs.size() << "  \n";
  out << "**Metrics in scope:** " << metrics.size() << "\n\n";

  out << "---\n\n";
  out << "## Per-Metric Summary\n\n";
  out << "| Metric | Runs | Finite | NaN | NaN % | Mean | Std | Min | Max | Weak | Strong |\n";
  out << "|--------|------|--------|-----|-------|------|-----|-----|-----|------|--------|\n";

  for (const auto& ms : stats) {
    double nan_pct = (ms.total_runs > 0) ? (100.0 * ms.nan_runs / ms.total_runs) : 0.0;
    out << "| " << ms.name
        << " | " << ms.total_runs
        << " | " << ms.finite_runs
        << " | " << ms.nan_runs
        << " | " << std::fixed << std::setprecision(0) << nan_pct << "%";
    if (ms.finite_runs > 0) {
      out << " | " << std::setprecision(4) << ms.mean
          << " | " << std::setprecision(4) << ms.stddev
          << " | " << std::setprecision(4) << ms.min_val
          << " | " << std::setprecision(4) << ms.max_val;
    } else {
      out << " | -- | -- | -- | --";
    }
    out << " | " << ms.weak_outliers
        << " | " << ms.strong_outliers
        << " |\n";
  }

  // overall health summary
  out << "\n---\n\n";
  out << "## Health Overview\n\n";

  int total_outliers = 0;
  int total_nans = 0;
  int all_clean = 0;
  for (const auto& ms : stats) {
    total_outliers += ms.weak_outliers + ms.strong_outliers;
    total_nans += ms.nan_runs;
    if (ms.nan_runs == 0 && ms.weak_outliers == 0 && ms.strong_outliers == 0)
      all_clean++;
  }

  out << "- **Clean metrics** (no NaN, no outliers): " << all_clean << " / " << stats.size() << "\n";
  out << "- **Total NaN entries:** " << total_nans << "\n";
  out << "- **Total outlier flags:** " << total_outliers << "\n";

  // list any metrics with issues
  bool any_issues = false;
  for (const auto& ms : stats) {
    if (ms.nan_runs > 0 || ms.strong_outliers > 0) {
      if (!any_issues) {
        out << "\n### Metrics Requiring Attention\n\n";
        any_issues = true;
      }
      out << "- **" << ms.name << "**:";
      if (ms.nan_runs > 0)
        out << " " << ms.nan_runs << " NaN run" << (ms.nan_runs > 1 ? "s" : "");
      if (ms.strong_outliers > 0)
        out << " " << ms.strong_outliers << " strong outlier" << (ms.strong_outliers > 1 ? "s" : "");
      out << "\n";
    }
  }

  out.close();
  std::cout << "[DONE] wrote out/REPORT.md\n";
}
