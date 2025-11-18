#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>

struct MetricInfo {
    std::string formula;
    std::string pattern;
    std::string physics;
    std::string rationale;
};

// Helper trim function
static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return std::string();
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

void generate_metrics_doc() {
    std::string inputFile = "configs/metrics_explanations.yaml";
    std::ifstream fin(inputFile);
    if (!fin.is_open()) {
        std::cerr << "Could not open " << inputFile << std::endl;
        return;
    }
    std::map<std::string, MetricInfo> metrics;
    std::string line;
    std::string currentKey;
    while (std::getline(fin, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        bool hasIndent = (!line.empty() && (line[0] == ' ' || line[0] == '\t'));
        size_t colonPos = trimmed.find(':');
        if (!hasIndent && colonPos != std::string::npos) {
            currentKey = trimmed.substr(0, colonPos);
            metrics[currentKey] = MetricInfo();
        } else if (hasIndent && colonPos != std::string::npos && !currentKey.empty()) {
            std::string subkey = trimmed.substr(0, colonPos);
            std::string value = trim(trimmed.substr(colonPos + 1));
            // Remove surrounding quotes if present
            if (!value.empty() && (value.front() == '"' || value.front() == '\'')) {
                char quote = value.front();
                if (value.back() == quote) {
                    value = value.substr(1, value.size() - 2);
                } else {
                    value = value.substr(1);
                }
            }
            MetricInfo &info = metrics[currentKey];
            if (subkey == "formula" || subkey == "Formula") {
                info.formula = value;
            } else if (subkey == "pattern" || subkey == "patterns" || subkey == "expected_pattern") {
                info.pattern = value;
            } else if (subkey == "physics" || subkey == "physics_context" || subkey == "physics_rationale") {
                info.physics = value;
            } else if (subkey == "rationale" || subkey == "why" || subkey == "reason") {
                info.rationale = value;
            } else {
                if (!info.rationale.empty()) info.rationale += " | ";
                info.rationale += subkey + ": " + value;
            }
        }
    }
    fin.close();
    std::string outFile = "docs/metrics_documentation.md";
    std::ofstream fout(outFile);
    if (!fout.is_open()) {
        std::cerr << "Could not open output file for writing: " << outFile << std::endl;
        return;
    }
    fout << "# Metric Explanations  \n\n";
    fout << "This document summarizes formulas, typical patterns, physics context, and rationale for each metric used in the real-data QA pipeline.  \n\n";
    for (const auto &kv : metrics) {
        const std::string &metric = kv.first;
        const MetricInfo &info = kv.second;
        fout << "## " << metric << "  \n";
        if (!info.formula.empty()) {
            fout << "- **Formula:** " << info.formula << "  \n";
        }
        if (!info.pattern.empty()) {
            fout << "- **Typical Pattern:** " << info.pattern << "  \n";
        }
        if (!info.physics.empty()) {
            fout << "- **Physics Context:** " << info.physics << "  \n";
        }
        if (!info.rationale.empty()) {
            fout << "- **Rationale:** " << info.rationale << "  \n";
        }
        fout << "  \n";
    }
    fout.close();
    std::cout << "Generated documentation in " << outFile << std::endl;
}

generate_metrics_doc();
