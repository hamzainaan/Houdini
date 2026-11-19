/*
*/

#include "houdini.h"
#include "bitboard.h"
#include "util_tools.h"

#ifndef USE_POPCNT
uint8_t PopCnt16[1 << 16];
#endif

int8_t SquareDistance[SQUARE_N][SQUARE_N];

Bitboard RookMask[SQUARE_N];
Bitboard BishopMask[SQUARE_N];
Bitboard* BishopAttackTable[64];
Bitboard* RookAttackTable[64];

const Bitboard BishopMagics[64] =
{
	0x007bfeffbfeffbffull, 0x003effbfeffbfe08ull, 0x0000401020200000ull,
	0x0000200810000000ull, 0x0000110080000000ull, 0x0000080100800000ull,
	0x0007efe0bfff8000ull, 0x00000fb0203fff80ull, 0x00007dff7fdff7fdull,
	0x0000011fdff7efffull, 0x0000004010202000ull, 0x0000002008100000ull,
	0x0000001100800000ull, 0x0000000801008000ull, 0x000007efe0bfff80ull,
	0x000000080f9fffc0ull, 0x0000400080808080ull, 0x0000200040404040ull,
	0x0000400080808080ull, 0x0000200200801000ull, 0x0000240080840000ull,
	0x0000080080840080ull, 0x0000040010410040ull, 0x0000020008208020ull,
	0x0000804000810100ull, 0x0000402000408080ull, 0x0000804000810100ull,
	0x0000404004010200ull, 0x0000404004010040ull, 0x0000101000804400ull,
	0x0000080800104100ull, 0x0000040400082080ull, 0x0000410040008200ull,
	0x0000208020004100ull, 0x0000110080040008ull, 0x0000020080080080ull,
	0x0000404040040100ull, 0x0000202040008040ull, 0x0000101010002080ull,
	0x0000080808001040ull, 0x0000208200400080ull, 0x0000104100200040ull,
	0x0000208200400080ull, 0x0000008840200040ull, 0x0000020040100100ull,
	0x007fff80c0280050ull, 0x0000202020200040ull, 0x0000101010100020ull,
	0x0007ffdfc17f8000ull, 0x0003ffefe0bfc000ull, 0x0000000820806000ull,
	0x00000003ff004000ull, 0x0000000100202000ull, 0x0000004040802000ull,
	0x007ffeffbfeff820ull, 0x003fff7fdff7fc10ull, 0x0003ffdfdfc27f80ull,
	0x000003ffefe0bfc0ull, 0x0000000008208060ull, 0x0000000003ff0040ull,
	0x0000000001002020ull, 0x0000000040408020ull, 0x00007ffeffbfeff9ull,
	0x007ffdff7fdff7fdull
};

const Bitboard RookMagics[64] =
{
	0x00a801f7fbfeffffull, 0x00180012000bffffull, 0x0040080010004004ull,
	0x0040040008004002ull, 0x0040020004004001ull, 0x0020008020010202ull,
	0x0040004000800100ull, 0x0810020990202010ull, 0x000028020a13fffeull,
	0x003fec008104ffffull, 0x00001800043fffe8ull, 0x00001800217fffe8ull,
	0x0000200100020020ull, 0x0000200080010020ull, 0x0000300043ffff40ull,
	0x000038010843fffdull, 0x00d00018010bfff8ull, 0x0009000c000efffcull,
	0x0004000801020008ull, 0x0002002004002002ull, 0x0001002002002001ull,
	0x0001001000801040ull, 0x0000004040008001ull, 0x0000802000200040ull,
	0x0040200010080010ull, 0x0000080010040010ull, 0x0004010008020008ull,
	0x0000020020040020ull, 0x0000010020020020ull, 0x0000008020010020ull,
	0x0000008020200040ull, 0x0000200020004081ull, 0x0040001000200020ull,
	0x0000080400100010ull, 0x0004010200080008ull, 0x0000200200200400ull,
	0x0000200100200200ull, 0x0000200080200100ull, 0x0000008000404001ull,
	0x0000802000200040ull, 0x00ffffb50c001800ull, 0x007fff98ff7fec00ull,
	0x003ffff919400800ull, 0x001ffff01fc03000ull, 0x0000010002002020ull,
	0x0000008001002020ull, 0x0003fff673ffa802ull, 0x0001fffe6fff9001ull,
	0x00ffffd800140028ull, 0x007fffe87ff7ffecull, 0x003fffd800408028ull,
	0x001ffff111018010ull, 0x000ffff810280028ull, 0x0007fffeb7ff7fd8ull,
	0x0003fffc0c480048ull, 0x0001ffffa2280028ull, 0x00ffffe4ffdfa3baull,
	0x007ffb7fbfdfeff6ull, 0x003fffbfdfeff7faull, 0x001fffeff7fbfc22ull,
	0x000ffffbf7fc2ffeull, 0x0007fffdfa03ffffull, 0x0003ffdeff7fbdecull,
	0x0001ffff99ffab2full
};


Bitboard bbSquare[SQUARE_N];
Bitboard bbAdjacentFiles[FILE_N];
Bitboard bbRanksForward[COLOR_N][RANK_N];
Bitboard bbBetween[SQUARE_N][SQUARE_N];
Bitboard bbConnection[SQUARE_N][SQUARE_N];
Bitboard bbForward[COLOR_N][SQUARE_N];
Bitboard PassedPawnMask[COLOR_N][SQUARE_N];
Bitboard PawnAttackRange[COLOR_N][SQUARE_N];
Bitboard PawnAttack[COLOR_N][SQUARE_N];
Bitboard EmptyAttack[PIECE_TYPE_N][SQUARE_N];
Bitboard KingZone[SQUARE_N];

namespace
{
	Bitboard MagicAttackR[102400];
#ifdef USE_PEXT
	Bitboard MagicAttackB[5248];
#endif

	static const int Local_BishopMagicIndex[64] =
	{
		16530,  9162,  9674,
		18532, 19172, 17700,
		5730, 19661, 17065,
		12921, 15683, 17764,
		19684, 18724,  4108,
		12936, 15747,  4066,
		14359, 36039, 20457,
		43291,  5606,  9497,
		15715, 13388,  5986,
		11814, 92656,  9529,
		18118,  5826,  4620,
		12958, 55229,  9892,
		33767, 20023,  6515,
		6483, 19622,  6274,
		18404, 14226, 17990,
		18920, 13862, 19590,
		5884, 12946,  5570,
		18740,  6242, 12326,
		4156, 12876, 17047,
		17780,  2494, 17716,
		17067,  9465, 16196,
		6166
	};

	static const int Local_RookMagicIndex[64] =
	{
		85487, 43101,     0,
		49085, 93168, 78956,
		60703, 64799, 30640,
		9256, 28647, 10404,
		63775, 14500, 52819,
		2048, 52037, 16435,
		29104, 83439, 86842,
		27623, 26599, 89583,
		7042, 84463, 82415,
		95216, 35015, 10790,
		53279, 70684, 38640,
		32743, 68894, 62751,
		41670, 25575,  3042,
		36591, 69918,  9092,
		17401, 40688, 96240,
		91632, 32495, 51133,
		78319, 12595,  5152,
		32110, 13894,  2546,
		41052, 77676, 73580,
		44947, 73565, 17682,
		56607, 56135, 44989,
		21479
	};

	void init_magic_sliders();

#ifndef USE_POPCNT
	unsigned popcount16(unsigned u)
	{
		u -= (u >> 1) & 0x5555U;
		u = ((u >> 2) & 0x3333U) + (u & 0x3333U);
		u = ((u >> 4) + u) & 0x0F0FU;
		return (u * 0x0101U) >> 8;
	}
#endif
}

#ifdef NO_BSF

// http://chessprogramming.wikispaces.com/BitScan
const uint64_t DeBruijn64 = 0x3F79D71B4CB0A89ULL;
const uint32_t DeBruijn32 = 0x783A9B23;

int MSBTable[256];
Square BSFTable[SQUARE_N];

unsigned bsf_index(Bitboard b)
{
	b ^= b - 1;
#if defined(IS_64BIT)
	return (b * DeBruijn64) >> 58;
#else
	return ((unsigned(b) ^ unsigned(b >> 32)) * DeBruijn32) >> 26;
#endif
}

Square lsb(Bitboard b)
{
  assert(b);
  return BSFTable[bsf_index(b)];
}

Square msb(Bitboard b)
{
  assert(b);
  unsigned b32;
  int result = 0;

  if (b > 0xFFFFFFFF)
  {
      b >>= 32;
      result = 32;
  }

  b32 = unsigned(b);

  if (b32 > 0xFFFF)
  {
      b32 >>= 16;
      result += 16;
  }

  if (b32 > 0xFF)
  {
      b32 >>= 8;
      result += 8;
  }

  return Square(result + MSBTable[b32]);
}

#endif // NO_BSF

const int KN_delta[][8] = { {}, { 9, 7, -7, -9, 8, 1, -1, -8 }, {}, { 17, 15, 10, 6, -6, -10, -15, -17 } };

void Bitboards::initialize()
{
#ifndef USE_POPCNT
	for (unsigned i = 0; i < (1 << 16); ++i)
		PopCnt16[i] = (uint8_t)popcount16(i);
#endif

	for (Square square = SQ_A1; square <= SQ_H8; ++square)
		bbSquare[square] = 1ULL << square;

#ifdef NO_BSF
	for (Square square = SQ_A1; square <= SQ_H8; ++square)
		BSFTable[bsf_index(bbSquare[s])] = square;
	for (Bitboard b = 2; b < 256; ++b)
		MSBTable[b] = MSBTable[b - 1] + !more_than_one(b);
#endif

	for (File file = FILE_A; file <= FILE_H; ++file)
		bbAdjacentFiles[file] = (file > FILE_A ? bbFile[file - 1] : 0) | (file < FILE_H ? bbFile[file + 1] : 0);

	for (Rank rank = RANK_1; rank < RANK_8; ++rank)
		bbRanksForward[WHITE][rank] = ~(bbRanksForward[BLACK][rank + 1] = bbRanksForward[BLACK][rank] | bbRank[rank]);

	for (Color color = WHITE; color <= BLACK; ++color)
		for (Square square = SQ_A1; square <= SQ_H8; ++square)
		{
			bbForward[color][square] = bbRanksForward[color][rank_of(square)] & bbFile[file_of(square)];
			PawnAttackRange[color][square] = bbRanksForward[color][rank_of(square)] & bbAdjacentFiles[file_of(square)];
			PassedPawnMask[color][square] = bbForward[color][square] | PawnAttackRange[color][square];
		}

	for (Square square1 = SQ_A1; square1 <= SQ_H8; ++square1)
		for (Square square2 = SQ_A1; square2 <= SQ_H8; ++square2)
			SquareDistance[square1][square2] = std::max(file_distance(square1, square2), rank_distance(square1, square2));

	for (Square square = SQ_A1; square <= SQ_H8; ++square)
	{
		PawnAttack[WHITE][square] = pawn_attack<WHITE>(bbSquare[square]);
		PawnAttack[BLACK][square] = pawn_attack<BLACK>(bbSquare[square]);
	}

	for (PieceType piece = KING; piece <= KNIGHT; ++(++piece))
		for (Square square = SQ_A1; square <= SQ_H8; ++square)
		{
			Bitboard b = 0;
			for (int i = 0; i < 8; ++i)
			{
				Square to = square + Square(KN_delta[piece][i]);
				if (to >= SQ_A1 && to <= SQ_H8 && distance(square, to) < 3)
					b |= to;
			}
			EmptyAttack[piece][square] = b;
		}

	for (Square square = SQ_A1; square <= SQ_H8; ++square)
	{
		Bitboard b = EmptyAttack[KING][square];
		if (file_of(square) == FILE_A)
			b |= b << 1;
		else if (file_of(square) == FILE_H)
			b |= b >> 1;
		if (rank_of(square) == RANK_1)
			b |= b << 8;
		else if (rank_of(square) == RANK_8)
			b |= b >> 8;
		KingZone[square] = b;
	}

	init_magic_sliders();

	for (Square square1 = SQ_A1; square1 <= SQ_H8; ++square1)
	{
		EmptyAttack[BISHOP][square1] = attack_bb_bishop(square1, 0);
		EmptyAttack[ROOK][square1] = attack_bb_rook(square1, 0);
		EmptyAttack[QUEEN][square1] = EmptyAttack[BISHOP][square1] | EmptyAttack[ROOK][square1];

		for (PieceType piece = BISHOP; piece <= ROOK; ++piece)
			for (Square square2 = SQ_A1; square2 <= SQ_H8; ++square2)
			{
				if (!(EmptyAttack[piece][square1] & square2))
					continue;

				bbConnection[square1][square2] = (attack_bb(piece, square1, 0) & attack_bb(piece, square2, 0)) | square1 | square2;
				bbBetween[square1][square2] = attack_bb(piece, square1, bbSquare[square2]) & attack_bb(piece, square2, bbSquare[square1]);
			}
	}
}


namespace
{
	Bitboard sliding_attacks(int square, Bitboard block, const int deltas[4][2],
		int fmin, int fmax, int rmin, int rmax)
	{
		Bitboard result = 0;
		int rk = square / 8, fl = square % 8, r, f;

		for (int direction = 0; direction < 4; direction++)
		{
			int dx = deltas[direction][0];
			int dy = deltas[direction][1];
			for (f = fl + dx, r = rk + dy;
				(dx == 0 || (f >= fmin && f <= fmax)) && (dy == 0 || (r >= rmin && r <= rmax));
				f += dx, r += dy)
			{
				result |= bbSquare[f + r * 8];
				if (block & bbSquare[f + r * 8])
					break;
			}
		}
		return result;
	}


#ifdef USE_PEXT
	void init_magic_bb_pext(Bitboard *attack, Bitboard* squareIndex[], Bitboard *mask, const int deltas[4][2])
	{
		for (int square = 0; square < 64; square++)
		{
			squareIndex[square] = attack;
			mask[square] = sliding_attacks(square, 0, deltas, 1, 6, 1, 6);

			Bitboard b = 0;
			do  // loop over complete set of mask[square], see http://chessprogramming.wikispaces.com/Traversing+Subsets+of+a+Set
			{
				squareIndex[square][pext(b, mask[square])] = sliding_attacks(square, b, deltas, 0, 7, 0, 7);
				b = (b - mask[square]) & mask[square];
				attack++;
			} while (b);
		}
	}
#else
	void init_magic_bb(Bitboard *attack, const int attackIndex[], Bitboard* squareIndex[], Bitboard *mask,
		int shift, const Bitboard mult[], const int deltas[4][2])
	{
		for (int square = 0; square < 64; square++)
		{
			int index = attackIndex[square];
			squareIndex[square] = attack + index;
			mask[square] = sliding_attacks(square, 0, deltas, 1, 6, 1, 6);

			Bitboard b = 0;
			do  // loop over complete set of mask[square], see http://chessprogramming.wikispaces.com/Traversing+Subsets+of+a+Set
			{
				int offset = (unsigned int)((b * mult[square]) >> shift);
				squareIndex[square][offset] = sliding_attacks(square, b, deltas, 0, 7, 0, 7);
				b = (b - mask[square]) & mask[square];
			} while (b);
		}
	}
#endif


	const int rook_deltas[4][2] = { { 0, 1 },{ 0, -1 },{ 1, 0 },{ -1, 0 } };
	const int bishop_deltas[4][2] = { { 1, 1 },{ -1, 1 },{ 1, -1 },{ -1, -1 } };

	void init_magic_sliders()
	{
#ifdef USE_PEXT
		init_magic_bb_pext(MagicAttackR, RookAttackTable, RookMask, rook_deltas);
		init_magic_bb_pext(MagicAttackB, BishopAttackTable, BishopMask, bishop_deltas);
#else
		init_magic_bb(MagicAttackR, Local_RookMagicIndex, RookAttackTable, RookMask, 52, RookMagics, rook_deltas);
		init_magic_bb(MagicAttackR, Local_BishopMagicIndex, BishopAttackTable,
			BishopMask, 55, BishopMagics, bishop_deltas);
#endif
	}

}
