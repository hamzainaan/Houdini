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
MoveEx* generateMoves(const Position& pos, MoveEx* zetten);

MoveEx* generateCapturesOnSquare(const Position& pos, MoveEx* zetten, Square veld);
MoveEx* generateLegalMoves(const Position& pos, MoveEx* zetten);

struct LegalMoveList 
{
	explicit LegalMoveList(const Position& pos) { einde = generateLegalMoves(pos, zetten); }
	const MoveEx* begin() const { return zetten; }
	const MoveEx* end() const { return einde; }

private:
	MoveEx* einde;
	MoveEx zetten[MAX_MOVES];
};

bool LegalMoveListContainsMove(const Position& pos, Move zet);
bool AtLeastOneLegalMove(const Position& pos);
bool LegalMoveListContainsCastling(const Position& pos, Move zet);

#endif // #ifndef MOVE_GENERATION_H
