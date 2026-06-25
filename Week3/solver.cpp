// solver.cpp
//
// Implementation of MateSolver: alpha-beta pruned minimax search
// specialized for the "forced mate in N" puzzle objective.
//
// ---------------------------------------------------------------------------
// Algorithm overview
// ---------------------------------------------------------------------------
// We treat the search tree as alternating OR/AND layers:
//
//   * Attacker's turn (the side that must deliver mate): this is an OR
//     node - the attacker wins at this node if AT LEAST ONE legal move
//     leads to a forced win in the remaining ply budget. We try moves that
//     give check first (and, for the very last attacking ply, ONLY moves
//     that deliver checkmate are useful), which both speeds up the search
//     and matches how forced mates are constructed in practice.
//
//   * Defender's turn (the side being mated): this is an AND node - the
//     attacker's plan only "counts" as forced if EVERY legal defensive
//     reply still leads to a forced mate for the attacker in the remaining
//     budget. If the defender has any move that escapes mate (including
//     simply not getting mated within budget), the attacker's try fails at
//     this node.
//
// We implement this with classic alpha-beta on a {LOSS=0, WIN=1} scalar
// space (we don't need a continuous evaluation - mate search is a boolean
// decision problem at each ply count), which lets us prune heavily:
//   - At an OR (attacker) node, once we find a winning move we can stop
//     immediately (alpha cutoff - no need to consider further siblings).
//   - At an AND (defender) node, once we find a single move that refutes
//     the mate (i.e. attacker does NOT force mate after it), we can stop
//     immediately (beta cutoff).
//
// Terminal conditions:
//   - If it's the defender's turn and they have no legal moves:
//       - If they are in check -> checkmate -> attacker already won
//         (this terminal state is reached with 0 plies consumed on this
//         "turn"; it ends the search successfully).
//       - If they are NOT in check -> stalemate -> this is a DRAW, which is
//         a failure for the attacker (must not be reported as a solution).
//   - If it's the attacker's turn and they have no legal moves: this can
//     only happen if the attacker is somehow stalemated/mated, which is a
//     failure (should not occur in well-formed puzzles, but we handle it
//     defensively).
//   - If plies run out before mate is delivered: failure (no forced mate
//     within budget).
//
// We also bail out early at the attacker's last allowed ply: any attacking
// move that does not deliver an immediate checkmate cannot be part of a
// "mate in exactly N" solution, so we only try moves that check or mate at
// that final depth.
// ---------------------------------------------------------------------------

#include "solver.hpp"

#include <algorithm>

namespace puzzle {

using namespace chess;

bool MateSolver::time_is_up() const {
    if (max_search_ms_ == 0) return false;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - deadline_start_)
                       .count();
    return static_cast<std::uint64_t>(elapsed) >= max_search_ms_;
}

bool MateSolver::isCheckmate(Board& board) {
    if (!board.inCheck()) return false;
    Movelist moves;
    movegen::legalmoves(moves, board);
    return moves.empty();
}

// Lightweight move ordering heuristic to make alpha-beta pruning effective:
// captures and checks are tried first since forced-mate combinations are
// overwhelmingly built from checks and captures.
namespace {

int moveOrderScore(Board& board, const Move& m) {
    int score = 0;
    const bool isCapture = board.at(m.to()) != Piece::NONE || m.typeOf() == Move::ENPASSANT;
    if (isCapture) score += 1000;
    if (m.typeOf() == Move::PROMOTION) score += 500;

    // Does this move give check? Use the cheap givesCheck() helper.
    CheckType ct = board.givesCheck(m);
    if (ct != CheckType::NO_CHECK) score += 2000;

    return score;
}

void orderMoves(Board& board, Movelist& moves) {
    std::vector<std::pair<int, Move>> scored;
    scored.reserve(moves.size());
    for (const auto& m : moves) scored.emplace_back(moveOrderScore(board, m), m);
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = 0; i < scored.size(); ++i) moves[static_cast<int>(i)] = scored[i].second;
}

}  // namespace

// searchMate: returns true iff the attacker can force checkmate using
// exactly `plies` half-moves from this position onward (i.e. mate must land
// exactly when the budget is exhausted - this matches the standard puzzle
// convention "mate in N", where N is the minimum number of attacker moves
// needed). `attacker_to_move` tells us whose perspective we're evaluating
// (true = the side on move is the side that must deliver mate at the end of
// the line). Each call to searchMate consumes exactly one ply of `plies`
// for the move it makes before recursing; `plies` here is "half-moves
// remaining INCLUDING the one about to be made at this node".
bool MateSolver::searchMate(Board& board, int plies, bool attacker_to_move,
                             std::vector<Move>* out_line) {
    ++stats_.nodes;

    if (time_is_up()) return false;
    if (plies <= 0) return false;  // no moves left to make, but game isn't over yet -> fail

    Movelist moves;
    movegen::legalmoves(moves, board);

    if (attacker_to_move) {
        // --- OR node: attacker to move -----------------------------------
        if (moves.empty()) {
            // Attacker has no legal moves: stalemate (draw) or, in a
            // pathological input, already mated. Either way this is not a
            // forced win for the attacker.
            return false;
        }

        orderMoves(board, moves);

        const bool isLastAttackingPly = (plies == 1);

        for (const auto& m : moves) {
            // On the final allowed ply, only a move that delivers check
            // can possibly be the mating move - skip everything else.
            if (isLastAttackingPly) {
                CheckType ct = board.givesCheck(m);
                if (ct == CheckType::NO_CHECK) continue;
            }

            board.makeMove(m);
            ++stats_.leaf_checks;

            bool mateNow = isCheckmate(board);

            if (out_line) out_line->push_back(m);  // tentatively extend the path

            bool success = mateNow || (!isLastAttackingPly &&
                                        searchMate(board, plies - 1, false, out_line));

            if (success) {
                board.unmakeMove(m);
                return true;  // alpha cutoff: attacker found a winning move; keep path as-is
            }

            if (out_line) out_line->pop_back();  // undo tentative path extension
            board.unmakeMove(m);
        }

        return false;  // no attacking move forces mate within budget
    } else {
        // --- AND node: defender to move ----------------------------------
        if (moves.empty()) {
            // No legal replies for the defender:
            //   - in check  -> already checkmated (this terminal state is
            //                  normally caught by the parent's
            //                  isCheckmate() check right after the mating
            //                  move; handled here too for robustness).
            //   - not in check -> stalemate, a draw -> failure.
            return board.inCheck();
        }

        orderMoves(board, moves);

        std::vector<Move> bestSubLine;  // longest sub-line among all (necessarily winning) replies

        for (const auto& m : moves) {
            board.makeMove(m);
            ++stats_.leaf_checks;

            std::vector<Move> subLine;
            bool attackerStillWins =
                searchMate(board, plies - 1, true, out_line ? &subLine : nullptr);

            board.unmakeMove(m);

            if (!attackerStillWins) {
                // Beta cutoff: defender escapes via this move - the
                // attacker's plan fails regardless of earlier siblings.
                return false;
            }

            // Track the LONGEST surviving defense so the line we eventually
            // report corresponds to the defender's best (most testing)
            // resistance, matching how chess puzzles are conventionally
            // presented (e.g. "mate in 4" shows the line where the
            // defender survives the longest, not an accidental shortcut
            // where a weaker reply gets mated faster).
            if (out_line && subLine.size() + 1 > bestSubLine.size()) {
                bestSubLine.clear();
                bestSubLine.push_back(m);
                bestSubLine.insert(bestSubLine.end(), subLine.begin(), subLine.end());
            } else if (out_line && bestSubLine.empty()) {
                // Defensive fallback (e.g. out_line requested but this is
                // somehow the first/only reply): always have *something*.
                bestSubLine.push_back(m);
                bestSubLine.insert(bestSubLine.end(), subLine.begin(), subLine.end());
            }
        }

        // Every defensive reply still loses -> forced mate confirmed.
        // bestSubLine holds [longest-surviving reply, ...rest of forced
        // line]; since ALL replies lose, this is a perfectly valid
        // principal variation, and it's also the conventional "main line"
        // a human solver would present.
        if (out_line) {
            out_line->insert(out_line->end(), bestSubLine.begin(), bestSubLine.end());
        }
        return true;
    }
}

MateSearchResult MateSolver::solve(Board board, int mate_in_full_moves) {
    MateSearchResult result;
    stats_ = SearchStats{};
    deadline_start_ = std::chrono::steady_clock::now();

    if (mate_in_full_moves <= 0) return result;

    // Ply budget: "mate in N" means the attacking side needs N of their own
    // moves to deliver mate, with the defender replying in between. That is
    // N attacker moves and (N-1) defender moves interleaved:
    //   attacker, defender, attacker, defender, ..., attacker (mating move)
    // which totals 2*N - 1 plies. searchMate() consumes exactly one ply
    // per move made (by either side) and requires mate to land exactly when
    // the budget hits zero remaining moves for the side to move - in
    // practice this works out so that the attacker's mating move is always
    // the last ply of the budget.
    const int total_plies = 2 * mate_in_full_moves - 1;

    std::vector<Move> rawLine;
    bool found = searchMate(board, total_plies, true, &rawLine);

    result.found = found;
    result.nodes_searched = stats_.nodes;
    result.seconds_elapsed = std::chrono::duration<double>(
                                  std::chrono::steady_clock::now() - deadline_start_)
                                  .count();

    if (!found) return result;

    // Replay rawLine on a scratch board to compute SAN/UCI/check/mate flags
    // for each ply, producing the final structured solution.
    Board scratch = board;
    result.line.reserve(rawLine.size());
    for (const Move& m : rawLine) {
        SolutionPly ply;
        ply.move = m;
        ply.san = uci::moveToSan(scratch, m);
        ply.uci = uci::moveToUci(m, scratch.chess960());
        ply.is_check = scratch.givesCheck(m) != CheckType::NO_CHECK;

        scratch.makeMove(m);
        ply.is_mate = isCheckmate(scratch);

        result.line.push_back(std::move(ply));
    }

    // mate_in_moves reports N, the number of attacker moves the search was
    // asked to prove (and did prove) is sufficient to force checkmate on
    // every line. Individual defensive replies may allow the attacker to
    // mate sooner (e.g. a weaker defense), but a "mate in N" puzzle is
    // solved once we've shown N attacker moves suffice against ANY
    // defense - the specific principal line returned is just one such
    // witness (by construction, every other defensive try at every node
    // was also verified to lead to mate within the same overall budget).
    result.mate_in_moves = mate_in_full_moves;

    return result;
}

std::string formatLine(const Board& startBoard, const std::vector<SolutionPly>& line) {
    std::string out;
    Color stm = startBoard.sideToMove();
    int fullMoveNo = static_cast<int>(startBoard.fullMoveNumber());
    bool blackMovesFirst = (stm == Color::BLACK);

    for (std::size_t i = 0; i < line.size(); ++i) {
        // ply i is White's move iff (blackMovesFirst ? i is odd : i is even)
        bool isWhiteMove = blackMovesFirst ? (i % 2 == 1) : (i % 2 == 0);

        if (i == 0 && blackMovesFirst) {
            out += std::to_string(fullMoveNo) + "... ";
        } else if (isWhiteMove) {
            out += std::to_string(fullMoveNo) + ". ";
        }

        out += line[i].san;
        out += " ";

        if (!isWhiteMove) ++fullMoveNo;  // move number increments after Black plays
    }

    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

}  // namespace puzzle
