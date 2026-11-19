/*

*/

#include <algorithm>

#include "types.h"
#include "position.h"
#include "pawns.h"

struct AttackInfo {
	Bitboard attack[COLOR_NB][PIECE_TYPE_NB];
	Bitboard pinned[COLOR_NB];
};

enum endgame_kind
{
	endgame_none,
	endgame_white_minor,
	endgame_black_minor,
	endgame_white_pawn,
	endgame_black_pawn,
	endgame_theoretical_draw,
	endgame_minor_minor,
	endgame_KBN_K,
	endgame_KP_K,
	endgame_KPs_KPs,
	endgame_KR_KP_white,
	endgame_KR_KP_black,
	endgame_KQ_KRP_white,
	endgame_KQ_KRP_black,
	endgame_KQ_KP,
	endgame_KB_KP,
	endgame_KBP_KB_identical,
	endgame_KBP_KB_opposite,
	endgame_KB_KPP,
	endgame_KBPP_KB_opposite,
	endgame_KRP_KB,
	ENDGAME_COUNT
};

typedef Value(*endgame_value_fx)(const Position& pos, AttackInfo& ai, const Pawns::Entry *pawnEntry, Value value);

endgame_value_fx endgame_value_functions[ENDGAME_COUNT];

#define SC(x) Value(x * 2)
inline Bitboard bb(Square s) { return (1ULL << s); }
inline Bitboard bb2(Square s1, Square s2) { return (1ULL << s1) | (1ULL << s2); }
inline Bitboard bb3(Square s1, Square s2, Square s3) { return (1ULL << s1) | (1ULL << s2) | (1ULL << s3); }
inline Bitboard bb4(Square s1, Square s2, Square s3, Square s4) { return (1ULL << s1) | (1ULL << s2) | (1ULL << s3) | (1ULL << s4); }

Bitboard attack_king_inc(Square s) { return StepAttacksBB[KING][s] | s; }



Value endgame_value_theoretical_draw(const Position& pos, AttackInfo& ai, const Pawns::Entry *pawnEntry, Value value)
{
	//VAR->exact_value = TRUE;
	return VALUE_DRAW;
}

Value endgame_value_minor_minor(const Position& pos, AttackInfo& ai, const Pawns::Entry *pawnEntry, Value value)
{
	// some piece vs piece positions can be mate with the king in the corner
	//VAR->exact_value = ((pos.pieces(WHITE, KING) | pos.pieces(BLACK, KING)) & bb4(SQ_A1, SQ_H1, SQ_A8, SQ_H8)) == 0;
	return VALUE_DRAW;
}


Value endgame_value_KPs_KPs(const Position& pos, AttackInfo& ai, const Pawns::Entry *pawnEntry, Value value)
{/*
	int v, ww, furthest_white_passed_pawn, furthest_black_passed_pawn;
	Square square, promotion_square, king_square;
	Rank rank;
	Bitboard passed_pawns;

	furthest_white_passed_pawn = 0;
	king_square = pos.square<KING>(BLACK);
	passed_pawns = pawnEntry->passedPawns[WHITE];
	while (passed_pawns)
	{
		square = pop_lsb(&passed_pawns);
		rank = rank_of(square);
		promotion_square = make_square(file_of(square), RANK_8);
		if ((GG->bb_king_supports_passed_pawn[WHITE][square] & pos.pieces(WHITE, KING)) != 0
			|| SquareDistance[promotion_square][king_square] > 8 - rank - (pos.side_to_move() == WHITE))
		{
			if (pos.pieces(WHITE) & in_front_bb[WHITE][square])
				rank--;
			if (attacks_bb<QUEEN>(promotion_square, pos.pieces() & ~GG->bb_square[square]) & pos.pieces(BLACK, KING))
				rank++;
			if (rank > furthest_white_passed_pawn)
				furthest_white_passed_pawn = rank;
		}
	}

	furthest_black_passed_pawn = 0;
	king_square = pos.square<KING>(WHITE);
	passed_pawns = pawnEntry->passed_pawns(BLACK);
	while (passed_pawns)
	{
		square = pop_lsb(&passed_pawns);
		rank = RANK_8 - rank_of(square);
		promotion_square = make_square(file_of(square), RANK_1);
		if ((GG->bb_king_supports_passed_pawn[BLACK][square] & pos.pieces(BLACK, KING)) != 0
			|| SquareDistance[promotion_square][king_square] > 7 - rank + (pos.side_to_move() == WHITE))
		{
			if (pos.pieces(BLACK) & in_front_bb[BLACK][square])
				rank--;
			if (attacks_bb<QUEEN>(promotion_square, pos.pieces() & ~GG->bb_square[square]) & pos.pieces(WHITE, KING))
				rank++;
			if (rank > furthest_black_passed_pawn)
				furthest_black_passed_pawn = rank;
		}
	}

	ww = (sint16)((pos.psq_score() + pawnEntry->score) & 0xffff);
	ww = ww / EVAL_MULTIPLY + Material_value;
	v = (ww * conversionFactor) / 128;

	if (furthest_white_passed_pawn > furthest_black_passed_pawn)
	{
		if (pos.side_to_move() == WHITE)
			furthest_white_passed_pawn++;
		if ((pos.pieces(BLACK, PAWN) & GG->bb_in_front_black[R8 - furthest_white_passed_pawn + 2]) == 0
				|| !more_than_one(pawnEntry->passed_pawns(BLACK)))
			v += SC(300) + SC(50) * furthest_white_passed_pawn;
		else
			v += SC(150) + SC(25) * furthest_white_passed_pawn;
	}

	if (furthest_black_passed_pawn > furthest_white_passed_pawn)
	{
		if (pos.side_to_move() == BLACK)
			furthest_black_passed_pawn++;
		if  ((pos.pieces(WHITE, PAWN) & GG->bb_in_front_white[furthest_black_passed_pawn - 2]) == 0
				|| !more_than_one(pawnEntry->passed_pawns[WHITE]))
			v -= SC(300) + SC(50) * furthest_black_passed_pawn;
		else
			v -= SC(150) + SC(25) * furthest_black_passed_pawn;
	}

	if (v > 0 && !pos.pieces(WHITE, PAWN))
		v = 0;
	if (v < 0 && !pos.pieces(BLACK, PAWN))
		v = 0;
	if (v > 0)
	{
		Bitboard bb_king_covers = attack_king_inc(pos.square<KING>(BLACK)];
		if (!(pos.pieces(WHITE, PAWN) & ~FileHBB) && bb_king_covers & bb(SQ_H8))
			v = 0;
		if (!(pos.pieces(WHITE, PAWN) & ~FileABB) && bb_king_covers & bb(SQ_A8))
			v = 0;
		if (!(pos.pieces(WHITE, PAWN) & ~FileBBB) && pos.pieces(WHITE, PAWN) & bb(SQ_B6) &&
			pos.pieces(BLACK, PAWN) & bb(SQ_B7) && bb_king_covers & bb2(SQ_B8, SQ_C8))
			v = 0;
		if (!(pos.pieces(WHITE, PAWN) & ~FileGBB) && pos.pieces(WHITE, PAWN) & bb(SQ_G6) &&
			pos.pieces(BLACK, PAWN) & bb(SQ_G7) && bb_king_covers & bb2(SQ_G8, SQ_F8))
			v = 0;
		// white pawns h6-g5-f4 blocked by a chain of h7-g6-f5
		if (pos.pieces(WHITE, PAWN) == bb2(SQ_H6,SQ_G5)
			&& (pos.pieces(BLACK, PAWN) & (bb2(SQ_H7,SQ_G6) | bb3(SQ_G7,SQ_F7,SQ_F6))) == bb2(SQ_H7,SQ_G6)
			&& bb_king_covers & bb(SQ_G8))
			v = 0;
		if (pos.pieces(WHITE, PAWN) == bb3(SQ_H6,SQ_G5,SQ_F4)
			&& (pos.pieces(BLACK, PAWN) & (bb3(SQ_H7,SQ_G6,SQ_F5) | bb3(SQ_G7,SQ_F7,SQ_F6) | bb3(SQ_E7,SQ_E6,SQ_E5))) == bb3(SQ_H7,SQ_G6,SQ_F5)
			&& bb_king_covers & bb(SQ_G8))
			v = 0;
		// white pawns a6-b5-c4 blocked by a chain of a7-b6-c5
		if (pos.pieces(WHITE, PAWN) == bb2(SQ_A6,SQ_B5)
			&& (pos.pieces(BLACK, PAWN) & (bb2(SQ_A7,SQ_B6) | bb3(SQ_B7,SQ_C7,SQ_C6))) == bb2(SQ_A7,SQ_B6)
			&& bb_king_covers & bb(SQ_B8))
			v = 0;
		if (pos.pieces(WHITE, PAWN) == bb3(SQ_A6,SQ_B5,SQ_C4)
			&& (pos.pieces(BLACK, PAWN) & (bb3(SQ_A7,SQ_B6,SQ_C5) | bb3(SQ_B7,SQ_C7,SQ_C6) | bb3(SQ_D7,SQ_D6,SQ_D5))) == bb3(SQ_A7,SQ_B6,SQ_C5)
			&& bb_king_covers & bb(SQ_B8))
			v = 0;
	}
	else if (v < 0)
	{
		Bitboard bb_king_covers = attack_king_inc(pos.square<KING>(WHITE)];
		if (!(pos.pieces(BLACK, PAWN) & ~FileHBB) && bb_king_covers & bb(SQ_H1))
			v = 0;
		if (!(pos.pieces(BLACK, PAWN) & ~FileABB) && bb_king_covers & bb(SQ_A1))
			v = 0;
		if (!(pos.pieces(BLACK, PAWN) & ~FileBBB) && pos.pieces(BLACK, PAWN) & bb(SQ_B3) &&
			pos.pieces(WHITE, PAWN) & bb(SQ_B2) && bb_king_covers & bb2(SQ_B1, SQ_C1))
			v = 0;
		if (!(pos.pieces(BLACK, PAWN) & ~FileGBB) && pos.pieces(BLACK, PAWN) & bb(SQ_G3) &&
			pos.pieces(WHITE, PAWN) & bb(SQ_G2) && bb_king_covers & bb2(SQ_G1, SQ_F1))
			v = 0;
		// black pawns h3-g4-f5 blocked by a chain of h2-g3-f4
		if (pos.pieces(BLACK, PAWN) == bb2(SQ_H3,SQ_G4)
			&& (pos.pieces(WHITE, PAWN) & (bb2(SQ_H2,SQ_G3) | bb3(SQ_G2,SQ_F2,SQ_F3))) == bb2(SQ_H2,SQ_G3)
			&& bb_king_covers & bb(SQ_G1))
			v = 0;
		if (pos.pieces(BLACK, PAWN) == bb3(SQ_H3,SQ_G4,SQ_F5)
			&& (pos.pieces(WHITE, PAWN) & (bb3(SQ_H2,SQ_G3,SQ_F4) | bb3(SQ_G2,SQ_F2,SQ_F3) | bb3(SQ_E2,SQ_E3,SQ_E4))) == bb3(SQ_H2,SQ_G3,SQ_F4)
			&& bb_king_covers & bb(SQ_G1))
			v = 0;
		// black pawns a3-b4-c5 blocked by a chain of a2-b3-c4
		if (pos.pieces(BLACK, PAWN) == bb2(SQ_A3,SQ_B4)
			&& (pos.pieces(WHITE, PAWN) & (bb2(SQ_A2,SQ_B3) | bb3(SQ_B2,SQ_C2,SQ_C3))) == bb2(SQ_A2,SQ_B3)
			&& bb_king_covers & bb(SQ_B1))
			v = 0;
		if (pos.pieces(BLACK, PAWN) == bb3(SQ_A3,SQ_B4,SQ_C5)
			&& (pos.pieces(WHITE, PAWN) & (bb3(SQ_A2,SQ_B3,SQ_C4) | bb3(SQ_B2,SQ_C2,SQ_C3) | bb3(SQ_D2,SQ_D3,SQ_D4))) == bb3(SQ_A2,SQ_B3,SQ_C4)
			&& bb_king_covers & bb(SQ_B1))
			v = 0;
	}
	return v;*/
return value;
}


Value endgame_value_KB_KPP(const Position& pos, AttackInfo& ai, const Pawns::Entry *pawnEntry, Value value)
{
	Square pawn1, pawn2, bishop, king;
	Bitboard bishop_coverage;

	if (pos.pieces(WHITE, BISHOP))
	{
		pawn1 = lsb(pos.pieces(BLACK, PAWN));
		pawn2 = msb(pos.pieces(BLACK, PAWN));
		bishop = lsb(pos.pieces(WHITE, BISHOP));
		king = pos.square<KING>(WHITE);
		bishop_coverage = attacks_bb<BISHOP>(bishop, pos.pieces()) | pos.pieces(WHITE, BISHOP);
		if ((file_of(king) == file_of(pawn1) && king < pawn1 || in_front_bb(BLACK, pawn1) & bishop_coverage)
			&& (file_of(king) == file_of(pawn2) && king < pawn2 || in_front_bb(BLACK, pawn2) & bishop_coverage))
			value = VALUE_DRAW;
	}
	else
	{
		pawn1 = lsb(pos.pieces(WHITE, PAWN));
		pawn2 = msb(pos.pieces(WHITE, PAWN));
		bishop = lsb(pos.pieces(BLACK, BISHOP));
		king = pos.square<KING>(BLACK);
		bishop_coverage = attacks_bb<BISHOP>(bishop, pos.pieces()) | pos.pieces(BLACK, BISHOP);

		if ((file_of(king) == file_of(pawn1) && king > pawn1 || in_front_bb(WHITE, pawn1) & bishop_coverage)
			&& (file_of(king) == file_of(pawn2) && king > pawn2 || in_front_bb(WHITE, pawn2) & bishop_coverage))
			value = VALUE_DRAW;
	}
	
	return value;
}


/*
test positions for KBPP_KB_opposite
===================================
draw
8/2k5/2P5/3P4/3K4/6b1/4B3/8 w - - 0 1
6k1/8/6PP/7K/8/2b5/8/5B2 w - - 0 1
5bk1/8/6PP/7K/8/8/8/5B2 w - - 0 1
3k4/3P4/2K1B3/5P2/5b2/8/8/8 b - - 0 1
2k5/8/PP6/1K6/3B4/5b2/8/8 w - - 0 1
win
8/3k4/8/2PP4/3K4/6b1/4B3/8 w - - 0 1
3k4/3P4/2K1B3/5P2/5b2/8/8/8 w - - 0 1
*/

Value endgame_value_KBPP_KB_opposite(const Position& pos, AttackInfo& ai, const Pawns::Entry *pawnEntry, Value value)
{
	Square pawn1, pawn2, bishop, king;
	int file_distance;
	Bitboard bishop_coverage;

	if (more_than_one(pos.pieces(WHITE, PAWN)) && value > 0)
	{
		pawn1 = lsb(pos.pieces(WHITE, PAWN));
		pawn2 = msb(pos.pieces(WHITE, PAWN));
		file_distance = std::abs(file_of(pawn1) - file_of(pawn2));
		if (file_distance == 2 && rank_of(pawn1) == 6 && rank_of(pawn2) == 6)
			return value;
		if (file_distance <= 2)
		{
			bishop = lsb(pos.pieces(BLACK, BISHOP));
			king = pos.square<KING>(BLACK);
			bishop_coverage = attacks_bb<BISHOP>(bishop, pos.pieces()) | pos.pieces(BLACK, BISHOP);

			if (king == pawn1 + 8 && light_square(king) ^ !light_square(bishop) && bishop_coverage & (pawn2 + 8))
				value = SC(4) * int(rank_of(pawn1) + rank_of(pawn2));
			if (king == pawn2 + 8 && light_square(king) ^ !light_square(bishop) && bishop_coverage & (pawn1 + 8))
				value = SC(4) * int(rank_of(pawn1) + rank_of(pawn2));
			if (file_distance <= 1 && pos.side_to_move() == BLACK && pos.attacks_from<KING>(king) & bishop_coverage & pawn2
				&& !(pos.checkers() & !SquareBB[pawn2]))
				value = SC(10) * (2 - popcount(pos.pieces(BLACK, PAWN)));
			if (pos.pieces(WHITE, PAWN) == bb2(SQ_A6, SQ_B6) && attack_king_inc(king) & bb(SQ_B8) && bishop_coverage & bb(SQ_A8))
				value = SC(10) * (2 - popcount(pos.pieces(BLACK, PAWN)));
			if (pos.pieces(WHITE, PAWN) == bb2(SQ_G6, SQ_H6) && attack_king_inc(king) & bb(SQ_G8) && bishop_coverage & bb(SQ_H8))
				value = SC(10) * (2 - popcount(pos.pieces(BLACK, PAWN)));
			if (rank_of(bishop) == file_of(bishop) && pawn1 == SQ_G6 && (pawn2 == SQ_H6 || pawn2 == SQ_H7) && (king == SQ_F8 || king == SQ_G8))
				value = SC(10) * (2 - popcount(pos.pieces(BLACK, PAWN)));
			if (bishop == SQ_F8 && pawn1 == SQ_G6 && pawn2 == SQ_H6 && pos.pieces(BLACK, KING) & bb2(SQ_G8, SQ_H8))
				value = SC(10) * (2 - popcount(pos.pieces(BLACK, PAWN)));
			if (rank_of(bishop) + file_of(bishop) == 7 && pawn1 == SQ_B6 && (pawn2 == SQ_A6 || pawn2 == SQ_A7) && (king == SQ_C8 || king == SQ_B8))
				value = SC(10) * (2 - popcount(pos.pieces(BLACK, PAWN)));
			if (bishop == SQ_C8 && pawn1 == SQ_B6 && pawn2 == SQ_A6 && pos.pieces(BLACK, KING) & bb2(SQ_A8, SQ_B8))
				value = SC(10) * (2 - popcount(pos.pieces(BLACK, PAWN)));
		}
	}
	else if (more_than_one(pos.pieces(BLACK, PAWN)) && value < 0)
	{
		pawn1 = msb(pos.pieces(BLACK, PAWN));
		pawn2 = lsb(pos.pieces(BLACK, PAWN));
		file_distance = std::abs(file_of(pawn1) - file_of(pawn2));
		if (file_distance == 2 && rank_of(pawn1) == 1 && rank_of(pawn2) == 1)
			return value;
		if (file_distance <= 2)
		{
			bishop = lsb(pos.pieces(WHITE, BISHOP));
			king = pos.square<KING>(WHITE);
			bishop_coverage = attacks_bb<BISHOP>(bishop, pos.pieces()) | pos.pieces(WHITE, BISHOP);

			if (king == pawn1 - 8 && light_square(king) ^ !light_square(bishop) && bishop_coverage & (pawn2 - 8))
				value = -SC(4) * (7 - rank_of(pawn1) + 7 - rank_of(pawn2));
			if (king == pawn2 - 8 && light_square(king) ^ !light_square(bishop) && bishop_coverage & (pawn1 - 8))
				value = -SC(4) * (7 - rank_of(pawn1) + 7 - rank_of(pawn2));
			if (file_distance <= 1 && pos.side_to_move() == WHITE && pos.attacks_from<KING>(king) & bishop_coverage & pawn2
				&& !(pos.checkers() & !SquareBB[pawn2]))
				value = -SC(10) * (2 - popcount(pos.pieces(WHITE, PAWN)));
			if (pos.pieces(BLACK, PAWN) == bb2(SQ_A3, SQ_B3) && attack_king_inc(king) & bb(SQ_B1) && bishop_coverage & bb(SQ_A1))
				value = -SC(10) * (2 - popcount(pos.pieces(WHITE, PAWN)));
			if (pos.pieces(BLACK, PAWN) == bb2(SQ_G3, SQ_H3) && attack_king_inc(king) & bb(SQ_G1) && bishop_coverage & bb(SQ_H1))
				value = -SC(10) * (2 - popcount(pos.pieces(WHITE, PAWN)));
			if (rank_of(bishop) == file_of(bishop) && pawn1 == SQ_B3 && (pawn2 == SQ_A3 || pawn2 == SQ_A2) && (king == SQ_C1 || king == SQ_B1))
				value = -SC(10) * (2 - popcount(pos.pieces(WHITE, PAWN)));
			if (bishop == SQ_C1 && pawn1 == SQ_B3 && pawn2 == SQ_A3 && pos.pieces(WHITE, KING) & bb2(SQ_A1, SQ_B1))
				value = -SC(10) * (2 - popcount(pos.pieces(WHITE, PAWN)));
			if (rank_of(bishop) + file_of(bishop) == 7 && pawn1 == SQ_G3 && (pawn2 == SQ_H3 || pawn2 == SQ_H2) && (king == SQ_F1 || king == SQ_G1))
				value = -SC(10) * (2 - popcount(pos.pieces(WHITE, PAWN)));
			if (bishop == SQ_F1 && pawn1 == SQ_G3 && pawn2 == SQ_H3 && pos.pieces(WHITE, KING) & bb2(SQ_G1, SQ_H1))
				value = -SC(10) * (2 - popcount(pos.pieces(WHITE, PAWN)));
		}
	}

	return value;
}


/*
test positions for KRP_KB
=========================
draw
5k2/R7/5P2/6K1/2b5/8/8/8 w - - 0 1
8/8/8/2B5/6k1/5p2/r7/5K2 b - - 0 1
8/k7/Pb6/1K6/8/8/R7/8 w - - 0 1
2k5/6R1/K7/P7/8/8/5b2/8 w - - 0 1
*/
Value endgame_value_KRP_KB(const Position& pos, AttackInfo& ai, const Pawns::Entry *pawnEntry, Value value)
{
	Square pawn, bishop;

	if (pos.pieces(WHITE, PAWN))
	{
		pawn = lsb(pos.pieces(WHITE, PAWN));
		bishop = lsb(pos.pieces(BLACK, BISHOP));

		if ((pawn == SQ_C6 || pawn == SQ_F6) 
			&& (attacks_bb<BISHOP>(bishop, pos.pieces()) & (pawn + 8) || pos.checkers() || pos.side_to_move() == BLACK)
			&& attack_king_inc(pos.square<KING>(BLACK)) & (pawn + 8))
			value = SC(50);

		if (file_of(pawn) == FILE_A && pawn >= SQ_A5 && (pos.pieces(BLACK, BISHOP) & DarkSquares))
		{
			if (attack_king_inc(pos.square<KING>(BLACK)) & in_front_bb(WHITE, pawn))
				value = SC(50);
			if (attack_king_inc(pos.square<KING>(BLACK)) & bb(SQ_B7) && attacks_bb<BISHOP>(bishop, pos.pieces()) & bb(SQ_A7))
				value = SC(50);
		}
		if (file_of(pawn) == FILE_H && pawn >= SQ_H5 && (pos.pieces(BLACK, BISHOP) & ~DarkSquares))
		{
			if (attack_king_inc(pos.square<KING>(BLACK)) & in_front_bb(WHITE, pawn))
				value = SC(50);
			if (attack_king_inc(pos.square<KING>(BLACK)) & bb(SQ_G7) && attacks_bb<BISHOP>(bishop, pos.pieces()) & bb(SQ_H7))
				value = SC(50);
		}
	}
	else
	{
		pawn = lsb(pos.pieces(BLACK, PAWN));
		bishop = lsb(pos.pieces(WHITE, BISHOP));

		if ((pawn == SQ_C3 || pawn == SQ_F3) 
			&& (attacks_bb<BISHOP>(bishop, pos.pieces()) & (pawn - 8) || pos.checkers() || pos.side_to_move() == WHITE)
			&& attack_king_inc(pos.square<KING>(WHITE)) & (pawn - 8))
			value = SC(-50);
		if (file_of(pawn) == FILE_A && pawn <= SQ_A4 && (pos.pieces(WHITE, BISHOP) & ~DarkSquares))
		{
			if (attack_king_inc(pos.square<KING>(WHITE)) & in_front_bb(BLACK, pawn))
				value = SC(-50);
			if (attack_king_inc(pos.square<KING>(WHITE)) & bb(SQ_B2) && attacks_bb<BISHOP>(bishop, pos.pieces()) & bb(SQ_A2))
				value = SC(-50);
		}
		if (file_of(pawn) == FILE_H && pawn <= SQ_H4 && (pos.pieces(WHITE, BISHOP) & DarkSquares))
		{
			if (attack_king_inc(pos.square<KING>(WHITE)) & in_front_bb(BLACK, pawn))
				value = SC(-50);
			if (attack_king_inc(pos.square<KING>(WHITE)) & bb(SQ_G2) && attacks_bb<BISHOP>(bishop, pos.pieces()) & bb(SQ_H2))
				value = SC(-50);
		}
	}

	return value;
}

// TODO: KRP_KBP like KRP_KB with a single unimportant extra pawn for the bishop side

void ext_init_endgame_functions()
{
	endgame_value_functions[endgame_none] = NULL;
	endgame_value_functions[endgame_white_minor] = &endgame_value_white_minor;
	endgame_value_functions[endgame_black_minor] = &endgame_value_black_minor;
	endgame_value_functions[endgame_white_pawn] = &endgame_value_white_pawn;
	endgame_value_functions[endgame_black_pawn] = &endgame_value_black_pawn;
	endgame_value_functions[endgame_theoretical_draw] = &endgame_value_theoretical_draw;
	endgame_value_functions[endgame_minor_minor] = &endgame_value_minor_minor;
	endgame_value_functions[endgame_KBN_K] = &endgame_value_KBN_K;
	endgame_value_functions[endgame_KP_K] = &endgame_value_KP_K;
	endgame_value_functions[endgame_KPs_KPs] = &endgame_value_KPs_KPs;
	endgame_value_functions[endgame_KR_KP_white] = &endgame_value_KR_KP_white;
	endgame_value_functions[endgame_KR_KP_black] = &endgame_value_KR_KP_black;
	endgame_value_functions[endgame_KQ_KRP_white] = &endgame_value_KQ_KRP_white;
	endgame_value_functions[endgame_KQ_KRP_black] = &endgame_value_KQ_KRP_black;
	endgame_value_functions[endgame_KQ_KP] = &endgame_value_KQ_KP;
	endgame_value_functions[endgame_KBP_KB_identical] = &endgame_value_KBP_KB_identical;
	endgame_value_functions[endgame_KB_KP] = &endgame_value_KB_KP;
	endgame_value_functions[endgame_KBP_KB_opposite] = &endgame_value_KBP_KB_opposite;
	endgame_value_functions[endgame_KB_KPP] = &endgame_value_KB_KPP;
	endgame_value_functions[endgame_KBPP_KB_opposite] = &endgame_value_KBPP_KB_opposite;
	endgame_value_functions[endgame_KRP_KB] = &endgame_value_KRP_KB;
}

endgame_kind compute_endgame_index(int WPawn, int WN, int WB, int WBL, int WBD, int WR, int WQ,
	int BPawn, int BN, int BB, int BBL, int BBD, int BR, int BQ)
{
	int WCount = WQ + WR + WB + WN;
	int BCount = BQ + BR + BB + BN;
	bool one_versus_one = (WCount == 1 && BCount == 1);

	//if (one_versus_one && WQ == 1 && BQ == 1)
	//	material->material_flags |= flag_queen_endgame;

	//if (one_versus_one && WR == 1 && BR == 1)
	//	material->material_flags |= flag_rook_endgame;

	//if (WCount == 0)
	//	material->material_flags |= flag_white_pawn;

	//if (BCount == 0)
	//	material->material_flags |= flag_black_pawn;

	//if (WB + WN == 1 && WCount == 1)
	//	material->material_flags |= flag_white_piece;

	//if (BB + BN == 1 && BCount == 1)
	//	material->material_flags |= flag_black_piece;

	// endgame index
	// ==============
	if (WPawn + WR + WQ + BPawn + BR + BQ == 0 && WCount <= 1 && BCount <= 1)
	{
		if (WCount == 1 && BCount == 1)
			return endgame_minor_minor;
		else
			return endgame_theoretical_draw;
	}

	if (WCount == 0 && WPawn == 1 && BCount + BPawn == 1 && BB == 1
		|| BCount == 0 && BPawn == 1 && WCount + WPawn == 1 && WB == 1)
		return endgame_KB_KP;

	if (WCount == 0 && WPawn == 2 && BCount + BPawn == 1 && BB == 1
		|| BCount == 0 && BPawn == 2 && WCount + WPawn == 1 && WB == 1)
		return endgame_KB_KPP;

	if (one_versus_one && (WBL == 1 && BBL == 1 || WBD == 1 && BBD == 1) && WPawn + BPawn == 1)
		return endgame_KBP_KB_identical;

	if (one_versus_one && (WBL == 1 && BBD == 1 || WBD == 1 && BBL == 1)
		&& (WPawn == 1 && BPawn <= 1 || BPawn == 1 && WPawn <= 1))
		return endgame_KBP_KB_opposite;

	if (one_versus_one && (WBL == 1 && BBD == 1 || WBD == 1 && BBL == 1)
		&& (WPawn == 2 && BPawn <= 2 || BPawn == 2 && WPawn <= 2))
		return endgame_KBPP_KB_opposite;

	if (one_versus_one && (WR == 1 && BB == 1 && WPawn == 1 && BPawn == 0 || BR == 1 && WB == 1 && BPawn == 1 && WPawn == 0))
		return endgame_KRP_KB;

	if (WCount == 0 && BCount == 0 && WPawn + BPawn != 0)
		return endgame_KPs_KPs;

	if (WCount == 0 && BCount == 0 && WPawn + BPawn == 1)
		return endgame_KP_K;

	if (WN == 1 && WB == 1 && WCount + WPawn == 2 && BCount + BPawn == 0)
		return endgame_KBN_K;

	if (BN == 1 && BB == 1 && WCount + WPawn == 0 && BCount + BPawn == 2)
		return endgame_KBN_K;

	return endgame_none;
}