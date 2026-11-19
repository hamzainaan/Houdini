/*
*/

#ifndef MOVE_GENERATION_H
#define MOVE_GENERATION_H

#include "houdini.h"

class Position;

enum MoveGeneration 
{
	GEN_SLAG_OF_PROMOTIE,
	GEN_RUSTIGE_ZETTEN,
	GEN_RUSTIGE_SCHAAKS,
	GEN_GA_UIT_SCHAAK,
	GEN_ALLE_ZETTEN,
	GEN_PION_OPMARS,
	GEN_DAME_SCHAAK,
	GEN_ROKADE
};

struct MoveEx 
{
	Move zet;
	SortValue waarde;

	operator Move() const { return zet; }
	void operator=(Move z) { zet = z; }
};

inline bool operator<(const MoveEx& f, const MoveEx& s) 
{
	return f.waarde < s.waarde;
}

template<MoveGeneration>
MoveEx* genereerZetten(const Position& pos, MoveEx* zetten);

MoveEx* genereerSlagzettenOpVeld(const Position& pos, MoveEx* zetten, Square veld);
MoveEx* genereerLegaleZetten(const Position& pos, MoveEx* zetten);

struct LegalMoveList 
{
	explicit LegalMoveList(const Position& pos) { einde = genereerLegaleZetten(pos, zetten); }
	const MoveEx* begin() const { return zetten; }
	const MoveEx* end() const { return einde; }

private:
	MoveEx* einde;
	MoveEx zetten[MAX_ZETTEN];
};

bool LegaleZettenLijstBevatZet(const Position& pos, Move zet);
bool MinstensEenLegaleZet(const Position& pos);
bool LegaleZettenLijstBevatRokade(const Position& pos, Move zet);

#endif // #ifndef MOVE_GENERATION_H
