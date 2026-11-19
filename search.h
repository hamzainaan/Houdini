/*
*/

#ifndef SEARCH_H
#define SEARCH_H

#include <atomic>

#include "houdini.h"
#include "util_tools.h"
#include "position.h"
#include "uci_time.h"

#ifdef CHESSBASE
#include "EngineTest.h"
extern EngineTest *Chessbase_License;
#endif

template<int MaxPlus, int MaxMin> struct PieceSquareStats;
typedef PieceSquareStats<3*8192, 3*8192> CounterMoveValues;

struct RootMove
{
	RootMove() {}
	RootMove(Move move) { pv.moveNumber = 1; pv.moves[0] = move; }

	bool operator<(const RootMove& rootMove) const { return rootMove.score < score; }
	bool operator==(const Move& move) const { return pv[0] == move; }
	bool find_ponder_move_in_tt(Position& pos);
	void complete_pv_with_tt(Position& pos);

	Depth depth = DEPTH_0;
	Value score = -HIGHEST_VALUE;
	Value previousScore = -HIGHEST_VALUE;
	Value startValue;
	PrincipalVariation pv;
};

struct RootMoves
{
	RootMoves() : moveCount(0) {}

	int moveCount, dummy;
	RootMove moves[MAX_MOVES];

	void add(RootMove rootMove) { moves[moveCount++] = rootMove; }
	RootMove& operator[](int index) { return moves[index]; }
	const RootMove& operator[](int index) const { return moves[index]; }
	void clear() { moveCount = 0; }

	int find(Move move)
	{
		for (int i = 0; i < moveCount; i++)
			if (moves[i].pv[0] == move)
				return i;
		return -1;
	}
};

struct SignalsType
{
	std::atomic_bool stopAnalysis, stopOnPonderhit;
};


namespace Search
{
	extern SignalsType Signals;
	extern TimeLimit Clock;
	extern bool Running;

	void initialize();
	void clear_all();
	void adjust_time_after_ponderhit();
	void export_table();
	void import_table();
	extern uint8_t LMReductions[2][2][64 * (int)PLY][64];
}


typedef Value(*EGTB_probe_fx)(Position& pos, Value alpha, Value beta);

namespace EGTB
{
	extern int MaxPieces_wdl, MaxPieces_dtz, MaxPieces_dtm;
	extern EGTB_probe_fx EGTB_probe_wdl;
	extern EGTB_probe_fx EGTB_probe_dtz;
	extern EGTB_probe_fx EGTB_probe_dtm;
	extern bool UseRule50;
}

#endif // #ifndef SEARCH_H
