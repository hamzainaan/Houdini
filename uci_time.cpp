/*
*/

#include "houdini.h"
#include "uci_time.h"
#include "uci.h"

TimeManagement TimeControl;

namespace
{
	double move_importance(int ply)
	{
		const double XScale = 7.64;
		const double XShift = 58.4;
		const double Skew = 0.183;

		double factor = 1.0;
		if (ply > 10 && ply < 70)
			factor = 1.225 - 0.00025 * (ply - 40) * (ply - 40);
		return factor * pow((1 + exp((ply - XShift) / XScale)), -Skew);
	}
}


void TimeManagement::initialize(TimeLimit& timeLimit, Color us, int ply)
{
	const int MovesHorizon = 50; 
	const double MaxRatio = 7.09;
	const double StealRatio = 0.35;
	const int64_t MinimumTime = 1;

	startTime = timeLimit.startTime;

	int MoveOverhead = 10 + UciOptions["Move Overhead"];
		
	optimalTime = maximumTime = timeLimit.time[us];
	int maxMoves = timeLimit.movestogo ? std::min(timeLimit.movestogo, MovesHorizon) : MovesHorizon;

	double moveImportance = move_importance(ply) * 0.89;
	double otherMovesImportance = 0;
	int64_t available = timeLimit.time[us] - MoveOverhead;

	for (int n = 1; n <= maxMoves; n++)
	{
		double ratio1 = moveImportance / (moveImportance + otherMovesImportance);
		int64_t t1 = std::llround(available * ratio1);

		double ratio2 = (MaxRatio * moveImportance) / (MaxRatio * moveImportance + otherMovesImportance);
		double ratio3 = (moveImportance + StealRatio * otherMovesImportance) / (moveImportance + otherMovesImportance);
		int64_t t2 = std::llround(available * std::min(ratio2, ratio3));

		optimalTime = std::min(t1, optimalTime);
		maximumTime = std::min(t2, maximumTime);

		otherMovesImportance += move_importance(ply + 2 * n);
		available += timeLimit.inc[us] - MoveOverhead;
	}

	optimalTime = std::max(optimalTime, MinimumTime);
	maximumTime = std::max(maximumTime, MinimumTime);

	if (UciOptions["Ponder"])
	{
		optimalTime += optimalTime * 3 / 10;
		optimalTime = std::min(optimalTime, maximumTime);
	}

	//	startTime = timeLimit.startTime;

	//	const int64_t MinimumTime = 1;
	//	int MoveOverhead = UciOptions["Move Overhead"];
	//	int time_for_player = timeLimit.time[us];
	//	int time_increment = timeLimit.inc[us];

	//	time_for_player = std::max(time_for_player - 250, 9 * time_for_player / 10);

	//	int n_max = timeLimit.movestogo ? std::min(timeLimit.movestogo, 60) : 60;
	//	int total_time = time_for_player + (n_max - 1) * (time_increment - MoveOverhead);

	//	optimalTime = (280 + n_max * std::min(ply, 80) / 16) * total_time / n_max / 256 - MoveOverhead;
	//	maximumTime = std::min(6 * optimalTime, (int64_t)total_time * (n_max + 2) / (3 * n_max));

	//	optimalTime = std::max(optimalTime, MinimumTime);
	//	maximumTime = std::max(maximumTime, MinimumTime);

	//	if (UciOptions["Ponder"])
	//		optimalTime += optimalTime / 4;

	//	int64_t max_time = std::max(1, time_for_player - MoveOverhead);
	//	maximumTime = std::min(maximumTime, max_time);
	//	optimalTime = std::min(optimalTime, max_time);
	//	optimalTime = std::min(optimalTime, maximumTime);
}

int64_t TimeManagement::elapsed() const
{
	return int64_t(now() - startTime);
}

void TimeManagement::adjustment_after_ponderhit()
{
	int64_t newMaxTime = maximumTime + elapsed();
	optimalTime = optimalTime * newMaxTime / maximumTime;
}
