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
#include <algorithm>

struct Row {
    int run;
    double y;
    double ey;
    int weak;
    int strong;
};

static std::string trim_ws(std::string s) {
    auto f=[](unsigned char c){return !std::isspace(c);};
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), f));
    s.erase(std::find_if(s.rbegin(), s.rend(), f).base(), s.end());
    return s;
}

static std::vector<std::string> metrics_from_conf(const char* conf) {
    std::vector<std::string> m;
    std::ifstream in(conf); std::string line;
    while (std::getline(in, line)) {
        line = trim_ws(line);
        if (line.empty() || line[0] == '#') continue;
        auto p = line.find(',');
        if (p == std::string::npos) continue;
        std::string name = trim_ws(line.substr(0, p));
        if (std::find(m.begin(), m.end(), name) == m.end())
            m.push_back(name);
    }
    return m;
}

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

// Config-driven dashboard: reads all metrics from metrics.conf,
// auto-sizes the grid, and produces a single PDF/PNG.
void plot_dashboard(const char* conf = "metrics.conf") {
    auto metrics = metrics_from_conf(conf);
    if (metrics.empty()) {
        std::cerr << "[WARN] no metrics found in " << conf << "\n";
        return;
    }

    int N = (int)metrics.size();
    // compute grid: ncols x nrows
    int ncols = (int)std::ceil(std::sqrt((double)N));
    int nrows = (int)std::ceil((double)N / ncols);

    std::vector<std::tuple<std::unique_ptr<TGraphErrors>, std::unique_ptr<TGraphErrors>, std::unique_ptr<TGraphErrors>>> all;
    all.reserve(N);
    for (auto& m : metrics) {
        auto tup = make_graphs(m);
        if (std::get<0>(tup)) {
            pad_axis(std::get<0>(tup).get());
        }
        all.push_back(std::move(tup));
    }

    int width  = 600 * ncols;
    int height = 450 * nrows;
    TCanvas c("c_dash", "QA Dashboard", width, height);
    c.Divide(ncols, nrows);

    for (int i = 0; i < N; ++i) {
        c.cd(i + 1);
        auto& tup = all[i];
        TGraphErrors* base   = std::get<0>(tup).get();
        TGraphErrors* wk     = std::get<1>(tup).get();
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
        if (wk && wk->GetN() > 0) {
            wk->SetMarkerColor(kOrange + 7);
            wk->Draw(hasData ? "P SAME" : "AP");
            leg.AddEntry(wk, "weak", "p");
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

    // generate filenames based on grid size
    std::string base_name = "out/dashboard_" + std::to_string(ncols) + "x" + std::to_string(nrows);
    c.SaveAs((base_name + ".pdf").c_str());
    c.SaveAs((base_name + ".png").c_str());
    std::cout << "[DONE] wrote " << base_name << ".{png,pdf} (" << N << " metrics, " << ncols << "x" << nrows << " grid)\n";
}
