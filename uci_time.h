/*
*/

#ifndef UCI_TIME_H
#define UCI_TIME_H

#include "houdini.h"
#include "util_tools.h"


struct TimeLimit
{
	TimeLimit()
	{
		nodes = time[WHITE] = time[BLACK] = inc[WHITE] = inc[BLACK] =
			movestogo = depth = movetime = mate = infinite = ponder = 0;
	}

	bool gebruikt_tijd_berekening() const
	{
		return !(mate | movetime | depth | nodes | infinite);
	}

	TimePoint startTijd;
	int time[COLOR_N], inc[COLOR_N], movestogo, depth, movetime, mate, infinite, ponder;
	uint64_t nodes;
	MaxMoveList zoekZetten;
};


class TimeManagement 
{
public:
	void initialisatie(TimeLimit& tijdLimiet, Color ik, int ply);
	int64_t optimum() const { return optimaleTijd; }
	int64_t maximum() const { return maximumTijd; }
	int64_t verstreken() const;
	void aanpassing_na_ponderhit();

private:
	TimePoint startTijd;
	int64_t optimaleTijd;
	int64_t maximumTijd;
};

extern TimeManagement Tijdscontrole;

#endif // #ifndef UCI_TIME_H
