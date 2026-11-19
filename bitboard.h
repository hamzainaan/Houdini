/*
*/

#ifndef BITBOARD_H
#define BITBOARD_H

#include "houdini.h"

#ifdef USE_PEXT
#  include <immintrin.h> // voor _pext_u64()
#  define pext(bezet, mask) _pext_u64(bezet, mask)
#  define blsr_u64(b) _blsr_u64(b)
#endif

namespace Bitbases
{
	bool probe(Square wksq, Square wpsq, Square bksq, Color us);
}

namespace Bitboards
{
	void initialisatie();
}

const Bitboard DonkereVelden = 0xAA55AA55AA55AA55ULL;

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

extern int8_t VeldAfstand[VELD_N][VELD_N];

extern Bitboard bbVeld[VELD_N];
const Bitboard bbLijn[LIJN_N] = { FileABB , FileBBB, FileCBB, FileDBB, FileEBB, FileFBB, FileGBB, FileHBB };
const Bitboard bbRij[RIJ_N] = { Rank1BB, Rank2BB, Rank3BB, Rank4BB, Rank5BB, Rank6BB, Rank7BB, Rank8BB };
extern Bitboard bbAangrenzendeLijnen[LIJN_N];
extern Bitboard bbRijenVoorwaarts[KLEUR_N][RIJ_N];
extern Bitboard bbTussen[VELD_N][VELD_N];
extern Bitboard bbVerbinding[VELD_N][VELD_N];
extern Bitboard bbVoorwaarts[KLEUR_N][VELD_N];
extern Bitboard VrijpionMask[KLEUR_N][VELD_N];
extern Bitboard PionAanvalBereik[KLEUR_N][VELD_N];
extern Bitboard PionAanval[KLEUR_N][VELD_N];
extern Bitboard LegeAanval[STUKTYPE_N][VELD_N];
extern Bitboard KoningZone[VELD_N];


#define USE_BB_SHIFT
#ifdef USE_BB_SHIFT
  #define bb(veld) (1ULL << veld)
#else
  #define bb(veld) bbVeld[veld]
#endif

inline Bitboard operator&(Bitboard b, Square veld)
{
	return b & bb(veld);
}

inline Bitboard operator|(Bitboard b, Square veld)
{
	return b | bb(veld);
}

inline Bitboard operator^(Bitboard b, Square veld)
{
	return b ^ bb(veld);
}

inline Bitboard& operator|=(Bitboard& b, Square veld)
{
	return b |= bb(veld);
}

inline Bitboard& operator^=(Bitboard& b, Square veld)
{
	return b ^= bb(veld);
}

inline bool meer_dan_een(Bitboard b)
{
#ifdef USE_PEXT
	return blsr_u64(b);
#else
	return b & (b - 1);
#endif
}

#undef bb

inline Bitboard bb_rij(Square veld)
{
	return bbRij[rij(veld)];
}

inline Bitboard bb_lijn(Square veld)
{
	return bbLijn[lijn(veld)];
}

inline Bitboard bb_lijn(File lijn)
{
	return bbLijn[lijn];
}

inline Bitboard bb(Square s) { return (1ULL << s); }
inline Bitboard bb2(Square s1, Square s2) { return (1ULL << s1) | (1ULL << s2); }
inline Bitboard bb3(Square s1, Square s2, Square s3) { return (1ULL << s1) | (1ULL << s2) | (1ULL << s3); }
inline Bitboard bb4(Square s1, Square s2, Square s3, Square s4) { return (1ULL << s1) | (1ULL << s2) | (1ULL << s3) | (1ULL << s4); }

template<Square Delta>
inline Bitboard shift_bb(Bitboard b) {
	return  Delta == BOVEN ? b << 8
		: Delta == ONDER ? b >> 8
		: Delta == RECHTSBOVEN ? (b & ~FileHBB) << 9
		: Delta == RECHTSONDER ? (b & ~FileHBB) >> 7
		: Delta == LINKSBOVEN ? (b & ~FileABB) << 7
		: Delta == LINKSONDER ? (b & ~FileABB) >> 9
		: 0;
}

inline Bitboard bb_aangrenzende_lijnen(File f)
{
	return bbAangrenzendeLijnen[f];
}

inline Bitboard bb_tussen(Square veld1, Square veld2)
{
	return bbTussen[veld1][veld2];
}

inline Bitboard bb_rijen_voorwaarts(Color kleur, Rank r)
{
	return bbRijenVoorwaarts[kleur][r];
}

inline Bitboard bb_rijen_voorwaarts(Color kleur, Square veld)
{
	return bbRijenVoorwaarts[kleur][rij(veld)];
}

inline Bitboard bb_voorwaarts(Color kleur, Square veld)
{
	return bbVoorwaarts[kleur][veld];
}

inline Bitboard pion_aanval_bereik(Color kleur, Square veld)
{
	return PionAanvalBereik[kleur][veld];
}

inline Bitboard vrijpion_mask(Color kleur, Square veld)
{
	return VrijpionMask[kleur][veld];
}

inline bool op_een_lijn(Square veld1, Square veld2, Square veld3)
{
	return bbVerbinding[veld1][veld2] & veld3;
}

inline int afstand(Square x, Square y) { return VeldAfstand[x][y]; }
inline int lijn_afstand(Square x, Square y) { return abs(lijn(x) - lijn(y)); }
inline int rij_afstand(Square x, Square y) { return abs(rij(x) - rij(y)); }

extern Bitboard* LoperAanvalTabel[64];
extern Bitboard LoperMask[64];
extern const Bitboard LoperMagics[64];

extern Bitboard* TorenAanvalTabel[64];
extern Bitboard TorenMask[64];
extern const Bitboard TorenMagics[64];

inline Bitboard aanval_bb_loper(Square veld, Bitboard bezet)
{
#ifdef USE_PEXT
	return LoperAanvalTabel[veld][pext(bezet, LoperMask[veld])];
#else
	return LoperAanvalTabel[veld][((bezet & LoperMask[veld]) * LoperMagics[veld]) >> 55];
#endif
}

inline Bitboard aanval_bb_toren(Square veld, Bitboard bezet)
{
#ifdef USE_PEXT
	return TorenAanvalTabel[veld][pext(bezet, TorenMask[veld])];
#else
	return TorenAanvalTabel[veld][((bezet & TorenMask[veld]) * TorenMagics[veld]) >> 52];
#endif
}


inline Bitboard aanval_bb(PieceType stukT, Square veld, Bitboard bezet)
{
	assert(stukT != PION);

	switch (stukT)
	{
	case LOPER: return aanval_bb_loper(veld, bezet);
	case TOREN: return aanval_bb_toren(veld, bezet);
	case DAME: return aanval_bb_loper(veld, bezet) | aanval_bb_toren(veld, bezet);
	default: return LegeAanval[stukT][veld];
	}
}

template<Color kleur>
inline Bitboard pion_aanval(Bitboard bb)
{
	if (kleur == WIT)
		return shift_bb<LINKSBOVEN>(bb) | shift_bb<RECHTSBOVEN>(bb);
	else
		return shift_bb<LINKSONDER>(bb) | shift_bb<RECHTSONDER>(bb);
}

template<Color kleur>
inline Bitboard shift_up(Bitboard bb)
{
	if (kleur == WIT)
		return shift_bb<BOVEN>(bb);
	else
		return shift_bb<ONDER>(bb);
}

template<Color kleur>
inline Bitboard shift_down(Bitboard bb)
{
	if (kleur == WIT)
		return shift_bb<ONDER>(bb);
	else
		return shift_bb<BOVEN>(bb);
}

template<Color kleur>
inline Bitboard shift_upleft(Bitboard bb)
{
	if (kleur == WIT)
		return shift_bb<LINKSBOVEN>(bb);
	else
		return shift_bb<LINKSONDER>(bb);
}

template<Color kleur>
inline Bitboard shift_upright(Bitboard bb)
{
	if (kleur == WIT)
		return shift_bb<RECHTSBOVEN>(bb);
	else
		return shift_bb<RECHTSONDER>(bb);
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
	const Square veld = lsb(*b);
#ifdef USE_PEXT
	*b = blsr_u64(*b);
#else
	*b &= *b - 1;
#endif
	return veld;
}

inline Square voorste_veld(Color kleur, Bitboard b) { return kleur == WIT ? msb(b) : lsb(b); }
inline Square achterste_veld(Color kleur, Bitboard b) { return kleur == WIT ? lsb(b) : msb(b); }

#endif // #ifndef BITBOARD_H
