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
	Move move;
	SortValue value;

	operator Move() const { return move; }
	void operator=(Move z) { move = z; }
};

inline bool operator<(const MoveEx& f, const MoveEx& s) 
{
	return f.value < s.value;
}

template<MoveGeneration>
MoveEx* generateMoves(const Position& pos, MoveEx* moves);

MoveEx* generateCapturesOnSquare(const Position& pos, MoveEx* moves, Square square);
MoveEx* generateLegalMoves(const Position& pos, MoveEx* moves);

struct LegalMoveList 
{
	explicit LegalMoveList(const Position& pos) { last = generateLegalMoves(pos, moves); }
	const MoveEx* begin() const { return moves; }
	const MoveEx* end() const { return last; }

private:
	MoveEx* last;
	MoveEx moves[MAX_MOVES];
};

bool LegalMoveListContainsMove(const Position& pos, Move move);
bool AtLeastOneLegalMove(const Position& pos);
bool LegalMoveListContainsCastling(const Position& pos, Move move);

#endif // #ifndef MOVE_GENERATION_H
