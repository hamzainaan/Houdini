/*
*/

#include <iostream>
#include <sstream>

#include "houdini.h"
#include "evaluation.h"
#include "util_tools.h"
#include "move_generation.h"
#include "move_selection.h"
#include "search.h"
#include "uci_time.h"
#include "search_smp.h"
#include "move_hash.h"
#include "uci.h"
#include "book.h"

#ifdef USE_LICENSE
#include "polarssl\aes.h"
#include "zlib\zlib.h"
unsigned int app_file_size();
#endif

#ifdef CHESSBASE
EngineTest *Chessbase_License;
#endif

namespace Search
{
	SignalsType Signals;
	TimeLimit Clock;
	bool Running;
	uint8_t LMReductions[2][2][64 * (int)PLY][64]; // [pv][progress][depth][move]
}

// depth added to EGTB_ProbeDepth
const Depth egtb_useful      = 0 * PLY;
const Depth egtb_not_useful = 10 * PLY;

int TB_Cardinality;
bool TB_RootInTB;
Depth TB_ProbeDepth;
Value TB_Score;

namespace EGTB
{
	int MaxPieces_wdl = 0;
	int MaxPieces_dtz = 0;
	int MaxPieces_dtm = 0;
	EGTB_probe_fx EGTB_probe_wdl = nullptr;
	EGTB_probe_fx EGTB_probe_dtm = nullptr;
	EGTB_probe_fx EGTB_probe_dtz = nullptr;
	bool UseRule50;
}

using namespace Search;

namespace
{
	enum NodeType { PV, NonPV };

	Value razer_margin(Depth d)
	{ 
		return Value(384);
	}
	
	const Value margin1[7] = { Value(0), Value(112), Value(243), Value(376), Value(510), Value(646), Value(784) };
	inline Value futility_margin1(Depth d) 
	{ 
		return margin1[(unsigned int)d / PLY];
	}
	
	inline Value futility_margin2(Depth d)
	{
		return Value(204 + 160 * int((unsigned int)d / PLY));
	}

	int LateMoveCountTable[2][32] =   // [progress][depth/2]
	{
		{ 0, 0, 3, 3, 4, 5, 6,  7,  8, 10, 12, 15, 17, 20, 23, 26, 30, 33, 37, 40, 44, 49, 53, 58, 63, 68,  73,  78,  83,  88,  94, 100 },
		{ 0, 0, 5, 5, 6, 7, 9, 11, 14, 17, 20, 23, 27, 31, 35, 40, 45, 50, 55, 60, 65, 71, 77, 84, 91, 98, 105, 112, 119, 127, 135, 143 }
	};
	inline int late_move_count(Depth d, bool progress)
	{
		return LateMoveCountTable[progress][(unsigned int)d / (PLY / 2)];
	}

	inline Depth lmr_reduction(bool pv, bool prog, Depth d, int n)
	{
		return Depth(LMReductions[pv][prog][std::min((int)d, 64 * (int)PLY - 1)][std::min(n, 63)]);
	}

	SortValue counterMoveBonus[MAX_PLY];
	inline SortValue counter_move_bonus(Depth d) { return counterMoveBonus[(unsigned int)d / PLY]; }
	inline SortValue history_bonus(Depth d) { return counterMoveBonus[(unsigned int)d / PLY]; }

	inline Depth counter_depth(Depth d, uint64_t nodes)
	{
		//return d;
		return (int)msb(nodes) > (int)d / 9 ? d + PLY : d;
	}

	struct EasyMoveManager
	{
		void clear()
		{
			keyAfterTwoMoves = 0;
			thirdMoveStable = 0;
			pv[0] = pv[1] = pv[2] = NO_MOVE;
		}

		Move expected_move(Key64 key) const
		{
			return keyAfterTwoMoves == key ? pv[2] : NO_MOVE;
		}

		void refreshPv(Position& pos, const PrincipalVariation& pvNew)
		{
			assert(pvNew.size() >= 3);

			thirdMoveStable = (pvNew[2] == pv[2]) ? thirdMoveStable + 1 : 0;

			if (pvNew[0] != pv[0] || pvNew[1] != pv[1] || pvNew[2] != pv[2])
			{
				pv[0] = pvNew[0];
				pv[1] = pvNew[1];
				pv[2] = pvNew[2];

				pos.do_move(pvNew[0]);
				pos.do_move(pvNew[1]);
				keyAfterTwoMoves = pos.key();
				pos.undo_move(pvNew[1]);
				pos.undo_move(pvNew[0]);
			}
		}

		int thirdMoveStable;
		Key64 keyAfterTwoMoves;
		Move pv[3];
	};

	EasyMoveManager EasyMove;
	Value DrawValue[COLOR_N];
	uint64_t previousInfoTime;

	template <NodeType NT>
	Value search(Position& pos, Value alpha, Value beta, Depth depth, bool cutNode);

	template <NodeType NT, bool inCheck>
	Value qsearch(Position& pos, Value alpha, Value beta, Depth depth);

	Value value_to_tt(Value v, int ply);
	Value value_from_tt(Value v, int ply);
	void copy_PV(Move* pv, Move move, Move* pvLower);
	void update_stats(const Position& pos, bool inCheck, Move move, Depth depth, Move* quietMoves, int quietCount);
	void update_stats_quiet(const Position& pos, bool inCheck, Depth depth, Move* quietMoves, int quietCount);
	void update_stats_minus(const Position& pos, bool inCheck, Move move, Depth depth);
	void time_control();

	Depth ComputeEgtbUsefulness(const Position& pos);
	void FilterRootMoves(Position& pos, RootMoves& rootMoves);

	const Value QSearchFutilityValue[PIECE_N] = {
		Value(102), VALUE_0, Value(308), Value(818), Value(827), Value(1186), Value(2228), VALUE_0,
		Value(102), VALUE_0, Value(308), Value(818), Value(827), Value(1186), Value(2228), VALUE_0
	};

	const Value LazyMarginQSearchLow = Value(480) + VALUE_1;
	const Value LazyMarginQSearchHigh = Value(480);

	PolyglotBook openingBook;

} // namespace


void Search::initialize()
{
	std::memset(LMReductions, 0, sizeof(LMReductions));

#if !defined(USE_LICENSE) && !defined(IMPORT_TABLES)
	for (int d = PLY; d < 64 * PLY; ++d)
		for (int n = 2; n < 64; ++n)
		{
			double rr = log(double(d) / PLY) * log(n) / 2 * int(PLY);
			if (rr < 6.4)
				continue;
			Depth r = Depth(std::lround(rr));

			LMReductions[false][1][d][n] = uint8_t(r);
			LMReductions[false][0][d][n] = uint8_t(r + (r < 2 * PLY ? 0 * PLY : PLY));
			LMReductions[true][1][d][n] = uint8_t(std::max(r - PLY, DEPTH_0));
			LMReductions[true][0][d][n] = LMReductions[true][1][d][n];

			//Reductions[false][1][d][n] = Depth(r - 1);
			//Reductions[false][0][d][n] = Depth(r + 2);
			//Reductions[true][1][d][n] = Depth(r - 7);
			//Reductions[true][0][d][n] = Depth(r - 4);
		}
#endif

	// quadratic function with initial slope  (1,1) -> (2,6) -> (3,13)
	for (int d = 1; d < MAX_PLY; ++d)
	{
		counterMoveBonus[d] = SortValue(std::min(8192, 24 * (d * d + 2 * d - 2)));
		//counterMoveBonus[d] = SortValue(std::min(8192, 24 * (d * d + 6 * d - 6)));
	}
}


void Search::clear_all()
{
	if (!UciOptions["Never Clear Hash"])
		MainHash.clear();
	Threads.clear_counter_move_history();

	for (int i = 0; i < Threads.threadCount; ++i)
	{
		Thread* th = Threads.threads[i];
		th->ti->history.clear();
		th->ti->evasionHistory.clear();
		th->ti->maxGainTable.clear();
		th->ti->counterMoves.clear();
		th->ti->counterfollowupMoves.clear();
		th->ti->captureHistory.clear();
	}

	Threads.main()->previousRootScore = HIGHEST_VALUE;
	Threads.main()->previousRootDepth = 999 * PLY;
	Threads.main()->quickMoveAllowed = false;
	openingBook.new_game();
}


/*
template<bool Root>
uint64_t Search::perft(Position& pos, Depth depth) {

  uint64_t cnt, nodes = 0;
  const bool leaf = (depth == 2 * PLY);

  for (const auto& m : MoveList<LEGAL_MOVES>(pos))
  {
      if (Root && depth <= PLY)
          cnt = 1, nodes++;
      else
      {
          pos.do_move(m);
          cnt = leaf ? MoveList<LEGAL_MOVES>(pos).size() : perft<false>(pos, depth - PLY);
          nodes += cnt;
          pos.undo_move(m);
      }
      if (Root)
          sync_cout << UCI::move(m, pos.is_chess960()) << ": " << cnt << sync_endl;
  }
  return nodes;
}

template uint64_t Search::perft<true>(Position&, Depth);
*/


void compute_EGTB_mate_PV(Position& pos, Value expected_score, PrincipalVariation& pv)
{
	Value score = NO_VALUE;

	for (const auto& move : LegalMoveList(pos))
	{
		pos.do_move(move);

		int num_pieces = pos.all_pieces_count();
		if (num_pieces <= EGTB::MaxPieces_dtm)
			score = EGTB::EGTB_probe_dtm(pos, -MATE_VALUE, MATE_VALUE);

		if (score == expected_score)
		{
			pv.add(move);
			compute_EGTB_mate_PV(pos, -score, pv);
			pos.undo_move(move);
			return;
		}
		else
			pos.undo_move(move);
	}
	return;
}


void choose_not_too_strong_move(Thread* thread)
{
	Value max_value, random_score;
	static PRNG rng(now());

	int max_index = 0;
	if (Threads.playStrength <= 10)
		random_score = Value(2 * (200 - 10 * Threads.playStrength));
	else if (Threads.playStrength <= 30)
		random_score = Value(2 * (120 - 2 * Threads.playStrength));
	else if (Threads.playStrength <= 60)
		random_score = Value(2 * (90 - Threads.playStrength));
	else
		random_score = Value(2 * (60 - Threads.playStrength / 2));

	max_value = VALUE_0; // avoid warning
	for (int PV_index = 0; PV_index < Threads.multiPV; PV_index++)
	{
		RootMove move = thread->rootMoves[PV_index];
		if (move.pv[0] == NO_MOVE)
			break;
		Value value = move.score;
		value += random_score * (rng.rand<unsigned>() & 1023) / 1024;
		if (PV_index == 0 || value > max_value)
		{
			max_value = value;
			max_index = PV_index;
		}
	}
	if (max_index != 0)
		std::swap(thread->rootMoves[0], thread->rootMoves[max_index]);
}

#define PONDER_SMART_HASH

void Search::adjust_time_after_ponderhit()
{
#ifdef PONDER_SMART_HASH
	MainHash.new_generation();
#endif
	if (Clock.uses_time_calculation())
		TimeControl.adjustment_after_ponderhit();
}


void Search::export_table()
{
	FILE *f = fopen("R:\\Schaak\\h6-lmr-export.dat", "wb");
	fwrite(LMReductions, 1, sizeof(LMReductions), f);
	fclose(f);
}


void Search::import_table()
{
	FILE *f = fopen("R:\\Schaak\\h6-lmr-export.dat", "rb");
	fread(LMReductions, 1, sizeof(LMReductions), f);
	fclose(f);
}


void MainThread::start_search()
{
	Running = true;
	rootPosition->copy_position(Threads.rootPosition, nullptr, nullptr);
	const Color us = rootPosition->side_to_move();
	TimeControl.initialize(Clock, us, rootPosition->game_ply());
	previousInfoTime = 0;
	interruptCounter = 0;

#ifdef USE_LICENSE
#ifdef CHESSBASE
	if (app_file_size() != 0xC3C3C3C3)
		memset(PST::psq, 0, sizeof(PST::psq));
#endif
#endif

	Threads.contemptColor = us;
	Threads.analysisMode = !Clock.uses_time_calculation();
	Threads.tacticalModeIndex = UciOptions["Tactical Mode"];
	for (int i = 0; i < Threads.threadCount; ++i)
		Threads.threads[i]->tacticalMode = Threads.tacticalModeIndex < 2 ? Threads.tacticalModeIndex : (i % Threads.tacticalModeIndex) == 0;

#if 1
	Threads.fiftyMovesDistance = UciOptions["FiftyMoveDistance"];
	Threads.fiftyMovesDistance = std::min(50, std::max(Threads.fiftyMovesDistance, rootPosition->fifty_moves_counter() / 2 + 5));
	Threads.pieceContempt = (Threads.analysisMode && !UciOptions["Analysis Contempt"]) ? 0 : int(UciOptions["Contempt"]);
	if (Threads.pieceContempt)
	{
		if (Threads.analysisMode)
			Threads.contemptColor = WHITE;

		int tempContempt = Threads.pieceContempt;
		Threads.pieceContempt = 0;
		Value v1 = Evaluation::evaluation(*rootPosition, NO_VALUE, NO_VALUE);
		Threads.pieceContempt = tempContempt;
		Value v2 = Evaluation::evaluation(*rootPosition, NO_VALUE, NO_VALUE);
		if (abs(v1) < WIN_VALUE && abs(v2) < WIN_VALUE)
			Threads.rootContemptValue = v2 - v1;
		else
			Threads.rootContemptValue = VALUE_0;
	}
	Threads.multiPV = Threads.multiPVmax = UciOptions["MultiPV"];
	Threads.multiPV_cp = UciOptions["MultiPV_cp"];
	if (Threads.multiPV_cp > 0)
		Threads.multiPV = 2;  // initial value, is updated by the algorithm
	if (UciOptions["UCI_LimitStrength"])
		Threads.playStrength = UCI::strength_for_elo(int(UciOptions["UCI_Elo"]) - 200);
	else
		Threads.playStrength = UciOptions["Strength"];
#else
	Threads.fiftyMovesDistance = 50;
	Threads.pieceContempt = Threads.analysisMode ? 0 : 3;
	Threads.multiPV = 1;
	Threads.multiPV_cp = 0;
	Threads.playStrength = 100;
#endif
	Threads.hideRedundantOutput = UciOptions["Hide Redundant Output"];

	Threads.activeThreadCount = Threads.threadCount;
	Threads.playStrengthMaxNodes = 0;
	if (Threads.playStrength != 100)
	{
		Threads.activeThreadCount = 1;
		Threads.multiPV_cp = 0;
		Threads.playStrengthMaxNodes = 600 << (Threads.playStrength / 5);
		if (Threads.playStrength <= 10)
			Threads.multiPV = 20 - Threads.playStrength / 2;
		else
			Threads.multiPV = 15 - Threads.playStrength / 8;
	}

	if (Threads.analysisMode)
	{
		DrawValue[us] = DRAW_VALUE;
		DrawValue[~us] = DRAW_VALUE;
	}
	else
	{
		DrawValue[us] = DRAW_VALUE - DEFAULT_DRAW_VALUE * rootPosition->game_phase() / MIDDLEGAME_PHASE;
		DrawValue[~us] = DRAW_VALUE + DEFAULT_DRAW_VALUE * rootPosition->game_phase() / MIDDLEGAME_PHASE;
	}

#ifdef PONDER_SMART_HASH
	if (!Clock.ponder)
#endif
		MainHash.new_generation();
	TB_RootInTB = false;
	EGTB::UseRule50 = UciOptions["EGTB Fifty Move Rule"];
	TB_ProbeDepth = UciOptions["EGTB Probe Depth"] * PLY;
	TB_Cardinality = std::max(EGTB::MaxPieces_wdl, std::max(EGTB::MaxPieces_dtm, EGTB::MaxPieces_dtz));

	rootMoves.moveCount = 0;
	for (const auto& move : LegalMoveList(*rootPosition))
		if (Clock.searchMoves.empty() || Clock.searchMoves.find(move) >= 0)
			rootMoves.add(RootMove(move));

	if (!Threads.analysisMode && UciOptions["Own Book"])
	{
		string bookFile = UciOptions["Book File"];
		if (!bookFile.empty() && bookFile != "<empty>")
		{
			Move bookMove = openingBook.probe(*rootPosition, bookFile, UciOptions["Best Book Line"]);
			if (bookMove)
			{
				int n = rootMoves.find(bookMove);
				if (n >= 0)
				{
					std::swap(rootMoves[0], rootMoves[n]);
					Threads.activeThreadCount = 1;  // only MainThread has an initialized position
					goto NO_ANALYSIS;
				}
			}
		}
	}

	// for analysis of positions
	// remove all positions that have not already occurred twice
	// to avoid a draw by twofold repetition in the starting position
	// good test position: 
	// position fen r5kr/2qp1pp1/2b1pn2/p1b4p/1pN1P3/2PB3P/PP1BQPP1/R3R1K1 w - - 1 18 moves a1c1 c5f8 b2b3 d7d5 e4d5 c6d5 
	// c4e3 d5c6 c1c2 c7b6 c3b4 a5b4 e1c1 c6b7 e3c4 b6c5 d2f4 c5d4 f4e3 d4d5 f2f3 b7a6 c1d1 a8e8 e2f2 d5b7 e3d4 h8h6 d4e3
	if (Threads.analysisMode)
	{
		PositionInfo* st = rootPosition->info();
		int e = std::min(st->draw50Moves, st->distanceToNullMove);
		while (e > 0)
		{
			st--;
			e--;
			PositionInfo* stst = st;
			bool found = false;
			for (int i = 2; i <= e; i += 2)
			{
				stst = stst - 2;
				if (stst->key == st->key)
				{
					found = true;
					break;
				}
			}

			// position has occurred only once, remove it from the list
			if (!found)
				st->key = 0;
		}
	}

	if (rootMoves.moveCount == 0)
	{
		rootMoves.add(RootMove(NO_MOVE));
		rootMoves[0].score = rootPosition->check_givers() ? -MATE_VALUE : DRAW_VALUE;
		rootMoves[0].depth = MAIN_THREAD_INC;
		Threads.activeThreadCount = 1;  // only MainThread has an initialized position
		sync_cout << UCI::pv(*rootPosition, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;
	}
	else
	{
		if (rootPosition->all_pieces_count() <= TB_Cardinality
			&& !rootPosition->castling_possible(ALL_CASTLING)
			&& Threads.playStrength == 100)
		{
			FilterRootMoves(*rootPosition, rootMoves);

			if (TB_RootInTB && rootMoves.moveCount == 1)
			{
				rootMoves[0].depth = MAIN_THREAD_INC;
				rootMoves[0].score = TB_Score;

				if (Threads.analysisMode && abs(TB_Score) > LONGEST_MATE_VALUE)
				{
					rootPosition->do_move(rootMoves[0].pv[0]);
					compute_EGTB_mate_PV(*rootPosition, TB_Score, rootMoves[0].pv);
					rootPosition->undo_move(rootMoves[0].pv[0]);
					rootMoves[0].depth = MAIN_THREAD_INC * rootMoves[0].pv.size();
				}
				if (TimeControl.elapsed() > 100)
					sync_cout << UCI::pv(*rootPosition, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;
				Threads.activeThreadCount = 1;  // only MainThread has an initialized position
				goto NO_ANALYSIS;
			}
		}

		open_tracefile();

		Threads.multiPVmax = std::min(Threads.multiPVmax, rootMoves.moveCount);
		Threads.multiPV = std::min(Threads.multiPV, rootMoves.moveCount);

		if (Threads.activeThreadCount > 1)
		{
			Threads.rootMoves = rootMoves;
			Threads.rootPositionInfo = rootPosition->info();
		}

		for (int i = 1; i < Threads.activeThreadCount; ++i)
			Threads.threads[i]->wake_up(true);

		Thread::start_search();

		close_tracefile();
	}
NO_ANALYSIS:

	if (!Signals.stopAnalysis && (Clock.ponder || Clock.infinite))
	{
		if (TimeControl.elapsed() <= 102)
			sync_cout << UCI::pv(*rootPosition, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;

		// depth = 1 -> some GUIs will not show the output, repeat it with depth 99
		if (rootMoves[0].depth == MAIN_THREAD_INC)
		{
			rootMoves[0].depth = 99 * MAIN_THREAD_INC;
			sync_cout << UCI::pv(*rootPosition, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;
		}

		Signals.stopOnPonderhit = true;
		wait(Signals.stopAnalysis);
	}

	Signals.stopAnalysis = true;

	for (int i = 1; i < Threads.activeThreadCount; ++i)
		Threads.threads[i]->wait_for_search_finished();

	// choose best thread (higher depth AND better score)
	Thread* bestThread = this;

	if (Threads.playStrength != 100)
		choose_not_too_strong_move(this);

	else if (!this->quickMovePlayed
		&& Threads.multiPV == 1
		&& !Clock.depth
		&&  rootMoves[0].pv[0] != NO_MOVE)
	{
		for (int i = 1; i < Threads.activeThreadCount; ++i)
		{
			Thread* th = Threads.threads[i];
			if (th->rootMoves[0].score > bestThread->rootMoves[0].score
				//&& (th->completedDepth > bestThread->completedDepth || th->rootMoves[0].score > LONGEST_MATE_VALUE))
				&& th->completedDepth > bestThread->completedDepth)
				bestThread = th;
		}
	}

	// still show a PV on fail high
	//if (bestThread->rootMoves[0].pv.size() == 1)
	//	bestThread->rootMoves[0].complete_pv_with_tt(*bestThread->rootPosition);

	previousRootScore = bestThread->rootMoves[0].score;
	previousRootDepth = bestThread->rootMoves[0].depth;

	if (bestThread != this || TimeControl.elapsed() <= 102 || Signals.stopOnPonderhit)
	{
		// use depth of the main Thread (to avoid jumps in the output)
		bestThread->rootMoves[0].depth = rootMoves[0].depth;
		sync_cout << UCI::pv(*bestThread->rootPosition, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;
	}
	sync_cout << "bestmove " << UCI::move(bestThread->rootMoves[0].pv[0], *rootPosition);

#ifdef USE_LICENSE
#ifdef CHESSBASE
	if (Chessbase_License->IsNotActivated()
		|| !Chessbase_License->TestOSMajor() || !Chessbase_License->TestNEngine() || !Chessbase_License->TestNEngine2())
#else
	if (app_file_size() != 0xC3C3C3C3)
#endif
		memset(PST::psq, 0, sizeof(PST::psq));
#endif

	if (bestThread->rootMoves[0].pv.size() > 1 || bestThread->rootMoves[0].find_ponder_move_in_tt(*rootPosition))
		std::cout << " ponder " << UCI::move(bestThread->rootMoves[0].pv[1], *rootPosition);

	std::cout << sync_endl;

	Threads.totalAnalysisTime += int(TimeControl.elapsed());

	Running = false;
}


void Thread::start_search()
{
	Value bestValue, alpha, beta, delta_alpha, delta_beta;
	Move quickMove = NO_MOVE;
	MainThread* mainThread = (this == Threads.main() ? Threads.main() : nullptr);
	//Depth last_smart_fail_high_depth = 15 * PLY;

	if (!mainThread)
	{
		rootPosition->copy_position(Threads.rootPosition, this, Threads.rootPositionInfo);
		rootMoves = Threads.rootMoves;
	}

	PositionInfo* st = rootPosition->info();

	std::memset(st + 1, 0, 2 * sizeof(PositionInfo));
	st->killers[0] = st->killers[1] = NO_MOVE;
	st->previousMove = NO_MOVE;
	(st - 2)->positionValue = VALUE_0;
	(st - 1)->positionValue = VALUE_0;
	(st - 1)->evalPositional = NO_EVAL;
	(st - 1)->moveNumber = 0;
	(st - 4)->counterMoveValues = (st - 3)->counterMoveValues = (st - 2)->counterMoveValues = (st - 1)->counterMoveValues = st->counterMoveValues = nullptr;
	(st - 1)->mp_endList = rootPosition->ti()->moveList;
	//(st - 1)->statsValue = SORT_MAX;

	for (int n = 0; n <= MAX_PLY; n++)
	{
		(st + n)->doNoEarlyPruning = false;
		(st + n)->excludedMove = NO_MOVE;
		(st + n)->lmr_reduction = 0;
		(st + n)->ply = n + 1;
	}

	bestValue = delta_alpha = delta_beta = alpha = -HIGHEST_VALUE;
	beta = HIGHEST_VALUE;
	completedDepth = 0 * PLY;

	if (mainThread)
	{
		quickMove = EasyMove.expected_move(rootPosition->key());
		EasyMove.clear();
		mainThread->quickMovePlayed = mainThread->failedLow = false;
		mainThread->quickMoveEvaluationBusy = mainThread->quickMoveEvaluationAborted = false;
		mainThread->bestMoveChanges = 0;
		//mainThread->quickMoveTested = false;

		// set st->pawnKey to 0 to later compute the maximum search depth for "seldepth" UCI output
		for (int i = 1; i <= MAX_PLY; i++)
			(st + i)->pawnKey = 0;
	}

	// quick move
	if (mainThread && !TB_RootInTB && !Clock.ponder && Threads.playStrength == 100 && !Threads.analysisMode
		&& mainThread->quickMoveAllowed && mainThread->previousRootDepth >= 12 * PLY && Threads.multiPV == 1)
	{
		TTTuple* ttt = MainHash.find_existing(rootPosition->key());
		if (ttt && ttt->bound() == EXACT_VALUE)
		{
			Value ttValue = value_from_tt(ttt->value(), st->ply);
			Move ttMove = ttt->move();
			Depth ttDepth = ttt->depth();

			if (ttDepth >= mainThread->previousRootDepth - 3 * PLY
				&& ttMove
				&& rootPosition->legal_move(ttMove)
				&& abs(ttValue) < WIN_VALUE)
			{
				Depth depth_singular = std::max(mainThread->previousRootDepth / 2, mainThread->previousRootDepth - 8 * PLY);
				Value v_singular = ttValue - Value(102);
				st->excludedMove = ttMove;
				st->positionValue = Evaluation::evaluation(*rootPosition, NO_VALUE, NO_VALUE);
				mainThread->quickMoveEvaluationBusy = true;
				Value v = search<NonPV>(*rootPosition, v_singular - VALUE_1, v_singular, depth_singular, false);
				mainThread->quickMoveEvaluationBusy = false;
				st->excludedMove = NO_MOVE;

				if (!mainThread->quickMoveEvaluationAborted && v < v_singular)
				{
					Signals.stopAnalysis = true;
					rootMoves[0].score = ttValue;
					rootMoves[0].pv.resize(1);
					rootMoves[0].pv[0] = ttMove;
					rootMoves[0].complete_pv_with_tt(*rootPosition);
					rootMoves[0].depth = ttDepth;
					mainThread->quickMoveAllowed = false;
					mainThread->quickMovePlayed = true;
					EasyMove.clear();
					completedDepth = mainThread->previousRootDepth - 2 * PLY;
					sync_cout << UCI::pv(*rootPosition, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;
					return;
				}
				mainThread->quickMoveEvaluationAborted = false;
			}
		}
	}
	if (mainThread)
		mainThread->quickMoveAllowed = true;

	// in tactical mode we need the initial score of every move
	if (tacticalMode)
	{
		previousTacticalDepth = 0 * PLY;

		// compute the initial score of every move
		st->mp_endList = (st - 1)->mp_endList;
		Move pv[MAX_PLY + 1];

		for (int i = 0; i < rootMoves.moveCount; i++)
		{
			Move move = rootMoves[i].pv[0];
			rootPosition->do_move(move);
			(st + 1)->doNoEarlyPruning = true;
			(st + 1)->pv = pv;
			(st + 1)->pv[0] = NO_MOVE;
			rootMoves[i].startValue = -::search<PV>(*rootPosition, -HIGHEST_VALUE, HIGHEST_VALUE, PLY, false);
			(st + 1)->doNoEarlyPruning = false;
			rootPosition->undo_move(move);
		}
	}

	int searchIteration = 0;
	Depth rootDepth = PLY / 2;

	while (++searchIteration < 100)
	{
		rootDepth += mainThread ? MAIN_THREAD_INC : OTHER_THREAD_INC;

		if (mainThread)
		{
			if (Clock.depth && searchIteration - 1 >= Clock.depth)
				Signals.stopAnalysis = true;

			if (Threads.playStrength != 100 && Threads.visited_nodes() >= Threads.playStrengthMaxNodes)
				Signals.stopAnalysis = true;
		}

		if (Signals.stopAnalysis)
			break;

		if (mainThread)
		{
			mainThread->bestMoveChanges /= 2;
			mainThread->failedLow = false;
		}

		if (mainThread && TimeControl.elapsed() > 1000)
			sync_cout << "info depth " << searchIteration << sync_endl;

		for (int i = 0; i < rootMoves.moveCount; i++)
			rootMoves[i].previousScore = rootMoves[i].score;

		for (activePV = 0; activePV < Threads.multiPV && !Signals.stopAnalysis; ++activePV)
		{
			Move prevBestMove = rootMoves[activePV].pv[0];
			int failHighCount = 0;

			if (rootDepth >= 5 * PLY)
			{
				delta_alpha = Value(14 + (threadIndex & 7));
				delta_beta = Value(14 - (threadIndex & 7));
				alpha = std::max(rootMoves[activePV].previousScore - delta_alpha, -HIGHEST_VALUE);
				beta = std::min(rootMoves[activePV].previousScore + delta_beta, HIGHEST_VALUE);
			}

			while (true)
			{
				if (alpha < -20 * VALUE_PAWN)
					alpha = -HIGHEST_VALUE;
				if (beta > 20 * VALUE_PAWN)
					beta = HIGHEST_VALUE;

//#define SMART_FAIL_HIGH
#ifdef SMART_FAIL_HIGH
				if (failHighCount == 2 && Threads.analysisMode && rootMoves[activePV].pv[0] != prevBestMove 
					&& rootDepth > last_smart_fail_high_depth)
				{
					last_smart_fail_high_depth = rootDepth;
					bool showOutput = mainThread && TimeControl.elapsed() > 4000;
					if (showOutput)
						sync_cout << "info string smart fail high - attempt reducing search depth to " << searchIteration - 5 << sync_endl;

					Move m = rootMoves[activePV].pv[0];
					Move pv[MAX_PLY + 1];
					(st + 1)->pv = pv;

					rootPosition->do_move(m);

					for (int depth_offset = 5; depth_offset >= 1; depth_offset -= 2)
					{
						bestValue = -::search<PV>(*rootPosition, -beta, -alpha, rootDepth - PLY - depth_offset * PLY, false);

						if (Signals.stopAnalysis)
							break;

						if (bestValue > alpha)
						{
							if (showOutput)
								sync_cout << "info string smart fail high successful" << sync_endl;
							rootDepth -= depth_offset * PLY;
							searchIteration -= depth_offset;
							break;
						}
					};

					rootPosition->undo_move(m);
					if (Signals.stopAnalysis)
						break;
				}
#endif

				tacticalModeUsed = false;

				bestValue = ::search<PV>(*rootPosition, alpha, beta, rootDepth, false);

				std::stable_sort(rootMoves.moves + activePV, rootMoves.moves + rootMoves.moveCount);

				if (Signals.stopAnalysis)
					break;

				if (tacticalModeUsed)
					previousTacticalDepth = rootDepth;

				// fail high is only resolved in the mainThread, if there is still time left
				bool failHighResolve = mainThread;

				if (mainThread && bestValue >= beta
					&& rootMoves[activePV].pv[0] == prevBestMove
					&& !Threads.analysisMode
					&& TimeControl.elapsed() > TimeControl.optimum() * 124 / 1024)
				{
					bool playEasyMove = rootMoves[0].pv[0] == quickMove && mainThread->bestMoveChanges < 31;

					if (playEasyMove)
						failHighResolve = false;

					else if (TimeControl.elapsed() > TimeControl.optimum() * 420 / 1024)
					{
						int improvementFactor = std::max(420, std::min(1304,
							652 + 160 * mainThread->failedLow - 12 * int(bestValue - mainThread->previousRootScore)));
						int unstableFactor = 1024 + mainThread->bestMoveChanges;
						if (TimeControl.elapsed() > TimeControl.optimum() * unstableFactor / 1024 * improvementFactor / 1024)
							failHighResolve = false;
					}
				}

				if (bestValue <= alpha)
				{
					beta = (alpha + beta) / 2;
					alpha = std::max(bestValue - delta_alpha, -HIGHEST_VALUE);
					failHighCount = 0;

					if (mainThread)
					{
						mainThread->failedLow = true;
						Signals.stopOnPonderhit = false;
					}
				}
				else if (bestValue >= beta && (failHighResolve || bestValue >= VALUE_PAWN * 8))
				{
					alpha = (alpha + beta) / 2;
					beta = std::min(bestValue + delta_beta, HIGHEST_VALUE);
					++failHighCount;
				}
				else
					break;

				delta_alpha += delta_alpha / 4 + Value(4);
				delta_beta += delta_beta / 4 + Value(4);

				assert(alpha >= -HIGHEST_VALUE && beta <= HIGHEST_VALUE);
			}

			std::stable_sort(rootMoves.moves + 0, rootMoves.moves + activePV + 1);

			if (mainThread)
			{
				if (Threads.multiPV_cp > 0 && activePV >= 1)
				{
					if (rootMoves[0].score - rootMoves[activePV].score > 2 * Threads.multiPV_cp)
						Threads.multiPV = activePV + 1;
					else
						Threads.multiPV = activePV + 2;
					Threads.multiPV = std::min(Threads.multiPV, Threads.multiPVmax);
				}

				if (!Threads.hideRedundantOutput &&
					((activePV + 1 == Threads.multiPV && TimeControl.elapsed() > 100) || TimeControl.elapsed() > 4000))
					sync_cout << UCI::pv(*rootPosition, alpha, beta, activePV, activePV) << sync_endl;
			}
		}

		if (!Signals.stopAnalysis)
			completedDepth = rootDepth;

		if (!mainThread)
			continue;

		if (Clock.mate
			&& bestValue >= LONGEST_MATE_VALUE
			&& MATE_VALUE - bestValue <= 2 * Clock.mate)
			Signals.stopAnalysis = true;

		if (!Threads.analysisMode && !Clock.ponder && bestValue > MATE_VALUE - 32
			&& rootDepth >= (MATE_VALUE - bestValue + 10) * PLY)
			Signals.stopAnalysis = true;

		if (!Threads.analysisMode && !Clock.ponder && bestValue < -MATE_VALUE + 32
			&& rootDepth >= (MATE_VALUE + bestValue + 10) * PLY)
			Signals.stopAnalysis = true;

		if (!Threads.analysisMode)
		{
			if (!Signals.stopAnalysis && !Signals.stopOnPonderhit)
			{
				int improvementFactor = std::max(420, std::min(1304,
					652 + 160 * mainThread->failedLow - 12 * int(bestValue - mainThread->previousRootScore)));
				int unstableFactor = 1024 + mainThread->bestMoveChanges;

				bool playEasyMove = rootMoves[0].pv[0] == quickMove
					&& mainThread->bestMoveChanges < 31
					&& TimeControl.elapsed() > TimeControl.optimum() * 124 / 1024;

				if ((rootMoves.moveCount == 1 && searchIteration > 10)
					|| TimeControl.elapsed() > TimeControl.optimum() * unstableFactor / 1024 * improvementFactor / 1024
					|| (mainThread->quickMovePlayed = playEasyMove))
				{
					// If we are allowed to ponder do not stop the search now but
					// keep pondering until the GUI sends "ponderhit" or "stop".
					if (Clock.ponder)
						Signals.stopOnPonderhit = true;
					else
						Signals.stopAnalysis = true;
#ifdef TRACE_TM
					trace_tm_msg(1 + rootPosition->game_ply() / 2, Time.elapsed(), Time.optimum(), mainThread->quickMovePlayed ? "Easy Move" : "");
#endif
				}
#if 0
				else if (searchIteration >= 12 && mainThread && !mainThread->quickMoveTested
					&& (Time.elapsed() >= Time.optimum() / 4 || searchIteration >= mainThread->previous_root_iteration - 1))
				{
					mainThread->quickMoveTested = true;

					int iteration_singular = std::max(searchIteration / 2, searchIteration - 8);
					Value v_singular = bestValue - Value(320);
					st->excludedMove = rootMoves[0].pv[0];
					st->doNoEarlyPruning = true;
					Value v = ::search<NonPV>(rootPosition, v_singular - 1, v_singular, rootDepthForIteration(iteration_singular), false);
					st->excludedMove = NO_MOVE;
					st->doNoEarlyPruning = false;

					if (v < v_singular)
					{
						if (Clock.ponder)
							Signals.stopOnPonderhit = true;
						else
							Signals.stopAnalysis = true;
					}
				}
#endif
			}

			if (rootMoves[0].pv.size() >= 3)
				EasyMove.refreshPv(*rootPosition, rootMoves[0].pv);
			else
				EasyMove.clear();
		}
	}

	if (!mainThread)
		return;

	// Clear any candidate easy move that wasn't stable for the last search
	// iterations; the second condition prevents consecutive fast moves.
	if (EasyMove.thirdMoveStable < 6 || mainThread->quickMovePlayed)
		EasyMove.clear();
}


namespace
{
	template <NodeType NT>
	Value search(Position& pos, Value alpha, Value beta, Depth depth, bool cutNode)
	{
		const bool PvNode = NT == PV;
		const int MAX_QUIET_MOVES = 64;

		assert(-HIGHEST_VALUE <= alpha && alpha < beta && beta <= HIGHEST_VALUE);
		assert(PvNode || (alpha == beta - 1));
		assert(depth >= PLY && depth < MAX_DEPTH);

		Move pv[MAX_PLY + 1], quietMoves[MAX_QUIET_MOVES];
		TTTuple* ttt;
		Key64 key64;
		Move ttMove, move, bestMove;
		Depth extension, newDepth, predictedDepth;
		Value ttValue, eval;
		bool inCheck, givesCheck, progress;
		bool captureOrPromotion, fullSearchNeeded;
		Piece movedPiece;
		int moveNumber, quietMoveCount;

		trace_msg("Search", depth, alpha, beta);

		PositionInfo* st = pos.info();
		const bool rootNode = PvNode && st->ply == 1;

		// Step 1. Initialize node
		Thread* myThread = pos.my_thread();
		inCheck = st->checkGivers;
		moveNumber = quietMoveCount = st->moveNumber = 0;

		if (myThread == Threads.main())
		{
			MainThread* mainThread = static_cast<MainThread*>(myThread);
			if (++mainThread->interruptCounter >= 4096)
			{
				if (mainThread->quickMoveEvaluationBusy)
				{
					if (mainThread->quickMoveEvaluationAborted)
						return alpha;
					int64_t elapsed = TimeControl.elapsed();
					if (elapsed > 1000 || elapsed > TimeControl.optimum() / 16)
					{
						mainThread->quickMoveEvaluationAborted = true;
						return alpha;
					}
				}
				time_control();
				mainThread->interruptCounter = 0;
			}
		}

		if (!rootNode)
		{
			// Step 2. Check for aborted search and immediate draw
			if (Signals.stopAnalysis.load(std::memory_order_relaxed) || st->moveRepetition || st->ply >= MAX_PLY)
				return st->ply >= MAX_PLY && !inCheck 
					? Evaluation::evaluation(pos, NO_VALUE, NO_VALUE)
					: DrawValue[pos.side_to_move()];

			// Step 3. Mate distance pruning
			alpha = std::max(mated_in(st->ply), alpha);
			beta = std::min(mate_in(st->ply + 1), beta);
			if (alpha >= beta)
				return alpha;
		}

		assert(1 <= st->ply && st->ply < MAX_PLY);

		bestMove = NO_MOVE;
		(st + 2)->killers[0] = (st + 2)->killers[1] = NO_MOVE;
		st->statsValue = SORT_MAX;
		//uint64_t nodes = pos.visited_nodes();

		// Step 4. Transposition table lookup
		if (st->excludedMove)
		{
			// st->positionValue is still correct, from the calling routine
			ttt = nullptr;
			ttMove = NO_MOVE;
			ttValue = NO_VALUE;  // redundant, but avoids warning from compiler
			key64 = 0;  // redundant, but avoids warning from compiler
			// also skip IID, because we do not store moves in the hash table anyway
			goto examine_all_moves;
		}

		//key64 = st->excludedMove ? st->key ^ pos.exclusion_key(st->excludedMove) : st->key;
		key64 = st->key;
		key64 ^= pos.draw50_key();
		ttt = MainHash.find_existing(key64);
		ttValue = ttt ? value_from_tt(ttt->value(), st->ply) : NO_VALUE;
		ttMove = rootNode ? myThread->rootMoves[myThread->activePV].pv[0]
			: ttt ? ttt->move() : NO_MOVE;

		if (!PvNode
			&& ttValue != NO_VALUE
			&& ttt->depth() >= depth
			&& (ttValue >= beta ? (ttt->bound() & LOWER_BOUND)
				: (ttt->bound() & UPPER_BOUND)))
		{
			if (ttMove)
			{
				if (ttValue >= beta)
					update_stats(pos, inCheck, ttMove, depth, nullptr, 0);
				else
					update_stats_minus(pos, inCheck, ttMove, depth);
			}

			return ttValue;
		}

		// Step 4a. Tablebase probe
		if (!rootNode && TB_Cardinality && depth >= TB_ProbeDepth && pos.material_or_castling_changed())
		{
			int numPieces = pos.all_pieces_count();

			if (numPieces <= TB_Cardinality
				//&& (/*numPieces < TB_Cardinality ||*/ depth >= TB_ProbeDepth + ComputeEgtbUsefulness(pos))
				&& depth >= TB_ProbeDepth
				&& !pos.castling_possible(ALL_CASTLING))
			{
				Value value;
				if (numPieces <= EGTB::MaxPieces_wdl)
					value = EGTB::EGTB_probe_wdl(pos, alpha, beta);
				else if (numPieces <= EGTB::MaxPieces_dtm)
					value = EGTB::EGTB_probe_dtm(pos, alpha, beta);
				else
					value = NO_VALUE;

				if (value != NO_VALUE)
				{
					ttt = MainHash.find_replace(key64);
					ttt->save(key64, value_to_tt(value, st->ply), EXACT_VALUE, 
						std::min(MAX_DEPTH - PLY, depth + 6 * PLY), 
						NO_MOVE, NO_VALUE, MainHash.generation());

					return value;
				}
#if 0
				// this is Joseph Ellis's way to possibly let TB scores be overridden by a found mate 
				// see https://github.com/official-stockfish/Stockfish/compare/master...jhellis3:tb_fix_2
				if (value == DRAW_VALUE
					|| !ttt || (value < DRAW_VALUE && ttValue > -EGTB_WIN_VALUE) || (value > DRAW_VALUE && ttValue < EGTB_WIN_VALUE))
				{
					ttt = MainHash.find_replace(key64);
					ttt->save(key64, value_to_tt(value, st->ply),
						value > DRAW_VALUE ? LOWER_BOUND : value < DRAW_VALUE ? UPPER_BOUND : EXACT_VALUE,
						depth, NO_MOVE, NO_VALUE, MainHash.generation());

					if (value == DRAW_VALUE)
						return value;
				}
#endif
			}
		}

		// Step 5. Evaluate the position statically
		if (inCheck)
		{
			st->positionValue = NO_VALUE;
			goto examine_all_moves;
		}

		if (ttt && ttt->eval() != NO_VALUE)
		{
			eval = st->positionValue = ttt->eval();
			st->strongThreat = ttt->threat();

			if (ttValue != NO_VALUE)
				if (ttt->bound() & (ttValue > eval ? LOWER_BOUND : UPPER_BOUND))
					eval = ttValue;
		}
		else
		{
			if (st->previousMove != NULL_MOVE)
				eval = Evaluation::evaluation(pos, NO_VALUE, NO_VALUE);
				//eval = Evaluation::evaluation(pos, 
					//PvNode || depth >= 4 * PLY ? NO_VALUE : alpha - LazyMarginQSearchLow - Value(120) * (depth / PLY),
					//PvNode || depth >= 4 * PLY ? NO_VALUE : beta + LazyMarginQSearchHigh + Value(120) * (depth / PLY));
			else
				eval = Evaluation::evaluation_after_nullmove((st - 1)->positionValue);

			st->positionValue = eval;
			if (st->eval_is_exact && !rootNode)
				return eval;

			ttt = MainHash.find_replace(key64);
			ttt->save(key64, NO_VALUE, NO_BOUND + st->strongThreat, NO_DEPTH, NO_MOVE,
				st->positionValue, MainHash.generation());
		}

		trace_eval(st->positionValue);

		if (st->previousMove != NULL_MOVE
			/*&& st->positionValue != NO_VALUE*/ && (st - 1)->positionValue != NO_VALUE
			&& st->materialKey == (st - 1)->materialKey)
		{
			const Value MAX_GAIN = Value(500);
			Value gain = -st->positionValue - (st - 1)->positionValue + 2 * VALUE_TEMPO;
			gain = std::min(MAX_GAIN, std::max(-MAX_GAIN, gain));
			pos.ti()->maxGainTable.update(st->movedPiece, st->previousMove, gain);
		}

		if (st->doNoEarlyPruning)
			goto no_early_pruning;

		// Step 6. Razoring
		if (!PvNode
			&& depth < 4 * PLY
			&& !ttMove
			&& eval + razer_margin(depth) <= alpha)
			//&& !(st->strongThreat & (pos.side_to_move() + 1)))
		{
			if (depth < 2 * PLY)
				return qsearch<NonPV, false>(pos, alpha, beta, DEPTH_0);

			Value ralpha = alpha - razer_margin(depth);
			Value v = qsearch<NonPV, false>(pos, ralpha, ralpha + VALUE_1, DEPTH_0);
			if (v <= ralpha)
				return v;
		}

		// Step 7. Futility pruning: child node
		if (!rootNode
			&& depth < 7 * PLY
			&& eval - futility_margin1(depth) >= beta
			&& eval < WIN_VALUE  // Do not return unproven wins
			&& st->nonPawnMaterial[pos.side_to_move()])
			return eval - futility_margin1(depth);

		// Step 8. Null move search with verification search
		if (!PvNode
			&& depth >= 2 * PLY
			&& eval >= beta + 2 * VALUE_TEMPO
			&& (!st->strongThreat || depth >= 8 * PLY)
			&& (st->positionValue >= beta || depth >= 12 * PLY)
			&& st->nonPawnMaterial[pos.side_to_move()]
			//&& pos.pieces(pos.side_to_move(), PAWN)
			&& (!Threads.analysisMode || depth < 8 * PLY || Evaluation::at_least_two_mobile_pieces(pos))
			)   // good test position for at_least_two_mobile_pieces: 8/6B1/p5p1/Pp4kp/1P5r/5P1Q/4q1PK/8 w - - 0 32 bm Qxh4
		{
			assert(eval - beta >= 0);

			Depth R;
			if (myThread->tacticalMode)
				R = depth < 4 * PLY ? depth :
				(540 + 66 * ((unsigned int)depth / PLY)
					+ std::max(std::min(310 * (eval - beta) / Value(204) - 20 - 15 * cutNode - 15 * (ttMove != NO_MOVE), 3 * 256), 0)
					) / 256 * PLY;
			else
				R = depth < 4 * PLY ? depth :
				(480 + 76 * ((unsigned int)depth / PLY)
				//(480 + 76 * std::min((unsigned int)depth / PLY, (unsigned int)depth / PLY / 2 + 6)
					+ std::max(std::min(310 * (eval - beta) / Value(204) - 20 - 15 * cutNode - 15 * (ttMove != NO_MOVE), 3 * 256), 0)
					) / 256 * PLY;

			st->mp_endList = (st - 1)->mp_endList;
			pos.do_null_move();
			(st + 1)->doNoEarlyPruning = true;
			Value value = depth - R < PLY ? -qsearch<NonPV, false>(pos, -beta, -beta + VALUE_1, DEPTH_0)
				: -search<NonPV>(pos, -beta, -beta + VALUE_1, depth - R, !cutNode);
			(st + 1)->doNoEarlyPruning = false;
			pos.undo_null_move();

			if ((st - 1)->lmr_reduction && (value < beta - Value(100) || value < -LONGEST_MATE_VALUE))
			{
				if ((st - 1)->lmr_reduction <= 2 * PLY)
					return beta - VALUE_1;
				depth += 2 * PLY;

				//int delta = std::min(int((st - 1)->lmr_reduction), int(2 * PLY));
				//(st - 1)->lmr_reduction -= delta;
				//depth += Depth(delta);
			}

			if (value >= beta)
			{
				if (value >= LONGEST_MATE_VALUE)
					value = beta;

				if (depth < 12 * PLY && abs(beta) < WIN_VALUE)
					return value;

				st->doNoEarlyPruning = true;
				Value v = depth - R < PLY ? qsearch<NonPV, false>(pos, beta - VALUE_1, beta, DEPTH_0)
					: search<NonPV>(pos, beta - VALUE_1, beta, depth - R, false);
				st->doNoEarlyPruning = false;

				if (v >= beta)
					return value;
			}
		}
#ifdef USE_LICENSE
		// Step 8.5. Try null move to detect a threat in order to possibly reduce LMR
		// dummy condition
		else if (Threads.dummyNullMoveThreat && depth >= 6 * PLY && eval >= beta && (st - 1)->lmr_reduction)
		{
			st->mp_endList = (st - 1)->mp_endList;
			pos.do_null_move();
			(st + 1)->doNoEarlyPruning = true;
			Value nullBeta = beta - Value(240);
			Value value = -search<NonPV>(pos, -nullBeta, -nullBeta + VALUE_1, depth / 2 - 2 * PLY, false);
			(st + 1)->doNoEarlyPruning = false;
			pos.undo_null_move();

			if (value < nullBeta)
			{
				if ((st - 1)->lmr_reduction <= 2 * PLY)
					return beta - VALUE_1;
				depth += 2 * PLY;
			}
		}
#endif

	no_early_pruning:

		// Step 9. ProbCut
		if (!PvNode
			&&  depth >= 5 * PLY
			&& (depth >= 8 * PLY || (st->strongThreat & (pos.side_to_move() + 1)))
			&&  abs(beta) < LONGEST_MATE_VALUE)
		{
			Value pc_beta = beta + Value(160);
			Depth pc_depth = depth - 4 * PLY;

			assert(pc_depth >= PLY);
			assert(st->previousMove != NO_MOVE);
			//assert(st->previousMove != NULL_MOVE);

			SeeValue bound = depth >= 8 * PLY ? pos.see_values()[st->capturedPiece] : std::max(SEE_0, SeeValue((pc_beta - st->positionValue) / 2));
			MoveSelection::init_probcut(pos, ttMove, bound);

			while ((move = MoveSelection::next_move(pos)) != NO_MOVE)
			{
				if (pos.legal_move(move))
				{
					pos.do_move(move);
					Value value = -search<NonPV>(pos, -pc_beta, -pc_beta + VALUE_1, pc_depth, !cutNode);
					pos.undo_move(move);
					if (value >= pc_beta)
						return value;
				}
			}
		}

		// Step 10. Internal iterative deepening
		if (depth >= (PvNode ? 5 * PLY : 8 * PLY)
			&& !ttMove
			//&& (!ttMove || (PvNode && tte->depth() < depth - 4 * PLY))
			//&& (PvNode || st->staticEval + 256 >= beta))
			&& (PvNode || cutNode || st->positionValue + Value(102) >= beta))
		{
			Depth d = depth - 2 * PLY - (PvNode ? DEPTH_0 : ((unsigned int)depth / PLY) / 4 * PLY);
			st->doNoEarlyPruning = true;
			search<NT>(pos, alpha, beta, d, !PvNode && cutNode);
			st->doNoEarlyPruning = false;

			ttt = MainHash.find_existing(key64);
			ttMove = ttt ? ttt->move() : NO_MOVE;
		}

	examine_all_moves:
		// careful, from here on st->positionValue is used - can be NO_VALUE (if inCheck)

		const CounterMoveValues* cmh = st->counterMoveValues;
		const CounterMoveValues* fmh = (st - 1)->counterMoveValues;
		const CounterMoveValues* fmh2 = (st - 3)->counterMoveValues;

		bool only_quiet_checks = !rootNode && depth < 8 * PLY
			&& st->positionValue + futility_margin2(depth - PLY) <= alpha;  // always false when inCheck

		MoveSelection::init_search(pos, ttMove, depth, only_quiet_checks);

#ifdef USE_LICENSE
		if (myThread == Threads.main() && Threads.tamperedWithExe)
		{
			((char *)LMReductions)[Threads.tamperedWithExe & (sizeof(LMReductions) - 1)] = 0;
			Threads.tamperedWithExe++;
		}
#endif

		Value bestValue = -HIGHEST_VALUE;
		//bool bestValue_is_repetition = false;

		//Move previous_move_back = NO_MOVE;
		//if (!rootNode && st->draw50Moves >= 2 && alpha > DrawValue[pos.side_to_move()]
		//	&& st->castlingRights == (st - 1)->castlingRights
		//	&& to_square(st->previousMove) != from_square((st - 1)->previousMove)
		//	&& !(bb_between(from_square(st->previousMove), to_square(st->previousMove)) & from_square((st - 1)->previousMove)))
		//	previous_move_back = make_move(move_type((st - 1)->previousMove), to_square((st - 1)->previousMove), from_square((st - 1)->previousMove));

		// 60% progress
		progress = st->positionValue >= (st - 2)->positionValue
			/* || st->positionValue == NO_VALUE test not needed */
			|| (st - 2)->positionValue == NO_VALUE;

		int LateMoveCount = depth < 16 * PLY ? late_move_count(depth, progress) : 999;
		//LateMoveCount += (st - 1)->moveNumber / 16;
		if (only_quiet_checks)
			LateMoveCount = 1;
		//if (inCheck)
		//	LateMoveCount = depth < 16 * PLY ? late_move_count((depth + PLY) / 2, false) : 999;
		bool discovered_check_possible = pos.discovered_check_possible();
		//bool pv_capture = false;
		bool postpone_possible = (PvNode || depth >= 12 * PLY) && Threads.threadCount > 1;

		// Step 11. Loop through moves
		while ((move = MoveSelection::next_move(pos)) != NO_MOVE)
		{
			assert(piece_color(pos.moved_piece(move)) == pos.side_to_move());

			if (move == st->excludedMove)
				continue;

			if (rootNode && myThread->rootMoves.find(move) < myThread->activePV)
				continue;

			st->moveNumber = ++moveNumber;

			//if (move == previous_move_back)
			//	continue;

			if (rootNode && myThread == Threads.main())
			{
				if (TimeControl.elapsed() > 4000)
					sync_cout << "info currmove " << UCI::move(move, pos)
						<< " currmovenumber " << moveNumber + myThread->activePV << sync_endl;
#ifdef USE_LICENSE
				if (Threads.tamperedWithExe)
					((int *)LateMoveCountTable)[Threads.tamperedWithExe & (sizeof(LateMoveCountTable) / sizeof(int) - 1)] = 220;
#endif
			}

			if (PvNode)
				(st + 1)->pv = nullptr;

			captureOrPromotion = pos.capture_or_promotion(move);
			movedPiece = pos.moved_piece(move);

			givesCheck = move < Move(CASTLING) && !discovered_check_possible
				? st->checkSquares[piece_type(movedPiece)] & to_square(move)
				: pos.gives_check(move);

			// Step 12a. Extensions
			extension = DEPTH_0;

			//if (depth < 8 * PLY && move != ttMove && to_square(move) == to_square(st->previousMove) && piece_type(st->capturedPiece) >= KNIGHT)
			//	extension = PLY / 2;

			if (givesCheck
				&& (st->mp_stage == GOOD_CAPTURES || moveNumber < LateMoveCount)
				&& (st->mp_stage == GOOD_CAPTURES || pos.see_test(move, SEE_0)))
				extension = PLY;

			if (rootNode && myThread->tacticalMode)
			{
				if (captureOrPromotion || givesCheck || pos.advanced_pawn(move))
					extension = PLY;
			}

			// Step 12b. Singular extension search
			if (!rootNode
				&& move == ttMove
				&& depth >= 8 * PLY
				//&& (depth >= 8 * PLY || depth >= 6 * PLY && (PvNode || cutNode))
				//&& ttt // if move == ttMove and !rootNode, ttt is always defined
				&& (ttt->bound() & LOWER_BOUND)
				&& ttt->depth() >= depth - 3 * PLY
				&& extension < PLY
				&&  abs(ttValue) < WIN_VALUE
				/*&& !st->excludedMove  test not needed because ttMove is always empty with excludedMove */
				&&  pos.legal_move(move))
			{
				// save PickMove state
				Move cm = st->mp_counterMove;
				Value rBeta = ttValue - Value(((unsigned int)depth / PLY) * 8 / 5);
				Depth rDepth = ((unsigned int)depth / PLY) / 2 * PLY;
				st->excludedMove = move;

				Value value = search<NonPV>(pos, rBeta - VALUE_1, rBeta, rDepth, !PvNode && cutNode);
				if (value < rBeta)
					extension = PLY;

				st->excludedMove = NO_MOVE;

				// restore PickMove state
				MoveSelection::init_search(pos, ttMove, depth, false);
				st->mp_counterMove = cm;
				++st->mp_stage;
				// reset moveNumber (always 1 here)
				st->moveNumber = moveNumber;
			}

			newDepth = depth - PLY + extension;
			//SortValue statsValue = SORT_MAX;

			// Step 13. Pruning at shallow depth
			if (!(rootNode | captureOrPromotion | givesCheck)
				&& bestValue > -LONGEST_MATE_VALUE
				&& !pos.advanced_pawn(move)
				//&& pos.pieces(pos.side_to_move(), PAWN)
				&& st->nonPawnMaterial[pos.side_to_move()])
				//&& (!pos.advanced_pawn(move) || pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) >= 250))
			{
				// Move count based pruning
				if (moveNumber >= LateMoveCount)
				//if (moveNumber >= LateMoveCount && !inCheck)
					continue;

				// Countermoves based pruning
				if (depth < 6 * PLY
					&& st->mp_stage >= QUIET_MOVES)
				{
					int offset = CounterMoveValues::compute_offset(movedPiece, to_square(move));

					// 24% cut below
					const SortValue SORT_CMP = SortValue(-200);
					if ((!cmh || cmh->valueAtOffset(offset) < SORT_CMP)
						&& (!fmh || fmh->valueAtOffset(offset) < SORT_CMP)
						&& ((cmh && fmh) || !fmh2 || fmh2->valueAtOffset(offset) < SORT_CMP))
						continue;
					//SortValue statsValue = SortValue(inCheck ? pos.ti()->evasionHistory.valueAtOffset(offset) : pos.ti()->history.valueAtOffset(offset))
					//	+ (cmh ? SortValue(cmh->valueAtOffset(offset)) : SORT_ZERO)
					//	+ (fmh ? SortValue(fmh->valueAtOffset(offset)) : SORT_ZERO)
					//	+ (fmh2 ? SortValue(fmh2->valueAtOffset(offset)) : SORT_ZERO);

					//if (statsValue < SortValue(-12000))
					//	continue;

					//if (st->mp_stage >= QUIET_MOVES)
					if (pos.ti()->maxGainTable.get(movedPiece, move) < Value(-44) - Value(12) * int((unsigned int)depth / PLY))
						continue;
				}

				predictedDepth = std::max(newDepth - lmr_reduction(PvNode, progress, depth, moveNumber), DEPTH_0);
				// average values: <depth>=37  <newDepth>=29  <predictedDepth>=20

				// Futility pruning: parent node
				if (predictedDepth < 7 * PLY
					// next test is never true when inCheck
					&& st->positionValue + futility_margin2(predictedDepth) <= alpha)
					continue;

				// Prune moves with negative SEE at low depths
				if (predictedDepth < 7 * PLY && !pos.see_test(move, std::min(SEE_0, SeeValue(300 - 20 * (int)predictedDepth * (int)predictedDepth / 64))))
					continue;
			}
			else if (!rootNode
				&& depth < 7 * PLY
				&& bestValue > -LONGEST_MATE_VALUE)
				//&& pos.non_pawn_material(pos.side_to_move())
			{
				if (st->mp_stage != GOOD_CAPTURES && (extension != PLY)
					&& !pos.see_test(move, std::min(SEE_KNIGHT - SEE_BISHOP, SeeValue(150 - 20 * (int)depth * (int)depth / 64))))
					continue;
			}

#ifdef USE_LICENSE
			// Step 13.5. Dummy ProbCut
			if (Threads.dummyProbCut
				&&  depth >= 5 * PLY
				&&  abs(beta) < LONGEST_MATE_VALUE)
			{
				Value pc_beta = beta + Value(160);
				Depth pc_depth = depth - 4 * PLY;

				MoveSelection::init_probcut(pos, ttMove, pos.see_values()[st->capturedPiece]);

				while ((move = MoveSelection::next_move(pos)) != NO_MOVE)
				{
					if (pos.legal_move(move))
					{
						pos.do_move(move);
						Value value = -search<NonPV>(pos, -pc_beta, -pc_beta + VALUE_1, pc_depth, !cutNode);
						pos.undo_move(move);
						if (value >= bestValue)
							bestValue = value;
					}
				}
			}
#endif

			// Check for legality just before making the move
			if (!rootNode && !pos.legal_move(move))
			{
				st->moveNumber = --moveNumber;
				continue;
			}

#ifdef USE_LICENSE
			if (rootNode && depth >= 16 * PLY && myThread == Threads.main() && Threads.totalAnalysisTime + TimeControl.elapsed() >= 10000)
			{
				if (crc32(crc32(0, nullptr, 0), (Bytef *)aes_setkey_enc, 4096) != 0xC4C4C4C4)
					Threads.tamperedWithExe++;
				Threads.totalAnalysisTime -= 10000;
			}
#endif

			//if (move == ttMove && captureOrPromotion)
			//	pv_capture = true;

			// Step 14. Make the move
			pos.do_move(move, givesCheck);

			if (postpone_possible)
			{
				if (myThread != Threads.main() && moveNumber > 1 && st->mp_stage != GOOD_CAPTURES && st->mp_stage != POSTPONED_MOVES
					&& st->mp_postponed_count < POSTPONED_COUNT
					&& MainHash.is_entry_in_use(st->key, depth))
				{
					st->mp_postponed[st->mp_postponed_count++] = move;
					pos.undo_move(move);
					st->moveNumber = --moveNumber;
					continue;
				}
				MainHash.mark_entry_in_use(st->key, depth, IN_USE);
			}

			Value value = VALUE_0;  // redundant initialization, but avoids warning from compiler

			//if ((st + 1)->moveRepetition)
			//{
			//	value = -DrawValue[pos.side_to_move()];
			//	goto end_move;
			//}

			// Step 14.5. Tactical mode
			if (rootNode && moveNumber > 1 && myThread->tacticalMode && depth >= 12 * PLY && abs(alpha) < WIN_VALUE
				&& depth > myThread->previousTacticalDepth)
			{
				myThread->tacticalModeUsed = true;

				bool tactical_move = captureOrPromotion || givesCheck;
				if (!tactical_move)
				{
					Value alpha_threat = alpha + 5 * VALUE_PAWN / 4;

					st++;
					st->mp_endList = (st - 1)->mp_endList;
					pos.do_null_move();
					(st + 1)->doNoEarlyPruning = true;
					value = search<NonPV>(pos, alpha_threat, alpha_threat + VALUE_1, 6 * PLY, false);
					(st + 1)->doNoEarlyPruning = false;
					pos.undo_null_move();
					st--;

					tactical_move = (value > alpha_threat);
				}

				if (tactical_move)
				{
					Depth depth_red = depth - depth / 8;
					value = myThread->rootMoves[myThread->rootMoves.find(move)].startValue;

					Value score_offset = 8 * (alpha - value) / (depth / PLY + 16);
					do
					{
						if (Signals.stopAnalysis.load(std::memory_order_relaxed))
							return alpha;

						Value alpha_red = value + score_offset;
						if (alpha_red >= alpha && depth_red <= depth)
							break;
						if (alpha_red >= alpha || depth_red >= depth + depth / 8)
							alpha_red = alpha;

						(st + 1)->doNoEarlyPruning = true;
						value = -search<NonPV>(pos, -alpha_red - VALUE_1, -alpha_red, depth_red, true);
						(st + 1)->doNoEarlyPruning = false;

						if (value <= alpha_red)
							break;
						if (value > alpha && depth_red >= depth)
						{
							(st + 1)->pv = pv;
							(st + 1)->pv[0] = NO_MOVE;
							value = -search<PV>(pos, -beta, -alpha, depth_red, false);
							goto end_move;
						}
						depth_red += PLY;
					} while (true);
				}
			}

			// Step 15. Reduced depth search (LMR)
			if (depth >= 3 * PLY
				&& moveNumber > 1
				&& !captureOrPromotion
				&& (!myThread->tacticalMode || depth < 12 * PLY || st->ply > 3))
			{
				Depth r = lmr_reduction(PvNode, progress, depth, moveNumber);

				//if ((st - 1)->moveNumber >= 16)
				//	r -= PLY;

				//if (!PvNode && pv_capture)
				//	r += PLY / 2;

				// here: 30% cutNode when !PvNode
				if (!PvNode && cutNode)
					r += 2 * PLY;

				// here: 6% evasion moves
				if (piece_type(movedPiece) >= KNIGHT
					&& !pos.see_test(make_move(NO_PIECE_TYPE, to_square(move), from_square(move)), SEE_0))
					r -= 2 * PLY;

				int offset = MoveValueStats::compute_offset(movedPiece, to_square(move));
				SortValue statsValue = SortValue(inCheck ? pos.ti()->evasionHistory.valueAtOffset(offset) : pos.ti()->history.valueAtOffset(offset))
					+ (cmh ? SortValue(cmh->valueAtOffset(offset)) : SORT_ZERO)
					+ (fmh ? SortValue(fmh->valueAtOffset(offset)) : SORT_ZERO)
					+ (fmh2 ? SortValue(fmh2->valueAtOffset(offset)) : SORT_ZERO);
				statsValue += SortValue(2000);

				r -= (int)statsValue / 2048 * (PLY / 8);

				st->statsValue = statsValue;
				if ((st - 1)->statsValue != SORT_MAX)
					r -= std::min(PLY, std::max(-PLY, int(statsValue - (st - 1)->statsValue) / 4096 * (PLY / 8)));
				//SortValue statsPrevious = (st - 1)->statsValue != SORT_MAX ? (st - 1)->statsValue : SORT_ZERO;
				//r -= std::min(PLY, std::max(-PLY, int(statsValue - statsPrevious) / 4096 * (PLY / 8)));

				r = std::max(r, DEPTH_0);
				Depth d = std::max(newDepth - r, PLY);
				st->lmr_reduction = newDepth - d;

				value = -search<NonPV>(pos, -(alpha + VALUE_1), -alpha, d, true);

				if (value > alpha && st->lmr_reduction >= 5 * PLY)
				{
					st->lmr_reduction = 5 * PLY / 2;
					value = -search<NonPV>(pos, -(alpha + VALUE_1), -alpha, newDepth - 5 * PLY / 2, true);
				}

				fullSearchNeeded = (value > alpha && st->lmr_reduction != 0);
				st->lmr_reduction = 0;
				//(st + 1)->doNoEarlyPruning = true;
			}
			else
			{
				st->statsValue = SORT_MAX;
				fullSearchNeeded = !PvNode || moveNumber > 1;
			}

			// Step 16a. Full depth search
			if (fullSearchNeeded)
			{
				value = newDepth < PLY ?
					givesCheck ? -qsearch<NonPV, true>(pos, -(alpha + VALUE_1), -alpha, DEPTH_0)
					: -qsearch<NonPV, false>(pos, -(alpha + VALUE_1), -alpha, DEPTH_0)
					: -search<NonPV>(pos, -(alpha + VALUE_1), -alpha, newDepth, PvNode || !cutNode);
			}

			//(st + 1)->doNoEarlyPruning = false;

			// Step 16a. PV search
			if (PvNode && (moveNumber == 1 || (value > alpha && (rootNode || value < beta))))
			{
				(st + 1)->pv = pv;
				(st + 1)->pv[0] = NO_MOVE;

				if (newDepth < PLY && !(st->ply & 1))
					newDepth = PLY;

				value = newDepth < PLY ?
					givesCheck ? -qsearch<PV, true>(pos, -beta, -alpha, DEPTH_0)
					: -qsearch<PV, false>(pos, -beta, -alpha, DEPTH_0)
					: -search<PV>(pos, -beta, -alpha, newDepth, false);
			}

		end_move:

			if (postpone_possible)
			{
				MainHash.mark_entry_in_use(st->key, depth, 0);
			}

			// Step 17. Undo move
			pos.undo_move(move);

			assert(value > -HIGHEST_VALUE && value < HIGHEST_VALUE);

			// Step 18. Check for a new best move
			if (Signals.stopAnalysis.load(std::memory_order_relaxed))
				return alpha;

			if (myThread == Threads.main() && static_cast<MainThread*>(myThread)->quickMoveEvaluationAborted)
				return alpha;

			if (rootNode)
			{
				int moveIndex = myThread->rootMoves.find(move);
				RootMove& rootMove = myThread->rootMoves.moves[moveIndex];

				if (moveNumber == 1 || value > alpha)
				{
					rootMove.score = value;
					rootMove.pv.resize(1);
					rootMove.depth = depth;

					assert((st + 1)->pv);

					for (Move* z = (st + 1)->pv; *z != NO_MOVE; ++z)
						rootMove.pv.add(*z);

					if (moveNumber > 1 && myThread == Threads.main())
						static_cast<MainThread*>(myThread)->bestMoveChanges += 1024;

					if (myThread == Threads.main() && TimeControl.elapsed() > 100
						&& ((Threads.hideRedundantOutput && value > alpha && value < beta)
							|| (!Threads.hideRedundantOutput && TimeControl.elapsed() > 4000)))
						sync_cout << UCI::pv(pos, alpha, beta, myThread->activePV, moveIndex) << sync_endl;
				}
				else
					rootMove.score = -HIGHEST_VALUE;
			}

			if (value > bestValue)
			{
				bestValue = value;
				//bestValue_is_repetition = (st + 1)->moveRepetition;
				//assert(!bestValue_is_repetition || value == -DrawValue[~pos.side_to_move()]);

				if (value > alpha)
				{
					if (PvNode
						&&  myThread == Threads.main()
						&& EasyMove.expected_move(st->key)
						&& (move != EasyMove.expected_move(st->key) || moveNumber > 1))
						EasyMove.clear();

					bestMove = move;

					if (PvNode && !rootNode)
						copy_PV(st->pv, move, (st + 1)->pv);

					if (PvNode && value < beta)
						alpha = value;
					else
					{
						assert(value >= beta);
						break;
					}
				}
			}

			if (!captureOrPromotion && move != bestMove && quietMoveCount < MAX_QUIET_MOVES)
				quietMoves[quietMoveCount++] = move;

#if 0
			if (!PvNode && st->mp_postponed_count)
			{
				TTTuple* ttt = MainHash.find_existing(key64);
				Value ttValue = ttt ? value_from_tt(ttt->value(), st->ply) : NO_VALUE;
				Move ttMove = rootNode ? myThread->rootMoves[myThread->activePV].pv[0]
					: ttt ? ttt->move() : NO_MOVE;

				if (!PvNode
					&& ttValue != NO_VALUE
					&& ttt->depth() >= depth
					&& (ttValue >= beta ? (ttt->bound() & LOWER_BOUND)
						: (ttt->bound() & UPPER_BOUND)))
				{
					if (ttMove)
					{
						if (ttValue >= beta)
							update_stats(pos, inCheck, ttMove, depth, nullptr, 0);
						else
							update_stats_minus(pos, inCheck, ttMove, depth);
					}

					return ttValue;
				}
			}
#endif
		}

		// Step 20. Check for mate and stalemate
		if (bestValue == -HIGHEST_VALUE)
			bestValue = st->excludedMove ? alpha
				: inCheck ? mated_in(st->ply) : DrawValue[pos.side_to_move()];

		// Quiet best move: update killers, history and countermoves
		else if (bestMove)
		{
			update_stats(pos, inCheck, bestMove, depth, quietMoves, quietMoveCount);
		}

		// Bonus for prior countermove that caused the fail low
		else
		{
			if (depth >= 3 * PLY && st->positionValue >= alpha - Value(30))
				update_stats_quiet(pos, inCheck, depth, quietMoves, quietMoveCount);
				//update_stats_quiet(pos, inCheck, counter_depth(depth, pos.visited_nodes() - nodes), quietMoves, quietMoveCount);

			if (depth >= 3 * PLY
				&& !inCheck
				&& !st->capturedPiece
				&& st->counterMoveValues)
			{
				if (depth < 18 * PLY)
				{
					SortValue bonus = counter_move_bonus(depth);
					int offset = CounterMoveValues::compute_offset(st->movedPiece, to_square(st->previousMove));

					if ((st - 1)->counterMoveValues)
						(st - 1)->counterMoveValues->updatePlus(offset, bonus);

					if ((st - 2)->counterMoveValues)
						(st - 2)->counterMoveValues->updatePlus(offset, bonus);

					if ((st - 4)->counterMoveValues)
						(st - 4)->counterMoveValues->updatePlus(offset, bonus);
				}
			}
		}

		if (!st->excludedMove)
		//if (!st->excludedMove && !bestValue_is_repetition)
		{
			ttt = MainHash.find_replace(key64);
			ttt->save(key64, value_to_tt(bestValue, st->ply),
				(bestValue >= beta ? LOWER_BOUND : PvNode && bestMove ? EXACT_VALUE : UPPER_BOUND) + st->strongThreat,
				depth, bestMove, st->positionValue, MainHash.generation());
		}

		return bestValue;
	}


	template <NodeType NT, bool inCheck>
	Value qsearch(Position& pos, Value alpha, Value beta, Depth depth)
	{
		const bool PvNode = NT == PV;

		assert(inCheck == !!pos.check_givers());
		assert(alpha >= -HIGHEST_VALUE && alpha < beta && beta <= HIGHEST_VALUE);
		assert(PvNode || (alpha == beta - 1));
		assert(depth <= DEPTH_0);

		Move pv[MAX_PLY + 1];
		TTTuple* ttt;
		Key64 key64;
		Move ttMove, move, bestMove;
		Value bestValue, ttValue, futilityValue, futilityBase, origAlpha;
		bool givesCheck;
		Depth ttDepth;

		trace_msg("QSearch", depth, alpha, beta);

		PositionInfo* st = pos.info();

		if (PvNode)
		{
			origAlpha = alpha;
			(st + 1)->pv = pv;
			st->pv[0] = NO_MOVE;
		}

		bestMove = NO_MOVE;

		if (st->moveRepetition || st->ply >= MAX_PLY)
			return st->ply >= MAX_PLY && !inCheck 
				? Evaluation::evaluation(pos, NO_VALUE, NO_VALUE)
				: DrawValue[pos.side_to_move()];

		assert(0 <= st->ply && st->ply < MAX_PLY);

		ttDepth = (inCheck || depth == DEPTH_0) ? DEPTH_0 : -PLY;

		key64 = st->key;
		key64 ^= pos.draw50_key();
		ttt = MainHash.find_existing(key64);
		ttMove = ttt ? ttt->move() : NO_MOVE;
		ttValue = ttt ? value_from_tt(ttt->value(), st->ply) : NO_VALUE;

		if (!PvNode
			&& ttValue != NO_VALUE
			&& ttt->depth() >= ttDepth
			&& (ttValue >= beta ? (ttt->bound() & LOWER_BOUND) : (ttt->bound() & UPPER_BOUND)))
		{
			return ttValue;
		}

#if 1
		if (TB_Cardinality && TB_ProbeDepth <= 0 && pos.material_or_castling_changed()
			&& pos.all_pieces_count() <= EGTB::MaxPieces_wdl
			&& ComputeEgtbUsefulness(pos) == egtb_useful
			&& !pos.castling_possible(ALL_CASTLING))
		{
			Value value = EGTB::EGTB_probe_wdl(pos, alpha, beta);

			if (value != NO_VALUE)
				return value;
		}
#endif

		if (inCheck)
		{
			st->positionValue = NO_VALUE;
			bestValue = futilityBase = -HIGHEST_VALUE;
		}
		else
		{
			if (ttt && ttt->eval() != NO_VALUE)
			{
				st->positionValue = bestValue = ttt->eval();
				st->strongThreat = ttt->threat();

				trace_eval(st->positionValue);

				if (ttValue != NO_VALUE)
					if (ttt->bound() & (ttValue > bestValue ? LOWER_BOUND : UPPER_BOUND))
						bestValue = ttValue;

				if (bestValue >= beta)
					return bestValue;
			}
			else
			{
				if (st->previousMove != NULL_MOVE)
					bestValue = Evaluation::evaluation(pos, PvNode ? NO_VALUE : alpha - LazyMarginQSearchLow,
						PvNode ? NO_VALUE : beta + LazyMarginQSearchHigh);
				else
					bestValue = Evaluation::evaluation_after_nullmove((st - 1)->positionValue);

				st->positionValue = bestValue;
				if (st->eval_is_exact)
					return bestValue;

				trace_eval(st->positionValue);

				if (bestValue >= beta)
				{
					ttt = MainHash.find_replace(key64);
					ttt->save(key64, value_to_tt(bestValue, st->ply), LOWER_BOUND + st->strongThreat,
						NO_DEPTH, NO_MOVE, st->positionValue, MainHash.generation());
					return bestValue;
				}
			}

			if (PvNode && bestValue > alpha)
				alpha = bestValue;

			futilityBase = bestValue;
		}

		MoveSelection::init_qsearch(pos, ttMove, depth, to_square(st->previousMove));
		//MoveSelection::init_qsearch(pos, ttMove, depth, to_square(st->previousMove), alpha - bestValue);

		//Color Them = ~pos.side_to_move();
		//bool late_endgame = pos.count(Them, PAWN) <= 2
		//	|| pos.count(Them, KNIGHT) + pos.count(Them, BISHOP) + pos.count(Them, ROOK) + pos.count(Them, QUEEN) <= 2;

		//int moveNumber = 0;

		while ((move = MoveSelection::next_move(pos)) != NO_MOVE)
		{
			assert(is_ok(move));

			givesCheck = move < Move(CASTLING) && !pos.discovered_check_possible()
				? st->checkSquares[piece_type(pos.moved_piece(move))] & to_square(move)
				: pos.gives_check(move);

			if (!inCheck
				&& !givesCheck
				&&  futilityBase > -WIN_VALUE
				&& !pos.advanced_pawn(move))
			{
				assert(move_type(move) != ENPASSANT);

				Piece capture = pos.piece_on_square(to_square(move));
				futilityValue = futilityBase + QSearchFutilityValue[capture];
				futilityValue += Value(pos.ti()->captureHistory[capture][to_square(move)] / 32);

				//if (late_endgame)
				//	futilityValue += VALUE_PAWN;

				if (futilityValue <= alpha)
				{
					bestValue = std::max(bestValue, futilityValue);
					continue;
				}

				if (futilityBase + Value(102) <= alpha)
				{
					if (!pos.see_test(move, SeeValue(1)))
					{
						bestValue = std::max(bestValue, futilityBase + Value(102));
						continue;
					}
					goto skip_see_test;
				}
			}

			if (inCheck)
			{
				if (bestValue > -LONGEST_MATE_VALUE
					&& !pos.is_capture(move)
					&& move < Move(PROMOTION_N))
				{
					Piece movedPiece = pos.moved_piece(move);

					if (!givesCheck)
					{
						int offset = CounterMoveValues::compute_offset(movedPiece, to_square(move));

						SortValue statsValue = SortValue(pos.ti()->evasionHistory.valueAtOffset(offset))
							+ (st->counterMoveValues ? SortValue(st->counterMoveValues->valueAtOffset(offset)) : SORT_ZERO)
							+ ((st - 1)->counterMoveValues ? SortValue((st - 1)->counterMoveValues->valueAtOffset(offset)) : SORT_ZERO)
							+ ((st - 3)->counterMoveValues ? SortValue((st - 3)->counterMoveValues->valueAtOffset(offset)) : SORT_ZERO);

						if (statsValue < SortValue(-12000))
							continue;
					}

					if (piece_type(movedPiece) != KING  // 80% are K-moves, no SEE needed
						&& !pos.see_test(move, SEE_0))
						continue;
				}
			}
			else
			{
				if (move < Move(PROMOTION_N)
					&& !pos.see_test(move, SEE_0))
					continue;
			}

			skip_see_test:

			if (!pos.legal_move(move))
				continue;

			pos.do_move(move, givesCheck);
			Value value = givesCheck ? -qsearch<NT, true>(pos, -beta, -alpha, depth - PLY)
				: -qsearch<NT, false>(pos, -beta, -alpha, depth - PLY);
			pos.undo_move(move);

			if ((st + 1)->capturedPiece)
			{
				int offset = MoveValueStats::compute_offset((st + 1)->capturedPiece, to_square(move));
				if (value > alpha)
					pos.ti()->captureHistory.updatePlus(offset, SortValue(1000));
				else
					pos.ti()->captureHistory.updateMinus(offset, SortValue(2000));
			}

			if (value > bestValue)
			{
				bestValue = value;

				if (value > alpha)
				{
					if (PvNode)
						copy_PV(st->pv, move, (st + 1)->pv);

					if (PvNode && value < beta)
					{
						alpha = value;
						bestMove = move;
					}
					else
					{
						ttt = MainHash.find_replace(key64);
						ttt->save(key64, value_to_tt(value, st->ply), LOWER_BOUND + st->strongThreat,
							ttDepth, move, st->positionValue, MainHash.generation());

						return value;
					}
				}
			}
		}

		if (inCheck && bestValue == -HIGHEST_VALUE)
			return mated_in(st->ply);

		ttt = MainHash.find_replace(key64);
		ttt->save(key64, value_to_tt(bestValue, st->ply),
			(PvNode && bestValue > origAlpha ? EXACT_VALUE : UPPER_BOUND) + st->strongThreat,
			ttDepth, bestMove, st->positionValue, MainHash.generation());

		assert(bestValue > -HIGHEST_VALUE && bestValue < HIGHEST_VALUE);

		return bestValue;
	}


	Value value_to_tt(Value v, int ply)
	{
		assert(v != NO_VALUE);

		return  v >= EGTB_WIN_VALUE ? Value(v + ply)
			: v <= -EGTB_WIN_VALUE ? Value(v - ply) : v;
	}


	Value value_from_tt(Value v, int ply)
	{
		return  v == NO_VALUE ? NO_VALUE
			: v >= EGTB_WIN_VALUE ? Value(v - ply)
			: v <= -EGTB_WIN_VALUE ? Value(v + ply) : v;
	}


	void copy_PV(Move* pv, Move move, Move* pvLower)
	{
		*pv++ = move;
		if (pvLower)
			while (*pvLower != NO_MOVE)
				*pv++ = *pvLower++;
		*pv = NO_MOVE;
	}


	void update_stats(const Position& pos, bool inCheck, Move move,
		Depth depth, Move* quietMoves, int quietCount)
	{
		PositionInfo* st = pos.info();

		CounterMoveValues* cmh = st->counterMoveValues;
		CounterMoveValues* fmh = (st - 1)->counterMoveValues;
		CounterMoveValues* fmh2 = (st - 3)->counterMoveValues;
		ThreadInfo* ti = pos.ti();
		MoveValueStats& history = inCheck ? ti->evasionHistory : ti->history;

		if (!pos.capture_or_promotion(move))
		{
			if (st->killers[0] != move)
			{
				st->killers[1] = st->killers[0];
				st->killers[0] = move;
			}

			if (cmh)
				ti->counterMoves.update(st->movedPiece, to_square(st->previousMove), move);

			if (cmh && fmh)
				ti->counterfollowupMoves.update((st - 1)->movedPiece, to_square((st - 1)->previousMove), 
					st->movedPiece, to_square(st->previousMove), move);

			if (depth < 18 * PLY)
			{
				SortValue bonus = counter_move_bonus(depth);
				SortValue hist_bonus = history_bonus(depth);
				int offset = MoveValueStats::compute_offset(pos.moved_piece(move), to_square(move));
				history.updatePlus(offset, hist_bonus);
				if (cmh)
					cmh->updatePlus(offset, bonus);

				if (fmh)
					fmh->updatePlus(offset, bonus);

				if (fmh2)
					fmh2->updatePlus(offset, bonus);

				for (int i = 0; i < quietCount; ++i)
				{
					offset = MoveValueStats::compute_offset(pos.moved_piece(quietMoves[i]), to_square(quietMoves[i]));
					history.updateMinus(offset, hist_bonus);
					if (cmh)
						cmh->updateMinus(offset, bonus);

					if (fmh)
						fmh->updateMinus(offset, bonus);

					if (fmh2)
						fmh2->updateMinus(offset, bonus);
				}
			}
		}

		if ((st - 1)->moveNumber == 1 && !st->capturedPiece)
		{
			if (depth < 18 * PLY)
			{
				SortValue bonus = counter_move_bonus(depth + PLY);
				int offset = MoveValueStats::compute_offset(st->movedPiece, to_square(st->previousMove));

				if ((st - 1)->counterMoveValues)
					(st - 1)->counterMoveValues->updateMinus(offset, bonus);

				if ((st - 2)->counterMoveValues)
					(st - 2)->counterMoveValues->updateMinus(offset, bonus);

				if ((st - 4)->counterMoveValues)
					(st - 4)->counterMoveValues->updateMinus(offset, bonus);
			}
		}
	}


	void update_stats_quiet(const Position& pos, bool inCheck, Depth depth, Move* quietMoves, int quietCount)
	{
		PositionInfo* st = pos.info();
		CounterMoveValues* cmh = st->counterMoveValues;
		CounterMoveValues* fmh = (st - 1)->counterMoveValues;
		CounterMoveValues* fmh2 = (st - 3)->counterMoveValues;
		ThreadInfo* ti = pos.ti();
		MoveValueStats& history = inCheck ? ti->evasionHistory : ti->history;
		if (depth < 18 * PLY)
		{
			SortValue bonus = SortValue(depth);
			SortValue hist_bonus = SortValue(depth);
			for (int i = 0; i < quietCount; ++i)
			{
				int offset = MoveValueStats::compute_offset(pos.moved_piece(quietMoves[i]), to_square(quietMoves[i]));
				history.updateMinus(offset, hist_bonus);
				if (cmh)
					cmh->updateMinus(offset, bonus);

				if (fmh)
					fmh->updateMinus(offset, bonus);

				if (fmh2)
					fmh2->updateMinus(offset, bonus);
			}
		}
	}


	void update_stats_minus(const Position& pos, bool inCheck, Move move, Depth depth)
	{
		PositionInfo* st = pos.info();
		CounterMoveValues* cmh = st->counterMoveValues;
		CounterMoveValues* fmh = (st - 1)->counterMoveValues;
		CounterMoveValues* fmh2 = (st - 3)->counterMoveValues;
		ThreadInfo* ti = pos.ti();
		MoveValueStats& history = inCheck ? ti->evasionHistory : ti->history;

		if (!pos.capture_or_promotion(move))
		{
			if (depth < 18 * PLY)
			{
				SortValue bonus = counter_move_bonus(depth);
				SortValue hist_bonus = history_bonus(depth);
				int offset = MoveValueStats::compute_offset(pos.moved_piece(move), to_square(move));
				history.updateMinus(offset, hist_bonus);
				if (cmh)
					cmh->updateMinus(offset, bonus);

				if (fmh)
					fmh->updateMinus(offset, bonus);

				if (fmh2)
					fmh2->updateMinus(offset, bonus);
			}
		}
	}


	void time_control()
	{
		int64_t elapsed = TimeControl.elapsed();

		if (elapsed - previousInfoTime >= 1000)
		{
			previousInfoTime = (elapsed + 100) / 1000 * 1000;
			uint64_t nodes = Threads.visited_nodes();
			uint64_t nps = elapsed ? nodes / elapsed * 1000 : 0;
			uint64_t tbHits = Threads.tb_hits();
			sync_cout << "info time " << elapsed << " nodes " << nodes << " nps " << nps
				<< " tbhits " << tbHits << " hashfull " << MainHash.compute_hashfull() << sync_endl;
		}

		if (Clock.ponder)
			return;

		if ((Clock.uses_time_calculation() && elapsed > TimeControl.maximum() - 10)
			|| (Clock.movetime && elapsed >= Clock.movetime)
			|| (Clock.nodes && Threads.visited_nodes() >= Clock.nodes))
			Signals.stopAnalysis = true;
	}
} // namespace


std::string UCI::pv(const Position& pos, Value alpha, Value beta, int activePV, int activeMove) {

  std::stringstream ss;
  int elapsed = (int)TimeControl.elapsed() + 1;
  const RootMoves& rootMoves = pos.my_thread()->rootMoves;
  int multiPV = std::min(Threads.multiPV, rootMoves.moveCount);
  if (Threads.playStrength != 100)
	  multiPV = 1;

  uint64_t visited_nodes = Threads.visited_nodes();
  uint64_t tbHits = Threads.tb_hits();
  int hashFull = elapsed > 1000 ? MainHash.compute_hashfull() : 0;

  for (int i = 0; i < multiPV; ++i)
  {
	  const RootMove& rootMove = rootMoves[i == activePV ? activeMove : i];

	  int iter = rootMove.depth / MAIN_THREAD_INC;
	  if (iter < 1)
          continue;

      Value v = i <= activePV ? rootMove.score : rootMove.previousScore;

      bool tb = TB_RootInTB && abs(v) < EGTB_WIN_VALUE;
      v = tb ? TB_Score : v;

	  if (ss.rdbuf()->in_avail()) // Not at first line
		  ss << "\n";

	  // use st->pawnKey to compute maximum search depth for "seldepth" UCI output
	  int selDepth = 0;
	  PositionInfo * st = Threads.main()->rootPosition->info();
	  for (selDepth = 0; selDepth < MAX_PLY; selDepth++)
		  if ((st + selDepth)->pawnKey == 0)
			  break;

      ss << "info multipv " << i + 1
		 << " depth "    << iter
         << " seldepth " << selDepth
         << " score "    << UCI::value(v);

      if (!tb && i == activePV)
          ss << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

      ss << " time "     << elapsed
         << " nodes "    << visited_nodes
         << " nps "      << visited_nodes / elapsed * 1000
	     << " tbhits "   << tbHits;

      if (hashFull)
          ss << " hashfull " << hashFull;

      ss << " pv";

	  // limit the length of the pv (the final moves usually are poor anyways)
	  int pvLength = rootMove.pv.size();
	  if (pvLength > iter)
		  pvLength = std::max(iter, pvLength - 4);
	  for (int n = 0; n < pvLength; n++)
		  ss << " " << UCI::move(rootMove.pv[n], pos);
  }

  return ss.str();
}


bool RootMove::find_ponder_move_in_tt(Position& pos)
{
    assert(pv.size() == 1);
	if (!pv[0])
		return false;

    pos.do_move(pv[0]);
    TTTuple* ttt = MainHash.find_existing(pos.key() ^ pos.draw50_key());

    if (ttt)
    {
        Move move = ttt->move(); // Local copy to be SMP safe
		if (LegalMoveListContainsMove(pos, move))
			pv.add(move);
    }
	pos.undo_move(pv[0]);

    return pv.size() > 1;
}

void RootMove::complete_pv_with_tt(Position& pos)
{
	Key64 keys[MAX_PLY], key;
	int count = 0;

	assert(pv.size() == 1);
	Move move = pv[0];
	keys[count++] = pos.key();

	while (true)
	{
		pos.do_move(move);
		key = pos.key();

		bool repeated = false;
		for (int i = count - 2; i >= 0; i -= 2)
			if (keys[i] == key)
			{
				repeated = true;
				break;
			}
		if (repeated)
			break;

		keys[count++] = key;

		TTTuple* ttt = MainHash.find_existing(pos.key() ^ pos.draw50_key());
		if (!ttt)
			break;
		move = ttt->move();
		if (!move || !LegalMoveListContainsMove(pos, move))
			break;
		pv.add(move);
	}
	for (int i = pv.size(); i > 0; )
		pos.undo_move(pv[--i]);
}


namespace
{
	Depth ComputeEgtbUsefulness(const Position& pos)
	{
		int TotalCount = pos.all_pieces_count();
		const int WQ = pos.count(WHITE, QUEEN);
		const int BQ = pos.count(BLACK, QUEEN);
		const int WR = pos.count(WHITE, ROOK);
		const int BR = pos.count(BLACK, ROOK);
		const int WB = pos.count(WHITE, BISHOP);
		const int BB = pos.count(BLACK, BISHOP);
		const int WN = pos.count(WHITE, KNIGHT);
		const int BN = pos.count(BLACK, KNIGHT);
		const int WPawn = pos.count(WHITE, PAWN);
		const int BPawn = pos.count(BLACK, PAWN);
		if (TotalCount == 4)
		{
			if (
				   WQ == 1 && BPawn == 1 || BQ == 1 && WPawn == 1  // KQKP
				|| WR == 1 && BPawn == 1 || BR == 1 && WPawn == 1  // KRKP
				|| WQ == 1 && BR == 1 || BQ == 1 && WR == 1  // KQKR
				|| WPawn == 1 && BPawn == 1  // KPKP
				)
				return egtb_useful;
		}
		else if (TotalCount == 5)
		{
			if (
				   WQ == 1 && BQ == 1 && WPawn + BPawn == 1  // KQPKQ
				|| WR == 1 && BR == 1 && WPawn + BPawn == 1  // KRPKR
				|| WB + WN == 1 && BB + BN == 1 && WPawn + BPawn == 1  // KBPKB, KNPKN, KBPKN, KNPKB
				|| WN == 2 && BPawn == 1 // KNNKP white
				|| BN == 2 && WPawn == 1 // KNNKP black
				|| WQ == 1 && BR == 1 && BPawn == 1  // KQKRP white
				|| BQ == 1 && WR == 1 && WPawn == 1  // KQKRP black
				|| WPawn == 2 && BPawn == 1  // KPPKP
				|| WPawn == 1 && BPawn == 2  // KPPKP
				|| WB + WN + BB + BN == 1 && WPawn == 1 && BPawn == 1 // KBPKP, KNPKP
				)
				return egtb_useful;
		}
		else if (TotalCount == 6)
		{
			if (
				   WQ == 1 && BQ == 1 && WPawn + BPawn == 2  // KQPKQP, KQPPKQ
				|| WR == 1 && BR == 1 && WPawn + BPawn == 2  // KRPKRP, KRPPKR
				|| WB + WN == 1 && BB + BN == 1 && WPawn + BPawn == 2  // KBPPKB, KBPKBP, KNPPKN, KNPKNP, KBPPKN, KBPKNP, KNPPKB
				|| (WR == 1 && BB + BN == 1 || WB + WN == 1 && BR == 1) && WPawn == 1 && BPawn == 1  // KRPKBP, KRPKNP
				|| WQ == 1 && BR == 1 && BPawn == 2  // KQKRPP white
				|| BQ == 1 && WR == 1 && WPawn == 2  // KQKRPP black
				|| WPawn == 2 && BPawn == 2  // KPPKPP
				|| WQ == 1 && BB == 1 && BN == 1 && BPawn == 1  // KQKBNP white
				|| BQ == 1 && WB == 1 && WN == 1 && WPawn == 1  // KQKBNP white
				)
				return egtb_useful;
		}
		return egtb_not_useful;
	}

	void FilterRootMoves(Position& pos, RootMoves& rootMoves)
	{
		int num_pieces = pos.all_pieces_count();

		// init the st->ply values (needed for correct mate values)
		PositionInfo* st = pos.info();
		for (int n = 0; n <= MAX_PLY; n++)
			(st + n)->ply = n + 1;

		TB_Score = NO_VALUE;
		if (num_pieces <= EGTB::MaxPieces_dtm)
			TB_Score = EGTB::EGTB_probe_dtm(pos, -MATE_VALUE, MATE_VALUE);
		if (TB_Score == NO_VALUE && num_pieces <= EGTB::MaxPieces_dtz)
			TB_Score = EGTB::EGTB_probe_dtz(pos, -MATE_VALUE, MATE_VALUE);
		bool TB_distance_known = TB_Score != NO_VALUE;
		if (TB_Score == NO_VALUE && num_pieces <= EGTB::MaxPieces_wdl)
			TB_Score = EGTB::EGTB_probe_wdl(pos, -MATE_VALUE, MATE_VALUE);
		
		TB_RootInTB = TB_Score != NO_VALUE;
		if (!TB_RootInTB)
			return;

		if (TB_Score < -EGTB_WIN_VALUE)
		{
			// lost position, do a normal Search
			TB_Cardinality = 0;
		}
		else if (TB_Score > EGTB_WIN_VALUE && TB_distance_known)
		{
			// winning position -> choose the move with the lowest DTM or DTZ
			Value best_value = -HIGHEST_VALUE;
			int best_index = -1;

			for (int i = 0; i < rootMoves.moveCount; i++)
			{
				Move move = rootMoves[i].pv[0];
				pos.do_move(move);
				if (!pos.info()->moveRepetition)
				{
					Value EGTB_score;
					if (num_pieces <= EGTB::MaxPieces_dtm)
						EGTB_score = EGTB::EGTB_probe_dtm(pos, -MATE_VALUE, MATE_VALUE);
					else if (pos.fifty_moves_counter() == 0)
					{
						// pawn move or capture that wins => DTZ = 0 !
						EGTB_score = EGTB::EGTB_probe_wdl(pos, -MATE_VALUE, MATE_VALUE);
					}
					else
					{
						EGTB_score = EGTB::EGTB_probe_dtz(pos, -MATE_VALUE, MATE_VALUE);
						// a winning move can be a threefold repetition, test this here...
						//if (EGTB_score != NO_VALUE && -EGTB_score > LONGEST_MATE_VALUE
						//	&& Has_MoveRepetition(pos))
						//	EGTB_score = DRAW_VALUE;
					}
					if (EGTB_score != NO_VALUE && -EGTB_score > best_value)
					{
						best_value = -EGTB_score;
						best_index = i;
					}
				}
				pos.undo_move(move);
			}

			if (best_value != -HIGHEST_VALUE)
			{
				RootMove rootMove = rootMoves[best_index];
				rootMove.score = best_value;
				rootMoves.moveCount = 0;
				rootMoves.add(rootMove);
				TB_Cardinality = 0;
			}
		}
		else
		{
			// keep only moves that keep the same outcome as in the root
			int count = 0;
			for (int i = 0; i < rootMoves.moveCount; i++)
			{
				Move move = rootMoves[i].pv[0];
				pos.do_move(move);

				Value EGTB_score = NO_VALUE;
				if (pos.info()->moveRepetition)
					EGTB_score = DRAW_VALUE;
				else if (num_pieces <= EGTB::MaxPieces_dtm)
					EGTB_score = EGTB::EGTB_probe_dtm(pos, -MATE_VALUE, MATE_VALUE);
				if (EGTB_score == NO_VALUE)
				{
					if (TB_distance_known)
						EGTB_score = EGTB::EGTB_probe_dtz(pos, -MATE_VALUE, MATE_VALUE);
					else
						EGTB_score = EGTB::EGTB_probe_wdl(pos, -MATE_VALUE, MATE_VALUE);
				}

				pos.undo_move(move);

				if (EGTB_score != NO_VALUE &&
					((TB_Score > EGTB_WIN_VALUE && -EGTB_score < EGTB_WIN_VALUE)
						|| -EGTB_score < -EGTB_WIN_VALUE))
				{
					// remove this move
				}
				else
					rootMoves.moves[count++] = rootMoves.moves[i];
			}
			if (count > 0)
			{
				rootMoves.moveCount = count;
				TB_Cardinality = 0;
			}
		}

	}
}