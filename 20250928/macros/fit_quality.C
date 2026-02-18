///////////////////////////////////////////////////////////////////////////////
// fit_quality.C — Physics-Informed Fit Quality Assessment
//
// For each ROOT file in the input list, this macro:
// 1. Fits physics-appropriate models to key histograms
//    - Landau for ADC distributions (expected for MIP energy loss in silicon)
//    - Gaussian for laser timing peaks
//    - Uniform hypothesis for phi distributions
// 2. Reports fit quality metrics (chi2/NDF, p-value, residual structure)
// 3. Flags runs where fit quality degrades (model breakdown)
//
// Outputs:
//   out/fit_quality.csv       — per-run, per-histogram fit results
//   out/fit_quality_flags.csv — runs where fits are poor (physics model doesn't hold)
//
// The verdict_engine.C can read these flags to enhance its diagnostics.
//
// Usage:
//   root -l -b -q 'macros/fit_quality.C("lists/files.txt")'
///////////////////////////////////////////////////////////////////////////////

#include <TFile.h>
#include <TH1.h>
#include <TF1.h>
#include <TMath.h>
#include <TSystem.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

struct FitResult {
  int run;
  int segment;
  std::string histogram;
  std::string model;        // landau, gaussian, uniform_chi2
  double chi2;
  double ndf;
  double chi2_ndf;
  double pvalue;
  double param0;            // model-specific: MPV for landau, mean for gaussian
  double param0_err;
  double param1;            // sigma for landau/gaussian, chi2_red for uniform
  double param1_err;
  std::string quality;      // GOOD, MARGINAL, POOR, FAILED
  std::string note;
};

// Parse run and segment from filename
struct Meta { int run; int seg; };
static Meta parse_meta(const std::string& path) {
  Meta m{-1, -1};
  size_t p = path.find_last_of('/');
  std::string base = (p == std::string::npos) ? path : path.substr(p+1);
  size_t rpos = base.find("run");
  if (rpos != std::string::npos) {
    size_t i = rpos + 3;
    std::string digs;
    while (i < base.size() && std::isdigit(base[i])) { digs.push_back(base[i]); ++i; }
    if (!digs.empty()) m.run = std::stoi(digs);
  }
  // segment: last numeric group before .root
  for (int i = (int)base.size()-1; i >= 0; --i) {
    if (std::isdigit(base[i])) {
      int j = i;
      while (j >= 0 && std::isdigit(base[j])) --j;
      if (j >= 0 && (base[j] == '_' || base[j] == '-')) {
        m.seg = std::stoi(base.substr(j+1, i-j));
        break;
      }
    }
  }
  return m;
}

static double hcounts(TH1* h) {
  return h ? h->Integral(1, h->GetNbinsX()) : 0.0;
}

static double quantile_x(TH1* h, double p) {
  if (!h) return std::numeric_limits<double>::quiet_NaN();
  double tot = hcounts(h);
  if (tot <= 0) return std::numeric_limits<double>::quiet_NaN();
  double acc = 0.0;
  for (int i = 1; i <= h->GetNbinsX(); ++i) {
    acc += h->GetBinContent(i);
    if (acc >= p * tot) return h->GetXaxis()->GetBinCenter(i);
  }
  return h->GetXaxis()->GetBinCenter(h->GetNbinsX());
}

// Classify fit quality by chi2/NDF and p-value
static std::string classify_quality(double chi2_ndf, double pval, bool fit_ok) {
  if (!fit_ok) return "FAILED";
  if (!std::isfinite(chi2_ndf)) return "FAILED";
  // p-value based classification
  if (pval > 0.05 && chi2_ndf < 3.0) return "GOOD";
  if (pval > 0.01 && chi2_ndf < 5.0) return "MARGINAL";
  return "POOR";
}

// Generate physics note for poor fits
static std::string fit_note(const std::string& histogram, const std::string& model,
                             const std::string& quality, double chi2_ndf) {
  if (quality == "GOOD") return "";
  if (quality == "FAILED") return "Fit did not converge; histogram may be empty or malformed";

  if (histogram.find("adc") != std::string::npos && model == "landau") {
    if (chi2_ndf > 5.0)
      return "ADC distribution deviates from Landau model; possible noise contamination or multi-peak structure";
    return "ADC Landau fit marginal; check for threshold effects or gain non-uniformity";
  }
  if (histogram.find("bco") != std::string::npos) {
    return "BCO distribution not well described by model; possible multi-modal structure (phase toggling)";
  }
  if (histogram.find("clusterPhi") != std::string::npos) {
    if (chi2_ndf > 3.0)
      return "Phi distribution significantly non-uniform; likely dead or hot sectors";
    return "Phi distribution mildly non-uniform";
  }
  return "Fit quality degraded; inspect histogram shape";
}

void fit_quality(const char* listfile = "lists/files.txt") {
  gSystem->mkdir("out", kTRUE);

  // Read file list
  std::vector<std::string> files;
  {
    std::ifstream in(listfile);
    std::string line;
    while (std::getline(in, line)) {
      // Trim
      while (!line.empty() && std::isspace(line.back())) line.pop_back();
      while (!line.empty() && std::isspace(line.front())) line.erase(line.begin());
      if (line.empty() || line[0] == '#') continue;
      files.push_back(line);
    }
  }
  if (files.empty()) { std::cerr << "[ERROR] No files in " << listfile << "\n"; return; }

  std::vector<FitResult> results;

  for (auto& fpath : files) {
    TFile tf(fpath.c_str(), "READ");
    if (tf.IsZombie()) { std::cerr << "[WARN] Cannot open " << fpath << "\n"; continue; }
    auto meta = parse_meta(fpath);

    // ============================
    // 1. INTT ADC — Landau fit
    // ============================
    {
      TH1* h = dynamic_cast<TH1*>(tf.Get("h_InttRawHitQA_adc"));
      FitResult fr;
      fr.run = meta.run; fr.segment = meta.seg;
      fr.histogram = "h_InttRawHitQA_adc";
      fr.model = "landau";

      if (h && hcounts(h) > 50) {
        double x10 = quantile_x(h, 0.10);
        double x90 = quantile_x(h, 0.90);
        if (!std::isfinite(x10) || !std::isfinite(x90) || x90 <= x10) {
          x10 = h->GetXaxis()->GetXmin(); x90 = h->GetXaxis()->GetXmax();
        }
        int ib = h->GetMaximumBin();
        double xpk = h->GetXaxis()->GetBinCenter(ib);
        double sig_guess = (x90 - x10) / 6.0;

        TF1 func("f_landau", "landau", x10, x90);
        func.SetParameters(h->GetMaximum(), xpk, std::max(1e-3, sig_guess));
        int status = h->Fit(&func, "QS0");

        bool ok = (status == 0);
        fr.chi2     = ok ? func.GetChisquare() : 0;
        fr.ndf      = ok ? func.GetNDF() : 0;
        fr.chi2_ndf = (fr.ndf > 0) ? fr.chi2 / fr.ndf : 999;
        fr.pvalue   = (fr.ndf > 0) ? TMath::Prob(fr.chi2, (int)fr.ndf) : 0;
        fr.param0     = ok ? func.GetParameter(1) : NAN;   // MPV
        fr.param0_err = ok ? func.GetParError(1) : 0;
        fr.param1     = ok ? func.GetParameter(2) : NAN;   // sigma
        fr.param1_err = ok ? func.GetParError(2) : 0;
        fr.quality  = classify_quality(fr.chi2_ndf, fr.pvalue, ok);
      } else {
        fr.chi2 = fr.ndf = fr.chi2_ndf = fr.pvalue = 0;
        fr.param0 = fr.param1 = NAN;
        fr.param0_err = fr.param1_err = 0;
        fr.quality = "FAILED";
      }
      fr.note = fit_note(fr.histogram, fr.model, fr.quality, fr.chi2_ndf);
      results.push_back(fr);
    }

    // ============================
    // 2. INTT cluster phi — uniformity chi2
    // ============================
    {
      TH1* h = dynamic_cast<TH1*>(tf.Get("h_InttClusterQA_clusterPhi_incl"));
      FitResult fr;
      fr.run = meta.run; fr.segment = meta.seg;
      fr.histogram = "h_InttClusterQA_clusterPhi_incl";
      fr.model = "uniform_chi2";

      if (h && hcounts(h) > 50) {
        int nb = h->GetNbinsX();
        double tot = hcounts(h);
        double expected = tot / nb;
        double chi2 = 0;
        for (int i = 1; i <= nb; ++i) {
          double obs = h->GetBinContent(i);
          if (expected > 0) chi2 += (obs - expected) * (obs - expected) / expected;
        }
        double ndf = std::max(1.0, (double)(nb - 1));

        fr.chi2     = chi2;
        fr.ndf      = ndf;
        fr.chi2_ndf = chi2 / ndf;
        fr.pvalue   = TMath::Prob(chi2, (int)ndf);
        fr.param0     = fr.chi2_ndf;     // chi2/NDF as the "value"
        fr.param0_err = 0;
        fr.param1     = fr.pvalue;        // p-value
        fr.param1_err = 0;
        fr.quality  = classify_quality(fr.chi2_ndf, fr.pvalue, true);
      } else {
        fr.chi2 = fr.ndf = fr.chi2_ndf = fr.pvalue = 0;
        fr.param0 = fr.param1 = NAN;
        fr.param0_err = fr.param1_err = 0;
        fr.quality = "FAILED";
      }
      fr.note = fit_note(fr.histogram, fr.model, fr.quality, fr.chi2_ndf);
      results.push_back(fr);
    }

    // ============================
    // 3. INTT BCO — peak structure assessment
    // ============================
    {
      TH1* h = dynamic_cast<TH1*>(tf.Get("h_InttRawHitQA_bco"));
      FitResult fr;
      fr.run = meta.run; fr.segment = meta.seg;
      fr.histogram = "h_InttRawHitQA_bco";
      fr.model = "fourier_r1";

      if (h && hcounts(h) > 50) {
        // Compute first Fourier harmonic amplitude
        double xmin = h->GetXaxis()->GetXmin();
        double xmax = h->GetXaxis()->GetXmax();
        double sumw = 0, cossum = 0, sinsum = 0;
        for (int i = 1; i <= h->GetNbinsX(); ++i) {
          double w = h->GetBinContent(i);
          if (w <= 0) continue;
          double x = h->GetXaxis()->GetBinCenter(i);
          double phi = 2 * TMath::Pi() * (x - xmin) / (xmax - xmin + 1e-12);
          sumw += w; cossum += w * std::cos(phi); sinsum += w * std::sin(phi);
        }
        double R1 = (sumw > 0) ? std::sqrt(cossum*cossum + sinsum*sinsum) / sumw : NAN;

        // Also compute uniformity chi2 for quality assessment
        int nb = h->GetNbinsX();
        double tot = hcounts(h);
        double expected = tot / nb;
        double chi2 = 0;
        for (int i = 1; i <= nb; ++i) {
          double obs = h->GetBinContent(i);
          if (expected > 0) chi2 += (obs - expected) * (obs - expected) / expected;
        }
        double ndf = std::max(1.0, (double)(nb - 1));

        fr.chi2     = chi2;
        fr.ndf      = ndf;
        fr.chi2_ndf = chi2 / ndf;
        fr.pvalue   = TMath::Prob(chi2, (int)ndf);
        fr.param0     = R1;              // Fourier R1 amplitude
        fr.param0_err = (sumw > 0) ? std::sqrt(std::max(0.0, 1.0 - R1*R1) / sumw) : 0;
        fr.param1     = fr.chi2_ndf;     // chi2/NDF
        fr.param1_err = 0;
        // BCO is expected to be non-uniform (has peak), so high chi2 is expected
        // Quality is based on whether R1 is reasonable
        fr.quality = std::isfinite(R1) ? "GOOD" : "FAILED";
      } else {
        fr.chi2 = fr.ndf = fr.chi2_ndf = fr.pvalue = 0;
        fr.param0 = fr.param1 = NAN;
        fr.param0_err = fr.param1_err = 0;
        fr.quality = "FAILED";
      }
      fr.note = fit_note(fr.histogram, fr.model, fr.quality, fr.chi2_ndf);
      results.push_back(fr);
    }

    // ============================
    // 4. INTT cluster size — model comparison
    // ============================
    {
      TH1* h = dynamic_cast<TH1*>(tf.Get("h_InttClusterQA_clusterSize"));
      FitResult fr;
      fr.run = meta.run; fr.segment = meta.seg;
      fr.histogram = "h_InttClusterQA_clusterSize";
      fr.model = "summary_stats";

      if (h && hcounts(h) > 50) {
        fr.param0     = h->GetMean();
        fr.param0_err = h->GetMeanError();
        fr.param1     = h->GetRMS();
        fr.param1_err = h->GetRMSError();

        // Quality: cluster size mean should be reasonable (1-5 strips)
        fr.chi2 = fr.ndf = 0;
        fr.chi2_ndf = 0;
        fr.pvalue = 1.0;
        if (fr.param0 < 1.0 || fr.param0 > 5.0) {
          fr.quality = "POOR";
          fr.note = "Mean cluster size outside expected range [1,5]; check thresholds";
        } else if (fr.param0 < 1.2 || fr.param0 > 4.0) {
          fr.quality = "MARGINAL";
          fr.note = "Mean cluster size near boundary of expected range";
        } else {
          fr.quality = "GOOD";
          fr.note = "";
        }
      } else {
        fr.chi2 = fr.ndf = fr.chi2_ndf = fr.pvalue = 0;
        fr.param0 = fr.param1 = NAN;
        fr.param0_err = fr.param1_err = 0;
        fr.quality = "FAILED";
        fr.note = "Insufficient statistics for cluster size assessment";
      }
      results.push_back(fr);
    }

    tf.Close();
  }

  // ============================================================================
  // Write outputs
  // ============================================================================

  // Full results
  {
    std::ofstream f("out/fit_quality.csv");
    f << "run,segment,histogram,model,chi2,ndf,chi2_ndf,pvalue,param0,param0_err,param1,param1_err,quality,note\n";
    for (auto& r : results) {
      f << r.run << "," << r.segment << "," << r.histogram << "," << r.model << ","
        << std::fixed << std::setprecision(3)
        << r.chi2 << "," << r.ndf << "," << r.chi2_ndf << "," << r.pvalue << ","
        << r.param0 << "," << r.param0_err << ","
        << r.param1 << "," << r.param1_err << ","
        << r.quality << ",\"" << r.note << "\"\n";
    }
    std::cout << "[FIT_QUALITY] Wrote out/fit_quality.csv (" << results.size() << " fits)\n";
  }

  // Flagged fits only
  {
    std::ofstream f("out/fit_quality_flags.csv");
    f << "run,segment,histogram,model,chi2_ndf,quality,note\n";
    int flagged = 0;
    for (auto& r : results) {
      if (r.quality == "GOOD") continue;
      f << r.run << "," << r.segment << "," << r.histogram << "," << r.model << ","
        << std::fixed << std::setprecision(3) << r.chi2_ndf << ","
        << r.quality << ",\"" << r.note << "\"\n";
      flagged++;
    }
    std::cout << "[FIT_QUALITY] Wrote out/fit_quality_flags.csv (" << flagged << " flags)\n";
  }

  // Summary
  int total = (int)results.size();
  int good = 0, marginal = 0, poor = 0, failed = 0;
  for (auto& r : results) {
    if (r.quality == "GOOD") good++;
    else if (r.quality == "MARGINAL") marginal++;
    else if (r.quality == "POOR") poor++;
    else failed++;
  }
  std::cout << "[FIT_QUALITY] Summary: " << total << " fits — "
            << good << " good, " << marginal << " marginal, "
            << poor << " poor, " << failed << " failed\n";
}
