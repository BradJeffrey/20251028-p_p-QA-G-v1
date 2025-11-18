#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <map>

using namespace std;

void diagnose_runs(const char* severity_file = "configs/severity_thresholds.yaml", const char* input_file = "out/physics_quality_perrun.csv", const char* output_file = "out/symptoms_perrun.csv") {
    // Load severity thresholds from YAML (simple parse)
    double mild = 1.0, moderate = 2.0, severe = 3.0;
    ifstream sf(severity_file);
    if (sf.is_open()) {
        string line;
        while (getline(sf, line)) {
            size_t pos = line.find(":");
            if (pos == string::npos) continue;
            string key = line.substr(0, pos);
            double value = atof(line.substr(pos + 1).c_str());
            // remove spaces
            key.erase(remove_if(key.begin(), key.end(), ::isspace), key.end());
            if (key == "mild") mild = value;
            else if (key == "moderate") moderate = value;
            else if (key == "severe") severe = value;
        }
    }
    // Open input CSV
    ifstream in(input_file);
    if (!in.is_open()) {
        cerr << "Cannot open " << input_file << endl;
        return;
    }
    // Output CSV header
    ofstream out(output_file);
    out << "run,gain_symptom,timing_symptom,phi_symptom,cluster_symptom,spread_symptom,asym_symptom,agg_score,cause" << endl;
    string header;
    getline(in, header); // skip header line
    string line;
    while (getline(in, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        string token;
        vector<string> tokens;
        while (getline(ss, token, ',')) {
            tokens.push_back(token);
        }
        if (tokens.size() < 7) continue;
        int run = stoi(tokens[0]);
        double gain = atof(tokens[1].c_str());
        double timing = atof(tokens[2].c_str());
        double phi = atof(tokens[3].c_str());
        double cluster = atof(tokens[4].c_str());
        double spread = atof(tokens[5].c_str());
        double asym = atof(tokens[6].c_str());
        auto categorize = [&](double v) {
            double av = fabs(v);
            if (av >= severe) return 3;
            if (av >= moderate) return 2;
            if (av >= mild) return 1;
            return 0;
        };
        int sg = categorize(gain);
        int st = categorize(timing);
        int sp = categorize(phi);
        int sc = categorize(cluster);
        int ss = categorize(spread);
        int sa = categorize(asym);
        double agg = (fabs(gain) + fabs(timing) + fabs(phi) + fabs(cluster) + fabs(spread) + fabs(asym)) / 6.0;
        // Determine primary cause as the metric with highest absolute value
        map<string,double> m;
        m["gain"] = fabs(gain);
        m["timing"] = fabs(timing);
        m["phi"] = fabs(phi);
        m["cluster"] = fabs(cluster);
        m["spread"] = fabs(spread);
        m["asym"] = fabs(asym);
        string cause = "none";
        double maxv = 0.0;
        for (auto &kv : m) {
            if (kv.second > maxv) {
                maxv = kv.second;
                cause = kv.first;
            }
        }
        out << run << "," << sg << "," << st << "," << sp << "," << sc << "," << ss << "," << sa << "," << agg << "," << cause << endl;
    }
    out.close();
    cout << "Symptoms per run written to " << output_file << endl;
}
