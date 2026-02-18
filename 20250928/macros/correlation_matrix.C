///////////////////////////////////////////////////////////////////////////////
// correlation_matrix.C — Cross-Metric Correlation Analysis
//
// Reads the wide-format per-run CSV and computes the full Pearson correlation
// matrix across all metrics. Identifies strongly correlated metric pairs
// (|R| > threshold) for use by the verdict engine.
//
// Outputs:
//   out/correlation_matrix.csv      — NxN correlation matrix
//   out/correlation_matrix.png/pdf  — TH2D heatmap visualisation
//   out/correlation_flags.csv       — pairs with |R| > 0.7
//
// Usage:
//   root -l -b -q 'macros/correlation_matrix.C("out/metrics_perrun_wide.csv")'
///////////////////////////////////////////////////////////////////////////////

#include <TCanvas.h>
#include <TH2D.h>
#include <TStyle.h>
#include <TSystem.h>
#include <TText.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace corr {

struct WideData {
  std::vector<int> runs;
  std::vector<std::string> cols;
  std::vector<std::vector<double>> data;  // [row][col]
};

static bool read_wide_csv(const std::string& path, WideData& wd) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  if (!std::getline(in, line)) return false;

  // Parse header
  std::stringstream hs(line);
  std::string cell;
  std::vector<std::string> header;
  while (std::getline(hs, cell, ',')) header.push_back(cell);
  if (header.size() < 3) return false;
  wd.cols.assign(header.begin() + 1, header.end());

  // Parse rows, skip any with NaN
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::stringstream ss(line);
    std::getline(ss, cell, ',');
    int run = std::stoi(cell);
    std::vector<double> row;
    bool has_nan = false;
    while (std::getline(ss, cell, ',')) {
      if (cell == "NaN" || cell == "nan" || cell.empty()) {
        has_nan = true;
        break;
      }
      row.push_back(std::stod(cell));
    }
    if (has_nan) continue;
    if ((int)row.size() != (int)wd.cols.size()) continue;
    wd.runs.push_back(run);
    wd.data.push_back(row);
  }
  return !wd.data.empty();
}

} // namespace corr

void correlation_matrix(const char* wide_csv = "out/metrics_perrun_wide.csv",
                        double flag_threshold = 0.7)
{
  using namespace corr;
  gSystem->mkdir("out", kTRUE);

  WideData wd;
  if (!read_wide_csv(wide_csv, wd)) {
    std::cerr << "[ERROR] Cannot read " << wide_csv << " or insufficient data\n";
    return;
  }

  int N = (int)wd.data.size();
  int P = (int)wd.cols.size();
  std::cout << "[CORR] " << N << " runs x " << P << " metrics\n";

  if (N < 3 || P < 2) {
    std::cerr << "[WARN] Need at least 3 runs and 2 metrics for correlation\n";
    return;
  }

  // Compute means and standard deviations
  std::vector<double> mu(P, 0), sd(P, 0);
  for (int j = 0; j < P; ++j) {
    for (int i = 0; i < N; ++i) mu[j] += wd.data[i][j];
    mu[j] /= N;
    for (int i = 0; i < N; ++i) {
      double d = wd.data[i][j] - mu[j];
      sd[j] += d * d;
    }
    sd[j] = (N > 1) ? std::sqrt(sd[j] / (N - 1)) : 0.0;
    if (sd[j] <= 0) sd[j] = 1.0;  // avoid division by zero
  }

  // Compute Pearson correlation matrix
  std::vector<std::vector<double>> R(P, std::vector<double>(P, 0.0));
  for (int a = 0; a < P; ++a) {
    R[a][a] = 1.0;
    for (int b = a + 1; b < P; ++b) {
      double cov = 0;
      for (int i = 0; i < N; ++i) {
        cov += (wd.data[i][a] - mu[a]) * (wd.data[i][b] - mu[b]);
      }
      cov /= (N - 1);
      double r = cov / (sd[a] * sd[b]);
      R[a][b] = r;
      R[b][a] = r;
    }
  }

  // Write correlation matrix CSV
  {
    std::ofstream f("out/correlation_matrix.csv");
    f << "metric";
    for (int j = 0; j < P; ++j) f << "," << wd.cols[j];
    f << "\n";
    for (int a = 0; a < P; ++a) {
      f << wd.cols[a];
      for (int b = 0; b < P; ++b) {
        f << "," << std::fixed << std::setprecision(4) << R[a][b];
      }
      f << "\n";
    }
    std::cout << "[CORR] Wrote out/correlation_matrix.csv\n";
  }

  // Write correlation flags (pairs with |R| > threshold)
  {
    std::ofstream f("out/correlation_flags.csv");
    f << "metric_a,metric_b,pearson_r,abs_r\n";
    int count = 0;
    for (int a = 0; a < P; ++a) {
      for (int b = a + 1; b < P; ++b) {
        double ar = std::fabs(R[a][b]);
        if (ar > flag_threshold) {
          f << wd.cols[a] << "," << wd.cols[b] << ","
            << std::fixed << std::setprecision(4) << R[a][b] << ","
            << ar << "\n";
          count++;
        }
      }
    }
    std::cout << "[CORR] Wrote out/correlation_flags.csv (" << count
              << " pairs with |R| > " << flag_threshold << ")\n";
  }

  // Draw heatmap
  gStyle->SetOptStat(0);
  auto* h2 = new TH2D("h_corr", "Metric Correlation Matrix;Metric;Metric",
                       P, 0, P, P, 0, P);
  for (int a = 0; a < P; ++a) {
    h2->GetXaxis()->SetBinLabel(a + 1, wd.cols[a].c_str());
    h2->GetYaxis()->SetBinLabel(a + 1, wd.cols[a].c_str());
    for (int b = 0; b < P; ++b) {
      h2->SetBinContent(a + 1, b + 1, R[a][b]);
    }
  }
  h2->SetMinimum(-1.0);
  h2->SetMaximum(1.0);
  h2->GetXaxis()->SetLabelSize(0.02);
  h2->GetYaxis()->SetLabelSize(0.02);
  h2->GetXaxis()->LabelsOption("v");

  int csize = std::max(800, P * 40 + 200);
  auto* c = new TCanvas("c_corr", "Correlation Matrix", csize, csize);
  c->SetLeftMargin(0.22);
  c->SetBottomMargin(0.22);
  c->SetRightMargin(0.14);
  gStyle->SetPalette(kRedBlue);
  h2->Draw("COLZ");

  // Add numeric labels for strong correlations
  auto* txt = new TText();
  txt->SetTextSize(0.015);
  txt->SetTextAlign(22);
  for (int a = 0; a < P; ++a) {
    for (int b = 0; b < P; ++b) {
      if (a == b) continue;
      if (std::fabs(R[a][b]) > flag_threshold) {
        txt->DrawText(a + 0.5, b + 0.5,
                      Form("%.2f", R[a][b]));
      }
    }
  }

  c->Print("out/correlation_matrix.png");
  c->Print("out/correlation_matrix.pdf");
  std::cout << "[CORR] Wrote out/correlation_matrix.{png,pdf}\n";
  std::cout << "[DONE] Correlation analysis complete.\n";
}
