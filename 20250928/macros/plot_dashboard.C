#include <TCanvas.h>
#include <TGraphErrors.h>
#include <TAxis.h>
#include <TLatex.h>
#include <TLegend.h>
#include <TSystem.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <tuple>

struct Row {
    int run;
    double y;
    double ey;
    int weak;
    int strong;
};

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
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (header) { header = false; continue; }
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string field;
        std::vector<std::string> fields;
        while (std::getline(ss, field, ',')) {
            fields.push_back(field);
        }
        if (fields.size() < 3) continue;
        Row r;
        r.run = std::stoi(fields[0]);
        r.y   = std::stod(fields[1]);
        r.ey  = std::stod(fields[2]);
        r.weak = 0;
        r.strong = 0;
        if (fields.size() >= 9) {
            r.weak = std::stoi(fields[7]);
            r.strong = std::stoi(fields[8]);
        }
        rows.push_back(r);
    }
    return !rows.empty();
}

static bool read_perfile(const std::string& metric, std::vector<Row>& rows) {
    std::string path = "out/metrics_" + metric + ".csv";
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (header) { header = false; continue; }
        if (line.empty()) continue;
        std::stringstream ss(line);
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
        r.weak = 0;
        r.strong = 0;
        rows.push_back(r);
    }
    return !rows.empty();
}

static std::tuple<std::unique_ptr<TGraphErrors>, std::unique_ptr<TGraphErrors>, std::unique_ptr<TGraphErrors>>
make_graphs(const std::string& metric) {
    std::vector<Row> rows;
    if (!read_perrun(metric, rows)) {
        std::cerr << "[INFO] per-run CSV missing for " << metric << ", falling back to per-file.\n";
        if (!read_perfile(metric, rows)) {
            return {nullptr, nullptr, nullptr};
        }
    }
    auto base = std::make_unique<TGraphErrors>();
    auto weak = std::make_unique<TGraphErrors>();
    auto strong = std::make_unique<TGraphErrors>();
    base->SetName(("gr_base_" + metric).c_str());
    weak->SetName(("gr_weak_" + metric).c_str());
    strong->SetName(("gr_strong_" + metric).c_str());
    int ib=0, iw=0, is=0;
    for (const auto& r : rows) {
        if (r.strong) {
            strong->SetPoint(is, r.run, r.y);
            strong->SetPointError(is, 0.0, r.ey);
            ++is;
        } else if (r.weak) {
            weak->SetPoint(iw, r.run, r.y);
            weak->SetPointError(iw, 0.0, r.ey);
            ++iw;
        } else {
            base->SetPoint(ib, r.run, r.y);
            base->SetPointError(ib, 0.0, r.ey);
            ++ib;
        }
    }
    base->SetTitle((metric + ";Run;" + metric).c_str());
    base->SetMarkerStyle(20);
    weak->SetMarkerStyle(20);
    strong->SetMarkerStyle(20);
    return {std::move(base), std::move(weak), std::move(strong)};
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
    std::vector<std::tuple<std::unique_ptr<TGraphErrors>, std::unique_ptr<TGraphErrors>, std::unique_ptr<TGraphErrors>>> all;
    all.reserve(metrics.size());
    for (auto &m : metrics) {
        auto tup = make_graphs(m);
        if (std::get<0>(tup)) {
            pad_axis(std::get<0>(tup).get());
        }
        all.push_back(std::move(tup));
    }
    TCanvas c("c_dash", "INTT dashboard (2x2)", 1200, 900);
    c.Divide(2,2);
    for (int i = 0; i < 4; ++i) {
        c.cd(i+1);
        auto &tup = all[i];
        TGraphErrors* base = std::get<0>(tup).get();
        TGraphErrors* weak = std::get<1>(tup).get();
        TGraphErrors* strong = std::get<2>(tup).get();
        TLegend leg(0.13, 0.77, 0.5, 0.92);
        leg.SetTextSize(0.035);
        bool hasData = false;
        if (base && base->GetN() > 0) {
            base->SetMarkerColor(kBlack);
            base->Draw("AP");
            leg.AddEntry(base, "normal", "p");
            hasData = true;
        }
        if (weak && weak->GetN() > 0) {
            weak->SetMarkerColor(kOrange+7);
            weak->Draw(hasData ? "P SAME" : "AP");
            leg.AddEntry(weak, "weak", "p");
            hasData = true;
        }
        if (strong && strong->GetN() > 0) {
            strong->SetMarkerColor(kRed);
            strong->Draw(hasData ? "P SAME" : "AP");
            leg.AddEntry(strong, "strong", "p");
            hasData = true;
        }
        if (hasData) {
            leg.Draw();
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
