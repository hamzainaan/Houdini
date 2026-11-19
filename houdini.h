/*
*/

#ifndef HOUDINI_H
#define HOUDINI_H

#include <cassert>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <string>

#if defined(_MSC_VER)
#	pragma warning(disable: 4127) // Conditional expression is constant
#	pragma warning(disable: 4146) // Unary minus operator applied to unsigned type
#	pragma warning(disable: 4800) // Forcing value to bool 'true' or 'false'
#	pragma warning(disable: 4996) // The function or variable may be unsafe
#else
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wparentheses"
#endif

//#define TRACE_LOG
#ifdef TRACE_LOG
void open_tracefile();
void close_tracefile();
void trace_do_move(int zet);
void trace_cancel_move();
void trace_eval(int waarde);
void trace_msg(const char *MSG, int diepte, int alfa, int beta, bool force = false);
void trace_reset_indent();
#else
inline void open_tracefile() {}
inline void close_tracefile() {}
inline void trace_do_move(int zet) {}
inline void trace_cancel_move() {}
inline void trace_eval(int waarde) {}
inline void trace_msg(const char *MSG, int diepte, int alfa, int beta, bool force = false) {}
#endif

//#define TRACE_TM
#ifdef TRACE_TM
void open_tracefile();
void close_tracefile();
void trace_tm_msg(int MoveNumber, int elapsed, int optimum, const char *MSG);
#endif

//#define PREMIUM
#ifdef PREMIUM
#   define USE_NALIMOV
#   define USE_LARGE_PAGES
#   define PRO_LICENSE
#endif

#if defined(PREMIUM) && defined(IS_64BIT)
#   define USE_NUMA
#endif

//#define USE_LICENSE
//#define CHESSBASE
//#define AQUARIUM
//#define IMPORT_TABELLEN
//#define EXPORT_TABELLEN

typedef uint64_t Key64;
typedef uint64_t Bitboard;

const int MAX_ZETTEN = 220;
const int MAX_PLY = 128;
const int MAX_PV = 63;

enum Move : unsigned int
{
	GEEN_ZET,
	NULL_ZET = 65
};

typedef uint16_t MoveCompact;

enum MoveType
{
	ROKADE     =  9 << 12,
	ENPASSANT  = 10 << 12,
	PROMOTIE_P = 11 << 12,
	PROMOTIE_L = 12 << 12,
	PROMOTIE_T = 13 << 12,
	PROMOTIE_D = 14 << 12
};

enum Color
{
	WIT, ZWART, KLEUR_N = 2
};

enum CastlingRight : uint8_t
{
	GEEN_ROKADE,
	WIT_KORT = 1,
	WIT_LANG = 2,
	ZWART_KORT = 4,
	ZWART_LANG = 8,
	ALLE_ROKADE = WIT_KORT | WIT_LANG | ZWART_KORT | ZWART_LANG,
	ROKADE_MOGELIJK_N = 16
};

enum GamePhase
{
	MIDDENSPEL_FASE = 26,
};

enum ScaleFactor
{
	REMISE_FACTOR = 0,
	EEN_PION_FACTOR = 75,
	NORMAAL_FACTOR = 100,
	MAX_FACTOR = 200,
	GEEN_FACTOR = 255
};

enum SortValue : int
{
	SORT_ZERO = 0,
	SORT_MAX = 999999
};

typedef int16_t SortValueCompact;

enum EvalValue : int
{
	EVAL_0 = 0,
	REMISE_EVAL = 0,
	GEEN_EVAL = 199999,
	PION_EVAL = 100 * 16,
	LOPER_EVAL = 360 * 16
};

enum MaterialValue : int
{  // eenheid = 8 cp
	MATL_0 = 0,
	MATL_PAARD = 41,
	MATL_LOPER = 42,
	MATL_TOREN = 64,
	MATL_DAME = 127,
};

enum SeeValue : int
{
	SEE_0 = 0,
	SEE_PION = 100,
	SEE_PAARD = 325,
	SEE_LOPER = 350,
	SEE_TOREN = 500,
	SEE_DAME = 950
};

enum Value : int  // in 1/200 pion
{
	WAARDE_0 = 0,
	WAARDE_1 = 1,
	REMISE_WAARDE = 0,
	WINST_WAARDE = 15000,
	EGTB_WIN_WAARDE = 28000,
	LANGSTE_MAT_WAARDE = 29800,   // geen 30000 omdat cutechess-cli dat dan als een matscore interpreteert
	MAT_WAARDE = 32700,
	HOOGSTE_WAARDE = 32750,
	GEEN_WAARDE = 32750,
};

#define DEFAULT_DRAW_VALUE Value(24)
#define WAARDE_TEMPO Value(24)
#define WAARDE_PION Value(200)

enum PieceType : uint8_t
{
	GEEN_STUKTYPE, KONING, PION, PAARD, LOPER, TOREN, DAME,
	ALLE_STUKKEN = 0, STUKKEN_ZONDER_KONING = 7,
	STUKTYPE_N = 8
};

enum Piece : uint8_t
{
	GEEN_STUK,
	W_KONING = 1, W_PION, W_PAARD, W_LOPER, W_TOREN, W_DAME,
	Z_KONING = 9, Z_PION, Z_PAARD, Z_LOPER, Z_TOREN, Z_DAME,
	STUK_N = 16
};

const MaterialValue MateriaalWaarden[STUK_N] =
{
	MATL_0, MATL_0, MATL_0, MATL_PAARD, MATL_LOPER, MATL_TOREN, MATL_DAME, MATL_0,
	MATL_0, MATL_0, MATL_0, MATL_PAARD, MATL_LOPER, MATL_TOREN, MATL_DAME, MATL_0
};

inline Value WaardeVanMateriaal(MaterialValue v) { return Value(16 * (int)v); }

const int StukFase[STUK_N] =
{
	0, 0, 0, 1, 1, 3, 6, 0,
	0, 0, 0, 1, 1, 3, 6, 0
};

const int MATERIAAL_HASH_GROOTTE = 16384;
const int PION_HASH_GROOTTE = 16384;

const int DEFAULT_BENCHMARK_DEPTH = 12;

enum Depth
{
	PLY = 8,
	MAIN_THREAD_INC = 9,
	OTHER_THREAD_INC = 9,

	DIEPTE_0 = 0,
	GEEN_DIEPTE = -6 * int(PLY),
	MAX_DIEPTE = MAX_PLY * int(PLY)
};

enum Square : int8_t
{
	SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
	SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
	SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
	SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
	SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
	SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
	SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
	SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
	SQ_NONE = 127,

	VELD_N = 64,

	BOVEN = 8,
	ONDER = -8,
	LINKS = -1,
	RECHTS = 1,

	RECHTSBOVEN = (int8_t)BOVEN + RECHTS,
	RECHTSONDER = (int8_t)ONDER + RECHTS,
	LINKSONDER = (int8_t)ONDER + LINKS,
	LINKSBOVEN = (int8_t)BOVEN + LINKS
};

enum File
{
	LIJN_A, LIJN_B, LIJN_C, LIJN_D, LIJN_E, LIJN_F, LIJN_G, LIJN_H, LIJN_N
};

enum Rank
{
	RIJ_1, RIJ_2, RIJ_3, RIJ_4, RIJ_5, RIJ_6, RIJ_7, RIJ_8, RIJ_N
};

enum PickMoveStage
{
	NORMAAL_ZOEKEN, GOEDE_SLAGEN_GEN, GOEDE_SLAGEN, KILLERS, KILLERS1, LxP_SLAGEN_GEN, LxP_SLAGEN,
		/*RUSTIGE_ZETTEN_GEN,*/ RUSTIGE_ZETTEN, SLECHTE_SLAGEN, UITGESTELDE_ZETTEN,
	GA_UIT_SCHAAK, UIT_SCHAAK_GEN, UIT_SCHAAK_LUS,
	QSEARCH_MET_SCHAAK, QSEARCH_1, QSEARCH_1_SLAGZETTEN, QSEARCH_SCHAAKZETTEN,
	QSEARCH_ZONDER_SCHAAK, QSEARCH_2, QSEARCH_2_SLAGZETTEN,
	//QSEARCH_MET_DAMESCHAAK, QSEARCH_3, QSEARCH_3_SLAGZETTEN, QSEARCH_DAMESCHAAKZETTEN,
	PROBCUT, PROBCUT_GEN, PROBCUT_SLAGZETTEN,
	TERUGSLAG_GEN, TERUGSLAG_ZETTEN
};

enum Score : int
{
	SCORE_ZERO
};

inline int ScoreMultiply(int x) { return x * 8 / 5; }
inline Value ValueFromEval(EvalValue v) { return Value(v / 8); }

inline Score maak_score(int mg, int eg)
{
	return Score((ScoreMultiply(mg) << 16) + ScoreMultiply(eg));
}

inline Score remake_score(EvalValue mg, EvalValue eg)
{
	return Score(((int)mg << 16) + (int)eg);
}

inline EvalValue mg_waarde(Score score)
{
	union { uint16_t u; int16_t s; } mg = { uint16_t(unsigned(score + 0x8000) >> 16) };
	return EvalValue(mg.s);
}

inline EvalValue eg_waarde(Score score)
{
	union { uint16_t u; int16_t s; } eg = { uint16_t(unsigned(score)) };
	return EvalValue(eg.s);
}

#define ENABLE_BASE_OPERATORS_ON(T)                             \
inline T operator+(T d1, T d2) { return T(int(d1) + int(d2)); } \
inline T operator-(T d1, T d2) { return T(int(d1) - int(d2)); } \
inline T operator*(int i, T d) { return T(i * int(d)); }        \
inline T operator*(T d, int i) { return T(int(d) * i); }        \
inline T operator*(unsigned int i, T d) { return T(i * int(d)); }        \
inline T operator*(T d, unsigned int i) { return T(int(d) * i); }        \
inline T operator-(T d) { return T(-int(d)); }                  \
inline T& operator+=(T& d1, T d2) { return d1 = d1 + d2; }      \
inline T& operator-=(T& d1, T d2) { return d1 = d1 - d2; }      \
inline T& operator*=(T& d, int i) { return d = T(int(d) * i); }

#define ENABLE_FULL_OPERATORS_ON(T)                             \
ENABLE_BASE_OPERATORS_ON(T)                                     \
inline T& operator++(T& d) { return d = T(int(d) + 1); }        \
inline T& operator--(T& d) { return d = T(int(d) - 1); }        \
inline T operator/(T d, int i) { return T(int(d) / i); }        \
inline int operator/(T d1, T d2) { return int(d1) / int(d2); }  \
inline T& operator/=(T& d, int i) { return d = T(int(d) / i); }

ENABLE_FULL_OPERATORS_ON(Value)
ENABLE_FULL_OPERATORS_ON(PieceType)
ENABLE_FULL_OPERATORS_ON(Piece)
ENABLE_FULL_OPERATORS_ON(Color)
ENABLE_FULL_OPERATORS_ON(Depth)
ENABLE_FULL_OPERATORS_ON(Square)
ENABLE_FULL_OPERATORS_ON(File)
ENABLE_FULL_OPERATORS_ON(Rank)
ENABLE_FULL_OPERATORS_ON(SortValue)
ENABLE_FULL_OPERATORS_ON(EvalValue)

ENABLE_BASE_OPERATORS_ON(Score)
ENABLE_BASE_OPERATORS_ON(MaterialValue)
ENABLE_BASE_OPERATORS_ON(SeeValue)

#undef ENABLE_FULL_OPERATORS_ON
#undef ENABLE_BASE_OPERATORS_ON

inline Score operator*(Score s1, Score s2);

inline Score operator/(Score score, int i)
{
	return remake_score(mg_waarde(score) / i, eg_waarde(score) / i);
}

inline Score score_muldiv(Score score, int mul, int div)
{
	return remake_score(mg_waarde(score) * mul / div, eg_waarde(score) * mul / div);
}

inline Color operator~(Color kleur)
{
	return Color(kleur ^ 1);
}

inline Square operator~(Square veld)
{
	return Square(veld ^ 56);
}

inline Value geeft_mat(int ply)
{
	return Value(MAT_WAARDE - ply);
}

inline Value staat_mat(int ply)
{
	return Value(-MAT_WAARDE + ply);
}

inline Square maak_veld(File f, Rank r)
{
	return Square((r << 3) + f);
}

inline Piece maak_stuk(Color kleur, PieceType stuk)
{
	return Piece((kleur << 3) + stuk);
}

inline PieceType stuk_type(Piece stuk)
{
	return PieceType(stuk & 7);
}

inline Color stuk_kleur(Piece stuk)
{
	assert(stuk != GEEN_STUK);
	return Color(stuk >> 3);
}

inline File lijn(Square veld)
{
	return File(veld & 7);
}

inline Rank rij(Square veld)
{
	return Rank(veld >> 3);
}

inline Square relatief_veld(Color kleur, Square veld)
{
	return Square(veld ^ (kleur * 56));
}

inline Rank relatieve_rij(Color kleur, Rank r)
{
	return Rank(r ^ (kleur * 7));
}

inline Rank relatieve_rij(Color kleur, Square veld)
{
	return relatieve_rij(kleur, rij(veld));
}

inline bool verschillende_kleur(Square v1, Square v2)
{
	int v = int(v1) ^ int(v2);
	return ((v >> 3) ^ v) & 1;
}

inline Square pion_vooruit(Color kleur)
{
	return kleur == WIT ? BOVEN : ONDER;
}

inline Square veld_voor(Color kleur, Square veld)
{
	return kleur == WIT ? veld + BOVEN : veld + ONDER;
}

inline Square veld_achter(Color kleur, Square veld)
{
	return kleur == WIT ? veld - BOVEN : veld - ONDER;
}

inline Square van_veld(Move zet)
{
	return Square((zet >> 6) & 0x3F);
}

inline Square naar_veld(Move zet)
{
	return Square(zet & 0x3F);
}

inline MoveType zet_type(Move zet)
{
	return MoveType(zet & (15 << 12));
}

inline PieceType promotie_stuk(Move zet)
{
	return PieceType(PAARD + (((zet >> 12) & 15) - (PROMOTIE_P >> 12)));
}

inline PieceType stuk_van_zet(Move zet)
{
	assert(zet < Move(ROKADE));
	return PieceType((zet >> 12) & 7);
}

inline Move maak_zet(PieceType stuk, Square van, Square naar)
{
	//return Zet(naar + (van << 6) + (stuk << 12));
	return Move(naar + (van << 6));
}

inline Move maak_zet(MoveType type, Square van, Square naar)
{
	return Move(naar + (van << 6) + type);
}

inline bool is_ok(Move zet)
{
	return zet != GEEN_ZET && zet != NULL_ZET;
}

template <int Capacity>
struct FixedMoveList
{
	int zetNummer;
	Move zetten[Capacity];

	FixedMoveList() : zetNummer(0) {}
	void add(Move zet) { if (zetNummer < Capacity) zetten[zetNummer++] = zet; }
	Move& operator[](int index) { return zetten[index]; }
	const Move& operator[](int index) const { return zetten[index]; }
	int size() const { return zetNummer; }
	void resize(int newSize) { zetNummer = newSize; }
	void clear() { zetNummer = 0; }
	bool empty() { return zetNummer == 0; }

	int find(Move zet)
	{
		for (int i = 0; i < zetNummer; i++)
			if (zetten[i] == zet)
				return i;
		return -1;
	}
};

typedef FixedMoveList<MAX_PV> PrincipalVariation;
typedef FixedMoveList<MAX_ZETTEN> MaxMoveList;

#endif // #ifndef HOUDINI_H
