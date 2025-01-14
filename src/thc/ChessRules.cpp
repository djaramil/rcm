/****************************************************************************
 * ChessRules.cpp Chess classes - Rules of chess
 *  Author:  Bill Forster
 *  License: MIT license. Full text of license is in associated file LICENSE
 *  Copyright 2010-2020, Bill Forster <billforsternz at gmail dot com>
 ****************************************************************************/

#include "ChessRules.h"
#include "PrivateChessDefs.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

using namespace std;
using namespace thc;

// Play a move
void ChessRules::PlayMove(Move move) {
    // Legal move - save it in history
    history.push_back(move);

    // Update full move count
    if (!white) {
        ++full_move_count;
    }

    // Update half move clock
    if (squares[move.src] == 'P' || squares[move.src] == 'p') {
        half_move_clock = 0;   // pawn move
    }
    else if (!IsEmptySquare(move.capture)) {
        half_move_clock = 0;   // capture
    }
    else {
        ++half_move_clock;    // neither pawn move nor capture
    }

    // Actually play the move
    PushMove(move);
}

void ChessRules::play_san_move(string_view san_move) {
    PlayMove(this->san_move(san_move));
}

void ChessRules::play_uci_move(string_view uci_move) {
    PlayMove(this->uci_move(uci_move));
}

bool ChessRules::is_legal() {
    return Evaluate();
}

bool ChessRules::is_legal(Move move) {
    PushMove(move);
    const auto okay = is_legal();
    PopMove(move);
    return okay;
}

MoveList ChessRules::select_legal(const MoveList& candidates) {
    MoveList legal;
    for (auto move : candidates) {
        if (is_legal(move)) {
            legal.push_back(move);
        }
    }
    return legal;
}

MoveList ChessRules::GenMoveList() {
    MoveList candidates;
    GenMoveList(candidates);
    return candidates;
}

MoveList ChessRules::GenLegalMoveList() {
    return select_legal(GenMoveList());
}

// Create a list of all legal moves in this position
void ChessRules::GenLegalMoveList(vector<Move>& moves) {
    moves = GenLegalMoveList();
}

// Create a list of all legal moves in this position, with extra info
void ChessRules::GenLegalMoveList(vector<Move>& moves,
                                  vector<bool>& check,
                                  vector<bool>& mate,
                                  vector<bool>& stalemate)
{
    moves.clear();
    check.clear();
    mate.clear();
    stalemate.clear();

    // Generate all moves, including illegal (e.g. put king in check) moves
    vector<Move> list2;
    GenMoveList(list2);

    // Loop copying the proven good ones
    for (auto move : list2) {
        PushMove(move);

        TERMINAL terminal_score;
        const bool okay = Evaluate(terminal_score);

        const Square king_to_move = static_cast<Square>(white ? d.wking_square : d.bking_square);
        const bool bcheck = AttackedPiece(king_to_move);
        PopMove(move);

        if (okay) {
            moves.push_back(move);
            stalemate.push_back(
                terminal_score == TERMINAL_WSTALEMATE ||
                terminal_score == TERMINAL_BSTALEMATE);
            const bool is_mate =
                terminal_score == TERMINAL_WCHECKMATE ||
                terminal_score == TERMINAL_BCHECKMATE;
            mate.push_back(is_mate);
            check.push_back(is_mate ? false : bcheck);
        }
    }
}

// Check draw rules (50 move rule etc.)
bool ChessRules::IsDraw(bool white_asks, DRAWTYPE& result) {
    bool   draw=false;

    // Insufficient mating material
    draw =  IsInsufficientDraw( white_asks, result );

    // 50 move rule
    if( !draw && half_move_clock>=100 )
    {
        result = DRAWTYPE_50MOVE;
        draw = true;
    }

    // 3 times repetition,
    if( !draw && GetRepetitionCount()>=3 )
    {
        result = DRAWTYPE_REPITITION;
        draw = true;
    }

    if( !draw )
        result = NOT_DRAW;
    return( draw );
}

// Get number of times position has been repeated
int ChessRules::GetRepetitionCount() {
    auto matches = 0;

    //  Save those aspects of current position that are changed by multiple
    //  PopMove() calls as we search backwards (i.e. squares, white,
    //  detail, detail_idx)
    char save_squares[sizeof(squares)];
    memcpy(save_squares, squares, sizeof save_squares);
    const auto save_detail_stack = detail_stack;
    bool          save_white     = white;
    DETAIL tmp{d};

    // Search backwards ....
    size_t nbr_half_moves = (full_move_count - 1) * 2 + (!white ? 1 : 0);
    nbr_half_moves = std::min(nbr_half_moves, history.size());
    nbr_half_moves = std::min(nbr_half_moves, detail_stack.size());
    auto idx = history.size();
    for (auto i = 0; i < nbr_half_moves; i++) {
        Move m = history[--idx];
        PopMove(m);

        // ... looking for matching positions
        if (white          == save_white       &&  // quick ones first!
            d.wking_square == tmp.wking_square &&
            d.bking_square == tmp.bking_square &&
            memcmp(squares, save_squares, sizeof squares) == 0)
        {
            matches++;
            if (d == tmp) {  // Castling flags and/or enpassant target different?
                continue;
            }

            // It might not be a match (but it could be - we have to unpack what the differences
            //  really mean)
            auto revoke_match = false;

            // Revoke match if different value of en-passant target square means different
            //  en passant possibilities
            if (d.enpassant_target != tmp.enpassant_target) {
                int ep_saved = tmp.enpassant_target;
                int ep_now   = d.enpassant_target;

                // Work out whether each en_passant is a real one, i.e. is there an opposition
                //  pawn in place to capture (if not it's just a double pawn advance with no
                //  actual enpassant consequences)
                auto real=false;
                auto ep = ep_saved;
                char const *squ = save_squares;
                for (auto j = 0; j < 2; j++) {
                    if (ep == a6) {
                            real = (squ[SE(ep)] == 'P');
                    }
                    else if (b6 <= ep && ep <= g6) {
                            real = (squ[SW(ep)] == 'P' || squ[SE(ep)] == 'P');
                    }
                    else if (ep == h6) {
                            real = (squ[SW(ep)] == 'P');
                    }
                    else if (ep == a3) {
                            real = (squ[NE(ep)] == 'p');
                    }
                    else if (b3 <= ep && ep <= g3) {
                            real = (squ[NE(ep)] == 'p' || squ[NW(ep)] == 'p');
                    }
                    else if (ep == h3) {
                            real = (squ[NW(ep)] == 'p' );
                    }
                    if (j > 0) {
                        ep_now = real ? ep : SQUARE_INVALID;    // evaluate second time through
                    }
                    else {
                        ep_saved = real ? ep : SQUARE_INVALID;  // evaluate first time through
                        ep = ep_now;                            // setup second time through
                        squ = squares;
                        real = false;
                    }
                }

                // If for example one en_passant is real and the other not, it's not a real match
                if (ep_saved != ep_now) {
                    revoke_match = true;
                }
            }

            // Revoke match if different value of castling flags means different
            //  castling possibilities
            if (!revoke_match && !eq_castling(d, tmp)) {
                bool wking_saved  = save_squares[e1]=='K' && save_squares[h1]=='R' && tmp.wking();
                bool wking_now    = squares[e1]=='K' && squares[h1]=='R' && d.wking();
                bool bking_saved  = save_squares[e8]=='k' && save_squares[h8]=='r' && tmp.bking();
                bool bking_now    = squares[e8]=='k' && squares[h8]=='r' && d.bking();
                bool wqueen_saved = save_squares[e1]=='K' && save_squares[a1]=='R' && tmp.wqueen();
                bool wqueen_now   = squares[e1]=='K' && squares[a1]=='R' && d.wqueen();
                bool bqueen_saved = save_squares[e8]=='k' && save_squares[a8]=='r' && tmp.bqueen();
                bool bqueen_now   = squares[e8]=='k' && squares[a8]=='r' && d.bqueen();
                revoke_match = (
                    wking_saved  != wking_now  ||
                    bking_saved  != bking_now  ||
                    wqueen_saved != wqueen_now ||
                    bqueen_saved != bqueen_now
                );
            }

            // If the real castling or enpassant possibilities differ, it's not a match
            //  At one stage we just did a naive binary match of the details - not good enough. For example
            //  a rook moving away from h1 doesn't affect the WKING flag, but does disallow white king side
            //  castling
            if (revoke_match) {
                matches--;
            }
        }

        // For performance reasons, abandon search early if pawn move
        //  or capture
        if( squares[m.src]=='P' || squares[m.src]=='p' || !IsEmptySquare(m.capture) )
            break;
    }

    // Restore current position
    memcpy(squares, save_squares, sizeof squares);
    white      = save_white;
    detail_stack = save_detail_stack;
    d = tmp;
    return matches+1;  // +1 counts original position
}

// Check insufficient material draw rule
bool ChessRules::IsInsufficientDraw(bool white_asks, DRAWTYPE& result) {
    char   piece;
    int    piece_count=0;
    bool   bishop_or_knight=false, lone_wking=true, lone_bking=true;
    bool   draw=false;

    // Loop through the board
    for( Square square=a8; square<=h1; ++square )
    {
        piece = squares[square];
        switch( piece )
        {
            case 'B': case 'b':
            case 'N': case 'n': bishop_or_knight = true;  // and fall through
            case 'Q': case 'q':
            case 'R': case 'r':
            case 'P': case 'p':
                piece_count++;
                if( isupper(piece) )
                    lone_wking = false;
                else
                    lone_bking = false;
                break;
        }
        if( !lone_wking && !lone_bking )
            break;  // quit early for performance
    }

    // Automatic draw if K v K or K v K+N or K v K+B
    //  (note that K+B v K+N etc. is not auto granted due to
    //   selfmates in the corner)
    if( piece_count==0 ||
        (piece_count==1 && bishop_or_knight)
      )
    {
        draw = true;
        result = DRAWTYPE_INSUFFICIENT_AUTO;
    }
    else {
        // Otherwise side playing against lone K can claim a draw
        if( white_asks && lone_bking )
        {
            draw   = true;
            result = DRAWTYPE_INSUFFICIENT;
        }
        else if( !white_asks && lone_wking )
        {
            draw   = true;
            result = DRAWTYPE_INSUFFICIENT;
        }
    }
    return( draw );
}

// Generate a list of all possible moves in a position
void ChessRules::GenMoveList(vector<Move>& moves) {
    moves.clear();

    for (Square square = a8; square <= h1; ++square) {
        // If square occupied by a piece of the right colour
        const auto piece = squares[square];
        if ((white && IsBlack(piece)) || (!white && IsWhite(piece))) {
            continue;
        }

        // Generate moves according to the occupying piece
        switch (piece) {
        case 'P':
            WhitePawnMoves(moves, square );
            break;
        case 'p':
            BlackPawnMoves(moves, square );
            break;
        case 'N': case 'n':
            ShortMoves(moves, square, knight_lookup[square], NOT_SPECIAL);
            break;
        case 'B': case 'b':
            LongMoves(moves, square, bishop_lookup[square]);
            break;
        case 'R': case 'r':
            LongMoves(moves, square, rook_lookup[square]);
            break;
        case 'Q': case 'q':
            LongMoves(moves, square, queen_lookup[square]);
            break;
        case 'K': case 'k':
            KingMoves(moves, square);
            break;
        }
    }
}

// Generate moves for pieces that move along multi-move rays (B,R,Q)
void ChessRules::LongMoves(vector<Move>& moves, Square square, const lte* ptr) {
    for (lte nbr_rays = *ptr++; nbr_rays != 0; --nbr_rays) {
        for (lte ray_len = *ptr++; ray_len != 0; --ray_len) {
            const Square dst = static_cast<Square>(*ptr++);
            const char piece = squares[dst];

            // If square not occupied (empty), add move to list
            if (IsEmptySquare(piece)) {
                moves.push_back({square, dst, NOT_SPECIAL, ' '});
            }
            // Else must move to end of ray
            else {
                ptr += ray_len - 1;

                // If not occupied by our man add a capture
                if ((white && IsBlack(piece)) || (!white && IsWhite(piece))) {
                    moves.push_back({square, dst, NOT_SPECIAL, piece});
                }
                break;
            }
        }
    }
}

// Generate moves for pieces that move along single move rays (N,K)
void ChessRules::ShortMoves(
    vector<Move>& moves, Square square, const lte* ptr, SPECIAL special)
{
    for (lte nbr_moves = *ptr++; nbr_moves != 0; --nbr_moves) {
        const Square dst = static_cast<Square>(*ptr++);
        const char piece = squares[dst];

        // If square not occupied (empty), add move to list
        if (IsEmptySquare(piece)) {
            moves.push_back({square, dst, special, ' '});
        }
        // Else if occupied by enemy man, add move to list as a capture
        else if ((white && IsBlack(piece)) || (!white && IsWhite(piece))) {
            moves.push_back({square, dst, special, piece});
        }
    }
}

// Generate list of king moves
void ChessRules::KingMoves(vector<Move>& moves, Square square) {
    const lte* ptr = king_lookup[square];
    ShortMoves(moves, square, ptr, SPECIAL_KING_MOVE);

    // White castling
    if (square == e1)   // king on e1 ?
    {
        // King side castling
        if (squares[g1] == ' '        &&
            squares[f1] == ' '        &&
            squares[h1] == 'R'        &&
            d.wking()                 &&
            !AttackedSquare(e1,false) &&
            !AttackedSquare(f1,false) &&
            !AttackedSquare(g1,false))
        {
            moves.push_back({e1, g1, SPECIAL_WK_CASTLING, ' '});
        }

        // Queen side castling
        if (squares[b1] == ' '        &&
            squares[c1] == ' '        &&
            squares[d1] == ' '        &&
            squares[a1] == 'R'        &&
            d.wqueen()                &&
            !AttackedSquare(e1,false) &&
            !AttackedSquare(d1,false) &&
            !AttackedSquare(c1,false))
        {
            moves.push_back({e1, c1, SPECIAL_WQ_CASTLING, ' '});
        }
    }

    // Black castling
    if (square == e8)   // king on e8 ?
    {
        // King side castling
        if (squares[g8] == ' '       &&
            squares[f8] == ' '       &&
            squares[h8] == 'r'       &&
            d.bking()                &&
            !AttackedSquare(e8,true) &&
            !AttackedSquare(f8,true) &&
            !AttackedSquare(g8,true))
        {
            moves.push_back({e8, g8, SPECIAL_BK_CASTLING, ' '});
        }

        // Queen side castling
        if (squares[b8] == ' '       &&
            squares[c8] == ' '       &&
            squares[d8] == ' '       &&
            squares[a8] == 'r'       &&
            d.bqueen()               &&
            !AttackedSquare(e8,true) &&
            !AttackedSquare(d8,true) &&
            !AttackedSquare(c8,true))
        {
            moves.push_back({e8, c8, SPECIAL_BQ_CASTLING, ' '});
        }
    }
}

// Generate list of white pawn moves
void ChessRules::WhitePawnMoves(vector<Move>& moves, Square square) {
    const lte* ptr = pawn_white_lookup[square];
    bool promotion = RANK(square) == '7';

    // Capture ray
    for (lte nbr_moves = *ptr++; nbr_moves != 0; --nbr_moves) {
        const Square dst = static_cast<Square>(*ptr++);
        if (dst == d.enpassant_target) {
            moves.push_back({square, dst, SPECIAL_WEN_PASSANT, 'p'});
        }
        else if (IsBlack(squares[dst])) {
            const auto capture = squares[dst];
            if (!promotion) {
                moves.push_back({square, dst, NOT_SPECIAL, capture});
            }
            else {
                // Generate (under)promotions in the order (Q),N,B,R
                //  but we no longer rely on this elsewhere as it
                //  stops us reordering moves
                moves.push_back({square, dst, SPECIAL_PROMOTION_QUEEN,  capture});
                moves.push_back({square, dst, SPECIAL_PROMOTION_KNIGHT, capture});
                moves.push_back({square, dst, SPECIAL_PROMOTION_BISHOP, capture});
                moves.push_back({square, dst, SPECIAL_PROMOTION_ROOK,   capture});
            }
        }
    }

    // Advance ray
    lte nbr_moves = *ptr++;
    for (int i = 0; i < nbr_moves; ++i) {
        const Square dst = static_cast<Square>(*ptr++);

        // If square occupied, end now
        if (!IsEmptySquare(squares[dst])) {
            break;
        }
        if (!promotion) {
            moves.push_back({square, dst, i == 0 ? NOT_SPECIAL : SPECIAL_WPAWN_2SQUARES, ' '});
        }
        else {
            // Generate (under)promotions in the order (Q),N,B,R
            //  but we no longer rely on this elsewhere as it
            //  stops us reordering moves
            moves.push_back({square, dst, SPECIAL_PROMOTION_QUEEN,  ' '});
            moves.push_back({square, dst, SPECIAL_PROMOTION_KNIGHT, ' '});
            moves.push_back({square, dst, SPECIAL_PROMOTION_BISHOP, ' '});
            moves.push_back({square, dst, SPECIAL_PROMOTION_ROOK,   ' '});
        }
    }
}

// Generate list of black pawn moves
void ChessRules::BlackPawnMoves(vector<Move>& moves, Square square) {
    const lte* ptr = pawn_black_lookup[square];
    bool promotion = RANK(square) == '2';

    // Capture ray
    for (lte nbr_moves = *ptr++; nbr_moves != 0; --nbr_moves) {
        const Square dst = static_cast<Square>(*ptr++);
        if (dst == d.enpassant_target) {
            moves.push_back({square, dst, SPECIAL_BEN_PASSANT, 'P'});
        }
        else if (IsWhite(squares[dst])) {
            const auto capture = squares[dst];
            if (!promotion) {
                moves.push_back({square, dst, NOT_SPECIAL, capture});
            }
            else {
                // Generate (under)promotions in the order (Q),N,B,R
                //  but we no longer rely on this elsewhere as it
                //  stops us reordering moves
                moves.push_back({square, dst, SPECIAL_PROMOTION_QUEEN,  capture});
                moves.push_back({square, dst, SPECIAL_PROMOTION_KNIGHT, capture});
                moves.push_back({square, dst, SPECIAL_PROMOTION_BISHOP, capture});
                moves.push_back({square, dst, SPECIAL_PROMOTION_ROOK,   capture});
            }
        }
    }

    // Advance ray
    lte nbr_moves = *ptr++;
    for (int i = 0; i < nbr_moves; ++i) {
        const Square dst = static_cast<Square>(*ptr++);

        // If square occupied, end now
        if (!IsEmptySquare(squares[dst])) {
            break;
        }
        if (!promotion) {
            moves.push_back({square, dst, i == 0 ? NOT_SPECIAL : SPECIAL_BPAWN_2SQUARES, ' '});
        }
        else {
            // Generate (under)promotions in the order (Q),N,B,R
            //  but we no longer rely on this elsewhere as it
            //  stops us reordering moves
            moves.push_back({square, dst, SPECIAL_PROMOTION_QUEEN,  ' '});
            moves.push_back({square, dst, SPECIAL_PROMOTION_KNIGHT, ' '});
            moves.push_back({square, dst, SPECIAL_PROMOTION_BISHOP, ' '});
            moves.push_back({square, dst, SPECIAL_PROMOTION_ROOK,   ' '});
        }
    }
}

// Make a move (with the potential to undo)
void ChessRules::PushMove(Move m) {
    // Push old details onto stack
    detail_stack.push_back(d);

    // Update castling prohibited flags for destination square, eg h8 -> bking
    switch (m.dst) {
    case a8: d.bqueen(false); break;
    case e8: d.bqueen(false);
    case h8: d.bking(false);  break;
    case a1: d.wqueen(false); break;
    case e1: d.wqueen(false);
    case h1: d.wking(false);  break;
    default:        // IMPORTANT - only dst is required since we also qualify
        break;      //  castling with presence of rook and king on right squares.
    }               //  (I.E. if a rook or king leaves its original square, the
                    //  castling prohibited flag is unaffected, but it doesn't
                    //  matter since we won't castle unless rook and king are
                    //  present on the right squares. If subsequently a king or
                    //  rook returns, that's okay too because the  castling flag
                    //  is cleared by its arrival on the m.dst square, so
                    //  castling remains prohibited).
    d.enpassant_target = SQUARE_INVALID;

    // Special handling might be required
    switch (m.special) {
    default:
        squares[m.dst] = squares[m.src];
        squares[m.src] = ' ';
        break;

    // King move updates king position in details field
    case SPECIAL_KING_MOVE:
        squares[m.dst] = squares[m.src];
        squares[m.src] = ' ';
        if (white) {
            d.wking_square = m.dst;
        }
        else {
            d.bking_square = m.dst;
        }
        break;

    // In promotion case, dst piece doesn't equal src piece
    case SPECIAL_PROMOTION_QUEEN:
        squares[m.src] = ' ';
        squares[m.dst] = (white?'Q':'q');
        break;

    // In promotion case, dst piece doesn't equal src piece
    case SPECIAL_PROMOTION_ROOK:
        squares[m.src] = ' ';
        squares[m.dst] = (white?'R':'r');
        break;

    // In promotion case, dst piece doesn't equal src piece
    case SPECIAL_PROMOTION_BISHOP:
        squares[m.src] = ' ';
        squares[m.dst] = (white?'B':'b');
        break;

    // In promotion case, dst piece doesn't equal src piece
    case SPECIAL_PROMOTION_KNIGHT:
        squares[m.src] = ' ';
        squares[m.dst] = (white?'N':'n');
        break;

    // White enpassant removes pawn south of destination
    case SPECIAL_WEN_PASSANT:
        squares[m.src] = ' ';
        squares[m.dst] = 'P';
        squares[ SOUTH(m.dst) ] = ' ';
        break;

    // Black enpassant removes pawn north of destination
    case SPECIAL_BEN_PASSANT:
        squares[m.src] = ' ';
        squares[m.dst] = 'p';
        squares[ NORTH(m.dst) ] = ' ';
        break;

    // White pawn advances 2 squares sets an enpassant target
    case SPECIAL_WPAWN_2SQUARES:
        squares[m.src] = ' ';
        squares[m.dst] = 'P';
        d.enpassant_target = SOUTH(m.dst);
        break;

    // Black pawn advances 2 squares sets an enpassant target
    case SPECIAL_BPAWN_2SQUARES:
        squares[m.src] = ' ';
        squares[m.dst] = 'p';
        d.enpassant_target = NORTH(m.dst);
        break;

    // Castling moves update 4 squares each
    case SPECIAL_WK_CASTLING:
        squares[e1] = ' ';
        squares[f1] = 'R';
        squares[g1] = 'K';
        squares[h1] = ' ';
        d.wking_square = g1;
        break;
    case SPECIAL_WQ_CASTLING:
        squares[e1] = ' ';
        squares[d1] = 'R';
        squares[c1] = 'K';
        squares[a1] = ' ';
        d.wking_square = c1;
        break;
    case SPECIAL_BK_CASTLING:
        squares[e8] = ' ';
        squares[f8] = 'r';
        squares[g8] = 'k';
        squares[h8] = ' ';
        d.bking_square = g8;
        break;
    case SPECIAL_BQ_CASTLING:
        squares[e8] = ' ';
        squares[d8] = 'r';
        squares[c8] = 'k';
        squares[a8] = ' ';
        d.bking_square = c8;
        break;
    }

    // Toggle who-to-move
    Toggle();
}

// Undo a move
void ChessRules::PopMove(Move m) {
    // Previous detail field
    d = detail_stack.back();
    detail_stack.pop_back();

    // Toggle who-to-move
    Toggle();

    // Special handling might be required
    switch (m.special) {
    default:
        squares[m.src] = squares[m.dst];
        squares[m.dst] = m.capture;
        break;

    // For promotion, src piece was a pawn
    case SPECIAL_PROMOTION_QUEEN:
    case SPECIAL_PROMOTION_ROOK:
    case SPECIAL_PROMOTION_BISHOP:
    case SPECIAL_PROMOTION_KNIGHT:
        if (white) {
            squares[m.src] = 'P';
        }
        else {
            squares[m.src] = 'p';
        }
        squares[m.dst] = m.capture;
        break;

    // White enpassant re-insert black pawn south of destination
    case SPECIAL_WEN_PASSANT:
        squares[m.src] = 'P';
        squares[m.dst] = ' ';
        squares[SOUTH(m.dst)] = 'p';
        break;

    // Black enpassant re-insert white pawn north of destination
    case SPECIAL_BEN_PASSANT:
        squares[m.src] = 'p';
        squares[m.dst] = ' ';
        squares[NORTH(m.dst)] = 'P';
        break;

    // Castling moves update 4 squares each
    case SPECIAL_WK_CASTLING:
        squares[e1] = 'K';
        squares[f1] = ' ';
        squares[g1] = ' ';
        squares[h1] = 'R';
        break;
    case SPECIAL_WQ_CASTLING:
        squares[e1] = 'K';
        squares[d1] = ' ';
        squares[c1] = ' ';
        squares[a1] = 'R';
        break;
    case SPECIAL_BK_CASTLING:
        squares[e8] = 'k';
        squares[f8] = ' ';
        squares[g8] = ' ';
        squares[h8] = 'r';
        break;
    case SPECIAL_BQ_CASTLING:
        squares[e8] = 'k';
        squares[d8] = ' ';
        squares[c8] = ' ';
        squares[a8] = 'r';
        break;
    }
}

// Determine if an occupied square is attacked
bool ChessRules::AttackedPiece(Square square) {
    const bool enemy_is_white = IsBlack(squares[square]);
    return AttackedSquare(square, enemy_is_white);
}

// Is a square is attacked by enemy ?
bool ChessRules::AttackedSquare(Square square, bool enemy_is_white) {
    Square dst;
    const lte *ptr = (enemy_is_white ? attacks_black_lookup[square] : attacks_white_lookup[square] );
    lte nbr_rays = *ptr++;
    while( nbr_rays-- ) {
        lte ray_len = *ptr++;
        while( ray_len-- ) {
            dst = (Square)*ptr++;
            char piece=squares[dst];

            // If square not occupied (empty), continue
            if( IsEmptySquare(piece) )
                ptr++;  // skip mask

            // Else if occupied
            else {
                lte mask = *ptr++;

                // White attacker ?
                if( IsWhite(piece) && enemy_is_white ) {
                    if( to_mask[piece] & mask )
                        return true;
                }

                // Black attacker ?
                else if( IsBlack(piece) && !enemy_is_white ) {
                    if( to_mask[piece] & mask )
                        return true;
                }

                // Goto end of ray
                ptr += (2*ray_len);
                ray_len = 0;
            }
        }
    }

    ptr = knight_lookup[square];
    lte nbr_squares = *ptr++;
    while( nbr_squares-- ) {
        dst = (Square)*ptr++;
        char piece=squares[dst];

        // If occupied by an enemy knight, we have found an attacker
        if( (enemy_is_white&&piece=='N') || (!enemy_is_white&&piece=='n') )
            return true;
    }
    return false;
}

// Evaluate a position, returns bool okay (not okay means illegal position)
bool ChessRules::Evaluate() {
    Square enemy_king = (Square)(white ? d.bking_square : d.wking_square);
    // Enemy king is attacked and our move, position is illegal
    return !AttackedPiece(enemy_king);
}

bool ChessRules::Evaluate(TERMINAL &score_terminal) {
    return Evaluate(nullptr, score_terminal);
}

bool ChessRules::Evaluate(vector<Move> *p, TERMINAL& score_terminal) {
    vector<Move> local_list;
    vector<Move>& list = p ? *p : local_list;
    int i, any;
    Square my_king, enemy_king;
    bool okay;
    score_terminal=NOT_TERMINAL;

    // Enemy king is attacked and our move, position is illegal
    enemy_king = (Square)(white ? d.bking_square : d.wking_square);
    if( AttackedPiece(enemy_king) )
        okay = false;

    // Else legal position
    else {
        okay = true;

        // Work out if the game is over by checking for any legal moves
        GenMoveList(list);
        any = 0;
        for (auto move: list) {
            PushMove(move);
            my_king = (Square)(white ? d.bking_square : d.wking_square);
            if( !AttackedPiece(my_king) )
                any++;
            PopMove(move);
        }

        // If no legal moves, position is either checkmate or stalemate
        if( any == 0 ) {
            my_king = (Square)(white ? d.wking_square : d.bking_square);
            if( AttackedPiece(my_king) )
                score_terminal = (white ? TERMINAL_WCHECKMATE
                                        : TERMINAL_BCHECKMATE);
            else
                score_terminal = (white ? TERMINAL_WSTALEMATE
                                        : TERMINAL_BSTALEMATE);
        }
    }
    return okay;
}

// Test for legal position, sets reason to a mask of possibly multiple reasons
bool ChessRules::IsLegal(ILLEGAL_REASON& reason) {
    int  ireason = 0;
    int  wkings=0, bkings=0, wpawns=0, bpawns=0, wpieces=0, bpieces=0;
    bool legal = true;
    int  file, rank;
    char p;
    Square opposition_king_location = SQUARE_INVALID;

    // Loop through the board
    file=0;     // go from a8,b8..h8,a7,b7..h1
    rank=7;
    for (;;) {
        Square sq = SQ('a'+file, '1'+rank);
        p = squares[sq];
        if ((p == 'P' || p == 'p') && (rank == 0 || rank == 7)) {
            legal = false;
            ireason |= IR_PAWN_POSITION;
        }
        if (IsWhite(p)) {
            if (p == 'P') {
                wpawns++;
            }
            else {
                wpieces++;
                if (p == 'K') {
                    wkings++;
                    if (!white) {
                        opposition_king_location = sq;
                    }
                }
            }
        }
        else if (IsBlack(p)) {
            if (p == 'p') {
                bpawns++;
            }
            else {
                bpieces++;
                if (p == 'k') {
                    bkings++;
                    if (white) {
                        opposition_king_location = sq;
                    }
                }
            }
        }
        if( sq == h1 ) {
            break;
        }
        else {
            file++;
            if (file == 8) {
                file = 0;
                rank--;
            }
        }
    }
    if (wkings!=1 || bkings!=1) {
        legal = false;
        ireason |= IR_NOT_ONE_KING_EACH;
    }
    if (opposition_king_location!=SQUARE_INVALID && AttackedPiece(opposition_king_location)) {
        legal = false;
        ireason |= IR_CAN_TAKE_KING;
    }
    if (wpieces>8 && (wpieces+wpawns)>16) {
        legal = false;
        ireason |= IR_WHITE_TOO_MANY_PIECES;
    }
    if (bpieces>8 && (bpieces+bpawns)>16) {
        legal = false;
        ireason |= IR_BLACK_TOO_MANY_PIECES;
    }
    if (wpawns > 8) {
        legal = false;
        ireason |= IR_WHITE_TOO_MANY_PAWNS;
    }
    if (bpawns > 8) {
        legal = false;
        ireason |= IR_BLACK_TOO_MANY_PAWNS;
    }
    reason = static_cast<ILLEGAL_REASON>(ireason);
    return legal;
}

Move ChessRules::uci_move(string_view uci_move) {
    vector<Move> legal_moves;
    GenLegalMoveList(legal_moves);

    const auto expected = Move(uci_move);
    for (const auto move : legal_moves) {
        if (move.src != expected.src || move.dst != expected.dst) {
            continue;
        }
        if (move.is_promotion() || expected.is_promotion()) {
            if (move.special != expected.special) {
                continue;
            }
        }
        return move;
    }
    throw domain_error("Invalid UCI move: " + string(uci_move));
}

string ChessRules::move_uci(Move move) {
    return move.uci();
}

// Read natural string move eg "Nf3"
//  return bool okay
Move ChessRules::san_move(string_view natural_in) {
    vector<Move> list;
    int  i, len=0;
    char src_file='\0', src_rank='\0', dst_file='\0', dst_rank='\0';
    char promotion='\0';
    bool enpassant=false;
    bool kcastling=false;
    bool qcastling=false;
    Square dst_=a8;
    Move *m, *found=NULL;
    char *s;
    char  move[10];
    bool  white=this->white;
    char  piece=(white?'P':'p');
    bool  default_piece=true;

    // Indicate no move found (yet)
    bool okay=true;

    // Copy to read-write variable
    okay = false;
    for( i=0; i<sizeof(move); i++ )
    {
        move[i] = natural_in[i];
        if( move[i]=='\0' || move[i]==' ' || move[i]=='\t' ||
            move[i]=='\r' || move[i]=='\n' )
        {
            move[i] = '\0';
            okay = true;
            break;
        }
    }
    if( okay )
    {

        // Trim string from end
        s = strchr(move,'\0') - 1;
        while( s>=move && !(isascii(*s) && isalnum(*s)) )
            *s-- = '\0';

        // Trim string from start
        s = move;
        while( *s==' ' || *s=='\t' )
            s++;
        len = (int)strlen(s);
        for( i=0; i<len+1; i++ )  // +1 gets '\0' at end
            move[i] = *s++;  // if no leading space this does
                            //  nothing, but no harm either

        // Trim enpassant
        if( len>=2 && move[len-1]=='p' )
        {
            if( 0 == strcmp(&move[len-2],"ep") )
            {
                move[len-2] = '\0';
                enpassant = true;
            }
            else if( len>=3 && 0==strcmp(&move[len-3],"e.p") )
            {
                move[len-3] = '\0';
                enpassant = true;
            }

            // Trim string from end, again
            s = strchr(move,'\0') - 1;
            while( s>=move && !(isascii(*s) && isalnum(*s)) )
                *s-- = '\0';
            len = (int)strlen(move);
        }

        // Promotion
        if( len>2 )  // We are supporting "ab" to mean Pawn a5xb6 (say), and this test makes sure we don't
        {            // mix that up with a lower case bishop promotion, and that we don't reject "ef" say
                     // on the basis that 'F' is not a promotion indication. We've never supported "abQ" say
                     // as a7xb8=Q, and we still don't so "abb" as a bishop promotion doesn't work, but we
                     // continue to support "ab=Q", and even "ab=b".
                     // The test also ensures we can access move[len-2] below
                     // These comments added when we changed the logic to support "b8Q" and "a7xb8Q", the
                     // '=' can optionally be omitted in such cases, the first change in this code for many,
                     // many years.
            char last = move[len-1];
            bool is_file = ('1'<=last && last<='8');
            if( !is_file )
            {
                switch( last )
                {
                    case 'O':
                    case 'o':   break;  // Allow castling!
                    case 'q':
                    case 'Q':   promotion='Q';  break;
                    case 'r':
                    case 'R':   promotion='R';  break;
                    case 'b':   if( len==3 && '2'<=move[1] && move[1]<='7' )
                                    break;  // else fall through to promotion - allows say "a5b" as disambiguating
                                            //  version of "ab" if there's more than one "ab" available! Something
                                            //  of an ultra refinement
                    case 'B':   promotion='B';  break;
                    case 'n':
                    case 'N':   promotion='N';  break;
                    default:    okay = false;   break;   // Castling and promotions are the only cases longer than 2
                                                         //  chars where a non-file ends a move. (Note we still accept
                                                         //  2 character pawn captures like "ef").
                }
                if( promotion )
                {
                    switch( move[len-2] )
                    {
                        case '=':
                        case '1':   // we now allow '=' to be omitted, as e.g. ChessBase mobile seems to (sometimes?)
                        case '8':   break;
                        default:    okay = false;   break;
                    }
                    if( okay )
                    {

                        // Trim string from end, again
                        move[len-1] = '\0';     // Get rid of 'Q', 'N' etc
                        s = move + len-2;
                        while( s>=move && !(isascii(*s) && isalnum(*s)) )
                            *s-- = '\0';    // get rid of '=' but not '1','8'
                        len = (int)strlen(move);
                    }
                }
            }
        }
    }

    // Castling
    if( okay )
    {
        if( 0==strcmp_ignore(move,"oo") || 0==strcmp_ignore(move,"o-o") )
        {
            strcpy( move, (white?"e1g1":"e8g8") );
            len       = 4;
            piece     = (white?'K':'k');
            default_piece = false;
            kcastling = true;
        }
        else if( 0==strcmp_ignore(move,"ooo") || 0==strcmp_ignore(move,"o-o-o") )
        {
            strcpy( move, (white?"e1c1":"e8c8") );
            len       = 4;
            piece     = (white?'K':'k');
            default_piece = false;
            qcastling = true;
        }
    }

    // Destination square for all except pawn takes pawn (eg "ef")
    if( okay )
    {
        if( len==2 && 'a'<=move[0] && move[0]<='h'
                   && 'a'<=move[1] && move[1]<='h' )
        {
            src_file = move[0]; // eg "ab" pawn takes pawn
            dst_file = move[1];
        }
        else if( len==3 && 'a'<=move[0] && move[0]<='h'
                        && '2'<=move[1] && move[1]<='7'
                        && 'a'<=move[2] && move[2]<='h' )
        {
            src_file = move[0]; // eg "a3b"  pawn takes pawn
            dst_file = move[2];
        }
        else if( len>=2 && 'a'<=move[len-2] && move[len-2]<='h'
                        && '1'<=move[len-1] && move[len-1]<='8' )
        {
            dst_file = move[len-2];
            dst_rank = move[len-1];
            dst_ = SQ(dst_file,dst_rank);
        }
        else
            okay = false;
    }

    // Source square and or piece
    if( okay )
    {
        if( len > 2 )
        {
            if( 'a'<=move[0] && move[0]<='h' &&
                '1'<=move[1] && move[1]<='8' )
            {
                src_file = move[0];
                src_rank = move[1];
            }
            else
            {
                switch( move[0] )
                {
                    case 'K':   piece = (white?'K':'k');    default_piece=false; break;
                    case 'Q':   piece = (white?'Q':'q');    default_piece=false; break;
                    case 'R':   piece = (white?'R':'r');    default_piece=false; break;
                    case 'N':   piece = (white?'N':'n');    default_piece=false; break;
                    case 'P':   piece = (white?'P':'p');    default_piece=false; break;
                    case 'B':   piece = (white?'B':'b');    default_piece=false; break;
                    default:
                    {
                        if( 'a'<=move[0] && move[0]<='h' )
                            src_file = move[0]; // eg "ef4"
                        else
                            okay = false;
                        break;
                    }
                }
                if( len>3  && src_file=='\0' )  // not eg "ef4" above
                {
                    if( '1'<=move[1] && move[1]<='8' )
                        src_rank = move[1];
                    else if( 'a'<=move[1] && move[1]<='h' )
                    {
                        src_file = move[1];
                        if( len>4 && '1'<=move[2] && move[2]<='8' )
                            src_rank = move[2];
                    }
                }
            }
        }
    }

    // Check against all possible moves
    if( okay )
    {
        GenLegalMoveList(list);

        // Have source and destination, eg "d2d3"
        if( enpassant )
            src_rank = dst_rank = '\0';
        if( src_file && src_rank && dst_file && dst_rank )
        {
            for (auto& m : list) {
                if( (default_piece || piece==squares[m.src])  &&
                    src_file  ==   FILE(m.src)       &&
                    src_rank  ==   RANK(m.src)       &&
                    dst_       ==   m.dst
                )
                {
                    if( kcastling )
                    {
                        if( m.special ==
                             (white?SPECIAL_WK_CASTLING:SPECIAL_BK_CASTLING) )
                            found = &m;
                    }
                    else if( qcastling )
                    {
                        if( m.special ==
                             (white?SPECIAL_WQ_CASTLING:SPECIAL_BQ_CASTLING) )
                            found = &m;
                    }
                    else
                        found = &m;
                    break;
                }
            }
        }

        // Have source file only, eg "Rae1"
        else if( src_file && dst_file && dst_rank )
        {
            for (auto& m : list) {
                if( piece     ==   squares[m.src]  &&
                    src_file  ==   FILE(m.src)         &&
                 /* src_rank  ==   RANK(m.src)  */
                    dst_       ==   m.dst
                )
                {
                    found = &m;
                    break;
                }
            }
        }

        // Have source rank only, eg "R2d2"
        else if( src_rank && dst_file && dst_rank )
        {
            for (auto& m : list) {
                if( piece     ==   squares[m.src]   &&
                 /* src_file  ==   FILE(m.src) */
                    src_rank  ==   RANK(m.src)          &&
                    dst_       ==   m.dst
                )
                {
                    found = &m;
                    break;
                }
            }
        }

        // Have destination file only eg e4f (because 2 ef moves are possible)
        else if( src_file && src_rank && dst_file )
        {
            for (auto& m : list) {
                if( piece     ==   squares[m.src]      &&
                    src_file  ==   FILE(m.src)             &&
                    src_rank  ==   RANK(m.src)             &&
                    dst_file  ==   FILE(m.dst)
                )
                {
                    found = &m;
                    break;
                }
            }
        }

        // Have files only, eg "ef"
        else if( src_file && dst_file )
        {
            for (auto& m : list) {
                if( piece     ==   squares[m.src]      &&
                    src_file  ==   FILE(m.src)             &&
                 /* src_rank  ==   RANK(m.src) */
                    dst_file  ==   FILE(m.dst)
                )
                {
                    if( enpassant )
                    {
                        if( m.special ==
                             (white?SPECIAL_WEN_PASSANT:SPECIAL_BEN_PASSANT) )
                            found = &m;
                    }
                    else
                        found = &m;
                    break;
                }
            }
        }

        // Have destination square only eg "a4"
        else if( dst_rank && dst_file )
        {
            for (auto& m : list) {
                if( piece     ==   squares[m.src]          &&
                    dst_       ==   m.dst
                )
                {
                    found = &m;
                    break;
                }
            }
        }
    }

    // Copy found move
    if( okay && found )
    {
        bool found_promotion =
            ( found->special == SPECIAL_PROMOTION_QUEEN ||
              found->special == SPECIAL_PROMOTION_ROOK ||
              found->special == SPECIAL_PROMOTION_BISHOP ||
              found->special == SPECIAL_PROMOTION_KNIGHT
            );
        if( promotion && !found_promotion )
            okay = false;
        if( found_promotion )
        {
            switch( promotion )
            {
                default:
                case 'Q': found->special = SPECIAL_PROMOTION_QUEEN;   break;
                case 'R': found->special = SPECIAL_PROMOTION_ROOK;    break;
                case 'B': found->special = SPECIAL_PROMOTION_BISHOP;  break;
                case 'N': found->special = SPECIAL_PROMOTION_KNIGHT;  break;
            }
        }
    }

    if (!okay || !found) {
        throw std::domain_error("Invalid SAN move: " + string(natural_in));
    }
    return *found;
}

string ChessRules::move_san(Move move) {
// Improved algorithm

    /* basic procedure is run the following algorithms in turn:
        pawn move     ?
        castling      ?
        Nd2 or Nxd2   ? (loop through all legal moves check if unique)
        Nbd2 or Nbxd2 ? (loop through all legal moves check if unique)
        N1d2 or N1xd2 ? (loop through all legal moves check if unique)
        Nb1d2 or Nb1xd2 (fallback if nothing else works)
    */

    char nmove[10];
    nmove[0] = '-';
    nmove[1] = '-';
    nmove[2] = '\0';
    vector<Move> list;
    vector<bool> check;
    vector<bool> mate;
    vector<bool> stalemate;
    enum
    {
        ALG_PAWN_MOVE,
        ALG_CASTLING,
        ALG_ND2,
        ALG_NBD2,
        ALG_N1D2,
        ALG_NB1D2
    };
    bool done=false;
    bool found = false;
    char append='\0';
    GenLegalMoveList(list, check, mate, stalemate);
    for (int i = 0; i != list.size(); ++i) {
        Move mfound = list[i];
        if( mfound == move )
        {
            found = true;
            if( mate[i] )
                append = '#';
            else if( check[i] )
                append = '+';
        }
    }

    // Loop through algorithms
    for( int alg=ALG_PAWN_MOVE; found && !done && alg<=ALG_NB1D2; alg++ )
    {
        bool do_loop = (alg==ALG_ND2 || alg==ALG_NBD2 || alg==ALG_N1D2);
        int matches=0;

        // Run the algorithm on the input move (i=-1) AND on all legal moves
        //  in a loop if do_loop set for this algorithm (i=0 to i=count-1)
        for (auto i = -1; !done && i < (do_loop ? int(list.size()) : 0); i++) {
            char compare[10];
            char *str_dst = (i == -1) ? nmove : compare;
            Move m = (i == -1) ? move : list[i];
            Square src_ = m.src;
            Square dst_ = m.dst;
            char t, p = squares[src_];
            if( islower(p) )
                p = (char)toupper(p);
            if( !IsEmptySquare(m.capture) ) // until we did it this way, enpassant was '-' instead of 'x'
                t = 'x';
            else
                t = '-';
            switch( alg )
            {
                // pawn move ? "e4" or "exf6", plus "=Q" etc if promotion
                case ALG_PAWN_MOVE:
                {
                    if( p == 'P' )
                    {
                        done = true;
                        if( t == 'x' )
                            sprintf( nmove, "%cx%c%c", FILE(src_),FILE(dst_),RANK(dst_) );
                        else
                            sprintf( nmove, "%c%c",FILE(dst_),RANK(dst_) );
                        char *s = strchr(nmove,'\0');
                        switch( m.special )
                        {
                            case SPECIAL_PROMOTION_QUEEN:
                                strcpy( s, "=Q" );  break;
                            case SPECIAL_PROMOTION_ROOK:
                                strcpy( s, "=R" );  break;
                            case SPECIAL_PROMOTION_BISHOP:
                                strcpy( s, "=B" );  break;
                            case SPECIAL_PROMOTION_KNIGHT:
                                strcpy( s, "=N" );  break;
                            default:
                                break;
                        }
                    }
                    break;
                }

                // castling ?
                case ALG_CASTLING:
                {
                    if( m.special==SPECIAL_WK_CASTLING || m.special==SPECIAL_BK_CASTLING )
                    {
                        strcpy( nmove, "O-O" );
                        done = true;
                    }
                    else if( m.special==SPECIAL_WQ_CASTLING || m.special==SPECIAL_BQ_CASTLING )
                    {
                        strcpy( nmove, "O-O-O" );
                        done = true;
                    }
                    break;
                }

                // Nd2 or Nxd2
                case ALG_ND2:
                {
                    if( t == 'x' )
                        sprintf( str_dst, "%cx%c%c", p, FILE(dst_), RANK(dst_) );
                    else
                        sprintf( str_dst, "%c%c%c", p, FILE(dst_), RANK(dst_) );
                    if( i >= 0 )
                    {
                        if( 0 == strcmp(nmove,compare) )
                            matches++;
                    }
                    break;
                }

                // Nbd2 or Nbxd2
                case ALG_NBD2:
                {
                    if( t == 'x' )
                        sprintf( str_dst, "%c%cx%c%c", p, FILE(src_), FILE(dst_), RANK(dst_) );
                    else
                        sprintf( str_dst, "%c%c%c%c", p, FILE(src_), FILE(dst_), RANK(dst_) );
                    if( i >= 0 )
                    {
                        if( 0 == strcmp(nmove,compare) )
                            matches++;
                    }
                    break;
                }

                // N1d2 or N1xd2
                case ALG_N1D2:
                {
                    if( t == 'x' )
                        sprintf( str_dst, "%c%cx%c%c", p, RANK(src_), FILE(dst_), RANK(dst_) );
                    else
                        sprintf( str_dst, "%c%c%c%c", p, RANK(src_), FILE(dst_), RANK(dst_) );
                    if( i >= 0 )
                    {
                        if( 0 == strcmp(nmove,compare) )
                            matches++;
                    }
                    break;
                }

                //  Nb1d2 or Nb1xd2
                case ALG_NB1D2:
                {
                    done = true;
                    if( t == 'x' )
                        sprintf( nmove, "%c%c%cx%c%c", p, FILE(src_), RANK(src_), FILE(dst_), RANK(dst_) );
                    else
                        sprintf( nmove, "%c%c%c%c%c", p, FILE(src_), RANK(src_), FILE(dst_), RANK(dst_) );
                    break;
                }
            }
        }   // end loop for all legal moves with given algorithm

        // If it's a looping algorithm and only one move matches nmove, we're done
        if( do_loop && matches==1 )
            done = true;
    }   // end loop for all algorithms
    if( append )
    {
        char *s = strchr(nmove,'\0');
        *s++ = append;
        *s = '\0';
    }
    return nmove;
}
