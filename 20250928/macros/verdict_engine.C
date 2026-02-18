///////////////////////////////////////////////////////////////////////////////
// verdict_engine.C — Automated Physics-Informed Run Verdict System
//
// Reads all QA outputs (per-run CSVs with robust z-scores, consistency
// analysis, control chart flags, ladder health) and produces:
//
// 1. out/verdicts.csv          — per-run, per-metric machine-readable verdicts
// 2. out/run_verdicts.csv      — per-run aggregate verdict (GOOD/SUSPECT/BAD)
// 3. out/VERDICT.md            — human-readable diagnostic report with
//                                physics-informed reasoning for every flag
//
// The engine classifies anomalies into patterns (drift, spike, step change,
// etc.) and maps them to plausible physics/hardware/engineering causes using
// the knowledge base in configs/physics_rules.yaml.
//
// Usage:
//   root -l -b -q 'macros/verdict_engine.C("metrics.conf")'
///////////////////////////////////////////////////////////////////////////////

#include <TMath.h>
#include <TSystem.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// Data structures
// ============================================================================

struct MetricRow {
  int run;
  double value;
  double stat_err;
  double entries;
  double z_local;
  int weak;
  int strong;
};

struct QCStatus {
  int run;
  double value;
  std::string status;   // PASS, WARN, FAIL
  std::string reason;   // threshold, robust_z, threshold+robust_z
};

struct ControlFlag {
  int run;
  double zrobust;
  int shewhart_ooc;
  double cusum_pos;
  double cusum_neg;
  std::string flag;     // PASS, WARN
};

struct LadderHealth {
  int run;
  int dead_count;
  int hot_count;
  int total_ladders;
};

// Per-metric verdict for a single run
struct RunMetricVerdict {
  int run;
  std::string metric;
  std::string verdict;          // GOOD, SUSPECT, BAD, EXCLUDED
  std::string severity;         // info, warning, critical
  std::string pattern;          // gradual_drift, step_change, spike, etc.
  std::vector<std::string> causes;
  std::string action;
  double z_local;
  double value;
};

// Aggregate per-run verdict
struct RunVerdict {
  int run;
  std::string verdict;          // GOOD, SUSPECT, BAD
  int n_good;
  int n_suspect;
  int n_bad;
  std::string worst_metric;
  std::string summary;
};

// ============================================================================
// Utilities
// ============================================================================

static std::string trim(std::string s) {
  auto f = [](unsigned char c){return !std::isspace(c);};
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), f));
  s.erase(std::find_if(s.rbegin(), s.rend(), f).base(), s.end());
  return s;
}

static std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> tokens;
  std::istringstream ss(s);
  std::string tok;
  while (std::getline(ss, tok, delim)) tokens.push_back(trim(tok));
  return tokens;
}

static std::vector<std::string> metrics_from_conf(const char* conf) {
  std::vector<std::string> m;
  std::set<std::string> seen;
  std::ifstream in(conf);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0]=='#') continue;
    size_t p = line.find(',');
    if (p == std::string::npos) continue;
    std::string name = trim(line.substr(0, p));
    if (seen.insert(name).second) m.push_back(name);
  }
  return m;
}

// ============================================================================
// CSV Readers
// ============================================================================

static bool read_perrun_robust(const std::string& path, std::vector<MetricRow>& rows) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (header) { header = false; continue; }
    if (line.empty()) continue;
    auto fields = split(line, ',');
    if (fields.size() < 9) continue;

    MetricRow r;
    try {
      r.run      = std::stoi(fields[0]);
      r.value    = std::stod(fields[1]);
      r.stat_err = std::stod(fields[2]);
      r.entries  = std::stod(fields[3]);
      // fields 4,5 = neighbors_median, neighbors_mad
      r.z_local  = (fields.size() > 6) ? std::stod(fields[6]) : 0.0;
      r.weak     = (fields.size() > 7) ? std::stoi(fields[7]) : 0;
      r.strong   = (fields.size() > 8) ? std::stoi(fields[8]) : 0;
    } catch (...) { continue; }

    if (!std::isfinite(r.z_local)) r.z_local = 0.0;
    rows.push_back(r);
  }
  return !rows.empty();
}

static bool read_qc_status(const std::string& path, std::vector<QCStatus>& rows) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (header) { header = false; continue; }
    if (line.empty()) continue;
    auto fields = split(line, ',');
    if (fields.size() < 3) continue;
    QCStatus q;
    try {
      q.run    = std::stoi(fields[0]);
      q.value  = std::stod(fields[1]);
      q.status = fields[2];
      q.reason = (fields.size() > 3) ? fields[3] : "";
    } catch (...) { continue; }
    rows.push_back(q);
  }
  return !rows.empty();
}

static bool read_control_flags(const std::string& path, std::vector<ControlFlag>& rows) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (header) { header = false; continue; }
    if (line.empty()) continue;
    auto fields = split(line, ',');
    if (fields.size() < 6) continue;
    ControlFlag c;
    try {
      c.run          = std::stoi(fields[0]);
      c.zrobust      = std::stod(fields[1]);
      c.shewhart_ooc = std::stoi(fields[2]);
      c.cusum_pos    = std::stod(fields[3]);
      c.cusum_neg    = std::stod(fields[4]);
      c.flag         = fields[5];
    } catch (...) { continue; }
    rows.push_back(c);
  }
  return !rows.empty();
}

static bool read_ladder_health(const std::string& path, std::vector<LadderHealth>& rows) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (header) { header = false; continue; }
    if (line.empty()) continue;
    auto fields = split(line, ',');
    if (fields.size() < 4) continue;
    LadderHealth h;
    try {
      h.run            = std::stoi(fields[0]);
      h.dead_count     = std::stoi(fields[1]);
      h.hot_count      = std::stoi(fields[2]);
      // field 3 = median, field 4 = total_ladders
      h.total_ladders  = (fields.size() > 4) ? std::stoi(fields[4]) : 112;
    } catch (...) { continue; }
    rows.push_back(h);
  }
  return !rows.empty();
}

static bool read_consistency_summary(const std::string& path,
    std::map<std::string, std::tuple<double,double,double,int,double>>& info)
{
  // metric,N,median,robust_sigma,slope,eslope,pval,cp_run,dBIC
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (header) { header = false; continue; }
    if (line.empty()) continue;
    auto f = split(line, ',');
    if (f.size() < 9) continue;
    try {
      std::string metric = f[0];
      double slope = std::stod(f[4]);
      double pval  = std::stod(f[6]);
      int cp_run   = std::stoi(f[7]);
      double dBIC  = std::stod(f[8]);
      double rsig  = std::stod(f[3]);
      info[metric] = {slope, pval, rsig, cp_run, dBIC};
    } catch (...) { continue; }
  }
  return !info.empty();
}

// ============================================================================
// Pattern Classification Engine
// ============================================================================

// Classify what pattern an anomaly follows based on context
static std::string classify_pattern(
    const std::string& metric,
    const std::vector<MetricRow>& data,
    double slope, double pval, int cp_run, double dBIC,
    int run_idx)
{
  // Check for changepoint near this run
  bool near_changepoint = false;
  if (cp_run > 0 && dBIC >= 10.0) {
    for (size_t i = 0; i < data.size(); ++i) {
      if (data[i].run == cp_run) {
        int dist = std::abs((int)i - run_idx);
        if (dist <= 2) near_changepoint = true;
        break;
      }
    }
  }

  // Get this run's z-score and check neighbors
  double z = std::fabs(data[run_idx].z_local);

  // Check if neighbors are also flagged (sustained shift vs isolated spike)
  int flagged_neighbors = 0;
  for (int j = std::max(0, run_idx-2); j <= std::min((int)data.size()-1, run_idx+2); ++j) {
    if (j == run_idx) continue;
    if (std::fabs(data[j].z_local) > 2.0) flagged_neighbors++;
  }

  // Classify
  if (near_changepoint) return "step_change";
  if (z > 4.0 && flagged_neighbors == 0) return "spike";
  if (pval < 0.01 && std::fabs(slope) > 0) {
    // Significant linear trend
    if (flagged_neighbors >= 2) return "gradual_drift";
  }
  if (flagged_neighbors >= 2) return "sustained_shift";
  if (z > 2.0) return "isolated_outlier";

  return "statistical_fluctuation";
}

// Map pattern to severity
static std::string pattern_severity(const std::string& pattern) {
  if (pattern == "spike") return "critical";
  if (pattern == "step_change") return "warning";
  if (pattern == "gradual_drift") return "warning";
  if (pattern == "sustained_shift") return "critical";
  if (pattern == "isolated_outlier") return "info";
  return "info";
}

// Generate physics-informed causes based on pattern and metric
static std::vector<std::string> infer_causes(
    const std::string& metric,
    const std::string& pattern,
    double value, double z,
    int dead_count, int hot_count)
{
  std::vector<std::string> causes;

  // ---- INTT ADC metrics ----
  if (metric.find("adc_peak") != std::string::npos ||
      metric.find("adc_median") != std::string::npos) {
    if (pattern == "gradual_drift") {
      causes.push_back("Temperature-dependent gain drift in INTT silicon sensors");
      causes.push_back("Gradual radiation damage affecting charge collection");
    } else if (pattern == "step_change") {
      causes.push_back("Calibration update applied between runs");
      causes.push_back("Hardware swap (sensor module or FPHX chip replacement)");
    } else if (pattern == "spike") {
      causes.push_back("Noisy run with electromagnetic pickup interference");
      causes.push_back("Beam conditions anomaly causing background spike");
    } else {
      causes.push_back("Statistical fluctuation in ADC distribution sampling");
    }
  }
  else if (metric.find("adc_p90") != std::string::npos) {
    if (z > 0) {
      causes.push_back("Growing electronic noise or crosstalk between channels");
      causes.push_back("Beam background increase filling high-ADC bins");
    } else {
      causes.push_back("Threshold adjustment cutting into signal tail");
    }
  }
  // ---- INTT phi uniformity ----
  else if (metric.find("phi_uniform") != std::string::npos ||
           metric.find("phi_chi2") != std::string::npos) {
    if (dead_count > 0 || hot_count > 0) {
      std::ostringstream ss;
      if (dead_count > 0) ss << dead_count << " dead ladder(s) creating azimuthal hole";
      if (hot_count > 0) {
        if (dead_count > 0) ss << "; ";
        ss << hot_count << " hot ladder(s) producing localized excess";
      }
      causes.push_back(ss.str());
    }
    if (pattern == "spike" || pattern == "step_change") {
      causes.push_back("HV trip or recovery on INTT sensor module");
      causes.push_back("Beam position shift illuminating detector asymmetrically");
    } else {
      causes.push_back("Progressive channel degradation affecting phi coverage");
    }
  }
  // ---- INTT BCO peak ----
  else if (metric.find("bco_peak") != std::string::npos) {
    if (pattern == "step_change" || pattern == "spike") {
      causes.push_back("Normal BCO phase toggling between two states (may be expected)");
      causes.push_back("DAQ timing reconfiguration");
    } else if (pattern == "gradual_drift") {
      causes.push_back("Clock oscillator frequency drift");
      causes.push_back("PLL instability in INTT readout timing chain");
    } else {
      causes.push_back("Timing jitter or synchronization fluctuation");
    }
  }
  // ---- Cluster size ----
  else if (metric.find("cluster_size") != std::string::npos) {
    if (value > 3.0) {
      causes.push_back("Threshold set too low, capturing noise hits into clusters");
      causes.push_back("Increasing electronic noise widening clusters");
    } else if (value < 1.5) {
      causes.push_back("Threshold set too high, splitting physical clusters");
      causes.push_back("Gain decrease reducing signal-to-noise ratio");
    } else {
      causes.push_back("Normal variation in cluster formation");
    }
  }
  // ---- Cluster phi RMS ----
  else if (metric.find("cluster_phi") != std::string::npos && metric.find("rms") != std::string::npos) {
    causes.push_back("Change in active azimuthal coverage (dead/recovered sectors)");
    causes.push_back("Beam position shift affecting illumination pattern");
  }
  // ---- Hit asymmetry ----
  else if (metric.find("hits_asym") != std::string::npos) {
    if (value > 0.5) {
      causes.push_back("Severe occupancy imbalance: likely dead or hot sensor");
      if (dead_count > 0) causes.push_back("Confirmed: " + std::to_string(dead_count) + " dead ladder(s) in this run");
    } else {
      causes.push_back("Moderate occupancy variation between sensors");
    }
  }
  // ---- Generic fallback ----
  else {
    causes.push_back("Anomalous value detected; manual inspection recommended");
  }

  if (causes.empty()) causes.push_back("No specific diagnosis available");
  return causes;
}

// Generate recommended action
static std::string infer_action(const std::string& metric, const std::string& pattern,
                                 const std::string& severity) {
  if (severity == "critical") {
    if (metric.find("bco") != std::string::npos)
      return "Flag run for timing review; alert trigger/timing group";
    if (metric.find("phi") != std::string::npos)
      return "Run ladder health check; inspect phi distribution for this run";
    return "Flag run for exclusion from physics analysis; inspect raw histograms";
  }
  if (severity == "warning") {
    if (pattern == "gradual_drift")
      return "Monitor trend over next runs; check hardware logs for correlated changes";
    if (pattern == "step_change")
      return "Check run logbook for calibration or hardware interventions near this run";
    return "Note for review; compare with other metrics for correlated anomalies";
  }
  return "No action needed; within expected variation";
}

// ============================================================================
// Main verdict engine
// ============================================================================

void verdict_engine(const char* conf = "metrics.conf") {
  auto metrics = metrics_from_conf(conf);
  if (metrics.empty()) {
    std::cerr << "[ERROR] No metrics found in " << conf << "\n";
    return;
  }

  gSystem->mkdir("out", kTRUE);

  // ---- Load global context ----

  // Consistency summary (slope, pval, changepoint info per metric)
  std::map<std::string, std::tuple<double,double,double,int,double>> consistency;
  read_consistency_summary("out/consistency_summary.csv", consistency);

  // Ladder health (per-run dead/hot counts)
  std::vector<LadderHealth> ladder_health;
  read_ladder_health("out/intt_ladder_health.csv", ladder_health);
  std::map<int, LadderHealth> ladder_by_run;
  for (auto& h : ladder_health) ladder_by_run[h.run] = h;

  // Collect all runs across all metrics
  std::set<int> all_runs;

  // ---- Per-metric analysis ----
  std::vector<RunMetricVerdict> all_verdicts;

  for (auto& m : metrics) {
    // Load per-run data with robust z-scores
    std::vector<MetricRow> data;
    std::string perrun_path = "out/metrics_" + m + "_perrun.csv";
    if (!read_perrun_robust(perrun_path, data)) {
      std::cerr << "[INFO] No per-run data for " << m << "; skipping\n";
      continue;
    }

    for (auto& r : data) all_runs.insert(r.run);

    // Load QC status if available
    std::map<int, QCStatus> qc_by_run;
    {
      std::vector<QCStatus> qc;
      if (read_qc_status("out/qc_status_" + m + ".csv", qc)) {
        for (auto& q : qc) qc_by_run[q.run] = q;
      }
    }

    // Load control chart flags if available
    std::map<int, ControlFlag> ctrl_by_run;
    {
      std::vector<ControlFlag> ctrl;
      if (read_control_flags("out/qc_control_" + m + ".csv", ctrl)) {
        for (auto& c : ctrl) ctrl_by_run[c.run] = c;
      }
    }

    // Get consistency info for this metric
    double slope = 0, pval = 1.0, rsig = 0;
    int cp_run = -1;
    double dBIC = 0;
    if (consistency.count(m)) {
      std::tie(slope, pval, rsig, cp_run, dBIC) = consistency[m];
    }

    // Evaluate each run
    for (size_t i = 0; i < data.size(); ++i) {
      auto& row = data[i];
      RunMetricVerdict v;
      v.run = row.run;
      v.metric = m;
      v.z_local = row.z_local;
      v.value = row.value;

      // Determine if this run is flagged
      bool is_flagged = false;
      bool is_severe = false;

      // Check robust z
      if (row.strong) { is_flagged = true; is_severe = true; }
      else if (row.weak) { is_flagged = true; }

      // Check QC status
      if (qc_by_run.count(row.run)) {
        auto& q = qc_by_run[row.run];
        if (q.status == "FAIL") { is_flagged = true; is_severe = true; }
        else if (q.status == "WARN") { is_flagged = true; }
      }

      // Check control charts
      if (ctrl_by_run.count(row.run)) {
        auto& c = ctrl_by_run[row.run];
        if (c.flag == "WARN") { is_flagged = true; }
        if (c.shewhart_ooc) { is_severe = true; }
      }

      if (!is_flagged) {
        v.verdict = "GOOD";
        v.severity = "info";
        v.pattern = "normal";
        v.causes.push_back("All checks passed");
        v.action = "No action needed";
      } else {
        // Classify the anomaly pattern
        v.pattern = classify_pattern(m, data, slope, pval, cp_run, dBIC, (int)i);
        v.severity = is_severe ? "critical" : pattern_severity(v.pattern);
        v.verdict = is_severe ? "BAD" : "SUSPECT";

        // Get ladder health context for this run
        int dead = 0, hot = 0;
        if (ladder_by_run.count(row.run)) {
          dead = ladder_by_run[row.run].dead_count;
          hot  = ladder_by_run[row.run].hot_count;
        }

        // Infer physics causes
        v.causes = infer_causes(m, v.pattern, row.value, row.z_local, dead, hot);
        v.action = infer_action(m, v.pattern, v.severity);
      }

      all_verdicts.push_back(v);
    }
  }

  // ============================================================================
  // Aggregate per-run verdicts
  // ============================================================================

  std::map<int, RunVerdict> run_agg;
  for (auto& v : all_verdicts) {
    auto& rv = run_agg[v.run];
    rv.run = v.run;
    if (v.verdict == "GOOD") rv.n_good++;
    else if (v.verdict == "SUSPECT") rv.n_suspect++;
    else if (v.verdict == "BAD") rv.n_bad++;

    // Track worst metric
    if (v.verdict == "BAD" || (v.verdict == "SUSPECT" && rv.worst_metric.empty())) {
      rv.worst_metric = v.metric;
    }
  }

  for (auto& [run, rv] : run_agg) {
    if (rv.n_bad > 0)           rv.verdict = "BAD";
    else if (rv.n_suspect > 0)  rv.verdict = "SUSPECT";
    else                        rv.verdict = "GOOD";

    std::ostringstream ss;
    ss << rv.n_good << " good, " << rv.n_suspect << " suspect, " << rv.n_bad << " bad";
    if (!rv.worst_metric.empty()) ss << " (worst: " << rv.worst_metric << ")";
    rv.summary = ss.str();
  }

  // ============================================================================
  // Write outputs
  // ============================================================================

  // 1. Per-metric verdicts CSV
  {
    std::ofstream f("out/verdicts.csv");
    f << "run,metric,verdict,severity,pattern,cause,action,z_local,value\n";
    for (auto& v : all_verdicts) {
      // Join causes with semicolons
      std::string cause_str;
      for (size_t i = 0; i < v.causes.size(); ++i) {
        if (i > 0) cause_str += "; ";
        cause_str += v.causes[i];
      }
      f << v.run << "," << v.metric << "," << v.verdict << ","
        << v.severity << "," << v.pattern << ",\""
        << cause_str << "\",\"" << v.action << "\","
        << std::fixed << std::setprecision(3) << v.z_local << ","
        << v.value << "\n";
    }
    std::cout << "[VERDICT] Wrote out/verdicts.csv (" << all_verdicts.size() << " entries)\n";
  }

  // 2. Per-run aggregate verdicts CSV
  {
    std::ofstream f("out/run_verdicts.csv");
    f << "run,verdict,n_good,n_suspect,n_bad,worst_metric,summary\n";
    for (auto& [run, rv] : run_agg) {
      f << rv.run << "," << rv.verdict << ","
        << rv.n_good << "," << rv.n_suspect << "," << rv.n_bad << ","
        << rv.worst_metric << ",\"" << rv.summary << "\"\n";
    }
    std::cout << "[VERDICT] Wrote out/run_verdicts.csv (" << run_agg.size() << " runs)\n";
  }

  // 3. Human-readable VERDICT.md
  {
    std::ofstream f("out/VERDICT.md");
    f << "# QA Verdict Report\n\n";
    f << "Automated physics-informed quality assessment.\n\n";

    // Read stamp if available
    {
      std::ifstream stamp("out/_stamp.txt");
      if (stamp) {
        f << "```\n";
        std::string line;
        while (std::getline(stamp, line)) f << line << "\n";
        f << "```\n\n";
      }
    }

    // Summary
    int total_good = 0, total_suspect = 0, total_bad = 0;
    for (auto& [run, rv] : run_agg) {
      if (rv.verdict == "GOOD") total_good++;
      else if (rv.verdict == "SUSPECT") total_suspect++;
      else total_bad++;
    }

    f << "## Summary\n\n";
    f << "| | Count |\n|---|---|\n";
    f << "| Total runs | " << run_agg.size() << " |\n";
    f << "| GOOD | " << total_good << " |\n";
    f << "| SUSPECT | " << total_suspect << " |\n";
    f << "| BAD | " << total_bad << " |\n\n";

    // Overall recommendation
    if (total_bad == 0 && total_suspect == 0) {
      f << "**Overall: All runs pass QA. No exclusions recommended.**\n\n";
    } else if (total_bad > 0) {
      f << "**Overall: " << total_bad << " run(s) recommended for exclusion from physics analysis.**\n\n";
    } else {
      f << "**Overall: " << total_suspect << " run(s) flagged for review. No exclusions yet.**\n\n";
    }

    // Per-run table
    f << "## Per-Run Verdicts\n\n";
    f << "| Run | Verdict | Good | Suspect | Bad | Worst Metric |\n";
    f << "|-----|---------|------|---------|-----|--------------|\n";
    for (auto& [run, rv] : run_agg) {
      std::string badge;
      if (rv.verdict == "GOOD")       badge = "GOOD";
      else if (rv.verdict == "SUSPECT") badge = "SUSPECT";
      else                             badge = "**BAD**";
      f << "| " << rv.run << " | " << badge << " | " << rv.n_good
        << " | " << rv.n_suspect << " | " << rv.n_bad
        << " | " << rv.worst_metric << " |\n";
    }
    f << "\n";

    // Detailed flagged runs
    f << "## Flagged Runs — Detailed Diagnosis\n\n";

    for (auto& [run, rv] : run_agg) {
      if (rv.verdict == "GOOD") continue;

      f << "### Run " << rv.run << " — " << rv.verdict << "\n\n";

      // Ladder health context if available
      if (ladder_by_run.count(run)) {
        auto& lh = ladder_by_run[run];
        if (lh.dead_count > 0 || lh.hot_count > 0) {
          f << "**INTT ladder health**: " << lh.dead_count << " dead, "
            << lh.hot_count << " hot (of " << lh.total_ladders << " total)\n\n";
        }
      }

      // Per-metric details for this run
      f << "| Metric | Value | z | Verdict | Pattern | Diagnosis |\n";
      f << "|--------|-------|---|---------|---------|----------|\n";
      for (auto& v : all_verdicts) {
        if (v.run != run || v.verdict == "GOOD") continue;
        std::string cause_brief = v.causes.empty() ? "" : v.causes[0];
        // Truncate if too long for table
        if (cause_brief.size() > 60) cause_brief = cause_brief.substr(0, 57) + "...";
        f << "| " << v.metric << " | " << std::fixed << std::setprecision(3)
          << v.value << " | " << v.z_local << " | " << v.verdict
          << " | " << v.pattern << " | " << cause_brief << " |\n";
      }
      f << "\n";

      // Expanded causes and actions
      for (auto& v : all_verdicts) {
        if (v.run != run || v.verdict == "GOOD") continue;
        f << "**" << v.metric << "** (" << v.severity << "):\n";
        f << "- Pattern: " << v.pattern << "\n";
        f << "- Possible causes:\n";
        for (auto& c : v.causes) f << "  - " << c << "\n";
        f << "- Recommended action: " << v.action << "\n\n";
      }

      f << "---\n\n";
    }

    // Metric health overview
    f << "## Metric Health Overview\n\n";
    f << "| Metric | Runs | Flagged | Flag Rate |\n";
    f << "|--------|------|---------|----------|\n";
    for (auto& m : metrics) {
      int total = 0, flagged = 0;
      for (auto& v : all_verdicts) {
        if (v.metric != m) continue;
        total++;
        if (v.verdict != "GOOD") flagged++;
      }
      if (total == 0) continue;
      double rate = 100.0 * flagged / total;
      f << "| " << m << " | " << total << " | " << flagged
        << " | " << std::fixed << std::setprecision(1) << rate << "% |\n";
    }
    f << "\n";

    // Consistency insights
    if (!consistency.empty()) {
      f << "## Trend Analysis\n\n";
      f << "| Metric | Slope | p-value | Changepoint Run | dBIC | Interpretation |\n";
      f << "|--------|-------|---------|-----------------|------|----------------|\n";
      for (auto& m : metrics) {
        if (!consistency.count(m)) continue;
        auto [sl, pv, rs, cp, db] = consistency[m];
        std::string interp;
        if (pv < 0.01 && std::fabs(sl) > 0) interp = "Significant trend detected";
        else if (db >= 10.0) interp = "Level shift at run " + std::to_string(cp);
        else interp = "Stable";
        f << "| " << m << " | " << std::scientific << std::setprecision(2) << sl
          << " | " << std::fixed << std::setprecision(4) << pv
          << " | " << (cp > 0 ? std::to_string(cp) : "—")
          << " | " << std::fixed << std::setprecision(1) << db
          << " | " << interp << " |\n";
      }
      f << "\n";
    }

    f << "---\n\n";
    f << "*Generated by verdict_engine.C — Physics-informed automated QA*\n";

    std::cout << "[VERDICT] Wrote out/VERDICT.md\n";
  }

  std::cout << "[VERDICT] Complete. " << all_verdicts.size() << " verdicts across "
            << run_agg.size() << " runs.\n";
  std::cout << "  GOOD: " << total_good << "  SUSPECT: " << total_suspect
            << "  BAD: " << total_bad << "\n";
}
