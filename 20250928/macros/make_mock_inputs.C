///////////////////////////////////////////////////////////////////////////////
// make_mock_inputs.C — Generate Mock ROOT Files for Pipeline Testing
//
// Creates realistic mock histogram files covering all three detector
// subsystems (INTT, MVTX, TPC) so the full pipeline can run end-to-end
// without real LFS data.
//
// Generates 5 files with run numbers 90001-90005, with varied parameters
// to test trend detection. Run 90004 has injected anomalies (dead INTT
// sector, dead MVTX chips, hot MVTX chip, shifted laser timing, dead
// TPC sector, degraded resolution).
//
// Usage:
//   root -l -b -q 'macros/make_mock_inputs.C()'
//   root -l -b -q 'macros/make_mock_inputs.C("data/","lists/mock_files.txt",5)'
///////////////////////////////////////////////////////////////////////////////

#include <TFile.h>
#include <TH1F.h>
#include <TH2F.h>
#include <TRandom3.h>
#include <TSystem.h>

#include <fstream>
#include <iostream>
#include <string>

void make_mock_inputs(const char* outdir = "data/",
                      const char* listfile = "lists/mock_files.txt",
                      int nfiles = 5)
{
  gSystem->mkdir(outdir, kTRUE);
  gSystem->mkdir("lists", kTRUE);

  TRandom3 rng(42);
  std::ofstream flist(listfile);

  int base_run = 90001;

  for (int ifile = 0; ifile < nfiles; ++ifile) {
    int run = base_run + ifile;
    // File 3 (run 90004) is the "anomalous" run
    bool anomalous = (ifile == 3);

    std::string fname = std::string(outdir) + "run" + std::to_string(run) + "-0000.root";
    flist << fname << "\n";

    TFile f(fname.c_str(), "RECREATE");

    // ============================
    // INTT histograms
    // ============================

    // ADC distribution (Landau)
    {
      double mpv = 50.0 + ifile * 0.3 + (anomalous ? 8.0 : 0.0);
      double sigma = 10.0;
      TH1F h("h_InttRawHitQA_adc", "INTT ADC;ADC;Counts", 256, 0, 256);
      for (int i = 0; i < 50000; ++i) h.Fill(rng.Landau(mpv, sigma));
      h.Write();
    }

    // BCO histogram with a peak
    {
      double peak = 32.0 + (anomalous ? 15.0 : 0.0);
      TH1F h("h_InttRawHitQA_bco", "INTT BCO;BCO;Counts", 128, 0, 128);
      for (int i = 0; i < 40000; ++i) h.Fill((int)rng.Gaus(peak, 4));
      h.Write();
    }

    // Cluster phi (uniform, with dead sector in anomalous run)
    {
      TH1F h("h_InttClusterQA_clusterPhi_incl", "INTT Cluster Phi;#phi;Counts",
             180, -3.14159, 3.14159);
      for (int i = 0; i < 80000; ++i) {
        double phi = rng.Uniform(-3.14159, 3.14159);
        if (anomalous && phi > 1.0 && phi < 1.5) continue;
        h.Fill(phi);
      }
      h.Write();
    }

    // Cluster phi L34
    {
      TH1F h("h_InttClusterQA_clusterPhi_l34", "INTT Cluster Phi L34;#phi;Counts",
             180, -3.14159, 3.14159);
      for (int i = 0; i < 40000; ++i) {
        double phi = rng.Uniform(-3.14159, 3.14159);
        if (anomalous && phi > 1.0 && phi < 1.5) continue;
        h.Fill(phi);
      }
      h.Write();
    }

    // Cluster size
    {
      double mean = 2.5 + ifile * 0.05;
      TH1F h("h_InttClusterQA_clusterSize", "INTT Cluster Size;Size;Counts", 10, 0, 10);
      for (int i = 0; i < 20000; ++i) h.Fill(rng.Gaus(mean, 0.7));
      h.Write();
    }

    // Sensor occupancy
    {
      TH1F h("h_InttRawHitQA_sensorOccupancy", "INTT Sensor Occupancy;Sensor;Hits",
             112, 0, 112);
      for (int i = 0; i < 50000; ++i) {
        int sensor = (int)rng.Uniform(0, 112);
        double rate = 1.0;
        if (anomalous && sensor < 5) rate = 0.01;
        if (rng.Uniform() < rate) h.Fill(sensor);
      }
      h.Write();
    }

    // Cluster sensor occupancy (for physqa median)
    {
      TH1F h("h_InttClusterQA_sensorOccupancy", "INTT Cluster Sensor Occ;Sensor;Clusters",
             112, 0, 112);
      for (int i = 0; i < 30000; ++i) h.Fill((int)rng.Uniform(0, 112));
      h.Write();
    }

    // ============================
    // MVTX histograms (TH2: stave x chip per layer)
    // ============================
    for (int L = 0; L <= 2; ++L) {
      int nstaves = (L == 0) ? 12 : (L == 1) ? 16 : 20;
      int nchips = 9;
      std::string hname = "h_MvtxRawHitQA_nhits_stave_chip_layer" + std::to_string(L);
      TH2F h(hname.c_str(), (hname + ";Stave;Chip").c_str(),
             nstaves, 0, nstaves, nchips, 0, nchips);

      double base_rate = 100.0 + ifile * 2.0;
      for (int is = 0; is < nstaves; ++is) {
        for (int ic = 0; ic < nchips; ++ic) {
          double occ = rng.Gaus(base_rate, base_rate * 0.15);
          if (anomalous && L == 0 && is == 3 && ic < 2) occ = 0;
          if (anomalous && L == 1 && is == 7 && ic == 4) occ = base_rate * 20;
          h.SetBinContent(is + 1, ic + 1, std::max(0.0, occ));
        }
      }
      h.Write();
    }

    // MVTX cluster size
    {
      TH1F h("h_MvtxClusterQA_clusterSize", "MVTX Cluster Size;Size;Counts", 10, 0, 10);
      for (int i = 0; i < 20000; ++i) h.Fill(rng.Gaus(3, 0.7));
      h.Write();
    }

    // ============================
    // TPC Laser timing histograms
    // ============================
    for (int R = 1; R <= 2; ++R) {
      for (const char* side : {"North", "South"}) {
        for (int line = 0; line < 12; ++line) {
          std::string hname = "h_TpcLaserQA_sample_R" + std::to_string(R) +
                              "_" + side + "_" + std::to_string(line);
          TH1F h(hname.c_str(), (hname + ";Time Sample;Counts").c_str(), 200, 0, 400);
          double mean = 200.0 + (std::string(side) == "South" ? 2.0 : 0.0)
                        + ifile * 0.1 + (anomalous ? 5.0 : 0.0);
          double sigma = 15.0;
          for (int i = 0; i < 5000; ++i) h.Fill(rng.Gaus(mean, sigma));
          h.Write();
        }
      }
    }

    // ============================
    // TPC sector ADC histograms (24 sectors x 3 rings)
    // ============================
    for (int isec = 0; isec < 24; ++isec) {
      for (int r = 0; r < 3; ++r) {
        std::string hname = "h_TpcRawHitQA_adc_sec" + std::to_string(isec) +
                            "_R" + std::to_string(r);
        TH1F h(hname.c_str(), (hname + ";ADC;Counts").c_str(), 256, 0, 1024);
        double n = 10000.0;
        if (anomalous && isec == 5) n = 100;
        for (int i = 0; i < (int)n; ++i) h.Fill(rng.Landau(120, 30));
        h.Write();
      }
    }

    // ============================
    // TPC cluster shape histograms (phi/z size per side per ring)
    // ============================
    for (int side = 0; side <= 1; ++side) {
      for (int r = 0; r < 3; ++r) {
        {
          std::string hname = "h_TpcClusterQA_phisize_side" + std::to_string(side) +
                              "_" + std::to_string(r);
          TH1F h(hname.c_str(), (hname + ";Phi Size;Counts").c_str(), 50, 0, 5);
          double mean = 2.0 + r * 0.1;
          for (int i = 0; i < 10000; ++i) h.Fill(rng.Gaus(mean, 0.4));
          h.Write();
        }
        {
          std::string hname = "h_TpcClusterQA_zsize_side" + std::to_string(side) +
                              "_" + std::to_string(r);
          TH1F h(hname.c_str(), (hname + ";Z Size;Counts").c_str(), 50, 0, 5);
          double mean = 1.8 + r * 0.15;
          for (int i = 0; i < 10000; ++i) h.Fill(rng.Gaus(mean, 0.35));
          h.Write();
        }
      }
    }

    // ============================
    // TPC resolution histograms
    // ============================
    for (int r = 0; r < 3; ++r) {
      {
        std::string hname = "h_TpcClusterQA_rphi_error_" + std::to_string(r);
        TH1F h(hname.c_str(), (hname + ";#sigma_{r#phi} [cm];Counts").c_str(), 50, 0, 2);
        double mean = 0.08 + r * 0.01 + (anomalous ? 0.03 : 0.0);
        for (int i = 0; i < 12000; ++i) h.Fill(rng.Gaus(mean, 0.015));
        h.Write();
      }
      {
        std::string hname = "h_TpcClusterQA_z_error_" + std::to_string(r);
        TH1F h(hname.c_str(), (hname + ";#sigma_z [cm];Counts").c_str(), 50, 0, 2);
        double mean = 0.12 + r * 0.015;
        for (int i = 0; i < 12000; ++i) h.Fill(rng.Gaus(mean, 0.02));
        h.Write();
      }
    }

    f.Close();
    std::cout << "[MOCK] Created " << fname << " (run " << run
              << (anomalous ? " ANOMALOUS" : "") << ")\n";
  }

  flist.close();
  std::cout << "[DONE] " << nfiles << " mock files written to " << outdir
            << "\n       File list: " << listfile << "\n";
}
