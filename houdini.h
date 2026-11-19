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
void trace_do_move(int move);
void trace_cancel_move();
void trace_eval(int value);
void trace_msg(const char *MSG, int depth, int alpha, int beta, bool force = false);
void trace_reset_indent();
#else
inline void open_tracefile() {}
inline void close_tracefile() {}
inline void trace_do_move(int move) {}
inline void trace_cancel_move() {}
inline void trace_eval(int value) {}
inline void trace_msg(const char *MSG, int depth, int alpha, int beta, bool force = false) {}
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

const int MAX_MOVES = 220;
const int MAX_PLY = 128;
const int MAX_PV = 63;

enum Move : unsigned int
{
	NO_MOVE,
	NULL_MOVE = 65
};

typedef uint16_t MoveCompact;

enum MoveType
{
	CASTLING     =  9 << 12,
	ENPASSANT  = 10 << 12,
	PROMOTION_N = 11 << 12,
	PROMOTION_B = 12 << 12,
	PROMOTION_R = 13 << 12,
	PROMOTION_Q = 14 << 12
};

enum Color
{
	WHITE, BLACK, COLOR_N = 2
};

enum CastlingRight : uint8_t
{
	NO_CASTLING,
	WHITE_SHORT = 1,
	WHITE_LONG = 2,
	BLACK_SHORT = 4,
	BLACK_LONG = 8,
	ALL_CASTLING = WHITE_SHORT | WHITE_LONG | BLACK_SHORT | BLACK_LONG,
	CASTLING_RIGHT_N = 16
};

enum GamePhase
{
	MIDDLEGAME_PHASE = 26,
};

enum ScaleFactor
{
	DRAW_FACTOR = 0,
	ONE_PAWN_FACTOR = 75,
	NORMAL_FACTOR = 100,
	MAX_FACTOR = 200,
	NO_FACTOR = 255
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
	DRAW_EVAL = 0,
	NO_EVAL = 199999,
	PAWN_EVAL = 100 * 16,
	BISHOP_EVAL = 360 * 16
};

enum MaterialValue : int
{  // eenheid = 8 cp
	MATL_0 = 0,
	MATL_KNIGHT = 41,
	MATL_BISHOP = 42,
	MATL_ROOK = 64,
	MATL_QUEEN = 127,
};

enum SeeValue : int
{
	SEE_0 = 0,
	SEE_PAWN = 100,
	SEE_KNIGHT = 325,
	SEE_BISHOP = 350,
	SEE_ROOK = 500,
	SEE_QUEEN = 950
};

enum Value : int  // in 1/200 pion
{
	VALUE_0 = 0,
	VALUE_1 = 1,
	DRAW_VALUE = 0,
	WIN_VALUE = 15000,
	EGTB_WIN_VALUE = 28000,
	LONGEST_MATE_VALUE = 29800,   // geen 30000 omdat cutechess-cli dat dan als een matscore interpreteert
	MATE_VALUE = 32700,
	HIGHEST_VALUE = 32750,
	NO_VALUE = 32750,
};

#define DEFAULT_DRAW_VALUE Value(24)
#define VALUE_TEMPO Value(24)
#define VALUE_PAWN Value(200)

enum PieceType : uint8_t
{
	NO_PIECE_TYPE, KING, PAWN, KNIGHT, BISHOP, ROOK, QUEEN,
	ALL_PIECES = 0, PIECES_WITHOUT_KING = 7,
	PIECE_TYPE_N = 8
};

enum Piece : uint8_t
{
	NO_PIECE,
	W_KING = 1, W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN,
	B_KING = 9, B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN,
	PIECE_N = 16
};

const MaterialValue MaterialValues[PIECE_N] =
{
	MATL_0, MATL_0, MATL_0, MATL_KNIGHT, MATL_BISHOP, MATL_ROOK, MATL_QUEEN, MATL_0,
	MATL_0, MATL_0, MATL_0, MATL_KNIGHT, MATL_BISHOP, MATL_ROOK, MATL_QUEEN, MATL_0
};

inline Value ValueFromMaterial(MaterialValue v) { return Value(16 * (int)v); }

const int PiecePhase[PIECE_N] =
{
	0, 0, 0, 1, 1, 3, 6, 0,
	0, 0, 0, 1, 1, 3, 6, 0
};

const int MATERIAL_HASH_SIZE = 16384;
const int PAWN_HASH_SIZE = 16384;

const int DEFAULT_BENCHMARK_DEPTH = 12;

enum Depth
{
	PLY = 8,
	MAIN_THREAD_INC = 9,
	OTHER_THREAD_INC = 9,

	DEPTH_0 = 0,
	NO_DEPTH = -6 * int(PLY),
	MAX_DEPTH = MAX_PLY * int(PLY)
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

	SQUARE_N = 64,

	UP = 8,
	DOWN = -8,
	LEFT = -1,
	RIGHT = 1,

	UP_RIGHT = (int8_t)UP + RIGHT,
	DOWN_RIGHT = (int8_t)DOWN + RIGHT,
	DOWN_LEFT = (int8_t)DOWN + LEFT,
	UP_LEFT = (int8_t)UP + LEFT
};

enum File
{
	FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H, FILE_N
};

enum Rank
{
	RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, RANK_N
};

enum PickMoveStage
{
	NORMAL_SEARCH, GOOD_CAPTURES_GEN, GOOD_CAPTURES, KILLERS, KILLERS1, BxN_CAPTURES_GEN, BxN_CAPTURES,
		/*RUSTIGE_ZETTEN_GEN,*/ QUIET_MOVES, BAD_CAPTURES, POSTPONED_MOVES,
	EVADE_CHECK, EVASIONS_GEN, EVASIONS_LOOP,
	QSEARCH_WITH_CHECKS, QSEARCH_1, QSEARCH_1_CAPTURES, QSEARCH_CHECKS,
	QSEARCH_WITHOUT_CHECKS, QSEARCH_2, QSEARCH_2_CAPTURES,
	//QSEARCH_MET_DAMESCHAAK, QSEARCH_3, QSEARCH_3_SLAGZETTEN, QSEARCH_DAMESCHAAKZETTEN,
	PROBCUT, PROBCUT_GEN, PROBCUT_CAPTURES,
	RECAPTURE_GEN, RECAPTURE_MOVES
};

enum Score : int
{
	SCORE_ZERO
};

inline int ScoreMultiply(int x) { return x * 8 / 5; }
inline Value ValueFromEval(EvalValue v) { return Value(v / 8); }

inline Score make_score(int mg, int eg)
{
	return Score((ScoreMultiply(mg) << 16) + ScoreMultiply(eg));
}

inline Score remake_score(EvalValue mg, EvalValue eg)
{
	return Score(((int)mg << 16) + (int)eg);
}

inline EvalValue mg_value(Score score)
{
	union { uint16_t u; int16_t s; } mg = { uint16_t(unsigned(score + 0x8000) >> 16) };
	return EvalValue(mg.s);
}

inline EvalValue eg_value(Score score)
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
	return remake_score(mg_value(score) / i, eg_value(score) / i);
}

inline Score score_muldiv(Score score, int mul, int div)
{
	return remake_score(mg_value(score) * mul / div, eg_value(score) * mul / div);
}

inline Color operator~(Color color)
{
	return Color(color ^ 1);
}

inline Square operator~(Square square)
{
	return Square(square ^ 56);
}

inline Value mate_in(int ply)
{
	return Value(MATE_VALUE - ply);
}

inline Value mated_in(int ply)
{
	return Value(-MATE_VALUE + ply);
}

inline Square make_square(File f, Rank r)
{
	return Square((r << 3) + f);
}

inline Piece make_piece(Color color, PieceType piece)
{
	return Piece((color << 3) + piece);
}

inline PieceType piece_type(Piece piece)
{
	return PieceType(piece & 7);
}

inline Color piece_color(Piece piece)
{
	assert(piece != NO_PIECE);
	return Color(piece >> 3);
}

inline File file_of(Square square)
{
	return File(square & 7);
}

inline Rank rank_of(Square square)
{
	return Rank(square >> 3);
}

inline Square relative_square(Color color, Square square)
{
	return Square(square ^ (color * 56));
}

inline Rank relative_rank(Color color, Rank r)
{
	return Rank(r ^ (color * 7));
}

inline Rank relative_rank(Color color, Square square)
{
	return relative_rank(color, rank_of(square));
}

inline bool different_color(Square v1, Square v2)
{
	int v = int(v1) ^ int(v2);
	return ((v >> 3) ^ v) & 1;
}

inline Square pawn_forward(Color color)
{
	return color == WHITE ? UP : DOWN;
}

inline Square square_in_front(Color color, Square square)
{
	return color == WHITE ? square + UP : square + DOWN;
}

inline Square square_behind(Color color, Square square)
{
	return color == WHITE ? square - UP : square - DOWN;
}

inline Square from_square(Move move)
{
	return Square((move >> 6) & 0x3F);
}

inline Square to_square(Move move)
{
	return Square(move & 0x3F);
}

inline MoveType move_type(Move move)
{
	return MoveType(move & (15 << 12));
}

inline PieceType promotion_piece(Move move)
{
	return PieceType(KNIGHT + (((move >> 12) & 15) - (PROMOTION_N >> 12)));
}

inline PieceType piece_of_move(Move move)
{
	assert(move < Move(CASTLING));
	return PieceType((move >> 12) & 7);
}

inline Move make_move(PieceType piece, Square from, Square to)
{
	//return Zet(naar + (van << 6) + (stuk << 12));
	return Move(to + (from << 6));
}

inline Move make_move(MoveType type, Square from, Square to)
{
	return Move(to + (from << 6) + type);
}

inline bool is_ok(Move move)
{
	return move != NO_MOVE && move != NULL_MOVE;
}

template <int Capacity>
struct FixedMoveList
{
	int moveNumber;
	Move moves[Capacity];

	FixedMoveList() : moveNumber(0) {}
	void add(Move move) { if (moveNumber < Capacity) moves[moveNumber++] = move; }
	Move& operator[](int index) { return moves[index]; }
	const Move& operator[](int index) const { return moves[index]; }
	int size() const { return moveNumber; }
	void resize(int newSize) { moveNumber = newSize; }
	void clear() { moveNumber = 0; }
	bool empty() { return moveNumber == 0; }

	int find(Move move)
	{
		for (int i = 0; i < moveNumber; i++)
			if (moves[i] == move)
				return i;
		return -1;
	}
};

typedef FixedMoveList<MAX_PV> PrincipalVariation;
typedef FixedMoveList<MAX_MOVES> MaxMoveList;

#endif // #ifndef HOUDINI_H
