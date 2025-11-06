#include <TCanvas.h>
#include <TGraphErrors.h>
#include <TAxis.h>

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct Row { int run; double y, ey; };

static bool read_perrun(const std::string& path, std::vector<Row>& rows) {
  std::ifstream in(path); if(!in) return false;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header) { header=false; continue; }
    if (s.empty()) continue;
    // run,value,error
    size_t p1=s.find(','); if(p1==std::string::npos) continue;
    size_t p2=s.find(',',p1+1); if(p2==std::string::npos) continue;
    Row r; r.run = std::stoi(s.substr(0,p1));
    r.y   = std::stod(s.substr(p1+1,p2-p1-1));
    r.ey  = std::stod(s.substr(p2+1));
    rows.push_back(r);
  }
  return !rows.empty();
}
static bool read_perfile(const std::string& path, std::vector<Row>& rows) {
  std::ifstream in(path); if(!in) return false;
  std::string s; bool header=true;
  while (std::getline(in,s)) {
    if (header) { header=false; continue; }
    if (s.empty()) continue;
    // run,segment,file,value,error
    std::stringstream ss(s);
    std::string a,b,c,d,e;
    if (!std::getline(ss,a,',')) continue;
    if (!std::getline(ss,b,',')) continue;
    if (!std::getline(ss,c,',')) continue;
    if (!std::getline(ss,d,',')) continue;
    if (!std::getline(ss,e,',')) continue;
    Row r; r.run = std::stoi(a); r.y = std::stod(d); r.ey = std::stod(e);
    rows.push_back(r);
  }
  return !rows.empty();
}

static std::unique_ptr<TGraphErrors> make_graph(const std::string& metric) {
  std::vector<Row> rows;
  std::string perrun = "out/metrics_"+metric+"_perrun.csv";
  std::string perfile= "out/metrics_"+metric+".csv";
  if (!read_perrun(perrun, rows)) {
    std::cerr<<"[INFO] per-run CSV missing for "<<metric<<", falling back to per-file.\n";
    if (!read_perfile(perfile, rows)) { return nullptr; }
  }
  auto gr = std::make_unique<TGraphErrors>();
  gr->SetName(("gr_dash_"+metric).c_str());
  for (size_t i=0;i<rows.size();++i) {
    gr->SetPoint(i, rows[i].run, rows[i].y);
    gr->SetPointError(i, 0.0, rows[i].ey);
  }
  gr->SetTitle((metric+";Run;"+metric).c_str());
  return gr;
}

// Usage: .x macros/plot_dashboard.C()
void plot_dashboard() {
  std::vector<std::string> metrics = {
    "cluster_size_intt_mean",
    "cluster_phi_intt_rms",
    "intt_adc_peak",
    "intt_hits_asym"
  };

  std::vector<std::unique_ptr<TGraphErrors>> gs;
  gs.reserve(metrics.size());
  for (auto& m : metrics) {
    auto g = make_graph(m);
    gs.push_back(std::move(g));
  }

  TCanvas c("c_dash","INTT dashboard (2x2)",1200,900);
  c.Divide(2,2);
  for (int i=0;i<4;i++) {
    c.cd(i+1);
    if (gs[i]) gs[i]->Draw("AP");
    else {
      TLatex lat; lat.SetTextSize(0.045);
      lat.DrawLatexNDC(0.15,0.5,Form("No data for %s", metrics[i].c_str()));
    }
  }
  gSystem->mkdir("out", true);
  c.SaveAs("out/dashboard_intt_2x2.pdf");
  c.SaveAs("out/dashboard_intt_2x2.png");
  std::cout<<"[DONE] wrote out/dashboard_intt_2x2.{png,pdf}\n";
}
