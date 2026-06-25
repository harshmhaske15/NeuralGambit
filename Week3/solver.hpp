#ifndef PUZZLE_SOLVER_HPP
#define PUZZLE_SOLVER_HPP

#include "chess.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace puzzle {
struct SolutionPly {
    chess::Move move;        // engine-internal move representation
    std::string san;         // Standard Algebraic Notation, computed BEFORE the move is made
    std::string uci;         // UCI long algebraic ("e2e4", "e7e8q", ...)
    bool is_check = false;   // true if this move gives check
    bool is_mate  = false;   // true if this move delivers checkmate
};

// Result of a forced-mate search.
struct MateSearchResult {
    bool found = false;                 // true iff a forced mate within the ply budget was found
    int mate_in_moves = 0;               // "mate in N" - N full moves of the attacking side
    std::vector<SolutionPly> line;       // the principal mating line (attacker + defender moves interleaved)
    std::uint64_t nodes_searched = 0;    // total nodes visited (for diagnostics)
    double seconds_elapsed = 0.0;        // wall-clock time taken
};

struct SearchStats {
    std::uint64_t nodes = 0;
    std::uint64_t leaf_checks = 0;
};

// MateSolver performs an alpha-beta-pruned minimax search whose sole
// objective is proving the existence (or absence) of a forced mate within a
// given ply budget, then reconstructing the principal variation.
class MateSolver {
   public:
    // max_search_ms caps total wall-clock time for a single solve() call.
    // 0 means "no time limit" (search to completion of the ply budget).
    explicit MateSolver(std::uint64_t max_search_ms = 0) : max_search_ms_(max_search_ms) {}

    MateSearchResult solve(chess::Board board, int mate_in_full_moves);

    const SearchStats& stats() const { return stats_; }

   private:
    std::uint64_t max_search_ms_;
    SearchStats stats_;
    std::chrono::steady_clock::time_point deadline_start_;
    bool time_is_up() const;
    bool searchMate(chess::Board& board, int plies, bool attacker_to_move,
                     std::vector<chess::Move>* out_line);

    // Quick static check: is the side to move already checkmated?
    static bool isCheckmate(chess::Board& board);
};

std::string formatLine(const chess::Board& startBoard, const std::vector<SolutionPly>& line);

}  // namespace puzzle

#endif  // PUZZLE_SOLVER_HPP
