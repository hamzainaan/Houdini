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

	bool uses_time_calculation() const
	{
		return !(mate | movetime | depth | nodes | infinite);
	}

	TimePoint startTime;
	int time[COLOR_N], inc[COLOR_N], movestogo, depth, movetime, mate, infinite, ponder;
	uint64_t nodes;
	MaxMoveList searchMoves;
};


class TimeManagement 
{
public:
	void initialize(TimeLimit& timeLimit, Color us, int ply);
	int64_t optimum() const { return optimalTime; }
	int64_t maximum() const { return maximumTime; }
	int64_t elapsed() const;
	void adjustment_after_ponderhit();

private:
	TimePoint startTime;
	int64_t optimalTime;
	int64_t maximumTime;
};

extern TimeManagement TimeControl;

#endif // #ifndef UCI_TIME_H
