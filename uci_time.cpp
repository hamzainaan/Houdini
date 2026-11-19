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

	//	startTijd = tijdLimiet.startTijd;

	//	const int64_t MinimumTijd = 1;
	//	int ZetOverhead = UciOpties["Move Overhead"];
	//	int tijd_voor_speler = tijdLimiet.time[ik];
	//	int tijd_increment = tijdLimiet.inc[ik];

	//	tijd_voor_speler = std::max(tijd_voor_speler - 250, 9 * tijd_voor_speler / 10);

	//	int n_max = tijdLimiet.movestogo ? std::min(tijdLimiet.movestogo, 60) : 60;
	//	int totale_tijd = tijd_voor_speler + (n_max - 1) * (tijd_increment - ZetOverhead);

	//	optimaleTijd = (280 + n_max * std::min(ply, 80) / 16) * totale_tijd / n_max / 256 - ZetOverhead;
	//	maximumTijd = std::min(6 * optimaleTijd, (int64_t)totale_tijd * (n_max + 2) / (3 * n_max));

	//	optimaleTijd = std::max(optimaleTijd, MinimumTijd);
	//	maximumTijd = std::max(maximumTijd, MinimumTijd);

	//	if (UciOpties["Ponder"])
	//		optimaleTijd += optimaleTijd / 4;

	//	int64_t max_tijd = std::max(1, tijd_voor_speler - ZetOverhead);
	//	maximumTijd = std::min(maximumTijd, max_tijd);
	//	optimaleTijd = std::min(optimaleTijd, max_tijd);
	//	optimaleTijd = std::min(optimaleTijd, maximumTijd);
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
