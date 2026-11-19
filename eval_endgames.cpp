/*
*/

#include "houdini.h"
#include "bitboard.h"
#include "eval_endgames.h"
#include "move_generation.h"
#include "search_smp.h"

using std::string;

namespace Zobrist 
{
	extern Key64 psq[PIECE_N][SQUARE_N];
}

namespace 
{

#define V(x) Value(x * 4 / 5)

	const Value PushToEdge[SQUARE_N] =
	{
		V(100), V(90), V(80), V(70), V(70), V(80), V(90), V(100),
		V(90), V(70), V(60), V(50), V(50), V(60), V(70),  V(90),
		V(80), V(60), V(40), V(30), V(30), V(40), V(60),  V(80),
		V(70), V(50), V(30), V(20), V(20), V(30), V(50),  V(70),
		V(70), V(50), V(30), V(20), V(20), V(30), V(50),  V(70),
		V(80), V(60), V(40), V(30), V(30), V(40), V(60),  V(80),
		V(90), V(70), V(60), V(50), V(50), V(60), V(70),  V(90),
		V(100), V(90), V(80), V(70), V(70), V(80), V(90), V(100)
	};

	const Value PushToCorner[SQUARE_N] =
	{
		V(100),  V(90),  V(80),  V(70),  V(60),  V(50),  V(40),  V(30),
		V(90),  V(80),  V(70),  V(60),  V(50),  V(40),  V(30),  V(40),
		V(80),  V(70),  V(55),  V(40),  V(40),  V(25),  V(40),  V(50),
		V(70),  V(60),  V(40),  V(20),  V(10),  V(40),  V(50),  V(60),
		V(60),  V(50),  V(40),  V(10),  V(20),  V(40),  V(60),  V(70),
		V(50),  V(40),  V(25),  V(40),  V(40),  V(55),  V(70),  V(80),
		V(40),  V(30),  V(40),  V(50),  V(60),  V(70),  V(80),  V(90),
		V(30),  V(40),  V(50),  V(60),  V(70),  V(80),  V(90), V(100)
	};

	const Value PushClose[8] = { V(0), V(0), V(100), V(80), V(60), V(40), V(20), V(10) };
	const Value PushAway[8] = { V(0), V(5), V(20), V(40), V(60), V(80), V(90), V(100) };

#undef V

	const int KRPPKRPScaleFactors[RANK_N] = { 0, 14, 16, 22, 33, 69, 0, 0 };

#ifndef NDEBUG
	bool count_material(const Position& pos, Color color, MaterialValue npm, int pawns)
	{
		return pos.non_pawn_material(color) == npm && pos.count(color, PAWN) == pawns;
	}
#endif

	Square normalize_pawn_side(const Position& pos, Color strongSide, Square square)
	{
		assert(pos.count(strongSide, PAWN) == 1);

		if (file_of(pos.piece_square(strongSide, PAWN)) >= FILE_E)
			square = Square(square ^ 7);

		if (strongSide == BLACK)
			square = ~square;

		return square;
	}

	Key64 material_key(Color color, const char* pieces)
	{
		Key64 materialKey = 0;
		for (PieceType piece = KING; piece <= QUEEN; ++piece)
		{
			int count = int(pieces[piece] - '0');
			for (int cnt = 0; cnt < count; ++cnt)
				materialKey ^= Zobrist::psq[make_piece(color, piece)][cnt];
			count = int(pieces[piece + 8] - '0');
			for (int cnt = 0; cnt < count; ++cnt)
				materialKey ^= Zobrist::psq[make_piece(~color, piece)][cnt];
		}

		return materialKey;
	}

	Bitboard attack_king_inc(Square s) { return EmptyAttack[KING][s] | s; }
} // namespace


Endgames::Endgames() {}

const Bitboard BlackModifier = 0xa4489c56;

void Endgames::add_value(const char* pieces, endgame_value_fx fW, endgame_value_fx fB)
{
	mapValue[material_key(WHITE, pieces)] = value_count;
	value_functions[value_count++] = fW;
	mapValue[material_key(BLACK, pieces)] = value_count;
	value_functions[value_count++] = fB;
}

void Endgames::add_scalefactor(const char* pieces, endgame_scalefactor_fx fW, endgame_scalefactor_fx fB)
{
	mapScaleFactor[material_key(WHITE, pieces)] = factor_count;
	factor_functions[factor_count++] = fW;
	mapScaleFactor[material_key(BLACK, pieces) ^ BlackModifier] = factor_count;
	factor_functions[factor_count++] = fB;
}

int Endgames::probe_value(Key64 key)
{
	FunctionIndexMap::const_iterator iter = mapValue.find(key);
	return iter == mapValue.end() ? -1 : iter->second;
}

int Endgames::probe_scalefactor(Key64 key, Color& strongSide)
{
	FunctionIndexMap::const_iterator iter = mapScaleFactor.find(key);
	if (iter != mapScaleFactor.end())
		return strongSide = WHITE, iter->second;
	iter = mapScaleFactor.find(key ^ BlackModifier);
	if (iter != mapScaleFactor.end())
		return strongSide = BLACK, iter->second;
	return -1;
}


template <Color strongSide>
Value endgameValue_KXK(const Position& pos) 
{
	const Color weakSide = ~strongSide;
	assert(count_material(pos, weakSide, MATL_0, 0));
	assert(!pos.check_givers()); // Eval is never called when in check

	if (pos.side_to_move() == weakSide && !AtLeastOneLegalMove(pos))
		return DRAW_VALUE;

	Square strongK = pos.king(strongSide);
	Square weakK = pos.king(weakSide);

	Value result = ValueFromMaterial(pos.non_pawn_material(strongSide))
		+ pos.count(strongSide, PAWN) * VALUE_PAWN
		+ PushToEdge[weakK]
		+ PushClose[distance(strongK, weakK)];

	if (result < WIN_VALUE)
		if (pos.count(strongSide, QUEEN) + pos.count(strongSide, ROOK)
			|| (pos.count(strongSide, BISHOP) && pos.count(strongSide, KNIGHT))
			|| (pos.pieces(strongSide, BISHOP) & DarkSquares && pos.pieces(strongSide, BISHOP) & ~DarkSquares))
			result += WIN_VALUE;

	return strongSide == pos.side_to_move() ? result : -result;
}


template <Color strongSide>
Value endgameValue_KBNK(const Position& pos) 
{
	const Color weakSide = ~strongSide;
	assert(count_material(pos, strongSide, MATL_KNIGHT + MATL_BISHOP, 0));
	assert(count_material(pos, weakSide, MATL_0, 0));

	Square strongK = pos.king(strongSide);
	Square weakK = pos.king(weakSide);
	Square bishop = pos.piece_square(strongSide, BISHOP);

	if (different_color(bishop, SQ_A1))
	{
		strongK = ~strongK;
		weakK = ~weakK;
	}

	Value result = WIN_VALUE
		+ PushClose[distance(strongK, weakK)]
		+ PushToCorner[weakK];

	return strongSide == pos.side_to_move() ? result : -result;
}


template <Color strongSide>
Value endgameValue_KPK(const Position& pos) 
{
	const Color weakSide = ~strongSide;
	assert(count_material(pos, strongSide, MATL_0, 1));
	assert(count_material(pos, weakSide, MATL_0, 0));

	Square strongK = normalize_pawn_side(pos, strongSide, pos.king(strongSide));
	Square weakK = normalize_pawn_side(pos, strongSide, pos.king(weakSide));
	Square pawn = normalize_pawn_side(pos, strongSide, pos.piece_square(strongSide, PAWN));

	Color us = strongSide == pos.side_to_move() ? WHITE : BLACK;

	if (!Bitbases::probe(strongK, pawn, weakK, us))
	{
		pos.info()->eval_is_exact = true;
		return DRAW_VALUE;
	}

	Value result = WIN_VALUE + Value(40) * int(rank_of(pawn));

	return strongSide == pos.side_to_move() ? result : -result;
}


/*
test posities voor KR_KP (randgevallen)
=======================================
remise
R7/8/8/7K/4p3/4k3/8/8 b - - 0 1
4R3/1K6/8/2k1p3/8/8/8/8 b - - 0 1
R7/2K5/8/2k1p3/8/8/8/8 b - - 0 1
R7/7K/8/4p3/2k5/8/8/8 b - - 0 1
6R1/5K2/8/3p4/2k5/8/8/8 b - - 0 1
3R4/2K5/8/8/2p5/2k5/8/8 w - - 0 1
K5R1/8/6p1/5k2/8/8/8/8 b - - 0 1
=> require 14 - 18 - 16 - 13 - 14 - 14 - 15 plies om remise zonder KR_KP extensie te herkennen

winst
4R3/8/8/8/4p2K/3k4/8/8 b - - 0 1
4R3/2K5/8/2k1p3/8/8/8/8 b - - 0 1
R7/7K/8/2k1p3/8/8/8/8 b - - 0 1
2R5/1K6/8/8/1p6/1k6/8/8 w - - 0 1
K5R1/8/6p1/5k2/8/8/8/8 w - - 0 1
*/
template <Color strongSide>
ScaleFactor endgameScaleFactor_KRKP(const Position& pos)
{
	const Color weakSide = ~strongSide;
	assert(count_material(pos, strongSide, MATL_ROOK, 0));
	assert(count_material(pos, weakSide, MATL_0, 1));

	Square white_king = relative_square(strongSide, pos.king(strongSide));
	Square black_king = relative_square(strongSide, pos.king(weakSide));
	Square rook = relative_square(strongSide, pos.piece_square(strongSide, ROOK));
	Square pawn = relative_square(strongSide, pos.piece_square(weakSide, PAWN));

	Square promotion = make_square(file_of(pawn), RANK_1);

	int promotion_distance, WK_distance, BK_distance, WK_distance2;
	bool pawn_attacked, rook_behind_pawn;

	if (!((pos.attack_from<KING>(white_king) | pos.pieces(strongSide, KING)) & bb_ranks_forward(weakSide, pawn)) &&
		(rank_of(pawn) <= RANK_3 || rank_of(black_king) <= rank_of(pawn)))
	{
		BK_distance = SquareDistance[black_king][pawn];
		pawn_attacked = (pos.attack_from<ROOK>(pos.piece_square(strongSide, ROOK)) | pos.attack_from<KING>(pos.king(strongSide)))
			& pos.piece_square(weakSide, PAWN);

		if (BK_distance <= 1 + (pos.side_to_move() == weakSide || !pawn_attacked))
		{
			rook_behind_pawn = (file_of(pawn) == file_of(rook));
			promotion_distance = rank_of(pawn) + SquareDistance[black_king][promotion] - 2 + (rook_behind_pawn);
			WK_distance = SquareDistance[white_king][promotion] - (pos.side_to_move() == strongSide);
			if ((file_of(white_king) < file_of(pawn) && file_of(black_king) < file_of(pawn) ||
				file_of(white_king) > file_of(pawn) && file_of(black_king) > file_of(pawn)) &&
				rank_of(white_king) >= rank_of(black_king))
				WK_distance++;

			if ((file_of(white_king) < file_of(black_king) && file_of(black_king) < file_of(pawn) ||
				file_of(white_king) > file_of(black_king) && file_of(black_king) > file_of(pawn)) &&
				rank_of(white_king) >= rank_of(black_king))
				WK_distance++;

			if (WK_distance > promotion_distance)
			{
				WK_distance2 = SquareDistance[white_king][pawn] - (pos.side_to_move() == strongSide);
				if (WK_distance2 > BK_distance ||
					rank_of(white_king) - (pos.side_to_move() == strongSide) > rank_of(pawn))
					return DRAW_FACTOR;
			}
		}
	}
	return NO_FACTOR;
}


/*
test posities voor KQ_KP
========================
remise
7Q/8/8/4K3/8/8/p7/1k6 w - - 0 1
Q7/8/8/8/8/8/pk3K2/8 w - - 0 1
2Q5/8/4K3/8/8/8/1kp5/8 w - - 0 1
7Q/8/8/4K3/8/8/2pk4/8 w - - 0 1
without KQ_KP extension even at 30 ply evaluations are higher than +6
4Q3/8/8/3K4/8/8/2pk4/8 w - - 0 1  => uitzonderingspositie op de regel van Euwe !!!

winst
8/8/8/8/8/8/pk3KQ1/8 w - - 0 1
Q7/8/8/K7/8/8/pk6/8 w - - 0 1
7Q/8/8/3K4/8/8/2pk4/8 w - - 0 1
8/8/1Q6/1K6/8/8/1kp5/8 w - - 0 1
*/
template <Color strongSide>
ScaleFactor endgameScaleFactor_KQKP(const Position& pos) 
{
	const Color weakSide = ~strongSide;
	assert(count_material(pos, strongSide, MATL_QUEEN, 0));
	assert(count_material(pos, weakSide, MATL_0, 1));

	if (!(pos.pieces(weakSide, PAWN) & (strongSide == WHITE ? bb4(SQ_A2, SQ_C2, SQ_F2, SQ_H2) : bb4(SQ_A7, SQ_C7, SQ_F7, SQ_H7))))
		return NO_FACTOR;

	Square white_king = relative_square(strongSide, pos.king(strongSide));
	Square black_king = relative_square(strongSide, pos.king(weakSide));
	Square queen = relative_square(strongSide, pos.piece_square(strongSide, QUEEN));
	Square pawn = relative_square(strongSide, pos.piece_square(weakSide, PAWN));

	Bitboard attack = pos.attack_from<QUEEN>(pos.piece_square(strongSide, QUEEN)) | pos.attack_from<KING>(pos.king(strongSide));
	bool white_to_move = pos.side_to_move() == strongSide;
	Bitboard BK_check = pos.check_givers();

	int flop = strongSide == WHITE ? 0 : 070;
	bool tempo = !white_to_move || !(attack & Square(pawn ^ flop));
	if (queen != pawn - 8 && rank_of(black_king) <= RANK_2 + tempo
		&& (SquareDistance[black_king][pawn] <= 1 + tempo || pawn == SQ_C2 && black_king == SQ_A1 || pawn == SQ_F2 && black_king == SQ_H1))
	{
		if (file_of(pawn) >= FILE_E)
		{
			flop ^= 07;
			pawn = Square(pawn ^ 07);
			white_king = Square(white_king ^ 07);
			black_king = Square(black_king ^ 07);
			queen = Square(queen ^ 07);
		}
		if (pawn == SQ_A2 && (file_of(white_king) > FILE_E || white_king > SQ_D5))
			return DRAW_FACTOR;
		if (pawn == SQ_C2 && file_of(black_king) < FILE_C && (file_of(white_king) > FILE_E || white_king > SQ_C4))
			return DRAW_FACTOR;
		if (pawn == SQ_C2 && file_of(black_king) >= FILE_C && (file_of(white_king) > FILE_G || white_king > SQ_D5))
			return DRAW_FACTOR;

		if (!white_to_move && !BK_check)
		{
			if (pawn == SQ_A2 && (SquareDistance[SQ_A1][black_king] == 1 || file_of(queen) != FILE_A && !(attack & Square(SQ_A1 ^ flop))))
				return DRAW_FACTOR;

			if (pawn == SQ_C2 && (SquareDistance[SQ_C1][black_king] == 1 || file_of(queen) != FILE_C && !(attack & Square(SQ_C1 ^ flop))))
				return DRAW_FACTOR;
		}
	}
	return NO_FACTOR;
}


template <Color strongSide>
Value endgameValue_KQKR(const Position& pos)
{
	const Color weakSide = ~strongSide;
	assert(count_material(pos, strongSide, MATL_QUEEN, 0));
	assert(count_material(pos, weakSide, MATL_ROOK, 0));

	Square strongK = pos.king(strongSide);
	Square weakK = pos.king(weakSide);

	Value result = 4 * VALUE_PAWN
		+ PushToEdge[weakK]
		+ PushClose[distance(strongK, weakK)];

	return strongSide == pos.side_to_move() ? result : -result;
}


template <Color strongSide>
Value endgameValue_KNNK(const Position& pos)
{ 
	return DRAW_VALUE;
}


/*
test posities
remise
k7/8/K7/P7/8/2B5/P7/8 w - - 0 1
8/Bk6/1P6/2K5/8/8/8/8 w - - 0 1 Ponziani
1k6/p7/P2K4/8/4B3/8/8/8 w - - 0 1
1k6/p7/P2K4/8/Pp2B3/8/8/8 w - - 0 1
2k5/1p6/1P2K3/8/5B2/8/8/8 w - - 0 1
2k5/1p6/1Pp1K3/8/1P3B2/8/8/8 w - - 0 1
2k5/1p6/1P2K3/8/4B3/8/8/8 w - - 0 1
KBP v KP: 8/8/6K1/8/p7/P4k2/8/4B3 w - - OK!
KBP v KP: 1b6/7p/8/1K5P/8/8/8/5k2 b - - OK!
KBP v KP: 4b3/8/p7/P3K3/8/8/8/2k5 b - - OK!
KBPP v KPP: 2k5/7p/6pP/6P1/5B2/3K4/8/8 w - - OK!
win
1k6/p7/Pp1K4/8/P3B3/8/8/8 w - - 0 1
8/Bpk5/8/P2K4/8/8/8/8 w - - 0 1
*/
template <Color strongSide>
ScaleFactor endgameScaleFactor_KBPsK(const Position& pos) 
{
	const Color weakSide = ~strongSide;
	assert(pos.non_pawn_material(strongSide) == MATL_BISHOP);
	assert(pos.count(strongSide, PAWN) >= 1);

	// kan laatste pion geslagen worden?
	Bitboard bb_black_attack = attack_king_inc(pos.king(weakSide));
	Bitboard bb_white_attack = pos.attack_from<KING>(pos.king(strongSide)) | pos.attack_from<BISHOP>(pos.piece_square(strongSide, BISHOP));
	if (pos.count(strongSide, PAWN) == 1 && pos.side_to_move() == weakSide
		&& pos.pieces(strongSide, PAWN) & bb_black_attack & ~bb_white_attack)
		return DRAW_FACTOR;

	Square white_king = relative_square(strongSide, pos.king(strongSide));
	Square black_king = relative_square(strongSide, pos.king(weakSide));
	Square bishop = relative_square(strongSide, pos.piece_square(strongSide, BISHOP));
	Square pawn = relative_square(strongSide, pos.piece_list(strongSide, PAWN)[0]);
	Bitboard white_pawns = 0, black_pawns = 0;

	if (DarkSquares & bishop)
	{
		white_king = Square(white_king ^ 07);
		black_king = Square(black_king ^ 07);
		bishop = Square(bishop ^ 07);
		pawn = Square(pawn ^ 07);
		for (int i = 0; i < pos.count(strongSide, PAWN); i++)
			white_pawns |= relative_square(strongSide, Square(pos.piece_list(strongSide, PAWN)[i] ^ 07));
		for (int i = 0; i < pos.count(weakSide, PAWN); i++)
			black_pawns |= relative_square(strongSide, Square(pos.piece_list(weakSide, PAWN)[i] ^ 07));
	}
	else
	{
		for (int i = 0; i < pos.count(strongSide, PAWN); i++)
			white_pawns |= relative_square(strongSide, pos.piece_list(strongSide, PAWN)[i]);
		for (int i = 0; i < pos.count(weakSide, PAWN); i++)
			black_pawns |= relative_square(strongSide, pos.piece_list(weakSide, PAWN)[i]);
	}
	// vanaf hier hebben we de situatie met een witte loper op de lichte velden

	Bitboard bb_king_covers = attack_king_inc(black_king);
	if (pos.count(strongSide, PAWN) > 1)
		pawn = SQ_NONE;

	if (!(white_pawns & ~FileHBB))
	{
		if (bb_king_covers & bb(SQ_H8))
		{
			if (white_pawns & bb(SQ_H5) && (black_pawns & bb2(SQ_G7, SQ_H6)) == bb2(SQ_G7, SQ_H6))
				;
			else
				return DRAW_FACTOR;
		}
		if (!(black_pawns & FileGBB)
			&& SquareDistance[SQ_G7][black_king] < SquareDistance[SQ_G7][white_king] + 1 - (pos.side_to_move() == strongSide)
			&& SquareDistance[SQ_H8][black_king] < 7 - rank_of(msb(white_pawns)) - (pos.side_to_move() == strongSide))
			return ScaleFactor(NORMAL_FACTOR / 4);
		if (!(white_pawns & ~bb2(SQ_H2, SQ_H3)) && (black_pawns & bb(SQ_H4)))
			return ScaleFactor(NORMAL_FACTOR / 4);
	}
	if (pawn == SQ_G6 && bishop == SQ_H7 && bb_king_covers & bb(SQ_H8))
		return DRAW_FACTOR;
	if (!(white_pawns & ~FileABB) && white_pawns & bb(SQ_A6) &&
		black_pawns & bb(SQ_A7) && bb_king_covers & bb(SQ_B8) &&
		(!(black_pawns & FileBBB) ||
			rank_of(lsb(white_pawns & FileABB)) >= rank_of(msb(black_pawns & FileBBB))))
		return DRAW_FACTOR;
	if (!(white_pawns & ~FileGBB) && white_pawns & bb(SQ_G6) &&
		black_pawns & bb(SQ_G7) && bb_king_covers & bb2(SQ_G8, SQ_F8))
		return DRAW_FACTOR;
	if (pawn == SQ_B6 && black_pawns & bb(SQ_B7) && bb_king_covers & bb2(SQ_B8, SQ_C8))
		return DRAW_FACTOR;
	// witte pionnen a6-b5-c4 geblokkeerd door een keten van a7-b6-c5
	if (white_pawns == bb2(SQ_A6, SQ_B5)
		&& (black_pawns & (bb2(SQ_A7, SQ_B6) | bb3(SQ_B7, SQ_C7, SQ_C6))) == bb2(SQ_A7, SQ_B6)
		&& bb_king_covers & bb(SQ_B8))
		return DRAW_FACTOR;
	if (white_pawns == bb3(SQ_A6, SQ_B5, SQ_C4)
		&& (black_pawns & (bb3(SQ_A7, SQ_B6, SQ_C5) | bb3(SQ_B7, SQ_C7, SQ_C6) | bb3(SQ_D7, SQ_D6, SQ_D5))) == bb3(SQ_A7, SQ_B6, SQ_C5)
		&& bb_king_covers & bb(SQ_B8))
		return DRAW_FACTOR;

	return NO_FACTOR;
}

/*
remise
8/4kp2/4r3/8/5QK1/8/8/8 w - - 0 1
winst
8/6kp/6r1/8/4KQ2/8/8/8 w - - 0 1
*/
template <Color strongSide>
ScaleFactor endgameScaleFactor_KQKRPs(const Position& pos) 
{
	const Color weakSide = ~strongSide;
	assert(count_material(pos, strongSide, MATL_QUEEN, 0));
	assert(pos.count(weakSide, ROOK) == 1);
	assert(pos.count(weakSide, PAWN) >= 1);

	Square kingSq = pos.king(weakSide);
	Square rsq = pos.piece_square(weakSide, ROOK);

	if (relative_rank(weakSide, kingSq) <= RANK_2
		&&  relative_rank(weakSide, pos.king(strongSide)) >= RANK_4
		&&  relative_rank(weakSide, rsq) == RANK_3
		&& (pos.pieces(weakSide, PAWN)
			& ~FileABB & ~FileHBB // a en h pion maken geen vesting !
			& pos.attack_from<KING>(kingSq)
			& pos.attack_from<PAWN>(rsq, strongSide)))
		return DRAW_FACTOR;

	return ONE_PAWN_FACTOR;
}


/// KRP vs KR. This function knows a handful of the most important classes of
/// drawn positions, but is far from perfect. It would probably be a good idea
/// to add more knowledge in the future.
///
/// It would also be nice to rewrite the actual code for this function,
/// which is mostly copied from Glaurung 1.x, and isn't very pretty.
template <Color strongSide>
ScaleFactor endgameScaleFactor_KRPKR(const Position& pos) {

  const Color weakSide = ~strongSide;
  assert(count_material(pos, strongSide, MATL_ROOK, 1));
  assert(count_material(pos, weakSide, MATL_ROOK, 0));

  // Assume sterkeZijde is white and the pawn is on files A-D
  Square wksq = normalize_pawn_side(pos, strongSide, pos.king(strongSide));
  Square bksq = normalize_pawn_side(pos, strongSide, pos.king(weakSide));
  Square wrsq = normalize_pawn_side(pos, strongSide, pos.piece_square(strongSide, ROOK));
  Square wpsq = normalize_pawn_side(pos, strongSide, pos.piece_square(strongSide, PAWN));
  Square brsq = normalize_pawn_side(pos, strongSide, pos.piece_square(weakSide, ROOK));

  File f = file_of(wpsq);
  Rank r = rank_of(wpsq);
  Square queeningSq = make_square(f, RANK_8);
  int tempo = (pos.side_to_move() == strongSide);

  // If the pawn is not too far advanced and the defending king defends the
  // queening square, use the third-rank defence.
  if (   r <= RANK_5
      && distance(bksq, queeningSq) <= 1
      && wksq <= SQ_H5
      && (rank_of(brsq) == RANK_6 || (r <= RANK_3 && rank_of(wrsq) != RANK_6)))
      return DRAW_FACTOR;

  // The defending side saves a draw by checking from behind in case the pawn
  // has advanced to the 6th rank with the king behind.
  if (   r == RANK_6
      && distance(bksq, queeningSq) <= 1
      && rank_of(wksq) + tempo <= RANK_6
      && (rank_of(brsq) == RANK_1 || (!tempo && file_distance(brsq, wpsq) >= 3)))
      return DRAW_FACTOR;

  if (   r >= RANK_6
      && bksq == queeningSq
      && rank_of(brsq) == RANK_1
      && (!tempo || distance(wksq, wpsq) >= 2))
      return DRAW_FACTOR;

  // White pawn on a7 and rook on a8 is a draw if black's king is on g7 or h7
  // and the black rook is behind the pawn.
  if (   wpsq == SQ_A7
      && wrsq == SQ_A8
      && (bksq == SQ_H7 || bksq == SQ_G7)
      && file_of(brsq) == FILE_A
      && (rank_of(brsq) <= RANK_3 || file_of(wksq) >= FILE_D || rank_of(wksq) <= RANK_5))
      return DRAW_FACTOR;

  // If the defending king blocks the pawn and the attacking king is too far
  // away, it's a draw.
  if (   r <= RANK_5
      && bksq == wpsq + UP
      && distance(wksq, wpsq) - tempo >= 2
      && distance(wksq, brsq) - tempo >= 2)
      return DRAW_FACTOR;

  // Pawn on the 7th rank supported by the rook from behind usually wins if the
  // attacking king is closer to the queening square than the defending king,
  // and the defending king cannot gain tempi by threatening the attacking rook.
  if (   r == RANK_7
      && f != FILE_A
      && file_of(wrsq) == f
      && wrsq != queeningSq
      && (distance(wksq, queeningSq) < distance(bksq, queeningSq) - 2 + tempo)
      && (distance(wksq, queeningSq) < distance(bksq, wrsq) + tempo))
      return ScaleFactor(MAX_FACTOR - 3 * distance(wksq, queeningSq));

  // Similar to the above, but with the pawn further back
  if (   f != FILE_A
      && file_of(wrsq) == f
      && wrsq < wpsq
      && (distance(wksq, queeningSq) < distance(bksq, queeningSq) - 2 + tempo)
      && (distance(wksq, wpsq + UP) < distance(bksq, wpsq + UP) - 2 + tempo)
      && (  distance(bksq, wrsq) + tempo >= 3
          || (    distance(wksq, queeningSq) < distance(bksq, wrsq) + tempo
              && (distance(wksq, wpsq + UP) < distance(bksq, wrsq) + tempo))))
      return ScaleFactor(MAX_FACTOR
                         - 12 * distance(wpsq, queeningSq)
                         - 3 * distance(wksq, queeningSq));

  // If the pawn is not far advanced and the defending king is somewhere in
  // the pawn's path, it's probably a draw.
  if (r <= RANK_4 && bksq > wpsq)
  {
      if (file_of(bksq) == file_of(wpsq))
          return ScaleFactor(16);
      if (   file_distance(bksq, wpsq) == 1
          && distance(wksq, bksq) > 2)
          return ScaleFactor(38 - 3 * distance(wksq, bksq));
  }
  return NO_FACTOR;
}

template <Color strongSide>
ScaleFactor endgameScaleFactor_KRPKB(const Position& pos) {

  const Color weakSide = ~strongSide;
  assert(count_material(pos, strongSide, MATL_ROOK, 1));
  assert(count_material(pos, weakSide, MATL_BISHOP, 0));

  // Test for a rook pawn
  if (pos.pieces(PAWN) & (FileABB | FileHBB))
  {
      Square squareK = pos.king(weakSide);
      Square bsq = pos.piece_square(weakSide, BISHOP);
      Square psq = pos.piece_square(strongSide, PAWN);
      Rank rk = relative_rank(strongSide, psq);
      Square push = pawn_forward(strongSide);

      // If the pawn is on the 5th rank and the pawn (currently) is on
      // the same color square as the bishop then there is a chance of
      // a fortress. Depending on the king position give a moderate
      // reduction or a stronger one if the defending king is near the
      // corner but not trapped there.
      if (rk == RANK_5 && !different_color(bsq, psq))
      {
          int d = distance(psq + 3 * push, squareK);

          if (d <= 2 && !(d == 0 && squareK == pos.king(strongSide) + 2 * push))
              return ScaleFactor(38);
          else
              return ScaleFactor(75);
      }

      // When the pawn has moved to the 6th rank we can be fairly sure
      // it's drawn if the bishop attacks the square in front of the
      // pawn from a reasonable distance and the defending king is near
      // the corner
      if (   rk == RANK_6
          && distance(psq + 2 * push, squareK) <= 1
          && (EmptyAttack[BISHOP][bsq] & (psq + push))
          && file_distance(bsq, psq) >= 2)
          return ScaleFactor(12);
  }

  return NO_FACTOR;
}

/// KRPP vs KRP. There is just a single rule: if the stronger side has no passed
/// pawns and the defending king is actively placed, the position is drawish.
template <Color strongSide>
ScaleFactor endgameScaleFactor_KRPPKRP(const Position& pos) {

  const Color weakSide = ~strongSide;
  assert(count_material(pos, strongSide, MATL_ROOK, 2));
  assert(count_material(pos, weakSide, MATL_ROOK, 1));

  Square wpsq1 = pos.piece_list(strongSide, PAWN)[0];
  Square wpsq2 = pos.piece_list(strongSide, PAWN)[1];
  Square bksq = pos.king(weakSide);

  // Does the stronger side have a passed pawn?
  if (pos.is_passed_pawn(strongSide, wpsq1) || pos.is_passed_pawn(strongSide, wpsq2))
      return NO_FACTOR;

  Rank r = std::max(relative_rank(strongSide, wpsq1), relative_rank(strongSide, wpsq2));

  if (   file_distance(bksq, wpsq1) <= 1
      && file_distance(bksq, wpsq2) <= 1
      && relative_rank(strongSide, bksq) > r)
  {
      assert(r > RANK_1 && r < RANK_7);
      return ScaleFactor(KRPPKRPScaleFactors[r]);
  }
  return NO_FACTOR;
}


template <Color strongSide>
ScaleFactor endgameScaleFactor_KPsK(const Position& pos) 
{
	const Color weakSide = ~strongSide;
	assert(pos.non_pawn_material(strongSide) == MATL_0);
	assert(pos.count(strongSide, PAWN) >= 2);
	assert(count_material(pos, weakSide, MATL_0, 0));

	Square squareK = pos.king(weakSide);
	Bitboard pawns = pos.pieces(strongSide, PAWN);

	if (!(pawns & ~bb_ranks_forward(weakSide, rank_of(squareK)))
		&& !((pawns & ~FileABB) && (pawns & ~FileHBB))
		&& file_distance(squareK, lsb(pawns)) <= 1)
		return DRAW_FACTOR;

	return NO_FACTOR;
}


/*
test posities voor KBP_KB_identical
===================================
remise
4k3/8/2KP4/5b2/8/8/4B3/8 b - - 0 1
8/2K5/3P4/2k2b2/8/8/4B3/8 w - - 0 1

winst
4k3/8/2KP4/5b2/8/8/4B3/8 w - - 0 1

test posities voor KBP_KB_opposite
==================================
remise
8/4B3/3P4/4Kb2/6k1/8/8/8 w - - 0 1
8/4B3/3P4/4K3/b7/8/8/7k w - - 0 1
*/
const Bitboard AverbakhRule = 0xfff7e3c180000000;

template <Color strongSide>
ScaleFactor endgameScaleFactor_KBPKB(const Position& pos)
{
	const Color weakSide = ~strongSide;
	assert(count_material(pos, strongSide, MATL_BISHOP, 1));
	assert(count_material(pos, weakSide, MATL_BISHOP, 0));

	int weakSideToMove = pos.side_to_move() == weakSide;
	if (weakSideToMove
		&& pos.attack_from<BISHOP>(pos.piece_square(strongSide, PAWN)) & pos.pieces(weakSide, BISHOP)) //& ~ai.gepend[sterkeZijde])
		return DRAW_FACTOR;

	if (different_color(pos.piece_square(strongSide, BISHOP), pos.piece_square(weakSide, BISHOP)))
	{
		Square white_king = pos.king(strongSide);
		Square white_bishop = pos.piece_square(strongSide, BISHOP);
		Square pawn = pos.piece_square(strongSide, PAWN);
		Square black_king = pos.king(weakSide);
		Square black_bishop = pos.piece_square(weakSide, BISHOP);
		Bitboard white_attack = pos.attack_from<PAWN>(pawn, WHITE) | pos.attack_from<BISHOP>(white_bishop) | pos.attack_from<KING>(white_king);
		//Bord zwart_aanval = pos.aanval_van<LOPER>(zwarte_loper) | pos.aanval_van<KONING>(zwarte_koning);
		if (relative_rank(strongSide, pawn) <= RANK_5)
			return DRAW_FACTOR;
		if (bb_forward(strongSide, pawn) & black_king && different_color(black_king, white_bishop))
			return DRAW_FACTOR;
		if (bb_forward(strongSide, pawn) & (pos.attack_from<BISHOP>(black_bishop) | black_bishop)
			&& (weakSideToMove || (!(white_attack & black_bishop))))
			return DRAW_FACTOR;
	}
	else
	{
		int flop = strongSide == WHITE ? ((pos.pieces(WHITE, BISHOP) & DarkSquares) ? 0 : 07)
			: ((pos.pieces(BLACK, BISHOP) & ~DarkSquares) ? 070 : 077);

		Square pawn = Square(pos.piece_square(strongSide, PAWN) ^ flop);
		Square white_king = Square(pos.king(strongSide) ^ flop);
		Square black_king = Square(pos.king(weakSide) ^ flop);
		//Veld witte_loper = Veld(pos.stuk_veld(sterkeZijde, LOPER) ^ flop);
		// hier aangekomen hebben we lopers op donkere velden, en wit is de sterke partij

		if (black_king > pawn && file_of(pawn) == file_of(black_king) && ~DarkSquares & black_king)
			return DRAW_FACTOR;

		if (SquareDistance[pawn][black_king] == 1 && rank_of(black_king) < rank_of(pawn)
			&& (abs(file_of(black_king) - file_of(white_king)) <= weakSideToMove)
			&& !(AverbakhRule & pawn))
			return DRAW_FACTOR;
	}

	return NO_FACTOR;
}


/// KBPP vs KB. It detects a few basic draws with opposite-colored bishops
template <Color strongSide>
ScaleFactor endgameScaleFactor_KBPPKB(const Position& pos)
{
  const Color weakSide = ~strongSide;
  assert(count_material(pos, strongSide, MATL_BISHOP, 2));
  assert(count_material(pos, weakSide, MATL_BISHOP, 0));

  Square wbsq = pos.piece_square(strongSide, BISHOP);
  Square bbsq = pos.piece_square(weakSide, BISHOP);

  if (!different_color(wbsq, bbsq))
      return NO_FACTOR;

  Square squareK = pos.king(weakSide);
  Square psq1 = pos.piece_list(strongSide, PAWN)[0];
  Square psq2 = pos.piece_list(strongSide, PAWN)[1];
  Square blockSq1, blockSq2;

  if (relative_rank(strongSide, psq1) > relative_rank(strongSide, psq2))
  {
      blockSq1 = psq1 + pawn_forward(strongSide);
      blockSq2 = make_square(file_of(psq2), rank_of(psq1));
  }
  else
  {
      blockSq1 = psq2 + pawn_forward(strongSide);
      blockSq2 = make_square(file_of(psq1), rank_of(psq2));
  }

  switch (file_distance(psq1, psq2))
  {
  case 0:
    // Both pawns are on the same file. It's an easy draw if the defender firmly
    // controls some square in the frontmost pawn's path.
    if (   file_of(squareK) == file_of(blockSq1)
        && relative_rank(strongSide, squareK) >= relative_rank(strongSide, blockSq1)
        && different_color(squareK, wbsq))
        return DRAW_FACTOR;
    else
        return NO_FACTOR;

  case 1:
    // Pawns on adjacent files. It's a draw if the defender firmly controls the
    // square in front of the frontmost pawn's path, and the square diagonally
    // behind this square on the file of the other pawn.
    if (   squareK == blockSq1
        && different_color(squareK, wbsq)
        && (   bbsq == blockSq2
            || (pos.attack_from<BISHOP>(blockSq2) & pos.pieces(weakSide, BISHOP))
            || rank_distance(psq1, psq2) >= 2))
        return DRAW_FACTOR;

    else if (   squareK == blockSq2
             && different_color(squareK, wbsq)
             && (   bbsq == blockSq1
                 || (pos.attack_from<BISHOP>(blockSq1) & pos.pieces(weakSide, BISHOP))))
        return DRAW_FACTOR;
    else
        return NO_FACTOR;

  default:
    // The pawns are not on the same file or adjacent files. No scaling.
    return NO_FACTOR;
  }
}


/// KBP vs KN. There is a single rule: If the defending king is somewhere along
/// the path of the pawn, and the square of the king is not of the same color as
/// the stronger side's bishop, it's a draw.
template <Color strongSide>
ScaleFactor endgameScaleFactor_KBPKN(const Position& pos) 
{
  const Color weakSide = ~strongSide;
  assert(count_material(pos, strongSide, MATL_BISHOP, 1));
  assert(count_material(pos, weakSide, MATL_KNIGHT, 0));

  Square pawnSq = pos.piece_square(strongSide, PAWN);
  Square strongBishopSq = pos.piece_square(strongSide, BISHOP);
  Square weakKingSq = pos.king(weakSide);

  if (   file_of(weakKingSq) == file_of(pawnSq)
      && relative_rank(strongSide, pawnSq) < relative_rank(strongSide, weakKingSq)
      && (   different_color(weakKingSq, strongBishopSq)
          || relative_rank(strongSide, weakKingSq) <= RANK_6))
      return DRAW_FACTOR;

  return NO_FACTOR;
}


/*
test posities
remise
k7/P7/2N5/8/8/8/8/7K w - - 0 1
K7/P2k4/8/8/8/8/7N/8 b - - 0 1
K7/P2k4/8/8/8/8/8/7N b - - 0 1
*/
template <Color strongSide>
ScaleFactor endgameScaleFactor_KNPK(const Position& pos) 
{
  const Color weakSide = ~strongSide;
  assert(count_material(pos, strongSide, MATL_KNIGHT, 1));
  assert(count_material(pos, weakSide, MATL_0, 0));

  // Assume sterkeZijde is white and the pawn is on files A-D
  Square white_king = normalize_pawn_side(pos, strongSide, pos.king(strongSide));
  Square knight = normalize_pawn_side(pos, strongSide, pos.piece_square(strongSide, KNIGHT));
  Square pawn = normalize_pawn_side(pos, strongSide, pos.piece_square(strongSide, PAWN));
  Square black_king = normalize_pawn_side(pos, strongSide, pos.king(weakSide));

  if (pawn == SQ_A7 && distance(SQ_A8, black_king) <= 1)
      return DRAW_FACTOR;

  if (pawn == SQ_A7 && white_king == SQ_A8 &&
	  (black_king == SQ_C7 && (!(pos.side_to_move() == strongSide) ^ !(~DarkSquares & knight)) ||
		  black_king == SQ_C8 && (!(pos.side_to_move() == strongSide) ^ !(DarkSquares & knight))))
	  return DRAW_FACTOR;

  return NO_FACTOR;
}


/// KNP vs KB. If knight can block bishop from taking pawn, it's a win.
/// Otherwise the position is drawn.
template <Color strongSide>
ScaleFactor endgameScaleFactor_KNPKB(const Position& pos) 
{
  const Color weakSide = ~strongSide;
  Square pawnSq = pos.piece_square(strongSide, PAWN);
  Square bishopSq = pos.piece_square(weakSide, BISHOP);
  Square weakKingSq = pos.king(weakSide);

  // King needs to get close to promoting pawn to prevent knight from blocking.
  // Rules for this are very tricky, so just approximate.
  if (bb_forward(strongSide, pawnSq) & pos.attack_from<BISHOP>(bishopSq))
      return ScaleFactor(2 * distance(weakKingSq, pawnSq));

  return NO_FACTOR;
}


/// KP vs KP. This is done by removing the weakest side's pawn and probing the
/// KP vs K bitbase: If the weakest side has a draw without the pawn, it probably
/// has at least a draw with the pawn as well. The exception is when the stronger
/// side's pawn is far advanced and not on a rook file; in this case it is often
/// possible to win (e.g. 8/4k3/3p4/3P4/6K1/8/8/8 w - - 0 1).
template <Color strongSide>
ScaleFactor endgameScaleFactor_KPKP(const Position& pos) 
{
  const Color weakSide = ~strongSide;
  assert(count_material(pos, strongSide, MATL_0, 1));
  assert(count_material(pos, weakSide, MATL_0, 1));

  // Assume sterkeZijde is white and the pawn is on files A-D
  Square wksq = normalize_pawn_side(pos, strongSide, pos.king(strongSide));
  Square bksq = normalize_pawn_side(pos, strongSide, pos.king(weakSide));
  Square psq  = normalize_pawn_side(pos, strongSide, pos.piece_square(strongSide, PAWN));

  Color us = strongSide == pos.side_to_move() ? WHITE : BLACK;

  // If the pawn has advanced to the fifth rank or further, and is not a
  // rook pawn, it's too dangerous to assume that it's at least a draw.
  if (rank_of(psq) >= RANK_5 && file_of(psq) != FILE_A)
      return NO_FACTOR;

  // Probe the KPK bitbase with the weakest side's pawn removed. If it's a draw,
  // it's probably at least a draw even with the pawn.
  return Bitbases::probe(wksq, psq, bksq, us) ? NO_FACTOR : DRAW_FACTOR;
}


void Endgames::initialize()
{
	if (!mapValue.empty())
		return;

	value_count = factor_count = 0;

	// end games with complete evaluation
	value_functions[value_count++] = &endgameValue_KXK<WHITE>;    // index 0
	value_functions[value_count++] = &endgameValue_KXK<BLACK>;  // index 1
	//           KPNBRQ  KPNBRQ
	add_value("0110000 0100000", &endgameValue_KPK<WHITE>,  &endgameValue_KPK<BLACK>);
	add_value("0102000 0100000", &endgameValue_KNNK<WHITE>, &endgameValue_KNNK<BLACK>);
	add_value("0101100 0100000", &endgameValue_KBNK<WHITE>, &endgameValue_KBNK<BLACK>);
	add_value("0100001 0100010", &endgameValue_KQKR<WHITE>, &endgameValue_KQKR<BLACK>);
	//addValue("0100010 0101000", &endgameValue_KRKN<WHITE>, &endgameValue_KRKN<BLACK>);
	//addValue("0100010 0100100", &endgameValue_KRKB<WHITE>, &endgameValue_KRKB<BLACK>);

	// end games with scale factor
	factor_functions[factor_count++] = &endgameScaleFactor_KBPsK<WHITE>;     // index 0 KB and pawns vs K
	factor_functions[factor_count++] = &endgameScaleFactor_KBPsK<BLACK>;   // index 1
	factor_functions[factor_count++] = &endgameScaleFactor_KQKRPs<WHITE>;    // index 2 KQ vs KR and pawns
	factor_functions[factor_count++] = &endgameScaleFactor_KQKRPs<BLACK>;  // index 3
	factor_functions[factor_count++] = &endgameScaleFactor_KPsK<WHITE>;      // index 4 K and pawns vs K
	factor_functions[factor_count++] = &endgameScaleFactor_KPsK<BLACK>;    // index 5
	factor_functions[factor_count++] = &endgameScaleFactor_KPKP<WHITE>;      // index 6 KP vs KP
	factor_functions[factor_count++] = &endgameScaleFactor_KPKP<BLACK>;    // index 7
	//                 KPNBRQ  KPNBRQ
	add_scalefactor("0100001 0110000", &endgameScaleFactor_KQKP<WHITE>,    &endgameScaleFactor_KQKP<BLACK>);
	add_scalefactor("0100010 0110000", &endgameScaleFactor_KRKP<WHITE>,    &endgameScaleFactor_KRKP<BLACK>);
	add_scalefactor("0110010 0100010", &endgameScaleFactor_KRPKR<WHITE>,   &endgameScaleFactor_KRPKR<BLACK>);
	add_scalefactor("0120010 0110010", &endgameScaleFactor_KRPPKRP<WHITE>, &endgameScaleFactor_KRPPKRP<BLACK>);
	add_scalefactor("0110100 0100100", &endgameScaleFactor_KBPKB<WHITE>,   &endgameScaleFactor_KBPKB<BLACK>);
	add_scalefactor("0120100 0100100", &endgameScaleFactor_KBPPKB<WHITE>,  &endgameScaleFactor_KBPPKB<BLACK>);
	add_scalefactor("0111000 0100000", &endgameScaleFactor_KNPK<WHITE>,    &endgameScaleFactor_KNPK<BLACK>);
	add_scalefactor("0111000 0100100", &endgameScaleFactor_KNPKB<WHITE>,   &endgameScaleFactor_KNPKB<BLACK>);
	add_scalefactor("0110100 0101000", &endgameScaleFactor_KBPKN<WHITE>,   &endgameScaleFactor_KBPKN<BLACK>);
	add_scalefactor("0110010 0100100", &endgameScaleFactor_KRPKB<WHITE>,   &endgameScaleFactor_KRPKB<BLACK>);
}


template Value endgameValue_KXK<WHITE>(const Position& pos);
template Value endgameValue_KXK<BLACK>(const Position& pos);
template ScaleFactor endgameScaleFactor_KBPsK<WHITE>(const Position& pos);
template ScaleFactor endgameScaleFactor_KBPsK<BLACK>(const Position& pos);
template ScaleFactor endgameScaleFactor_KQKRPs<WHITE>(const Position& pos);
template ScaleFactor endgameScaleFactor_KQKRPs<BLACK>(const Position& pos);
template ScaleFactor endgameScaleFactor_KPsK<WHITE>(const Position& pos);
template ScaleFactor endgameScaleFactor_KPsK<BLACK>(const Position& pos);
template ScaleFactor endgameScaleFactor_KPKP<WHITE>(const Position& pos);
template ScaleFactor endgameScaleFactor_KPKP<BLACK>(const Position& pos);
