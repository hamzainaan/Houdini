/*
*/

#ifndef MOVE_SELECTION_H
#define MOVE_SELECTION_H

#include "houdini.h"
#include "move_generation.h"
#include "position.h"

template<typename T>
struct PieceSquareTable
{
	const T* operator[](Piece piece) const { return table[piece]; }
	T* operator[](Piece piece) { return table[piece]; }
	void clear() { std::memset(table, 0, sizeof(table)); }

	T get(Piece piece, Square to) { return table[piece][to]; }
	void update(Piece piece, Square to, T v)
	{
		table[piece][to] = v;
	}

protected:
	T table[PIECE_N][SQUARE_N];
};


template<int MaxPlus, int MaxMin>
struct PieceSquareStats : PieceSquareTable<SortValueCompact>
{
	static int compute_offset(Piece piece, Square to) { return 64 * int(piece) + int(to); }
	SortValueCompact valueAtOffset(int offset) const { return *((const SortValueCompact*)table + offset); }

	void fill(SortValue v)
	{
		int vv = (uint16_t(v) << 16) | uint16_t(v);
		std::fill((int *)table, (int *)((char *)table + sizeof(table)), vv);
	}

	void updatePlus(int offset, SortValue v)
	{
		//v = v * 2 * MaxPlus / (MaxPlus + MaxMin);
		SortValueCompact& elem = *((SortValueCompact*)table + offset);
		elem -= elem * int(v) / MaxPlus;
		elem += v;
	}

	void updateMinus(int offset, SortValue v)
	{
		//v = v * 2 * MaxMin / (MaxPlus + MaxMin);
		SortValueCompact& elem = *((SortValueCompact*)table + offset);
		elem -= elem * int(v) / MaxMin;
		elem -= v;
	}
};


template<typename T>
struct ColorSquareStats
{
	const T* operator[](Color color) const { return table[color]; }
	T* operator[](Color color) { return table[color]; }
	void clear() { std::memset(table, 0, sizeof(table)); }

private:
	T table[COLOR_N][SQUARE_N];
};


struct CounterMoveFullStats
{
	Move get(Color color, Move move) { return Move(table[color][move & 0xfff]); }
	void clear() { std::memset(table, 0, sizeof(table)); }
	void update(Color color, Move move1, Move move)
	{
		table[color][move1 & 0xfff] = move;
	}

private:
	MoveCompact table[COLOR_N][64 * 64];
};


struct CounterMoveFullPieceStats
{
	Move get(Piece piece, Move move) { return Move(table[piece][move & 0xfff]); }
	void clear() { std::memset(table, 0, sizeof(table)); }
	void update(Piece piece, Move move1, Move move)
	{
		table[piece][move1 & 0xfff] = move;
	}

private:
	MoveCompact table[PIECE_N][64 * 64];
};


struct CounterFollowUpMoveStats
{
	Move get(Piece piece1, Square to1, Piece piece2, Square to2) { return Move(table[piece1][to1][piece_type(piece2)][to2]); }
	void clear() { std::memset(table, 0, sizeof(table)); }
	void update(Piece piece1, Square to1, Piece piece2, Square to2, Move move)
	{
		table[piece1][to1][piece_type(piece2)][to2] = move;
	}

private:
	MoveCompact table[PIECE_N][SQUARE_N][PIECE_TYPE_N][SQUARE_N];
};


struct MaxGainStats
{
	Value get(Piece piece, Move move) const { return table[piece][move & 0x0fff]; }
	void clear() { std::memset(table, 0, sizeof(table)); }
	void update(Piece piece, Move move, Value gain) 
	{
		Value* pGain = &table[piece][move & 0x0fff];
		*pGain += Value((gain - *pGain + 8) >> 4);
	}

private:
	Value table[PIECE_N][64 * 64];
};


struct SpecialKillerStats
{
	static int index_our_pieces(const Position &pos, Color color);
	static int index_their_pieces(const Position &pos, Color color, Square square);

	Move get(Color color, int index) const { return table[color][index]; }
	void clear() { std::memset(table, 0, sizeof(table)); }
	void update(Color color, int index, Move move) 
	{
		table[color][index] = move;
	}
private:
	Move table[COLOR_N][65536];
};


typedef PieceSquareTable<MoveCompact> CounterMoveStats;
typedef PieceSquareStats<8192, 8192> MoveValueStats;
typedef PieceSquareStats<3*8192, 3*8192> CounterMoveValues;
typedef PieceSquareTable<CounterMoveValues> CounterMoveHistory;


inline PickMoveStage& operator++(PickMoveStage& d) { return d = PickMoveStage(int(d) + 1); }

namespace MoveSelection 
{
	void init_search(const Position&, Move, Depth, bool);
	void init_qsearch(const Position&, Move, Depth, Square);
	void init_probcut(const Position&, Move, SeeValue);

	Move next_move(const Position& pos);

	template<MoveGeneration> void score(const Position& pos);
}

#endif // #ifndef MOVE_SELECTION_H
