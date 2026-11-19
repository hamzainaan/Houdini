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
	const T* operator[](Piece stuk) const { return tabel[stuk]; }
	T* operator[](Piece stuk) { return tabel[stuk]; }
	void clear() { std::memset(tabel, 0, sizeof(tabel)); }

	T get(Piece stuk, Square naar) { return tabel[stuk][naar]; }
	void update(Piece stuk, Square naar, T v)
	{
		tabel[stuk][naar] = v;
	}

protected:
	T tabel[PIECE_N][SQUARE_N];
};


template<int MaxPlus, int MaxMin>
struct PieceSquareStats : PieceSquareTable<SortValueCompact>
{
	static int compute_offset(Piece stuk, Square naar) { return 64 * int(stuk) + int(naar); }
	SortValueCompact valueAtOffset(int offset) const { return *((const SortValueCompact*)tabel + offset); }

	void fill(SortValue v)
	{
		int vv = (uint16_t(v) << 16) | uint16_t(v);
		std::fill((int *)tabel, (int *)((char *)tabel + sizeof(tabel)), vv);
	}

	void updatePlus(int offset, SortValue v)
	{
		//v = v * 2 * MaxPlus / (MaxPlus + MaxMin);
		SortValueCompact& elem = *((SortValueCompact*)tabel + offset);
		elem -= elem * int(v) / MaxPlus;
		elem += v;
	}

	void updateMinus(int offset, SortValue v)
	{
		//v = v * 2 * MaxMin / (MaxPlus + MaxMin);
		SortValueCompact& elem = *((SortValueCompact*)tabel + offset);
		elem -= elem * int(v) / MaxMin;
		elem -= v;
	}
};


template<typename T>
struct ColorSquareStats
{
	const T* operator[](Color kleur) const { return tabel[kleur]; }
	T* operator[](Color kleur) { return tabel[kleur]; }
	void clear() { std::memset(tabel, 0, sizeof(tabel)); }

private:
	T tabel[COLOR_N][SQUARE_N];
};


struct CounterMoveFullStats
{
	Move get(Color kleur, Move zet) { return Move(tabel[kleur][zet & 0xfff]); }
	void clear() { std::memset(tabel, 0, sizeof(tabel)); }
	void update(Color kleur, Move zet1, Move zet)
	{
		tabel[kleur][zet1 & 0xfff] = zet;
	}

private:
	MoveCompact tabel[COLOR_N][64 * 64];
};


struct CounterMoveFullPieceStats
{
	Move get(Piece stuk, Move zet) { return Move(tabel[stuk][zet & 0xfff]); }
	void clear() { std::memset(tabel, 0, sizeof(tabel)); }
	void update(Piece stuk, Move zet1, Move zet)
	{
		tabel[stuk][zet1 & 0xfff] = zet;
	}

private:
	MoveCompact tabel[PIECE_N][64 * 64];
};


struct CounterFollowUpMoveStats
{
	Move get(Piece stuk1, Square naar1, Piece stuk2, Square naar2) { return Move(tabel[stuk1][naar1][piece_type(stuk2)][naar2]); }
	void clear() { std::memset(tabel, 0, sizeof(tabel)); }
	void update(Piece stuk1, Square naar1, Piece stuk2, Square naar2, Move zet)
	{
		tabel[stuk1][naar1][piece_type(stuk2)][naar2] = zet;
	}

private:
	MoveCompact tabel[PIECE_N][SQUARE_N][PIECE_TYPE_N][SQUARE_N];
};


struct MaxGainStats
{
	Value get(Piece stuk, Move zet) const { return tabel[stuk][zet & 0x0fff]; }
	void clear() { std::memset(tabel, 0, sizeof(tabel)); }
	void update(Piece stuk, Move zet, Value gain) 
	{
		Value* pGain = &tabel[stuk][zet & 0x0fff];
		*pGain += Value((gain - *pGain + 8) >> 4);
	}

private:
	Value tabel[PIECE_N][64 * 64];
};


struct SpecialKillerStats
{
	static int index_our_pieces(const Position &pos, Color kleur);
	static int index_their_pieces(const Position &pos, Color kleur, Square veld);

	Move get(Color kleur, int index) const { return tabel[kleur][index]; }
	void clear() { std::memset(tabel, 0, sizeof(tabel)); }
	void update(Color kleur, int index, Move zet) 
	{
		tabel[kleur][index] = zet;
	}
private:
	Move tabel[COLOR_N][65536];
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
