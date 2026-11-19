/*
*/

#ifndef BITBOARD_H
#define BITBOARD_H

#include "houdini.h"

#ifdef USE_PEXT
#  include <immintrin.h> // voor _pext_u64()
#  define pext(occupied, mask) _pext_u64(occupied, mask)
#  define blsr_u64(b) _blsr_u64(b)
#endif

namespace Bitbases
{
	bool probe(Square wksq, Square wpsq, Square bksq, Color us);
}

namespace Bitboards
{
	void initialize();
}

const Bitboard DarkSquares = 0xAA55AA55AA55AA55ULL;

const Bitboard FileABB = 0x0101010101010101ULL;
const Bitboard FileBBB = FileABB << 1;
const Bitboard FileCBB = FileABB << 2;
const Bitboard FileDBB = FileABB << 3;
const Bitboard FileEBB = FileABB << 4;
const Bitboard FileFBB = FileABB << 5;
const Bitboard FileGBB = FileABB << 6;
const Bitboard FileHBB = FileABB << 7;

const Bitboard Rank1BB = 0xFF;
const Bitboard Rank2BB = Rank1BB << 8;
const Bitboard Rank3BB = Rank1BB << 16;
const Bitboard Rank4BB = Rank1BB << 24;
const Bitboard Rank5BB = Rank1BB << 32;
const Bitboard Rank6BB = Rank1BB << 40;
const Bitboard Rank7BB = Rank1BB << 48;
const Bitboard Rank8BB = Rank1BB << 56;

extern int8_t SquareDistance[SQUARE_N][SQUARE_N];

extern Bitboard bbSquare[SQUARE_N];
const Bitboard bbFile[FILE_N] = { FileABB , FileBBB, FileCBB, FileDBB, FileEBB, FileFBB, FileGBB, FileHBB };
const Bitboard bbRank[RANK_N] = { Rank1BB, Rank2BB, Rank3BB, Rank4BB, Rank5BB, Rank6BB, Rank7BB, Rank8BB };
extern Bitboard bbAdjacentFiles[FILE_N];
extern Bitboard bbRanksForward[COLOR_N][RANK_N];
extern Bitboard bbBetween[SQUARE_N][SQUARE_N];
extern Bitboard bbConnection[SQUARE_N][SQUARE_N];
extern Bitboard bbForward[COLOR_N][SQUARE_N];
extern Bitboard PassedPawnMask[COLOR_N][SQUARE_N];
extern Bitboard PawnAttackRange[COLOR_N][SQUARE_N];
extern Bitboard PawnAttack[COLOR_N][SQUARE_N];
extern Bitboard EmptyAttack[PIECE_TYPE_N][SQUARE_N];
extern Bitboard KingZone[SQUARE_N];


#define USE_BB_SHIFT
#ifdef USE_BB_SHIFT
  #define bb(square) (1ULL << square)
#else
  #define bb(square) bbSquare[square]
#endif

inline Bitboard operator&(Bitboard b, Square square)
{
	return b & bb(square);
}

inline Bitboard operator|(Bitboard b, Square square)
{
	return b | bb(square);
}

inline Bitboard operator^(Bitboard b, Square square)
{
	return b ^ bb(square);
}

inline Bitboard& operator|=(Bitboard& b, Square square)
{
	return b |= bb(square);
}

inline Bitboard& operator^=(Bitboard& b, Square square)
{
	return b ^= bb(square);
}

inline bool more_than_one(Bitboard b)
{
#ifdef USE_PEXT
	return blsr_u64(b);
#else
	return b & (b - 1);
#endif
}

#undef bb

inline Bitboard bb_rank(Square square)
{
	return bbRank[rank_of(square)];
}

inline Bitboard bb_file(Square square)
{
	return bbFile[file_of(square)];
}

inline Bitboard bb_file(File file)
{
	return bbFile[file];
}

inline Bitboard bb(Square s) { return (1ULL << s); }
inline Bitboard bb2(Square s1, Square s2) { return (1ULL << s1) | (1ULL << s2); }
inline Bitboard bb3(Square s1, Square s2, Square s3) { return (1ULL << s1) | (1ULL << s2) | (1ULL << s3); }
inline Bitboard bb4(Square s1, Square s2, Square s3, Square s4) { return (1ULL << s1) | (1ULL << s2) | (1ULL << s3) | (1ULL << s4); }

template<Square Delta>
inline Bitboard shift_bb(Bitboard b) {
	return  Delta == UP ? b << 8
		: Delta == DOWN ? b >> 8
		: Delta == UP_RIGHT ? (b & ~FileHBB) << 9
		: Delta == DOWN_RIGHT ? (b & ~FileHBB) >> 7
		: Delta == UP_LEFT ? (b & ~FileABB) << 7
		: Delta == DOWN_LEFT ? (b & ~FileABB) >> 9
		: 0;
}

inline Bitboard bb_adjacent_files(File f)
{
	return bbAdjacentFiles[f];
}

inline Bitboard bb_between(Square square1, Square square2)
{
	return bbBetween[square1][square2];
}

inline Bitboard bb_ranks_forward(Color color, Rank r)
{
	return bbRanksForward[color][r];
}

inline Bitboard bb_ranks_forward(Color color, Square square)
{
	return bbRanksForward[color][rank_of(square)];
}

inline Bitboard bb_forward(Color color, Square square)
{
	return bbForward[color][square];
}

inline Bitboard pawn_attack_range(Color color, Square square)
{
	return PawnAttackRange[color][square];
}

inline Bitboard passed_pawn_mask(Color color, Square square)
{
	return PassedPawnMask[color][square];
}

inline bool on_a_line(Square square1, Square square2, Square square3)
{
	return bbConnection[square1][square2] & square3;
}

inline int distance(Square x, Square y) { return SquareDistance[x][y]; }
inline int file_distance(Square x, Square y) { return abs(file_of(x) - file_of(y)); }
inline int rank_distance(Square x, Square y) { return abs(rank_of(x) - rank_of(y)); }

extern Bitboard* BishopAttackTable[64];
extern Bitboard BishopMask[64];
extern const Bitboard BishopMagics[64];

extern Bitboard* RookAttackTable[64];
extern Bitboard RookMask[64];
extern const Bitboard RookMagics[64];

inline Bitboard attack_bb_bishop(Square square, Bitboard occupied)
{
#ifdef USE_PEXT
	return BishopAttackTable[square][pext(occupied, BishopMask[square])];
#else
	return BishopAttackTable[square][((occupied & BishopMask[square]) * BishopMagics[square]) >> 55];
#endif
}

inline Bitboard attack_bb_rook(Square square, Bitboard occupied)
{
#ifdef USE_PEXT
	return RookAttackTable[square][pext(occupied, RookMask[square])];
#else
	return RookAttackTable[square][((occupied & RookMask[square]) * RookMagics[square]) >> 52];
#endif
}


inline Bitboard attack_bb(PieceType pieceT, Square square, Bitboard occupied)
{
	assert(pieceT != PAWN);

	switch (pieceT)
	{
	case BISHOP: return attack_bb_bishop(square, occupied);
	case ROOK: return attack_bb_rook(square, occupied);
	case QUEEN: return attack_bb_bishop(square, occupied) | attack_bb_rook(square, occupied);
	default: return EmptyAttack[pieceT][square];
	}
}

template<Color color>
inline Bitboard pawn_attack(Bitboard bb)
{
	if (color == WHITE)
		return shift_bb<UP_LEFT>(bb) | shift_bb<UP_RIGHT>(bb);
	else
		return shift_bb<DOWN_LEFT>(bb) | shift_bb<DOWN_RIGHT>(bb);
}

template<Color color>
inline Bitboard shift_up(Bitboard bb)
{
	if (color == WHITE)
		return shift_bb<UP>(bb);
	else
		return shift_bb<DOWN>(bb);
}

template<Color color>
inline Bitboard shift_down(Bitboard bb)
{
	if (color == WHITE)
		return shift_bb<DOWN>(bb);
	else
		return shift_bb<UP>(bb);
}

template<Color color>
inline Bitboard shift_upleft(Bitboard bb)
{
	if (color == WHITE)
		return shift_bb<UP_LEFT>(bb);
	else
		return shift_bb<DOWN_LEFT>(bb);
}

template<Color color>
inline Bitboard shift_upright(Bitboard bb)
{
	if (color == WHITE)
		return shift_bb<UP_RIGHT>(bb);
	else
		return shift_bb<DOWN_RIGHT>(bb);
}


#if defined(USE_POPCNT) && (defined(__INTEL_COMPILER) || defined(_MSC_VER))
#  include <nmmintrin.h> // voor _mm_popcnt_u64()
#endif

inline int popcount(Bitboard b)
{
#ifndef USE_POPCNT

	extern uint8_t PopCnt16[1 << 16];
	union { Bitboard bb; uint16_t u[4]; } v = { b };
	return PopCnt16[v.u[0]] + PopCnt16[v.u[1]] + PopCnt16[v.u[2]] + PopCnt16[v.u[3]];

#elif defined(_MSC_VER) || defined(__INTEL_COMPILER)

#ifdef IS_64BIT
	return (int)_mm_popcnt_u64(b);
#else
	return _mm_popcnt_u32((unsigned int)b)
		+ _mm_popcnt_u32((unsigned int)(b >> 32));
#endif

#else

#ifdef IS_64BIT
	return __builtin_popcountll(b);
#else
	return __builtin_popcountl((unsigned long)b)
		+ __builtin_popcountl((unsigned long)(b >> 32));
#endif

#endif
}


#if defined(__GNUC__)

inline Square lsb(Bitboard b)
{
  assert(b);
  return Square(__builtin_ctzll(b));
}

inline Square msb(Bitboard b)
{
  assert(b);
  return Square(63 ^ __builtin_clzll(b));
}

#elif defined(_WIN64) && defined(_MSC_VER)

#include <intrin.h> // voor _BitScanForward64()

inline Square lsb(Bitboard b)
{
  assert(b);
  unsigned long idx;
  _BitScanForward64(&idx, b);
  return (Square) idx;
}

inline Square msb(Bitboard b)
{
  assert(b);
  unsigned long idx;
  _BitScanReverse64(&idx, b);
  return (Square) idx;
}

#else

#define NO_BSF

Square lsb(Bitboard b);
Square msb(Bitboard b);

#endif


inline Square pop_lsb(Bitboard* b)
{
	const Square square = lsb(*b);
#ifdef USE_PEXT
	*b = blsr_u64(*b);
#else
	*b &= *b - 1;
#endif
	return square;
}

inline Square frontmost_square(Color color, Bitboard b) { return color == WHITE ? msb(b) : lsb(b); }
inline Square rearmost_square(Color color, Bitboard b) { return color == WHITE ? lsb(b) : msb(b); }

#endif // #ifndef BITBOARD_H
