#include "chess.hpp"
#include "solver.hpp"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace chess;
using namespace puzzle;

namespace {
enum class CompareOutcome {
    MATCH,            
    MISMATCH,         
    NOT_COMPARABLE,   
};

struct CompareResult {
    CompareOutcome outcome;
    std::string detail;  
};

bool looksLikeCleanSan(const std::string& token) {
    if (token.empty()) return false;
    for (char c : token) {
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '#' ||
                  c == '=' || c == 'x' || c == 'O' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool tokenizeReference(const std::string& reference, std::vector<std::string>& tokens,
                        std::string& reason) {
    tokens.clear();

    std::istringstream iss(reference);
    std::string word;
    while (iss >> word) {
        // Strip a leading move-number prefix like "12." or "12...".
        std::size_t pos = 0;
        while (pos < word.size() && std::isdigit(static_cast<unsigned char>(word[pos]))) ++pos;
        if (pos > 0 && pos < word.size() && word[pos] == '.') {
            std::size_t afterDots = pos;
            while (afterDots < word.size() && word[afterDots] == '.') ++afterDots;
            word = word.substr(afterDots);
        }
        if (word.empty()) continue;

        // Free-text connectors / branch markers we explicitly refuse to
        // interpret as a single forced line.
        std::string lower = word;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                        [](unsigned char c) { return std::tolower(c); });
        if (lower == "if" || lower == "or" || lower == "w/" || lower == "e.p." ||
            lower == "e.p" || lower.find('/') != std::string::npos ||
            lower.find('(') != std::string::npos || lower.find(')') != std::string::npos) {
            reason = "reference contains free-text/shorthand token '" + word +
                     "' that isn't standard SAN";
            return false;
        }

        if (!looksLikeCleanSan(word)) {
            reason = "reference contains a non-SAN token '" + word + "'";
            return false;
        }

        tokens.push_back(word);
    }

    if (tokens.empty()) {
        reason = "reference text contained no recognizable moves";
        return false;
    }
    return true;
}

// Compares the solver's solved line against the reference solution text for one puzzle.
CompareResult compareToReference(const std::vector<SolutionPly>& solvedLine,
                                  const std::string& referenceSolution) {
    std::vector<std::string> refTokens;
    std::string reason;

    if (!tokenizeReference(referenceSolution, refTokens, reason)) {
        return {CompareOutcome::NOT_COMPARABLE, reason};
    }

    // Compare move by move, up to the shorter of the two sequences
    std::size_t n = std::min(refTokens.size(), solvedLine.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (refTokens[i] != solvedLine[i].san) {
            std::ostringstream oss;
            oss << "move #" << (i + 1) << " differs: reference says '" << refTokens[i]
                << "' but solver played '" << solvedLine[i].san << "'";
            return {CompareOutcome::MISMATCH, oss.str()};
        }
    }

    return {CompareOutcome::MATCH, ""};
}

struct Options {
    std::vector<std::pair<std::string, int>> files = {
        {"mate_in_2.json", 2},
        {"mate_in_3.json", 3},
        {"mate_in_4.json", 4},
    };
    std::uint64_t time_limit_ms = 5000;  // per-puzzle search time cap
    int limit_per_file = -1;             // -1 = no limit, else cap puzzles examined per file
    bool quiet = false;                  // suppress per-puzzle lines, just print summaries
};

bool parseArgs(int argc, char** argv, Options& opts) {
    bool filesOverridden = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--limit" && i + 1 < argc) {
            opts.limit_per_file = std::stoi(argv[++i]);
        } else if (arg == "--time-ms" && i + 1 < argc) {
            opts.time_limit_ms = std::stoull(argv[++i]);
        } else if (arg == "--quiet") {
            opts.quiet = true;
        } else if (arg == "--file" && i + 1 < argc) {
            std::string spec = argv[++i];
            auto colon = spec.find_last_of(':');
            if (colon == std::string::npos) {
                std::cerr << "error: --file expects path:mateIn, got '" << spec << "'\n";
                return false;
            }
            std::string path = spec.substr(0, colon);
            int mateIn = std::stoi(spec.substr(colon + 1));
            if (!filesOverridden) {
                opts.files.clear();
                filesOverridden = true;
            }
            opts.files.emplace_back(path, mateIn);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0]
                      << " [--limit N] [--time-ms MS] [--quiet] [--file path:mateIn ...]\n";
            return false;
        } else {
            std::cerr << "warning: ignoring unrecognized argument '" << arg << "'\n";
        }
    }
    return true;
}

// Loads a single JSON puzzle file of the form {"<fen>": "<solution text>", ...}.
bool loadPuzzleFile(const std::string& path, nlohmann::json& out) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "error: could not open puzzle file '" << path << "'\n";
        return false;
    }
    try {
        f >> out;
    } catch (const std::exception& e) {
        std::cerr << "error: failed to parse JSON in '" << path << "': " << e.what() << "\n";
        return false;
    }
    return true;
}

} 

int main(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) return 1;

    int grandTotal = 0, grandSolved = 0;
    int grandRefMatch = 0, grandRefMismatch = 0, grandRefNotComparable = 0;
    auto overallStart = std::chrono::steady_clock::now();

    for (auto& [path, mateIn] : opts.files) {
        nlohmann::json puzzles;
        if (!loadPuzzleFile(path, puzzles)) {
            std::cerr << "Skipping '" << path << "' due to load error.\n";
            continue;
        }

        std::cout << "\n=================================================================\n";
        std::cout << " Solving " << path << "  (mate in " << mateIn << ")\n";
        std::cout << "=================================================================\n";

        int total = 0, solved = 0;
        int refMatch = 0, refMismatch = 0, refNotComparable = 0;
        std::uint64_t totalNodes = 0;
        double totalTime = 0.0;

        for (auto& [fen, referenceSolution] : puzzles.items()) {
            if (opts.limit_per_file >= 0 && total >= opts.limit_per_file) break;
            ++total;

            // --- Supply the puzzle to the solver. ---
            Board board(fen);
            MateSolver solver(opts.time_limit_ms);
            MateSearchResult result = solver.solve(board, mateIn);

            totalNodes += result.nodes_searched;
            totalTime += result.seconds_elapsed;

            std::string refText = referenceSolution.get<std::string>();

            if (result.found) {
                ++solved;

                // --- Verify the solver's answer against the JSON's reference solution. ---
                CompareResult cmp = compareToReference(result.line, refText);
                switch (cmp.outcome) {
                    case CompareOutcome::MATCH: ++refMatch; break;
                    case CompareOutcome::MISMATCH: ++refMismatch; break;
                    case CompareOutcome::NOT_COMPARABLE: ++refNotComparable; break;
                }

                if (!opts.quiet || cmp.outcome == CompareOutcome::MISMATCH) {
                    const char* tag = cmp.outcome == CompareOutcome::MATCH
                                           ? "[ OK / MATCH    ]"
                                           : cmp.outcome == CompareOutcome::MISMATCH
                                                 ? "[ OK / MISMATCH ]"
                                                 : "[ OK / N-COMP   ]";
                    std::cout << "  " << tag << " " << fen << "\n"
                              << "         solved:    " << formatLine(board, result.line) << "\n"
                              << "         reference: " << refText << "\n";
                    if (cmp.outcome != CompareOutcome::MATCH) {
                        std::cout << "         note:      " << cmp.detail << "\n";
                    }
                }
            } else {
                std::cout << "  [MISS] " << fen << "\n"
                          << "         reference: " << refText << "\n"
                          << "         solver could not find a mate in " << mateIn
                          << " within the search budget\n";
            }
        }

        std::cout << "-----------------------------------------------------------------\n";
        std::cout << " " << path << " summary: " << solved << "/" << total << " puzzles solved.\n";
        std::cout << "   matched reference solution:        " << refMatch << "\n";
        std::cout << "   MISMATCHED reference solution:     " << refMismatch << "\n";
        std::cout << "   reference not directly comparable: " << refNotComparable << "\n";
        std::cout << "   total search nodes: " << totalNodes << "   total search time: "
                   << std::fixed << std::setprecision(3) << totalTime << "s\n";

        grandTotal += total;
        grandSolved += solved;
        grandRefMatch += refMatch;
        grandRefMismatch += refMismatch;
        grandRefNotComparable += refNotComparable;
    }

    auto overallEnd = std::chrono::steady_clock::now();
    double overallWall = std::chrono::duration<double>(overallEnd - overallStart).count();

    std::cout << "\n=================================================================\n";
    std::cout << " GRAND TOTAL: " << grandSolved << "/" << grandTotal << " puzzles solved.\n";
    std::cout << "   matched reference solution:        " << grandRefMatch << "\n";
    std::cout << "   MISMATCHED reference solution:     " << grandRefMismatch << "\n";
    std::cout << "   reference not directly comparable: " << grandRefNotComparable << "\n";
    std::cout << " Wall-clock time: " << std::fixed << std::setprecision(2) << overallWall
              << "s\n";
    std::cout << "=================================================================\n";

    return (grandSolved == grandTotal) ? 0 : 1;
}
