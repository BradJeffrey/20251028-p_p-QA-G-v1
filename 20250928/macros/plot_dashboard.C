#include <TCanvas.h>
#include <TGraphErrors.h>
#include <TAxis.h>
#include <TLatex.h>
#include <TSystem.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>
#include <cmath>

struct Row { int run; double y, ey; };

// Helper to find per-run CSV for a metric; tries both metrics_ and metric_ prefixes
static std::string find_perrun_csv(const std::string& metric) {
    std::string path1 = "out/metrics_" + metric + "_perrun.csv";
    std::ifstream f1(path1);
    if (f1.good()) return path1;
    std::string path2 = "out/metric_" + metric + "_perrun.csv";
    std::ifstream f2(path2);
    if (f2.good()) return path2;
    return "";
}

static bool read_perrun(const std::string& metric, std::vector<Row>& rows) {
    std::string path = find_perrun_csv(metric);
    if (path.empty()) return false;
    std::ifstream in(path);
    if (!in) return false;
    std::string s;
    bool header = true;
    while (std::getline(in, s)) {
        if (header) { header = false; continue; }
        if (s.empty()) continue;
        std::stringstream ss(s);
        std::string f0, f1, f2;
        if (!std::getline(ss, f0, ',')) continue;
        if (!std::getline(ss, f1, ',')) continue;
        if (!std::getline(ss, f2, ',')) continue;
        Row r;
        r.run = std::stoi(f0);
        r.y   = std::stod(f1);
        r.ey  = std::stod(f2);
        rows.push_back(r);
    }
    return !rows.empty();
}

static bool read_perfile(const std::string& metric, std::vector<Row>& rows) {
    std::string path = "out/metrics_" + metric + ".csv";
    std::ifstream in(path);
    if (!in) return false;
    std::string s;
    bool header = true;
    while (std::getline(in, s)) {
        if (header) { header = false; continue; }
        if (s.empty()) continue;
        std::stringstream ss(s);
        std::string a,b,c,d,e;
        if (!std::getline(ss, a, ',')) continue;
        if (!std::getline(ss, b, ',')) continue;
        if (!std::getline(ss, c, ',')) continue;
        if (!std::getline(ss, d, ',')) continue;
        if (!std::getline(ss, e, ',')) continue;
        Row r;
        r.run = std::stoi(a);
        r.y   = std::stod(d);
        r.ey  = std::stod(e);
        rows.push_back(r);
    }
    return !rows.empty();
}

static std::unique_ptr<TGraphErrors> make_graph(const std::string& metric) {
    std::vector<Row> rows;
    if (!read_perrun(metric, rows)) {
        std::cerr << "[INFO] per-run CSV missing for " << metric << ", falling back to per-file.\n";
        if (!read_perfile(metric, rows)) {
            return nullptr;
        }
    }
    auto gr = std::make_unique<TGraphErrors>();
    gr->SetName(("gr_dash_" + metric).c_str());
    for (size_t i = 0; i < rows.size(); ++i) {
        gr->SetPoint(i, rows[i].run, rows[i].y);
        gr->SetPointError(i, 0.0, rows[i].ey);
    }
    gr->SetTitle((metric + ";Run;" + metric).c_str());
    return gr;
}

// Pad the Y-axis if all points have the same value
static void pad_axis(TGraphErrors* gr) {
    if (!gr) return;
    int n = gr->GetN();
    if (n == 0) return;
    double x, y;
    double ymin = 1e100;
    double ymax = -1e100;
    for (int i = 0; i < n; ++i) {
        gr->GetPoint(i, x, y);
        if (y < ymin) ymin = y;
        if (y > ymax) ymax = y;
    }
    if (ymin == ymax) {
        double pad = (std::abs(ymax) > 0 ? 0.05 * std::abs(ymax) : 1.0);
        gr->GetYaxis()->SetLimits(ymin - pad, ymax + pad);
    }
}

void plot_dashboard() {
    std::vector<std::string> metrics = {
        "cluster_size_intt_mean",
        "cluster_phi_intt_rms",
        "intt_adc_peak",
        "intt_hits_asym"
    };
    std::vector<std::unique_ptr<TGraphErrors>> gs;
    gs.reserve(metrics.size());
    for (auto &m : metrics) {
        auto g = make_graph(m);
        if (g) {
            pad_axis(g.get());
        }
        gs.push_back(std::move(g));
    }
    TCanvas c("c_dash", "INTT dashboard (2x2)", 1200, 900);
    c.Divide(2,2);
    for (int i = 0; i < 4; ++i) {
        c.cd(i+1);
        if (gs[i]) {
            gs[i]->Draw("AP");
        } else {
            TLatex lat;
            lat.SetTextSize(0.045);
            lat.DrawLatexNDC(0.15, 0.5, Form("No data for %s", metrics[i].c_str()));
        }
    }
    gSystem->mkdir("out", true);
    c.SaveAs("out/dashboard_intt_2x2.pdf");
    c.SaveAs("out/dashboard_intt_2x2.png");
    std::cout << "[DONE] wrote out/dashboard_intt_2x2.{png,pdf}\n";
}
