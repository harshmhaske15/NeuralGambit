// solver.hpp
//
// Declares MateSolver: a forced-mate search engine built on top of
// disservin/chess-library (chess.hpp). It uses minimax with alpha-beta
// pruning, but the objective is NOT "best evaluation" - it is the classic
// chess-puzzle objective:
//
//      "Side to move forces checkmate in exactly N of their own moves,
//       no matter how the opponent replies."
//
// This is sometimes called a "proof-number"/"mate search" problem. We
// implement it as an alpha-beta search over a boolean/score space:
//      - On the mating side's turn, the side wins if ANY move leads to a
//        forced mate within the remaining ply budget (an OR node).
//      - On the defending side's turn, the side that is mated only loses if
//        EVERY legal reply still leads to forced mate (an AND node).
//
// The search depth is bounded by "mate in N moves" (N full moves for the
// side that mates), i.e. 2*N - 1 plies if the mating side moves first, or
// 2*N plies if mate is delivered on the (N)th move after a single ply has
// already elapsed (handled generically below via a plies budget).
//
#ifndef PUZZLE_SOLVER_HPP
#define PUZZLE_SOLVER_HPP

#include "chess.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace puzzle {

// A single ply of the solution, recorded in multiple representations so
// that callers (e.g. main.cpp's verifier) can pick whichever is convenient.
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

// Search statistics, exposed for diagnostics/benchmarking. (No
// transposition table is implemented - the search is fast enough without
// one for the puzzle sizes this engine targets - so there's no tt_hits
// counter here.)
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

    // Attempts to find a forced checkmate delivered by the side to move in
    // `mate_in_full_moves` of their own moves (the classic "mate in N"
    // puzzle framing: N=2 means "white/black to move mates on their 2nd
    // move", i.e. a 3- or 4-ply search depending on who is mated last).
    //
    // Returns a MateSearchResult; result.found is false if no forced mate
    // exists within the given move budget (or the position is already
    // illegal/terminal in a way that makes the question moot).
    MateSearchResult solve(chess::Board board, int mate_in_full_moves);

    const SearchStats& stats() const { return stats_; }

   private:
    std::uint64_t max_search_ms_;
    SearchStats stats_;
    std::chrono::steady_clock::time_point deadline_start_;
    bool time_is_up() const;

    // Plies budget version of the search. `plies` is the number of half
    // moves remaining. attacker_to_move indicates whether the side who is
    // trying to deliver mate is the one currently on move.
    //
    // Returns true if, from `board`'s current position with `plies` half
    // moves left, the attacking side can force checkmate. When true and
    // `out_line` is non-null, the principal mating line is appended to it
    // (in order).
    bool searchMate(chess::Board& board, int plies, bool attacker_to_move,
                     std::vector<chess::Move>* out_line);

    // Quick static check: is the side to move already checkmated?
    static bool isCheckmate(chess::Board& board);
};

// Converts a solved line into a human-readable string, e.g.:
//   "1. Rxh7+ Kxh7 2. Rh5#"     (attacker moves first)
//   "1... Bc5+ 2. Kxc5 Qb6+ 3. Kd5 Qd6#"   (defender to move first, per the
//                                            FEN's side-to-move, attacker is black)
std::string formatLine(const chess::Board& startBoard, const std::vector<SolutionPly>& line);

}  // namespace puzzle

#endif  // PUZZLE_SOLVER_HPP
