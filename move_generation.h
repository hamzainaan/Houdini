/*
*/

#ifndef MOVE_GENERATION_H
#define MOVE_GENERATION_H

#include "houdini.h"

class Position;

enum MoveGeneration 
{
	GEN_CAPTURES_OR_PROMOTIONS,
	GEN_QUIET_MOVES,
	GEN_QUIET_CHECKS,
	GEN_EVASIONS,
	GEN_ALL_MOVES,
	GEN_PAWN_ADVANCES,
	GEN_QUEEN_CHECKS,
	GEN_CASTLING
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
	MoveEx zetten[MAX_MOVES];
};

bool LegaleZettenLijstBevatZet(const Position& pos, Move zet);
bool MinstensEenLegaleZet(const Position& pos);
bool LegaleZettenLijstBevatRokade(const Position& pos, Move zet);

#endif // #ifndef MOVE_GENERATION_H
