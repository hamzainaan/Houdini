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
	RootMove(Move zet) { pv.zetNummer = 1; pv.zetten[0] = zet; }

	bool operator<(const RootMove& rootZet) const { return rootZet.score < score; }
	bool operator==(const Move& zet) const { return pv[0] == zet; }
	bool vind_ponder_zet_in_tt(Position& pos);
	void vervolledig_pv_met_tt(Position& pos);

	Depth diepte = DIEPTE_0;
	Value score = -HOOGSTE_WAARDE;
	Value vorigeScore = -HOOGSTE_WAARDE;
	Value startWaarde;
	PrincipalVariation pv;
};

struct RootMoves
{
	RootMoves() : zetAantal(0) {}

	int zetAantal, dummy;
	RootMove zetten[MAX_ZETTEN];

	void add(RootMove rootZet) { zetten[zetAantal++] = rootZet; }
	RootMove& operator[](int index) { return zetten[index]; }
	const RootMove& operator[](int index) const { return zetten[index]; }
	void clear() { zetAantal = 0; }

	int find(Move zet)
	{
		for (int i = 0; i < zetAantal; i++)
			if (zetten[i].pv[0] == zet)
				return i;
		return -1;
	}
};

struct SignalsType
{
	std::atomic_bool stopAnalyse, stopBijPonderhit;
};


namespace Search
{
	extern SignalsType Signalen;
	extern TimeLimit Klok;
	extern bool Running;

	void initialisatie();
	void wis_alles();
	void pas_tijd_aan_na_ponderhit();
	void export_tabel();
	void import_tabel();
	extern uint8_t LMReducties[2][2][64 * (int)PLY][64];
}


typedef Value(*EGTB_probe_fx)(Position& pos, Value alfa, Value beta);

namespace EGTB
{
	extern int MaxPieces_wdl, MaxPieces_dtz, MaxPieces_dtm;
	extern EGTB_probe_fx EGTB_probe_wdl;
	extern EGTB_probe_fx EGTB_probe_dtz;
	extern EGTB_probe_fx EGTB_probe_dtm;
	extern bool UseRule50;
}

#endif // #ifndef SEARCH_H
