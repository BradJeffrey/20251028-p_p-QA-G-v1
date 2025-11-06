#include <TFile.h>
#include <TH1.h>
#include <TString.h>
#include <TSystem.h>
#include <TMath.h>
#include <TROOT.h>
#include <TDirectory.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>

static double h_quantile(TH1* h, double p) {
  if (!h || h->GetEntries() <= 0) return TMath::QuietNaN();
  double q[1], probs[1]; probs[0] = p;
  h->GetQuantiles(1, q, probs);
  return q[0];
}

static double ks_uniform_p(TH1* h) {
  if (!h || h->GetEntries() <= 0) return TMath::QuietNaN();
  TH1* ref = (TH1*)h->Clone("ref_uniform");
  ref->Reset("ICES");
  int nb = h->GetNbinsX();
  double tot = h->GetEntries();
  double per = (nb > 0 ? tot/nb : 0);
  for (int i = 1; i <= nb; ++i) ref->SetBinContent(i, per);
  double p = h->KolmogorovTest(ref, "M");
  delete ref;
  return p;
}

static double chi2_uniform_red(TH1* h) {
  if (!h || h->GetEntries() <= 0) return TMath::QuietNaN();
  int nb = h->GetNbinsX();
  double tot = h->GetEntries();
  if (nb <= 1 || tot <= 0) return TMath::QuietNaN();
  double expect = tot/nb;
  double chi2 = 0.0; int dof = 0;
  for (int i = 1; i <= nb; ++i) {
    double obs = h->GetBinContent(i);
    double var = std::max(1.0, expect);
    chi2 += (obs - expect)*(obs - expect)/var;
    ++dof;
  }
  int red = std::max(1, dof - 1);
  return chi2 / red;
}

void extract_quick(const char* list = "lists/files.txt") {
  gSystem->mkdir("out", true);

  // Open per-metric CSVs
  std::ofstream csv_adc_peak("out/metrics_intt_adc_peak.csv");
  std::ofstream csv_adc_med ("out/metrics_intt_adc_median_p50.csv");
  std::ofstream csv_adc_p90 ("out/metrics_intt_adc_p90.csv");
  std::ofstream csv_phi_ks  ("out/metrics_intt_phi_uniform_r1.csv");
  std::ofstream csv_phi_chi ("out/metrics_intt_phi_chi2_reduced.csv");
  std::ofstream csv_bco_peak("out/metrics_intt_bco_peak.csv");

  // NEW: additional CSVs
  std::ofstream csv_cluster_size_mean("out/metrics_cluster_size_intt_mean.csv");
  std::ofstream csv_cluster_phi_rms  ("out/metrics_cluster_phi_intt_rms.csv");
  std::ofstream csv_hits_asym        ("out/metrics_intt_hits_asym.csv");

  auto write_header = [](std::ofstream& f){ f << "run,segment,file,value,error,weight\n"; };
  write_header(csv_adc_peak);
  write_header(csv_adc_med);
  write_header(csv_adc_p90);
  write_header(csv_phi_ks);
  write_header(csv_phi_chi);
  write_header(csv_bco_peak);
  write_header(csv_cluster_size_mean);
  write_header(csv_cluster_phi_rms);
  write_header(csv_hits_asym);

  std::ifstream in(list);
  if (!in) { std::cerr << "[ERR] cannot open " << list << "\n"; return; }

  std::string fpath;
  while (std::getline(in, fpath)) {
    if (fpath.empty()) continue;

    // crude run parsing: find "run" then digits
    long run = 0;
    {
      size_t p = fpath.find("run");
      if (p != std::string::npos) {
        size_t i = p + 3;
        while (i < fpath.size() && isdigit(fpath[i])) { run = run*10 + (fpath[i]-'0'); ++i; }
      }
    }
    int seg = -1;

    TFile f(fpath.c_str(), "READ");
    if (f.IsZombie()) {
      std::cerr << "[WARN] cannot open file: " << fpath << " -> writing NaN rows\n";
      csv_adc_peak << run << "," << seg << "," << fpath << ",nan,0,0\n";
      csv_adc_med  << run << "," << seg << "," << fpath << ",nan,0,0\n";
      csv_adc_p90  << run << "," << seg << "," << fpath << ",nan,0,0\n";
      csv_phi_ks   << run << "," << seg << "," << fpath << ",nan,0,0\n";
      csv_phi_chi  << run << "," << seg << "," << fpath << ",nan,0,0\n";
      csv_bco_peak << run << "," << seg << "," << fpath << ",nan,0,0\n";
      csv_cluster_size_mean << run << "," << seg << "," << fpath << ",nan,0,0\n";
      csv_cluster_phi_rms   << run << "," << seg << "," << fpath << ",nan,0,0\n";
      csv_hits_asym         << run << "," << seg << "," << fpath << ",nan,0,0\n";
      continue;
    }

    // Load histograms
    TH1* hadc = (TH1*)f.Get("h_InttRawHitQA_adc");
    TH1* hphi = (TH1*)f.Get("h_InttClusterQA_clusterPhi_incl");
    TH1* hbco = (TH1*)f.Get("h_InttRawHitQA_bco");

    // adc_peak (maxbin center)
    if (hadc && hadc->GetEntries() > 0) {
      int ib = hadc->GetMaximumBin();
      double x = hadc->GetXaxis()->GetBinCenter(ib);
      csv_adc_peak << run << "," << seg << "," << fpath << "," << x << ",0," << hadc->GetEntries() << "\n";
    } else { csv_adc_peak << run << "," << seg << "," << fpath << ",nan,0,0\n"; }

    // adc median p50
    if (hadc && hadc->GetEntries() > 0) {
      double med = h_quantile(hadc, 0.5);
      csv_adc_med << run << "," << seg << "," << fpath << "," << med << ",0," << hadc->GetEntries() << "\n";
    } else { csv_adc_med << run << "," << seg << "," << fpath << ",nan,0,0\n"; }

    // adc p90
    if (hadc && hadc->GetEntries() > 0) {
      double p90 = h_quantile(hadc, 0.9);
      csv_adc_p90 << run << "," << seg << "," << fpath << "," << p90 << ",0," << hadc->GetEntries() << "\n";
    } else { csv_adc_p90 << run << "," << seg << "," << fpath << ",nan,0,0\n"; }

    // phi KS (uniform p-value) and chi2 reduced vs uniform
    if (hphi && hphi->GetEntries() > 0) {
      double p = ks_uniform_p(hphi);
      csv_phi_ks << run << "," << seg << "," << fpath << "," << p << ",0," << hphi->GetEntries() << "\n";
      double chi = chi2_uniform_red(hphi);
      csv_phi_chi << run << "," << seg << "," << fpath << "," << chi << ",0," << hphi->GetEntries() << "\n";
    } else {
      csv_phi_ks  << run << "," << seg << "," << fpath << ",nan,0,0\n";
      csv_phi_chi << run << "," << seg << "," << fpath << ",nan,0,0\n";
    }

    // bco peak
    if (hbco && hbco->GetEntries() > 0) {
      int ib = hbco->GetMaximumBin();
      double x = hbco->GetXaxis()->GetBinCenter(ib);
      csv_bco_peak << run << "," << seg << "," << fpath << "," << x << ",0," << hbco->GetEntries() << "\n";
    } else { csv_bco_peak << run << "," << seg << "," << fpath << ",nan,0,0\n"; }

    // NEW: cluster size mean
    TH1* hsize = (TH1*)f.Get("h_InttClusterQA_clusterSize");
    if (hsize && hsize->GetEntries() > 0) {
      double mean_val = hsize->GetMean();
      csv_cluster_size_mean << run << "," << seg << "," << fpath << "," << mean_val << ",0," << hsize->GetEntries() << "\n";
    } else { csv_cluster_size_mean << run << "," << seg << "," << fpath << ",nan,0,0\n"; }

    // NEW: cluster phi RMS (reuse hphi)
    if (hphi && hphi->GetEntries() > 0) {
      double rms_val = hphi->GetRMS();
      csv_cluster_phi_rms << run << "," << seg << "," << fpath << "," << rms_val << ",0," << hphi->GetEntries() << "\n";
    } else { csv_cluster_phi_rms << run << "," << seg << "," << fpath << ",nan,0,0\n"; }

    // NEW: hit asymmetry from sensor occupancy
    TH1* hocc = (TH1*)f.Get("h_InttRawHitQA_sensorOccupancy");
    if (hocc && hocc->GetEntries() > 0) {
      double max_val = 0, min_val = 0; bool first_bin = true;
      for (int bi = 1; bi <= hocc->GetNbinsX(); ++bi) {
        double content = hocc->GetBinContent(bi);
        if (first_bin) { max_val = min_val = content; first_bin = false; }
        else {
          if (content > max_val) max_val = content;
          if (content < min_val) min_val = content;
        }
      }
      double asym = TMath::QuietNaN();
      if (max_val + min_val > 0) asym = (max_val - min_val) / (max_val + min_val);
      csv_hits_asym << run << "," << seg << "," << fpath << "," << asym << ",0," << hocc->GetEntries() << "\n";
    } else { csv_hits_asym << run << "," << seg << "," << fpath << ",nan,0,0\n"; }
  }

  std::cout << "[OK] extract_quick wrote per-metric CSV files in out/\n";
}
