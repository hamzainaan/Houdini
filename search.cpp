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
	SignalsType Signalen;
	TimeLimit Klok;
	bool Running;
	uint8_t LMReducties[2][2][64 * (int)PLY][64]; // [pv][vooruitgang][diepte][zet]
}

// diepte opgeteld bij EGTB_ProbeDepth
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
	inline Value futility_marge1(Depth d) 
	{ 
		return margin1[(unsigned int)d / PLY];
	}
	
	inline Value futility_marge2(Depth d)
	{
		return Value(204 + 160 * int((unsigned int)d / PLY));
	}

	int LateZetAantal[2][32] =   // [vooruitgang][diepte/2]
	{
		{ 0, 0, 3, 3, 4, 5, 6,  7,  8, 10, 12, 15, 17, 20, 23, 26, 30, 33, 37, 40, 44, 49, 53, 58, 63, 68,  73,  78,  83,  88,  94, 100 },
		{ 0, 0, 5, 5, 6, 7, 9, 11, 14, 17, 20, 23, 27, 31, 35, 40, 45, 50, 55, 60, 65, 71, 77, 84, 91, 98, 105, 112, 119, 127, 135, 143 }
	};
	inline int late_move_count(Depth d, bool vooruitgang)
	{
		return LateZetAantal[vooruitgang][(unsigned int)d / (PLY / 2)];
	}

	inline Depth lmr_reductie(bool pv, bool vg, Depth d, int n)
	{
		return Depth(LMReducties[pv][vg][std::min((int)d, 64 * (int)PLY - 1)][std::min(n, 63)]);
	}

	SortValue counterZetBonus[MAX_PLY];
	inline SortValue counter_zet_bonus(Depth d) { return counterZetBonus[(unsigned int)d / PLY]; }
	inline SortValue history_bonus(Depth d) { return counterZetBonus[(unsigned int)d / PLY]; }

	inline Depth counter_diepte(Depth d, uint64_t knopen)
	{
		//return d;
		return (int)msb(knopen) > (int)d / 9 ? d + PLY : d;
	}

	struct EasyMoveManager
	{
		void clear()
		{
			sleutelNaTweeZetten = 0;
			derdeZetStabiel = 0;
			pv[0] = pv[1] = pv[2] = NO_MOVE;
		}

		Move expected_move(Key64 sleutel) const
		{
			return sleutelNaTweeZetten == sleutel ? pv[2] : NO_MOVE;
		}

		void verversPv(Position& pos, const PrincipalVariation& pvNieuw)
		{
			assert(pvNieuw.size() >= 3);

			derdeZetStabiel = (pvNieuw[2] == pv[2]) ? derdeZetStabiel + 1 : 0;

			if (pvNieuw[0] != pv[0] || pvNieuw[1] != pv[1] || pvNieuw[2] != pv[2])
			{
				pv[0] = pvNieuw[0];
				pv[1] = pvNieuw[1];
				pv[2] = pvNieuw[2];

				pos.do_move(pvNieuw[0]);
				pos.do_move(pvNieuw[1]);
				sleutelNaTweeZetten = pos.sleutel();
				pos.undo_move(pvNieuw[1]);
				pos.undo_move(pvNieuw[0]);
			}
		}

		int derdeZetStabiel;
		Key64 sleutelNaTweeZetten;
		Move pv[3];
	};

	EasyMoveManager GemakkelijkeZet;
	Value RemiseWaarde[COLOR_N];
	uint64_t vorigeInfoTijd;

	template <NodeType NT>
	Value search(Position& pos, Value alpha, Value beta, Depth diepte, bool cutNode);

	template <NodeType NT, bool staatSchaak>
	Value qsearch(Position& pos, Value alfa, Value beta, Depth diepte);

	Value value_to_tt(Value v, int ply);
	Value value_from_tt(Value v, int ply);
	void copy_PV(Move* pv, Move zet, Move* pvLager);
	void update_stats(const Position& pos, bool staatSchaak, Move zet, Depth diepte, Move* rustigeZetten, int rustigAantal);
	void update_stats_quiet(const Position& pos, bool staatSchaak, Depth diepte, Move* rustigeZetten, int rustigAantal);
	void update_stats_minus(const Position& pos, bool staatSchaak, Move zet, Depth diepte);
	void time_control();

	Depth ComputeEgtbUsefulness(const Position& pos);
	void FilterRootMoves(Position& pos, RootMoves& rootZetten);

	const Value QSearchFutilityWaarde[PIECE_N] = {
		Value(102), VALUE_0, Value(308), Value(818), Value(827), Value(1186), Value(2228), VALUE_0,
		Value(102), VALUE_0, Value(308), Value(818), Value(827), Value(1186), Value(2228), VALUE_0
	};

	const Value LazyMarginQSearchLow = Value(480) + VALUE_1;
	const Value LazyMarginQSearchHigh = Value(480);

	PolyglotBook openingBoek;

} // namespace


void Search::initialize()
{
	std::memset(LMReducties, 0, sizeof(LMReducties));

#if !defined(USE_LICENSE) && !defined(IMPORT_TABLES)
	for (int d = PLY; d < 64 * PLY; ++d)
		for (int n = 2; n < 64; ++n)
		{
			double rr = log(double(d) / PLY) * log(n) / 2 * int(PLY);
			if (rr < 6.4)
				continue;
			Depth r = Depth(std::lround(rr));

			LMReducties[false][1][d][n] = uint8_t(r);
			LMReducties[false][0][d][n] = uint8_t(r + (r < 2 * PLY ? 0 * PLY : PLY));
			LMReducties[true][1][d][n] = uint8_t(std::max(r - PLY, DEPTH_0));
			LMReducties[true][0][d][n] = LMReducties[true][1][d][n];

			//Reductions[false][1][d][n] = Depth(r - 1);
			//Reductions[false][0][d][n] = Depth(r + 2);
			//Reductions[true][1][d][n] = Depth(r - 7);
			//Reductions[true][0][d][n] = Depth(r - 4);
		}
#endif

	// kwadratische functie met initiële helling  (1,1) -> (2,6) -> (3,13)
	for (int d = 1; d < MAX_PLY; ++d)
	{
		counterZetBonus[d] = SortValue(std::min(8192, 24 * (d * d + 2 * d - 2)));
		//counterZetBonus[d] = SorteerWaarde(std::min(8192, 24 * (d * d + 6 * d - 6)));
	}
}


void Search::clear_all()
{
	if (!UciOpties["Never Clear Hash"])
		HoofdHash.clear();
	Threads.clear_counter_move_history();

	for (int i = 0; i < Threads.threadCount; ++i)
	{
		Thread* th = Threads.threads[i];
		th->ti->history.clear();
		th->ti->evasionHistory.clear();
		th->ti->maxWinstTabel.clear();
		th->ti->counterZetten.clear();
		th->ti->counterfollowupZetten.clear();
		th->ti->slagGeschiedenis.clear();
	}

	Threads.main()->vorigeRootScore = HIGHEST_VALUE;
	Threads.main()->vorigeRootDiepte = 999 * PLY;
	Threads.main()->snelleZetToegelaten = false;
	openingBoek.new_game();
}


/*
template<bool Root>
uint64_t Search::perft(Position& pos, Depth depth) {

  uint64_t cnt, nodes = 0;
  const bool leaf = (depth == 2 * PLY);

  for (const auto& m : MoveList<LEGALE_ZETTEN>(pos))
  {
      if (Root && depth <= PLY)
          cnt = 1, nodes++;
      else
      {
          pos.speel_zet(m);
          cnt = leaf ? MoveList<LEGALE_ZETTEN>(pos).size() : perft<false>(pos, depth - PLY);
          nodes += cnt;
          pos.neem_zet_terug(m);
      }
      if (Root)
          sync_cout << UCI::move(m, pos.is_chess960()) << ": " << cnt << sync_endl;
  }
  return nodes;
}

template uint64_t Search::perft<true>(Position&, Depth);
*/


void compute_EGTB_mate_PV(Position& pos, Value verwachte_score, PrincipalVariation& pv)
{
	Value score = NO_VALUE;

	for (const auto& zet : LegalMoveList(pos))
	{
		pos.do_move(zet);

		int aantal_stukken = pos.all_pieces_count();
		if (aantal_stukken <= EGTB::MaxPieces_dtm)
			score = EGTB::EGTB_probe_dtm(pos, -MATE_VALUE, MATE_VALUE);

		if (score == verwachte_score)
		{
			pv.add(zet);
			compute_EGTB_mate_PV(pos, -score, pv);
			pos.undo_move(zet);
			return;
		}
		else
			pos.undo_move(zet);
	}
	return;
}


void choose_not_too_strong_move(Thread* thread)
{
	Value max_waarde, random_score;
	static PRNG rng(now());

	int max_index = 0;
	if (Threads.speelSterkte <= 10)
		random_score = Value(2 * (200 - 10 * Threads.speelSterkte));
	else if (Threads.speelSterkte <= 30)
		random_score = Value(2 * (120 - 2 * Threads.speelSterkte));
	else if (Threads.speelSterkte <= 60)
		random_score = Value(2 * (90 - Threads.speelSterkte));
	else
		random_score = Value(2 * (60 - Threads.speelSterkte / 2));

	max_waarde = VALUE_0; // avoid warning
	for (int PV_index = 0; PV_index < Threads.multiPV; PV_index++)
	{
		RootMove zet = thread->rootZetten[PV_index];
		if (zet.pv[0] == NO_MOVE)
			break;
		Value waarde = zet.score;
		waarde += random_score * (rng.rand<unsigned>() & 1023) / 1024;
		if (PV_index == 0 || waarde > max_waarde)
		{
			max_waarde = waarde;
			max_index = PV_index;
		}
	}
	if (max_index != 0)
		std::swap(thread->rootZetten[0], thread->rootZetten[max_index]);
}

#define PONDER_SMART_HASH

void Search::adjust_time_after_ponderhit()
{
#ifdef PONDER_SMART_HASH
	HoofdHash.new_generation();
#endif
	if (Klok.uses_time_calculation())
		Tijdscontrole.adjustment_after_ponderhit();
}


void Search::export_table()
{
	FILE *f = fopen("R:\\Schaak\\h6-lmr-export.dat", "wb");
	fwrite(LMReducties, 1, sizeof(LMReducties), f);
	fclose(f);
}


void Search::import_table()
{
	FILE *f = fopen("R:\\Schaak\\h6-lmr-export.dat", "rb");
	fread(LMReducties, 1, sizeof(LMReducties), f);
	fclose(f);
}


void MainThread::start_search()
{
	Running = true;
	rootStelling->copy_position(Threads.rootStelling, nullptr, nullptr);
	const Color ik = rootStelling->side_to_move();
	Tijdscontrole.initialize(Klok, ik, rootStelling->game_ply());
	vorigeInfoTijd = 0;
	interruptTeller = 0;

#ifdef USE_LICENSE
#ifdef CHESSBASE
	if (app_file_size() != 0xC3C3C3C3)
		memset(PST::psq, 0, sizeof(PST::psq));
#endif
#endif

	Threads.contemptKleur = ik;
	Threads.analyseModus = !Klok.uses_time_calculation();
	Threads.tactischeModusIndex = UciOpties["Tactical Mode"];
	for (int i = 0; i < Threads.threadCount; ++i)
		Threads.threads[i]->tactischeModus = Threads.tactischeModusIndex < 2 ? Threads.tactischeModusIndex : (i % Threads.tactischeModusIndex) == 0;

#if 1
	Threads.vijftigZettenAfstand = UciOpties["FiftyMoveDistance"];
	Threads.vijftigZettenAfstand = std::min(50, std::max(Threads.vijftigZettenAfstand, rootStelling->fifty_moves_counter() / 2 + 5));
	Threads.stukContempt = (Threads.analyseModus && !UciOpties["Analysis Contempt"]) ? 0 : int(UciOpties["Contempt"]);
	if (Threads.stukContempt)
	{
		if (Threads.analyseModus)
			Threads.contemptKleur = WHITE;

		int tempContempt = Threads.stukContempt;
		Threads.stukContempt = 0;
		Value v1 = Evaluation::evaluatie(*rootStelling, NO_VALUE, NO_VALUE);
		Threads.stukContempt = tempContempt;
		Value v2 = Evaluation::evaluatie(*rootStelling, NO_VALUE, NO_VALUE);
		if (abs(v1) < WIN_VALUE && abs(v2) < WIN_VALUE)
			Threads.rootContemptWaarde = v2 - v1;
		else
			Threads.rootContemptWaarde = VALUE_0;
	}
	Threads.multiPV = Threads.multiPVmax = UciOpties["MultiPV"];
	Threads.multiPV_cp = UciOpties["MultiPV_cp"];
	if (Threads.multiPV_cp > 0)
		Threads.multiPV = 2;  // beginwaarde, wordt bijgewerkt door algoritme
	if (UciOpties["UCI_LimitStrength"])
		Threads.speelSterkte = UCI::strength_for_elo(int(UciOpties["UCI_Elo"]) - 200);
	else
		Threads.speelSterkte = UciOpties["Strength"];
#else
	Threads.vijftigZettenAfstand = 50;
	Threads.stukContempt = Threads.analyseModus ? 0 : 3;
	Threads.multiPV = 1;
	Threads.multiPV_cp = 0;
	Threads.speelSterkte = 100;
#endif
	Threads.hideRedundantOutput = UciOpties["Hide Redundant Output"];

	Threads.activeThreadCount = Threads.threadCount;
	Threads.speelSterkteMaxKnopen = 0;
	if (Threads.speelSterkte != 100)
	{
		Threads.activeThreadCount = 1;
		Threads.multiPV_cp = 0;
		Threads.speelSterkteMaxKnopen = 600 << (Threads.speelSterkte / 5);
		if (Threads.speelSterkte <= 10)
			Threads.multiPV = 20 - Threads.speelSterkte / 2;
		else
			Threads.multiPV = 15 - Threads.speelSterkte / 8;
	}

	if (Threads.analyseModus)
	{
		RemiseWaarde[ik] = DRAW_VALUE;
		RemiseWaarde[~ik] = DRAW_VALUE;
	}
	else
	{
		RemiseWaarde[ik] = DRAW_VALUE - DEFAULT_DRAW_VALUE * rootStelling->game_phase() / MIDDLEGAME_PHASE;
		RemiseWaarde[~ik] = DRAW_VALUE + DEFAULT_DRAW_VALUE * rootStelling->game_phase() / MIDDLEGAME_PHASE;
	}

#ifdef PONDER_SMART_HASH
	if (!Klok.ponder)
#endif
		HoofdHash.new_generation();
	TB_RootInTB = false;
	EGTB::UseRule50 = UciOpties["EGTB Fifty Move Rule"];
	TB_ProbeDepth = UciOpties["EGTB Probe Depth"] * PLY;
	TB_Cardinality = std::max(EGTB::MaxPieces_wdl, std::max(EGTB::MaxPieces_dtm, EGTB::MaxPieces_dtz));

	rootZetten.zetAantal = 0;
	for (const auto& zet : LegalMoveList(*rootStelling))
		if (Klok.zoekZetten.empty() || Klok.zoekZetten.find(zet) >= 0)
			rootZetten.add(RootMove(zet));

	if (!Threads.analyseModus && UciOpties["Own Book"])
	{
		string boekBestand = UciOpties["Book File"];
		if (!boekBestand.empty() && boekBestand != "<empty>")
		{
			Move boekZet = openingBoek.probe(*rootStelling, boekBestand, UciOpties["Best Book Line"]);
			if (boekZet)
			{
				int n = rootZetten.find(boekZet);
				if (n >= 0)
				{
					std::swap(rootZetten[0], rootZetten[n]);
					Threads.activeThreadCount = 1;  // enkel MainThread heeft een geïnitialiseerde stelling
					goto NO_ANALYSIS;
				}
			}
		}
	}

	// voor analyse van stellingen
	// verwijder alle posities die al niet tweemaal zijn voorgevallen
	// om remise door tweevoudige zetherhaling in de beginpositie te vermijden
	// goede test positie: 
	// position fen r5kr/2qp1pp1/2b1pn2/p1b4p/1pN1P3/2PB3P/PP1BQPP1/R3R1K1 w - - 1 18 moves a1c1 c5f8 b2b3 d7d5 e4d5 c6d5 
	// c4e3 d5c6 c1c2 c7b6 c3b4 a5b4 e1c1 c6b7 e3c4 b6c5 d2f4 c5d4 f4e3 d4d5 f2f3 b7a6 c1d1 a8e8 e2f2 d5b7 e3d4 h8h6 d4e3
	if (Threads.analyseModus)
	{
		PositionInfo* st = rootStelling->info();
		int e = std::min(st->remise50Zetten, st->afstandTotNullzet);
		while (e > 0)
		{
			st--;
			e--;
			PositionInfo* stst = st;
			bool gevonden = false;
			for (int i = 2; i <= e; i += 2)
			{
				stst = stst - 2;
				if (stst->sleutel == st->sleutel)
				{
					gevonden = true;
					break;
				}
			}

			// position has occurred only once, remove it from the list
			if (!gevonden)
				st->sleutel = 0;
		}
	}

	if (rootZetten.zetAantal == 0)
	{
		rootZetten.add(RootMove(NO_MOVE));
		rootZetten[0].score = rootStelling->check_givers() ? -MATE_VALUE : DRAW_VALUE;
		rootZetten[0].diepte = MAIN_THREAD_INC;
		Threads.activeThreadCount = 1;  // enkel MainThread heeft een geïnitialiseerde stelling
		sync_cout << UCI::pv(*rootStelling, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;
	}
	else
	{
		if (rootStelling->all_pieces_count() <= TB_Cardinality
			&& !rootStelling->castling_possible(ALL_CASTLING)
			&& Threads.speelSterkte == 100)
		{
			FilterRootMoves(*rootStelling, rootZetten);

			if (TB_RootInTB && rootZetten.zetAantal == 1)
			{
				rootZetten[0].diepte = MAIN_THREAD_INC;
				rootZetten[0].score = TB_Score;

				if (Threads.analyseModus && abs(TB_Score) > LONGEST_MATE_VALUE)
				{
					rootStelling->do_move(rootZetten[0].pv[0]);
					compute_EGTB_mate_PV(*rootStelling, TB_Score, rootZetten[0].pv);
					rootStelling->undo_move(rootZetten[0].pv[0]);
					rootZetten[0].diepte = MAIN_THREAD_INC * rootZetten[0].pv.size();
				}
				if (Tijdscontrole.elapsed() > 100)
					sync_cout << UCI::pv(*rootStelling, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;
				Threads.activeThreadCount = 1;  // enkel MainThread heeft een geïnitialiseerde stelling
				goto NO_ANALYSIS;
			}
		}

		open_tracefile();

		Threads.multiPVmax = std::min(Threads.multiPVmax, rootZetten.zetAantal);
		Threads.multiPV = std::min(Threads.multiPV, rootZetten.zetAantal);

		if (Threads.activeThreadCount > 1)
		{
			Threads.rootZetten = rootZetten;
			Threads.rootStellingInfo = rootStelling->info();
		}

		for (int i = 1; i < Threads.activeThreadCount; ++i)
			Threads.threads[i]->wake_up(true);

		Thread::start_search();

		close_tracefile();
	}
NO_ANALYSIS:

	if (!Signalen.stopAnalyse && (Klok.ponder || Klok.infinite))
	{
		if (Tijdscontrole.elapsed() <= 102)
			sync_cout << UCI::pv(*rootStelling, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;

		// diepte = 1 -> some GUIs will not show the output, repeat it with diepte 99
		if (rootZetten[0].diepte == MAIN_THREAD_INC)
		{
			rootZetten[0].diepte = 99 * MAIN_THREAD_INC;
			sync_cout << UCI::pv(*rootStelling, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;
		}

		Signalen.stopBijPonderhit = true;
		wait(Signalen.stopAnalyse);
	}

	Signalen.stopAnalyse = true;

	for (int i = 1; i < Threads.activeThreadCount; ++i)
		Threads.threads[i]->wait_for_search_finished();

	// kies beste thread (hogere diepte EN betere score)
	Thread* bestThread = this;

	if (Threads.speelSterkte != 100)
		choose_not_too_strong_move(this);

	else if (!this->snelleZetGespeeld
		&& Threads.multiPV == 1
		&& !Klok.depth
		&&  rootZetten[0].pv[0] != NO_MOVE)
	{
		for (int i = 1; i < Threads.activeThreadCount; ++i)
		{
			Thread* th = Threads.threads[i];
			if (th->rootZetten[0].score > bestThread->rootZetten[0].score
				//&& (th->afgewerkteDiepte > bestThread->afgewerkteDiepte || th->rootZetten[0].score > LANGSTE_MAT_WAARDE))
				&& th->afgewerkteDiepte > bestThread->afgewerkteDiepte)
				bestThread = th;
		}
	}

	// bij fail high toch een PV tonen
	//if (bestThread->rootZetten[0].pv.size() == 1)
	//	bestThread->rootZetten[0].vervolledig_pv_met_tt(*bestThread->rootStelling);

	vorigeRootScore = bestThread->rootZetten[0].score;
	vorigeRootDiepte = bestThread->rootZetten[0].diepte;

	if (bestThread != this || Tijdscontrole.elapsed() <= 102 || Signalen.stopBijPonderhit)
	{
		// gebruik diepte van de main Thread (om geen sprongen te hebben in de output)
		bestThread->rootZetten[0].diepte = rootZetten[0].diepte;
		sync_cout << UCI::pv(*bestThread->rootStelling, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;
	}
	sync_cout << "bestmove " << UCI::zet(bestThread->rootZetten[0].pv[0], *rootStelling);

#ifdef USE_LICENSE
#ifdef CHESSBASE
	if (Chessbase_License->IsNotActivated()
		|| !Chessbase_License->TestOSMajor() || !Chessbase_License->TestNEngine() || !Chessbase_License->TestNEngine2())
#else
	if (app_file_size() != 0xC3C3C3C3)
#endif
		memset(PST::psq, 0, sizeof(PST::psq));
#endif

	if (bestThread->rootZetten[0].pv.size() > 1 || bestThread->rootZetten[0].find_ponder_move_in_tt(*rootStelling))
		std::cout << " ponder " << UCI::zet(bestThread->rootZetten[0].pv[1], *rootStelling);

	std::cout << sync_endl;

	Threads.totaleAnalyseTijd += int(Tijdscontrole.elapsed());

	Running = false;
}


void Thread::start_search()
{
	Value besteWaarde, alfa, beta, delta_alfa, delta_beta;
	Move snelleZet = NO_MOVE;
	MainThread* mainThread = (this == Threads.main() ? Threads.main() : nullptr);
	//Diepte last_smart_fail_high_diepte = 15 * PLY;

	if (!mainThread)
	{
		rootStelling->copy_position(Threads.rootStelling, this, Threads.rootStellingInfo);
		rootZetten = Threads.rootZetten;
	}

	PositionInfo* st = rootStelling->info();

	std::memset(st + 1, 0, 2 * sizeof(PositionInfo));
	st->killers[0] = st->killers[1] = NO_MOVE;
	st->vorigeZet = NO_MOVE;
	(st - 2)->stellingWaarde = VALUE_0;
	(st - 1)->stellingWaarde = VALUE_0;
	(st - 1)->evalPositioneel = NO_EVAL;
	(st - 1)->zetNummer = 0;
	(st - 4)->counterZetWaarden = (st - 3)->counterZetWaarden = (st - 2)->counterZetWaarden = (st - 1)->counterZetWaarden = st->counterZetWaarden = nullptr;
	(st - 1)->mp_eindeLijst = rootStelling->ti()->zettenLijst;
	//(st - 1)->statsWaarde = SORT_MAX;

	for (int n = 0; n <= MAX_PLY; n++)
	{
		(st + n)->doeGeenVroegePruning = false;
		(st + n)->uitgeslotenZet = NO_MOVE;
		(st + n)->lmr_reductie = 0;
		(st + n)->ply = n + 1;
	}

	besteWaarde = delta_alfa = delta_beta = alfa = -HIGHEST_VALUE;
	beta = HIGHEST_VALUE;
	afgewerkteDiepte = 0 * PLY;

	if (mainThread)
	{
		snelleZet = GemakkelijkeZet.expected_move(rootStelling->sleutel());
		GemakkelijkeZet.clear();
		mainThread->snelleZetGespeeld = mainThread->failedLow = false;
		mainThread->snelleZetEvaluatieBezig = mainThread->snelleZetEvaluatieAfgebroken = false;
		mainThread->besteZetVerandert = 0;
		//mainThread->snelleZetGetest = false;

		// st->pionSleutel op 0 zetten om later de maximale zoekdiepte te berekenen voor "seldepth" UCI output
		for (int i = 1; i <= MAX_PLY; i++)
			(st + i)->pionSleutel = 0;
	}

	// snelle zet
	if (mainThread && !TB_RootInTB && !Klok.ponder && Threads.speelSterkte == 100 && !Threads.analyseModus
		&& mainThread->snelleZetToegelaten && mainThread->vorigeRootDiepte >= 12 * PLY && Threads.multiPV == 1)
	{
		TTTuple* ttt = HoofdHash.find_existing(rootStelling->sleutel());
		if (ttt && ttt->limiet() == EXACT_VALUE)
		{
			Value ttWaarde = value_from_tt(ttt->waarde(), st->ply);
			Move ttZet = ttt->zet();
			Depth ttDiepte = ttt->diepte();

			if (ttDiepte >= mainThread->vorigeRootDiepte - 3 * PLY
				&& ttZet
				&& rootStelling->legal_move(ttZet)
				&& abs(ttWaarde) < WIN_VALUE)
			{
				Depth depth_singular = std::max(mainThread->vorigeRootDiepte / 2, mainThread->vorigeRootDiepte - 8 * PLY);
				Value v_singular = ttWaarde - Value(102);
				st->uitgeslotenZet = ttZet;
				st->stellingWaarde = Evaluation::evaluatie(*rootStelling, NO_VALUE, NO_VALUE);
				mainThread->snelleZetEvaluatieBezig = true;
				Value v = search<NonPV>(*rootStelling, v_singular - VALUE_1, v_singular, depth_singular, false);
				mainThread->snelleZetEvaluatieBezig = false;
				st->uitgeslotenZet = NO_MOVE;

				if (!mainThread->snelleZetEvaluatieAfgebroken && v < v_singular)
				{
					Signalen.stopAnalyse = true;
					rootZetten[0].score = ttWaarde;
					rootZetten[0].pv.resize(1);
					rootZetten[0].pv[0] = ttZet;
					rootZetten[0].complete_pv_with_tt(*rootStelling);
					rootZetten[0].diepte = ttDiepte;
					mainThread->snelleZetToegelaten = false;
					mainThread->snelleZetGespeeld = true;
					GemakkelijkeZet.clear();
					afgewerkteDiepte = mainThread->vorigeRootDiepte - 2 * PLY;
					sync_cout << UCI::pv(*rootStelling, -HIGHEST_VALUE, HIGHEST_VALUE, 999, 0) << sync_endl;
					return;
				}
				mainThread->snelleZetEvaluatieAfgebroken = false;
			}
		}
	}
	if (mainThread)
		mainThread->snelleZetToegelaten = true;

	// in tactische modus hebben we de initiele score van elke zet nodig
	if (tactischeModus)
	{
		vorigeTactischeDiepte = 0 * PLY;

		// bereken de initiele score van elke zet
		st->mp_eindeLijst = (st - 1)->mp_eindeLijst;
		Move pv[MAX_PLY + 1];

		for (int i = 0; i < rootZetten.zetAantal; i++)
		{
			Move zet = rootZetten[i].pv[0];
			rootStelling->do_move(zet);
			(st + 1)->doeGeenVroegePruning = true;
			(st + 1)->pv = pv;
			(st + 1)->pv[0] = NO_MOVE;
			rootZetten[i].startWaarde = -::search<PV>(*rootStelling, -HIGHEST_VALUE, HIGHEST_VALUE, PLY, false);
			(st + 1)->doeGeenVroegePruning = false;
			rootStelling->undo_move(zet);
		}
	}

	int zoekIteratie = 0;
	Depth rootDiepte = PLY / 2;

	while (++zoekIteratie < 100)
	{
		rootDiepte += mainThread ? MAIN_THREAD_INC : OTHER_THREAD_INC;

		if (mainThread)
		{
			if (Klok.depth && zoekIteratie - 1 >= Klok.depth)
				Signalen.stopAnalyse = true;

			if (Threads.speelSterkte != 100 && Threads.visited_nodes() >= Threads.speelSterkteMaxKnopen)
				Signalen.stopAnalyse = true;
		}

		if (Signalen.stopAnalyse)
			break;

		if (mainThread)
		{
			mainThread->besteZetVerandert /= 2;
			mainThread->failedLow = false;
		}

		if (mainThread && Tijdscontrole.elapsed() > 1000)
			sync_cout << "info depth " << zoekIteratie << sync_endl;

		for (int i = 0; i < rootZetten.zetAantal; i++)
			rootZetten[i].vorigeScore = rootZetten[i].score;

		for (actievePV = 0; actievePV < Threads.multiPV && !Signalen.stopAnalyse; ++actievePV)
		{
			Move prevBestMove = rootZetten[actievePV].pv[0];
			int failHighCount = 0;

			if (rootDiepte >= 5 * PLY)
			{
				delta_alfa = Value(14 + (threadIndex & 7));
				delta_beta = Value(14 - (threadIndex & 7));
				alfa = std::max(rootZetten[actievePV].vorigeScore - delta_alfa, -HIGHEST_VALUE);
				beta = std::min(rootZetten[actievePV].vorigeScore + delta_beta, HIGHEST_VALUE);
			}

			while (true)
			{
				if (alfa < -20 * VALUE_PAWN)
					alfa = -HIGHEST_VALUE;
				if (beta > 20 * VALUE_PAWN)
					beta = HIGHEST_VALUE;

//#define SMART_FAIL_HIGH
#ifdef SMART_FAIL_HIGH
				if (failHighCount == 2 && Threads.analyseModus && rootZetten[actievePV].pv[0] != prevBestMove 
					&& rootDiepte > last_smart_fail_high_diepte)
				{
					last_smart_fail_high_diepte = rootDiepte;
					bool showOutput = mainThread && Tijdscontrole.elapsed() > 4000;
					if (showOutput)
						sync_cout << "info string smart fail high - attempt reducing search depth to " << zoekIteratie - 5 << sync_endl;

					Move m = rootZetten[actievePV].pv[0];
					Move pv[MAX_PLY + 1];
					(st + 1)->pv = pv;

					rootStelling->do_move(m);

					for (int diepte_offset = 5; diepte_offset >= 1; diepte_offset -= 2)
					{
						besteWaarde = -::search<PV>(*rootStelling, -beta, -alfa, rootDiepte - PLY - diepte_offset * PLY, false);

						if (Signalen.stopAnalyse)
							break;

						if (besteWaarde > alfa)
						{
							if (showOutput)
								sync_cout << "info string smart fail high successful" << sync_endl;
							rootDiepte -= diepte_offset * PLY;
							zoekIteratie -= diepte_offset;
							break;
						}
					};

					rootStelling->undo_move(m);
					if (Signalen.stopAnalyse)
						break;
				}
#endif

				tactischeModusGebruikt = false;

				besteWaarde = ::search<PV>(*rootStelling, alfa, beta, rootDiepte, false);

				std::stable_sort(rootZetten.zetten + actievePV, rootZetten.zetten + rootZetten.zetAantal);

				if (Signalen.stopAnalyse)
					break;

				if (tactischeModusGebruikt)
					vorigeTactischeDiepte = rootDiepte;

				// fail high wordt enkel maar opgelost in de mainThread, als er nog tijd over is
				bool failHighOplossen = mainThread;

				if (mainThread && besteWaarde >= beta
					&& rootZetten[actievePV].pv[0] == prevBestMove
					&& !Threads.analyseModus
					&& Tijdscontrole.elapsed() > Tijdscontrole.optimum() * 124 / 1024)
				{
					bool speelGemakkelijkeZet = rootZetten[0].pv[0] == snelleZet && mainThread->besteZetVerandert < 31;

					if (speelGemakkelijkeZet)
						failHighOplossen = false;

					else if (Tijdscontrole.elapsed() > Tijdscontrole.optimum() * 420 / 1024)
					{
						int verbeteringFactor = std::max(420, std::min(1304,
							652 + 160 * mainThread->failedLow - 12 * int(besteWaarde - mainThread->vorigeRootScore)));
						int onstabielFactor = 1024 + mainThread->besteZetVerandert;
						if (Tijdscontrole.elapsed() > Tijdscontrole.optimum() * onstabielFactor / 1024 * verbeteringFactor / 1024)
							failHighOplossen = false;
					}
				}

				if (besteWaarde <= alfa)
				{
					beta = (alfa + beta) / 2;
					alfa = std::max(besteWaarde - delta_alfa, -HIGHEST_VALUE);
					failHighCount = 0;

					if (mainThread)
					{
						mainThread->failedLow = true;
						Signalen.stopBijPonderhit = false;
					}
				}
				else if (besteWaarde >= beta && (failHighOplossen || besteWaarde >= VALUE_PAWN * 8))
				{
					alfa = (alfa + beta) / 2;
					beta = std::min(besteWaarde + delta_beta, HIGHEST_VALUE);
					++failHighCount;
				}
				else
					break;

				delta_alfa += delta_alfa / 4 + Value(4);
				delta_beta += delta_beta / 4 + Value(4);

				assert(alfa >= -HIGHEST_VALUE && beta <= HIGHEST_VALUE);
			}

			std::stable_sort(rootZetten.zetten + 0, rootZetten.zetten + actievePV + 1);

			if (mainThread)
			{
				if (Threads.multiPV_cp > 0 && actievePV >= 1)
				{
					if (rootZetten[0].score - rootZetten[actievePV].score > 2 * Threads.multiPV_cp)
						Threads.multiPV = actievePV + 1;
					else
						Threads.multiPV = actievePV + 2;
					Threads.multiPV = std::min(Threads.multiPV, Threads.multiPVmax);
				}

				if (!Threads.hideRedundantOutput &&
					((actievePV + 1 == Threads.multiPV && Tijdscontrole.elapsed() > 100) || Tijdscontrole.elapsed() > 4000))
					sync_cout << UCI::pv(*rootStelling, alfa, beta, actievePV, actievePV) << sync_endl;
			}
		}

		if (!Signalen.stopAnalyse)
			afgewerkteDiepte = rootDiepte;

		if (!mainThread)
			continue;

		if (Klok.mate
			&& besteWaarde >= LONGEST_MATE_VALUE
			&& MATE_VALUE - besteWaarde <= 2 * Klok.mate)
			Signalen.stopAnalyse = true;

		if (!Threads.analyseModus && !Klok.ponder && besteWaarde > MATE_VALUE - 32
			&& rootDiepte >= (MATE_VALUE - besteWaarde + 10) * PLY)
			Signalen.stopAnalyse = true;

		if (!Threads.analyseModus && !Klok.ponder && besteWaarde < -MATE_VALUE + 32
			&& rootDiepte >= (MATE_VALUE + besteWaarde + 10) * PLY)
			Signalen.stopAnalyse = true;

		if (!Threads.analyseModus)
		{
			if (!Signalen.stopAnalyse && !Signalen.stopBijPonderhit)
			{
				int verbeteringFactor = std::max(420, std::min(1304,
					652 + 160 * mainThread->failedLow - 12 * int(besteWaarde - mainThread->vorigeRootScore)));
				int onstabielFactor = 1024 + mainThread->besteZetVerandert;

				bool speelGemakkelijkeZet = rootZetten[0].pv[0] == snelleZet
					&& mainThread->besteZetVerandert < 31
					&& Tijdscontrole.elapsed() > Tijdscontrole.optimum() * 124 / 1024;

				if ((rootZetten.zetAantal == 1 && zoekIteratie > 10)
					|| Tijdscontrole.elapsed() > Tijdscontrole.optimum() * onstabielFactor / 1024 * verbeteringFactor / 1024
					|| (mainThread->snelleZetGespeeld = speelGemakkelijkeZet))
				{
					// If we are allowed to ponder do not stop the search now but
					// keep pondering until the GUI sends "ponderhit" or "stop".
					if (Klok.ponder)
						Signalen.stopBijPonderhit = true;
					else
						Signalen.stopAnalyse = true;
#ifdef TRACE_TM
					trace_tm_msg(1 + rootStelling->game_ply() / 2, Time.elapsed(), Time.optimum(), mainThread->snelleZetGespeeld ? "Easy Move" : "");
#endif
				}
#if 0
				else if (zoekIteratie >= 12 && mainThread && !mainThread->snelleZetGetest
					&& (Time.elapsed() >= Time.optimum() / 4 || zoekIteratie >= mainThread->vorige_root_iteration - 1))
				{
					mainThread->snelleZetGetest = true;

					int iteration_singular = std::max(zoekIteratie / 2, zoekIteratie - 8);
					Value v_singular = besteWaarde - Value(320);
					st->uitgeslotenZet = rootZetten[0].pv[0];
					st->doeGeenVroegePruning = true;
					Value v = ::search<NonPV>(rootStelling, v_singular - 1, v_singular, rootDepthForIteration(iteration_singular), false);
					st->uitgeslotenZet = NO_MOVE;
					st->doeGeenVroegePruning = false;

					if (v < v_singular)
					{
						if (Klok.ponder)
							Signalen.stopBijPonderhit = true;
						else
							Signalen.stopAnalyse = true;
					}
				}
#endif
			}

			if (rootZetten[0].pv.size() >= 3)
				GemakkelijkeZet.verversPv(*rootStelling, rootZetten[0].pv);
			else
				GemakkelijkeZet.clear();
		}
	}

	if (!mainThread)
		return;

	// Clear any candidate easy move that wasn't stable for the last search
	// iterations; the second condition prevents consecutive fast moves.
	if (GemakkelijkeZet.derdeZetStabiel < 6 || mainThread->snelleZetGespeeld)
		GemakkelijkeZet.clear();
}


namespace
{
	template <NodeType NT>
	Value search(Position& pos, Value alfa, Value beta, Depth diepte, bool cutNode)
	{
		const bool PvNode = NT == PV;
		const int MAX_QUIET_MOVES = 64;

		assert(-HIGHEST_VALUE <= alfa && alfa < beta && beta <= HIGHEST_VALUE);
		assert(PvNode || (alfa == beta - 1));
		assert(diepte >= PLY && diepte < MAX_DEPTH);

		Move pv[MAX_PLY + 1], rustigeZetten[MAX_QUIET_MOVES];
		TTTuple* ttt;
		Key64 sleutel64;
		Move ttZet, zet, besteZet;
		Depth verlenging, nieuweDiepte, voorspeldeDiepte;
		Value ttWaarde, eval;
		bool staatSchaak, geeftSchaak, vooruitgang;
		bool slagOfPromotie, volledigZoekenNodig;
		Piece bewogenStuk;
		int zetNummer, rustigeZetAantal;

		trace_msg("Search", diepte, alfa, beta);

		PositionInfo* st = pos.info();
		const bool rootKnoop = PvNode && st->ply == 1;

		// Step 1. Initialize node
		Thread* mijnThread = pos.my_thread();
		staatSchaak = st->schaakGevers;
		zetNummer = rustigeZetAantal = st->zetNummer = 0;

		if (mijnThread == Threads.main())
		{
			MainThread* mainThread = static_cast<MainThread*>(mijnThread);
			if (++mainThread->interruptTeller >= 4096)
			{
				if (mainThread->snelleZetEvaluatieBezig)
				{
					if (mainThread->snelleZetEvaluatieAfgebroken)
						return alfa;
					int64_t elapsed = Tijdscontrole.elapsed();
					if (elapsed > 1000 || elapsed > Tijdscontrole.optimum() / 16)
					{
						mainThread->snelleZetEvaluatieAfgebroken = true;
						return alfa;
					}
				}
				time_control();
				mainThread->interruptTeller = 0;
			}
		}

		if (!rootKnoop)
		{
			// Step 2. Check for aborted search and immediate draw
			if (Signalen.stopAnalyse.load(std::memory_order_relaxed) || st->zetHerhaling || st->ply >= MAX_PLY)
				return st->ply >= MAX_PLY && !staatSchaak 
					? Evaluation::evaluatie(pos, NO_VALUE, NO_VALUE)
					: RemiseWaarde[pos.side_to_move()];

			// Step 3. Mate distance pruning
			alfa = std::max(mated_in(st->ply), alfa);
			beta = std::min(mate_in(st->ply + 1), beta);
			if (alfa >= beta)
				return alfa;
		}

		assert(1 <= st->ply && st->ply < MAX_PLY);

		besteZet = NO_MOVE;
		(st + 2)->killers[0] = (st + 2)->killers[1] = NO_MOVE;
		st->statsWaarde = SORT_MAX;
		//uint64_t knopen = pos.bezochte_knopen();

		// Step 4. Transposition table lookup
		if (st->uitgeslotenZet)
		{
			// st->stellingwaarde is nog juist, van de callende routine
			ttt = nullptr;
			ttZet = NO_MOVE;
			ttWaarde = NO_VALUE;  // overbodig, maar vermijdt warning van compiler
			sleutel64 = 0;  // overbodig, maar vermijdt warning van compiler
			// sla ook IID over, want we slaan toch geen zetten op in de hashtabel
			goto bekijk_alle_zetten;
		}

		//sleutel64 = st->uitgeslotenZet ? st->sleutel ^ pos.uitzondering_sleutel(st->uitgeslotenZet) : st->sleutel;
		sleutel64 = st->sleutel;
		sleutel64 ^= pos.draw50_key();
		ttt = HoofdHash.find_existing(sleutel64);
		ttWaarde = ttt ? value_from_tt(ttt->waarde(), st->ply) : NO_VALUE;
		ttZet = rootKnoop ? mijnThread->rootZetten[mijnThread->actievePV].pv[0]
			: ttt ? ttt->zet() : NO_MOVE;

		if (!PvNode
			&& ttWaarde != NO_VALUE
			&& ttt->diepte() >= diepte
			&& (ttWaarde >= beta ? (ttt->limiet() & LOWER_BOUND)
				: (ttt->limiet() & UPPER_BOUND)))
		{
			if (ttZet)
			{
				if (ttWaarde >= beta)
					update_stats(pos, staatSchaak, ttZet, diepte, nullptr, 0);
				else
					update_stats_minus(pos, staatSchaak, ttZet, diepte);
			}

			return ttWaarde;
		}

		// Step 4a. Tablebase probe
		if (!rootKnoop && TB_Cardinality && diepte >= TB_ProbeDepth && pos.material_or_castling_changed())
		{
			int aantalStukken = pos.all_pieces_count();

			if (aantalStukken <= TB_Cardinality
				//&& (/*aantalStukken < TB_Cardinality ||*/ diepte >= TB_ProbeDepth + BerekenEgtbNut(pos))
				&& diepte >= TB_ProbeDepth
				&& !pos.castling_possible(ALL_CASTLING))
			{
				Value waarde;
				if (aantalStukken <= EGTB::MaxPieces_wdl)
					waarde = EGTB::EGTB_probe_wdl(pos, alfa, beta);
				else if (aantalStukken <= EGTB::MaxPieces_dtm)
					waarde = EGTB::EGTB_probe_dtm(pos, alfa, beta);
				else
					waarde = NO_VALUE;

				if (waarde != NO_VALUE)
				{
					ttt = HoofdHash.find_replace(sleutel64);
					ttt->save(sleutel64, value_to_tt(waarde, st->ply), EXACT_VALUE, 
						std::min(MAX_DEPTH - PLY, diepte + 6 * PLY), 
						NO_MOVE, NO_VALUE, HoofdHash.generatie());

					return waarde;
				}
#if 0
				// dit is Joseph Ellis manier om TB scores eventueel te laten overschrijven door een gevonden mat 
				// zie https://github.com/official-stockfish/Stockfish/compare/master...jhellis3:tb_fix_2
				if (waarde == DRAW_VALUE
					|| !ttt || (waarde < DRAW_VALUE && ttWaarde > -EGTB_WIN_VALUE) || (waarde > DRAW_VALUE && ttWaarde < EGTB_WIN_VALUE))
				{
					ttt = HoofdHash.find_replace(sleutel64);
					ttt->save(sleutel64, value_to_tt(waarde, st->ply),
						waarde > DRAW_VALUE ? LOWER_BOUND : waarde < DRAW_VALUE ? UPPER_BOUND : EXACT_VALUE,
						diepte, NO_MOVE, NO_VALUE, HoofdHash.generatie());

					if (waarde == DRAW_VALUE)
						return waarde;
				}
#endif
			}
		}

		// Step 5. Evaluate the position statically
		if (staatSchaak)
		{
			st->stellingWaarde = NO_VALUE;
			goto bekijk_alle_zetten;
		}

		if (ttt && ttt->eval() != NO_VALUE)
		{
			eval = st->stellingWaarde = ttt->eval();
			st->sterkeDreiging = ttt->dreiging();

			if (ttWaarde != NO_VALUE)
				if (ttt->limiet() & (ttWaarde > eval ? LOWER_BOUND : UPPER_BOUND))
					eval = ttWaarde;
		}
		else
		{
			if (st->vorigeZet != NULL_MOVE)
				eval = Evaluation::evaluatie(pos, NO_VALUE, NO_VALUE);
				//eval = Evaluatie::evaluatie(pos, 
					//PvNode || diepte >= 4 * PLY ? GEEN_WAARDE : alfa - LazyMarginQSearchLow - Waarde(120) * (diepte / PLY),
					//PvNode || diepte >= 4 * PLY ? GEEN_WAARDE : beta + LazyMarginQSearchHigh + Waarde(120) * (diepte / PLY));
			else
				eval = Evaluation::evaluation_after_nullmove((st - 1)->stellingWaarde);

			st->stellingWaarde = eval;
			if (st->eval_is_exact && !rootKnoop)
				return eval;

			ttt = HoofdHash.find_replace(sleutel64);
			ttt->save(sleutel64, NO_VALUE, NO_BOUND + st->sterkeDreiging, NO_DEPTH, NO_MOVE,
				st->stellingWaarde, HoofdHash.generatie());
		}

		trace_eval(st->stellingWaarde);

		if (st->vorigeZet != NULL_MOVE
			/*&& st->stellingWaarde != GEEN_WAARDE*/ && (st - 1)->stellingWaarde != NO_VALUE
			&& st->materiaalSleutel == (st - 1)->materiaalSleutel)
		{
			const Value MAX_GAIN = Value(500);
			Value gain = -st->stellingWaarde - (st - 1)->stellingWaarde + 2 * VALUE_TEMPO;
			gain = std::min(MAX_GAIN, std::max(-MAX_GAIN, gain));
			pos.ti()->maxWinstTabel.update(st->bewogenStuk, st->vorigeZet, gain);
		}

		if (st->doeGeenVroegePruning)
			goto geen_vroege_pruning;

		// Step 6. Razoring
		if (!PvNode
			&& diepte < 4 * PLY
			&& !ttZet
			&& eval + razer_margin(diepte) <= alfa)
			//&& !(st->sterkeDreiging & (pos.aan_zet() + 1)))
		{
			if (diepte < 2 * PLY)
				return qsearch<NonPV, false>(pos, alfa, beta, DEPTH_0);

			Value ralpha = alfa - razer_margin(diepte);
			Value v = qsearch<NonPV, false>(pos, ralpha, ralpha + VALUE_1, DEPTH_0);
			if (v <= ralpha)
				return v;
		}

		// Step 7. Futility pruning: child node
		if (!rootKnoop
			&& diepte < 7 * PLY
			&& eval - futility_marge1(diepte) >= beta
			&& eval < WIN_VALUE  // Do not return unproven wins
			&& st->nietPionMateriaal[pos.side_to_move()])
			return eval - futility_marge1(diepte);

		// Step 8. Null move search with verification search
		if (!PvNode
			&& diepte >= 2 * PLY
			&& eval >= beta + 2 * VALUE_TEMPO
			&& (!st->sterkeDreiging || diepte >= 8 * PLY)
			&& (st->stellingWaarde >= beta || diepte >= 12 * PLY)
			&& st->nietPionMateriaal[pos.side_to_move()]
			//&& pos.stukken(pos.aan_zet(), PION)
			&& (!Threads.analyseModus || diepte < 8 * PLY || Evaluation::at_least_two_mobile_pieces(pos))
			)   // goede test positie voor minstens_twee_mobiele_stukken: 8/6B1/p5p1/Pp4kp/1P5r/5P1Q/4q1PK/8 w - - 0 32 bm Qxh4
		{
			assert(eval - beta >= 0);

			Depth R;
			if (mijnThread->tactischeModus)
				R = diepte < 4 * PLY ? diepte :
				(540 + 66 * ((unsigned int)diepte / PLY)
					+ std::max(std::min(310 * (eval - beta) / Value(204) - 20 - 15 * cutNode - 15 * (ttZet != NO_MOVE), 3 * 256), 0)
					) / 256 * PLY;
			else
				R = diepte < 4 * PLY ? diepte :
				(480 + 76 * ((unsigned int)diepte / PLY)
				//(480 + 76 * std::min((unsigned int)diepte / PLY, (unsigned int)diepte / PLY / 2 + 6)
					+ std::max(std::min(310 * (eval - beta) / Value(204) - 20 - 15 * cutNode - 15 * (ttZet != NO_MOVE), 3 * 256), 0)
					) / 256 * PLY;

			st->mp_eindeLijst = (st - 1)->mp_eindeLijst;
			pos.do_null_move();
			(st + 1)->doeGeenVroegePruning = true;
			Value waarde = diepte - R < PLY ? -qsearch<NonPV, false>(pos, -beta, -beta + VALUE_1, DEPTH_0)
				: -search<NonPV>(pos, -beta, -beta + VALUE_1, diepte - R, !cutNode);
			(st + 1)->doeGeenVroegePruning = false;
			pos.undo_null_move();

			if ((st - 1)->lmr_reductie && (waarde < beta - Value(100) || waarde < -LONGEST_MATE_VALUE))
			{
				if ((st - 1)->lmr_reductie <= 2 * PLY)
					return beta - VALUE_1;
				diepte += 2 * PLY;

				//int delta = std::min(int((st - 1)->lmr_reductie), int(2 * PLY));
				//(st - 1)->lmr_reductie -= delta;
				//diepte += Diepte(delta);
			}

			if (waarde >= beta)
			{
				if (waarde >= LONGEST_MATE_VALUE)
					waarde = beta;

				if (diepte < 12 * PLY && abs(beta) < WIN_VALUE)
					return waarde;

				st->doeGeenVroegePruning = true;
				Value v = diepte - R < PLY ? qsearch<NonPV, false>(pos, beta - VALUE_1, beta, DEPTH_0)
					: search<NonPV>(pos, beta - VALUE_1, beta, diepte - R, false);
				st->doeGeenVroegePruning = false;

				if (v >= beta)
					return waarde;
			}
		}
#ifdef USE_LICENSE
		// Step 8.5. Probeer null move om een dreiging te achterhalen om eventueel LMR te verminderen
		// dummy conditie
		else if (Threads.dummyNullMoveDreiging && diepte >= 6 * PLY && eval >= beta && (st - 1)->lmr_reductie)
		{
			st->mp_eindeLijst = (st - 1)->mp_eindeLijst;
			pos.do_null_move();
			(st + 1)->doeGeenVroegePruning = true;
			Value nullBeta = beta - Value(240);
			Value waarde = -search<NonPV>(pos, -nullBeta, -nullBeta + VALUE_1, diepte / 2 - 2 * PLY, false);
			(st + 1)->doeGeenVroegePruning = false;
			pos.undo_null_move();

			if (waarde < nullBeta)
			{
				if ((st - 1)->lmr_reductie <= 2 * PLY)
					return beta - VALUE_1;
				diepte += 2 * PLY;
			}
		}
#endif

	geen_vroege_pruning:

		// Step 9. ProbCut
		if (!PvNode
			&&  diepte >= 5 * PLY
			&& (diepte >= 8 * PLY || (st->sterkeDreiging & (pos.side_to_move() + 1)))
			&&  abs(beta) < LONGEST_MATE_VALUE)
		{
			Value pc_beta = beta + Value(160);
			Depth pc_diepte = diepte - 4 * PLY;

			assert(pc_diepte >= PLY);
			assert(st->vorigeZet != NO_MOVE);
			//assert(st->vorigeZet != NULL_ZET);

			SeeValue limiet = diepte >= 8 * PLY ? pos.see_values()[st->geslagenStuk] : std::max(SEE_0, SeeValue((pc_beta - st->stellingWaarde) / 2));
			MoveSelection::init_probcut(pos, ttZet, limiet);

			while ((zet = MoveSelection::next_move(pos)) != NO_MOVE)
			{
				if (pos.legal_move(zet))
				{
					pos.do_move(zet);
					Value waarde = -search<NonPV>(pos, -pc_beta, -pc_beta + VALUE_1, pc_diepte, !cutNode);
					pos.undo_move(zet);
					if (waarde >= pc_beta)
						return waarde;
				}
			}
		}

		// Step 10. Internal iterative deepening
		if (diepte >= (PvNode ? 5 * PLY : 8 * PLY)
			&& !ttZet
			//&& (!ttZet || (PvNode && tte->diepte() < diepte - 4 * PLY))
			//&& (PvNode || st->staticEval + 256 >= beta))
			&& (PvNode || cutNode || st->stellingWaarde + Value(102) >= beta))
		{
			Depth d = diepte - 2 * PLY - (PvNode ? DEPTH_0 : ((unsigned int)diepte / PLY) / 4 * PLY);
			st->doeGeenVroegePruning = true;
			search<NT>(pos, alfa, beta, d, !PvNode && cutNode);
			st->doeGeenVroegePruning = false;

			ttt = HoofdHash.find_existing(sleutel64);
			ttZet = ttt ? ttt->zet() : NO_MOVE;
		}

	bekijk_alle_zetten:
		// opgepast vanaf hier gebruik van st->stellingWaarde - kan GEEN_WAARDE zijn (als staatSchaak)

		const CounterMoveValues* cmh = st->counterZetWaarden;
		const CounterMoveValues* fmh = (st - 1)->counterZetWaarden;
		const CounterMoveValues* fmh2 = (st - 3)->counterZetWaarden;

		bool alleen_rustige_schaakzetten = !rootKnoop && diepte < 8 * PLY
			&& st->stellingWaarde + futility_marge2(diepte - PLY) <= alfa;  // altijd false bij staatSchaak

		MoveSelection::init_search(pos, ttZet, diepte, alleen_rustige_schaakzetten);

#ifdef USE_LICENSE
		if (mijnThread == Threads.main() && Threads.geknoeiMetExe)
		{
			((char *)LMReducties)[Threads.geknoeiMetExe & (sizeof(LMReducties) - 1)] = 0;
			Threads.geknoeiMetExe++;
		}
#endif

		Value besteWaarde = -HIGHEST_VALUE;
		//bool besteWaarde_is_herhaling = false;

		//Zet vorige_zet_terug = GEEN_ZET;
		//if (!rootKnoop && st->remise50Zetten >= 2 && alfa > RemiseWaarde[pos.aan_zet()]
		//	&& st->rokadeMogelijkheden == (st - 1)->rokadeMogelijkheden
		//	&& naar_veld(st->vorigeZet) != van_veld((st - 1)->vorigeZet)
		//	&& !(bb_tussen(van_veld(st->vorigeZet), naar_veld(st->vorigeZet)) & van_veld((st - 1)->vorigeZet)))
		//	vorige_zet_terug = maak_zet(zet_type((st - 1)->vorigeZet), naar_veld((st - 1)->vorigeZet), van_veld((st - 1)->vorigeZet));

		// 60% vooruitgang
		vooruitgang = st->stellingWaarde >= (st - 2)->stellingWaarde
			/* || st->stellingWaarde == GEEN_WAARDE test niet nodig */
			|| (st - 2)->stellingWaarde == NO_VALUE;

		int LateMoveCount = diepte < 16 * PLY ? late_move_count(diepte, vooruitgang) : 999;
		//LateMoveCount += (st - 1)->zetNummer / 16;
		if (alleen_rustige_schaakzetten)
			LateMoveCount = 1;
		//if (staatSchaak)
		//	LateMoveCount = diepte < 16 * PLY ? late_zet_aantal((diepte + PLY) / 2, false) : 999;
		bool discovered_check_possible = pos.discovered_check_possible();
		//bool pv_slag = false;
		bool uitstellen_mogelijk = (PvNode || diepte >= 12 * PLY) && Threads.threadCount > 1;

		// Step 11. Loop through moves
		while ((zet = MoveSelection::next_move(pos)) != NO_MOVE)
		{
			assert(piece_color(pos.moved_piece(zet)) == pos.side_to_move());

			if (zet == st->uitgeslotenZet)
				continue;

			if (rootKnoop && mijnThread->rootZetten.find(zet) < mijnThread->actievePV)
				continue;

			st->zetNummer = ++zetNummer;

			//if (zet == vorige_zet_terug)
			//	continue;

			if (rootKnoop && mijnThread == Threads.main())
			{
				if (Tijdscontrole.elapsed() > 4000)
					sync_cout << "info currmove " << UCI::zet(zet, pos)
						<< " currmovenumber " << zetNummer + mijnThread->actievePV << sync_endl;
#ifdef USE_LICENSE
				if (Threads.geknoeiMetExe)
					((int *)LateZetAantal)[Threads.geknoeiMetExe & (sizeof(LateZetAantal) / sizeof(int) - 1)] = 220;
#endif
			}

			if (PvNode)
				(st + 1)->pv = nullptr;

			slagOfPromotie = pos.slag_of_promotie(zet);
			bewogenStuk = pos.moved_piece(zet);

			geeftSchaak = zet < Move(CASTLING) && !discovered_check_possible
				? st->schaakVelden[piece_type(bewogenStuk)] & to_square(zet)
				: pos.gives_check(zet);

			// Step 12a. Extensions
			verlenging = DEPTH_0;

			//if (diepte < 8 * PLY && zet != ttZet && naar_veld(zet) == naar_veld(st->vorigeZet) && stuk_type(st->geslagenStuk) >= PAARD)
			//	verlenging = PLY / 2;

			if (geeftSchaak
				&& (st->mp_etappe == GOOD_CAPTURES || zetNummer < LateMoveCount)
				&& (st->mp_etappe == GOOD_CAPTURES || pos.see_test(zet, SEE_0)))
				verlenging = PLY;

			if (rootKnoop && mijnThread->tactischeModus)
			{
				if (slagOfPromotie || geeftSchaak || pos.advanced_pawn(zet))
					verlenging = PLY;
			}

			// Step 12b. Singular extension search
			if (!rootKnoop
				&& zet == ttZet
				&& diepte >= 8 * PLY
				//&& (diepte >= 8 * PLY || diepte >= 6 * PLY && (PvNode || cutNode))
				//&& ttt // als zet == ttZet en !rootKnoop is ttt altijd gedefinieerd
				&& (ttt->limiet() & LOWER_BOUND)
				&& ttt->diepte() >= diepte - 3 * PLY
				&& verlenging < PLY
				&&  abs(ttWaarde) < WIN_VALUE
				/*&& !st->uitgeslotenZet  test niet nodig want ttZet is altijd leeg bij uitgeslotenZet */
				&&  pos.legal_move(zet))
			{
				// bewaar PikZet state
				Move cm = st->mp_counterZet;
				Value rBeta = ttWaarde - Value(((unsigned int)diepte / PLY) * 8 / 5);
				Depth rDiepte = ((unsigned int)diepte / PLY) / 2 * PLY;
				st->uitgeslotenZet = zet;

				Value waarde = search<NonPV>(pos, rBeta - VALUE_1, rBeta, rDiepte, !PvNode && cutNode);
				if (waarde < rBeta)
					verlenging = PLY;

				st->uitgeslotenZet = NO_MOVE;

				// herstel PikZet state
				MoveSelection::init_search(pos, ttZet, diepte, false);
				st->mp_counterZet = cm;
				++st->mp_etappe;
				// zetNummer terugzetten (hier altijd 1)
				st->zetNummer = zetNummer;
			}

			nieuweDiepte = diepte - PLY + verlenging;
			//SorteerWaarde statsWaarde = SORT_MAX;

			// Step 13. Pruning at shallow depth
			if (!(rootKnoop | slagOfPromotie | geeftSchaak)
				&& besteWaarde > -LONGEST_MATE_VALUE
				&& !pos.advanced_pawn(zet)
				//&& pos.stukken(pos.aan_zet(), PION)
				&& st->nietPionMateriaal[pos.side_to_move()])
				//&& (!pos.vooruitgeschoven_pion(zet) || pos.niet_pion_materiaal(WIT) + pos.niet_pion_materiaal(ZWART) >= 250))
			{
				// Move count based pruning
				if (zetNummer >= LateMoveCount)
				//if (zetNummer >= LateMoveCount && !staatSchaak)
					continue;

				// Countermoves based pruning
				if (diepte < 6 * PLY
					&& st->mp_etappe >= QUIET_MOVES)
				{
					int offset = CounterMoveValues::compute_offset(bewogenStuk, to_square(zet));

					// 24% cut hieronder
					const SortValue SORT_CMP = SortValue(-200);
					if ((!cmh || cmh->valueAtOffset(offset) < SORT_CMP)
						&& (!fmh || fmh->valueAtOffset(offset) < SORT_CMP)
						&& ((cmh && fmh) || !fmh2 || fmh2->valueAtOffset(offset) < SORT_CMP))
						continue;
					//SorteerWaarde statsWaarde = SorteerWaarde(staatSchaak ? pos.ti()->evasionHistory.valueAtOffset(offset) : pos.ti()->history.valueAtOffset(offset))
					//	+ (cmh ? SorteerWaarde(cmh->valueAtOffset(offset)) : SORT_ZERO)
					//	+ (fmh ? SorteerWaarde(fmh->valueAtOffset(offset)) : SORT_ZERO)
					//	+ (fmh2 ? SorteerWaarde(fmh2->valueAtOffset(offset)) : SORT_ZERO);

					//if (statsWaarde < SorteerWaarde(-12000))
					//	continue;

					//if (st->mp_etappe >= RUSTIGE_ZETTEN)
					if (pos.ti()->maxWinstTabel.get(bewogenStuk, zet) < Value(-44) - Value(12) * int((unsigned int)diepte / PLY))
						continue;
				}

				voorspeldeDiepte = std::max(nieuweDiepte - lmr_reductie(PvNode, vooruitgang, diepte, zetNummer), DEPTH_0);
				// gemiddelde waarden: <depth>=37  <nieuweDiepte>=29  <voorspeldeDiepte>=20

				// Futility pruning: parent node
				if (voorspeldeDiepte < 7 * PLY
					// volgende test is nooit waar bij staatSchaak
					&& st->stellingWaarde + futility_marge2(voorspeldeDiepte) <= alfa)
					continue;

				// Prune moves with negative SEE at low depths
				if (voorspeldeDiepte < 7 * PLY && !pos.see_test(zet, std::min(SEE_0, SeeValue(300 - 20 * (int)voorspeldeDiepte * (int)voorspeldeDiepte / 64))))
					continue;
			}
			else if (!rootKnoop
				&& diepte < 7 * PLY
				&& besteWaarde > -LONGEST_MATE_VALUE)
				//&& pos.niet_pion_materiaal(pos.aan_zet())
			{
				if (st->mp_etappe != GOOD_CAPTURES && (verlenging != PLY)
					&& !pos.see_test(zet, std::min(SEE_KNIGHT - SEE_BISHOP, SeeValue(150 - 20 * (int)diepte * (int)diepte / 64))))
					continue;
			}

#ifdef USE_LICENSE
			// Step 13.5. Dummy ProbCut
			if (Threads.dummyProbCut
				&&  diepte >= 5 * PLY
				&&  abs(beta) < LONGEST_MATE_VALUE)
			{
				Value pc_beta = beta + Value(160);
				Depth pc_diepte = diepte - 4 * PLY;

				MoveSelection::init_probcut(pos, ttZet, pos.see_values()[st->geslagenStuk]);

				while ((zet = MoveSelection::next_move(pos)) != NO_MOVE)
				{
					if (pos.legal_move(zet))
					{
						pos.do_move(zet);
						Value waarde = -search<NonPV>(pos, -pc_beta, -pc_beta + VALUE_1, pc_diepte, !cutNode);
						pos.undo_move(zet);
						if (waarde >= besteWaarde)
							besteWaarde = waarde;
					}
				}
			}
#endif

			// Check for legality just before making the move
			if (!rootKnoop && !pos.legal_move(zet))
			{
				st->zetNummer = --zetNummer;
				continue;
			}

#ifdef USE_LICENSE
			if (rootKnoop && diepte >= 16 * PLY && mijnThread == Threads.main() && Threads.totaleAnalyseTijd + Tijdscontrole.elapsed() >= 10000)
			{
				if (crc32(crc32(0, nullptr, 0), (Bytef *)aes_setkey_enc, 4096) != 0xC4C4C4C4)
					Threads.geknoeiMetExe++;
				Threads.totaleAnalyseTijd -= 10000;
			}
#endif

			//if (zet == ttZet && slagOfPromotie)
			//	pv_slag = true;

			// Step 14. Make the move
			pos.do_move(zet, geeftSchaak);

			if (uitstellen_mogelijk)
			{
				if (mijnThread != Threads.main() && zetNummer > 1 && st->mp_etappe != GOOD_CAPTURES && st->mp_etappe != POSTPONED_MOVES
					&& st->mp_uitgesteld_aantal < POSTPONED_COUNT
					&& HoofdHash.is_entry_in_use(st->sleutel, diepte))
				{
					st->mp_uitgesteld[st->mp_uitgesteld_aantal++] = zet;
					pos.undo_move(zet);
					st->zetNummer = --zetNummer;
					continue;
				}
				HoofdHash.mark_entry_in_use(st->sleutel, diepte, IN_USE);
			}

			Value waarde = VALUE_0;  // overbodige initialisatie, maar vermijdt warning van compiler

			//if ((st + 1)->zetHerhaling)
			//{
			//	waarde = -RemiseWaarde[pos.aan_zet()];
			//	goto einde_zet;
			//}

			// Step 14.5. Tactische modus
			if (rootKnoop && zetNummer > 1 && mijnThread->tactischeModus && diepte >= 12 * PLY && abs(alfa) < WIN_VALUE
				&& diepte > mijnThread->vorigeTactischeDiepte)
			{
				mijnThread->tactischeModusGebruikt = true;

				bool tactische_zet = slagOfPromotie || geeftSchaak;
				if (!tactische_zet)
				{
					Value alfa_dreiging = alfa + 5 * VALUE_PAWN / 4;

					st++;
					st->mp_eindeLijst = (st - 1)->mp_eindeLijst;
					pos.do_null_move();
					(st + 1)->doeGeenVroegePruning = true;
					waarde = search<NonPV>(pos, alfa_dreiging, alfa_dreiging + VALUE_1, 6 * PLY, false);
					(st + 1)->doeGeenVroegePruning = false;
					pos.undo_null_move();
					st--;

					tactische_zet = (waarde > alfa_dreiging);
				}

				if (tactische_zet)
				{
					Depth diepte_red = diepte - diepte / 8;
					waarde = mijnThread->rootZetten[mijnThread->rootZetten.find(zet)].startWaarde;

					Value score_offset = 8 * (alfa - waarde) / (diepte / PLY + 16);
					do
					{
						if (Signalen.stopAnalyse.load(std::memory_order_relaxed))
							return alfa;

						Value alfa_red = waarde + score_offset;
						if (alfa_red >= alfa && diepte_red <= diepte)
							break;
						if (alfa_red >= alfa || diepte_red >= diepte + diepte / 8)
							alfa_red = alfa;

						(st + 1)->doeGeenVroegePruning = true;
						waarde = -search<NonPV>(pos, -alfa_red - VALUE_1, -alfa_red, diepte_red, true);
						(st + 1)->doeGeenVroegePruning = false;

						if (waarde <= alfa_red)
							break;
						if (waarde > alfa && diepte_red >= diepte)
						{
							(st + 1)->pv = pv;
							(st + 1)->pv[0] = NO_MOVE;
							waarde = -search<PV>(pos, -beta, -alfa, diepte_red, false);
							goto einde_zet;
						}
						diepte_red += PLY;
					} while (true);
				}
			}

			// Step 15. Reduced depth search (LMR)
			if (diepte >= 3 * PLY
				&& zetNummer > 1
				&& !slagOfPromotie
				&& (!mijnThread->tactischeModus || diepte < 12 * PLY || st->ply > 3))
			{
				Depth r = lmr_reductie(PvNode, vooruitgang, diepte, zetNummer);

				//if ((st - 1)->zetNummer >= 16)
				//	r -= PLY;

				//if (!PvNode && pv_slag)
				//	r += PLY / 2;

				// hier: 30% cutNode bij !PvNode
				if (!PvNode && cutNode)
					r += 2 * PLY;

				// hier: 6% ontwijkzetten
				if (piece_type(bewogenStuk) >= KNIGHT
					&& !pos.see_test(make_move(NO_PIECE_TYPE, to_square(zet), from_square(zet)), SEE_0))
					r -= 2 * PLY;

				int offset = MoveValueStats::compute_offset(bewogenStuk, to_square(zet));
				SortValue statsWaarde = SortValue(staatSchaak ? pos.ti()->evasionHistory.valueAtOffset(offset) : pos.ti()->history.valueAtOffset(offset))
					+ (cmh ? SortValue(cmh->valueAtOffset(offset)) : SORT_ZERO)
					+ (fmh ? SortValue(fmh->valueAtOffset(offset)) : SORT_ZERO)
					+ (fmh2 ? SortValue(fmh2->valueAtOffset(offset)) : SORT_ZERO);
				statsWaarde += SortValue(2000);

				r -= (int)statsWaarde / 2048 * (PLY / 8);

				st->statsWaarde = statsWaarde;
				if ((st - 1)->statsWaarde != SORT_MAX)
					r -= std::min(PLY, std::max(-PLY, int(statsWaarde - (st - 1)->statsWaarde) / 4096 * (PLY / 8)));
				//SorteerWaarde statsVorig = (st - 1)->statsWaarde != SORT_MAX ? (st - 1)->statsWaarde : SORT_ZERO;
				//r -= std::min(PLY, std::max(-PLY, int(statsWaarde - statsVorig) / 4096 * (PLY / 8)));

				r = std::max(r, DEPTH_0);
				Depth d = std::max(nieuweDiepte - r, PLY);
				st->lmr_reductie = nieuweDiepte - d;

				waarde = -search<NonPV>(pos, -(alfa + VALUE_1), -alfa, d, true);

				if (waarde > alfa && st->lmr_reductie >= 5 * PLY)
				{
					st->lmr_reductie = 5 * PLY / 2;
					waarde = -search<NonPV>(pos, -(alfa + VALUE_1), -alfa, nieuweDiepte - 5 * PLY / 2, true);
				}

				volledigZoekenNodig = (waarde > alfa && st->lmr_reductie != 0);
				st->lmr_reductie = 0;
				//(st + 1)->doeGeenVroegePruning = true;
			}
			else
			{
				st->statsWaarde = SORT_MAX;
				volledigZoekenNodig = !PvNode || zetNummer > 1;
			}

			// Step 16a. Full depth search
			if (volledigZoekenNodig)
			{
				waarde = nieuweDiepte < PLY ?
					geeftSchaak ? -qsearch<NonPV, true>(pos, -(alfa + VALUE_1), -alfa, DEPTH_0)
					: -qsearch<NonPV, false>(pos, -(alfa + VALUE_1), -alfa, DEPTH_0)
					: -search<NonPV>(pos, -(alfa + VALUE_1), -alfa, nieuweDiepte, PvNode || !cutNode);
			}

			//(st + 1)->doeGeenVroegePruning = false;

			// Step 16a. PV search
			if (PvNode && (zetNummer == 1 || (waarde > alfa && (rootKnoop || waarde < beta))))
			{
				(st + 1)->pv = pv;
				(st + 1)->pv[0] = NO_MOVE;

				if (nieuweDiepte < PLY && !(st->ply & 1))
					nieuweDiepte = PLY;

				waarde = nieuweDiepte < PLY ?
					geeftSchaak ? -qsearch<PV, true>(pos, -beta, -alfa, DEPTH_0)
					: -qsearch<PV, false>(pos, -beta, -alfa, DEPTH_0)
					: -search<PV>(pos, -beta, -alfa, nieuweDiepte, false);
			}

		einde_zet:

			if (uitstellen_mogelijk)
			{
				HoofdHash.mark_entry_in_use(st->sleutel, diepte, 0);
			}

			// Step 17. Undo move
			pos.undo_move(zet);

			assert(waarde > -HIGHEST_VALUE && waarde < HIGHEST_VALUE);

			// Step 18. Check for a new best move
			if (Signalen.stopAnalyse.load(std::memory_order_relaxed))
				return alfa;

			if (mijnThread == Threads.main() && static_cast<MainThread*>(mijnThread)->snelleZetEvaluatieAfgebroken)
				return alfa;

			if (rootKnoop)
			{
				int zetIndex = mijnThread->rootZetten.find(zet);
				RootMove& rootZet = mijnThread->rootZetten.zetten[zetIndex];

				if (zetNummer == 1 || waarde > alfa)
				{
					rootZet.score = waarde;
					rootZet.pv.resize(1);
					rootZet.diepte = diepte;

					assert((st + 1)->pv);

					for (Move* z = (st + 1)->pv; *z != NO_MOVE; ++z)
						rootZet.pv.add(*z);

					if (zetNummer > 1 && mijnThread == Threads.main())
						static_cast<MainThread*>(mijnThread)->besteZetVerandert += 1024;

					if (mijnThread == Threads.main() && Tijdscontrole.elapsed() > 100
						&& ((Threads.hideRedundantOutput && waarde > alfa && waarde < beta)
							|| (!Threads.hideRedundantOutput && Tijdscontrole.elapsed() > 4000)))
						sync_cout << UCI::pv(pos, alfa, beta, mijnThread->actievePV, zetIndex) << sync_endl;
				}
				else
					rootZet.score = -HIGHEST_VALUE;
			}

			if (waarde > besteWaarde)
			{
				besteWaarde = waarde;
				//besteWaarde_is_herhaling = (st + 1)->zetHerhaling;
				//assert(!besteWaarde_is_herhaling || waarde == -RemiseWaarde[~pos.aan_zet()]);

				if (waarde > alfa)
				{
					if (PvNode
						&&  mijnThread == Threads.main()
						&& GemakkelijkeZet.expected_move(st->sleutel)
						&& (zet != GemakkelijkeZet.expected_move(st->sleutel) || zetNummer > 1))
						GemakkelijkeZet.clear();

					besteZet = zet;

					if (PvNode && !rootKnoop)
						copy_PV(st->pv, zet, (st + 1)->pv);

					if (PvNode && waarde < beta)
						alfa = waarde;
					else
					{
						assert(waarde >= beta);
						break;
					}
				}
			}

			if (!slagOfPromotie && zet != besteZet && rustigeZetAantal < MAX_QUIET_MOVES)
				rustigeZetten[rustigeZetAantal++] = zet;

#if 0
			if (!PvNode && st->mp_uitgesteld_aantal)
			{
				TTTuple* ttt = HoofdHash.find_existing(sleutel64);
				Value ttWaarde = ttt ? value_from_tt(ttt->waarde(), st->ply) : NO_VALUE;
				Move ttZet = rootKnoop ? mijnThread->rootZetten[mijnThread->actievePV].pv[0]
					: ttt ? ttt->zet() : NO_MOVE;

				if (!PvNode
					&& ttWaarde != NO_VALUE
					&& ttt->diepte() >= diepte
					&& (ttWaarde >= beta ? (ttt->limiet() & LOWER_BOUND)
						: (ttt->limiet() & UPPER_BOUND)))
				{
					if (ttZet)
					{
						if (ttWaarde >= beta)
							update_stats(pos, staatSchaak, ttZet, diepte, nullptr, 0);
						else
							update_stats_minus(pos, staatSchaak, ttZet, diepte);
					}

					return ttWaarde;
				}
			}
#endif
		}

		// Step 20. Check for mate and stalemate
		if (besteWaarde == -HIGHEST_VALUE)
			besteWaarde = st->uitgeslotenZet ? alfa
				: staatSchaak ? mated_in(st->ply) : RemiseWaarde[pos.side_to_move()];

		// Quiet best move: update killers, history and countermoves
		else if (besteZet)
		{
			update_stats(pos, staatSchaak, besteZet, diepte, rustigeZetten, rustigeZetAantal);
		}

		// Bonus for prior countermove that caused the fail low
		else
		{
			if (diepte >= 3 * PLY && st->stellingWaarde >= alfa - Value(30))
				update_stats_quiet(pos, staatSchaak, diepte, rustigeZetten, rustigeZetAantal);
				//update_stats_rustig(pos, staatSchaak, counter_diepte(diepte, pos.bezochte_knopen() - knopen), rustigeZetten, rustigeZetAantal);

			if (diepte >= 3 * PLY
				&& !staatSchaak
				&& !st->geslagenStuk
				&& st->counterZetWaarden)
			{
				if (diepte < 18 * PLY)
				{
					SortValue bonus = counter_zet_bonus(diepte);
					int offset = CounterMoveValues::compute_offset(st->bewogenStuk, to_square(st->vorigeZet));

					if ((st - 1)->counterZetWaarden)
						(st - 1)->counterZetWaarden->updatePlus(offset, bonus);

					if ((st - 2)->counterZetWaarden)
						(st - 2)->counterZetWaarden->updatePlus(offset, bonus);

					if ((st - 4)->counterZetWaarden)
						(st - 4)->counterZetWaarden->updatePlus(offset, bonus);
				}
			}
		}

		if (!st->uitgeslotenZet)
		//if (!st->uitgeslotenZet && !besteWaarde_is_herhaling)
		{
			ttt = HoofdHash.find_replace(sleutel64);
			ttt->save(sleutel64, value_to_tt(besteWaarde, st->ply),
				(besteWaarde >= beta ? LOWER_BOUND : PvNode && besteZet ? EXACT_VALUE : UPPER_BOUND) + st->sterkeDreiging,
				diepte, besteZet, st->stellingWaarde, HoofdHash.generatie());
		}

		return besteWaarde;
	}


	template <NodeType NT, bool staatSchaak>
	Value qsearch(Position& pos, Value alfa, Value beta, Depth diepte)
	{
		const bool PvNode = NT == PV;

		assert(staatSchaak == !!pos.check_givers());
		assert(alfa >= -HIGHEST_VALUE && alfa < beta && beta <= HIGHEST_VALUE);
		assert(PvNode || (alfa == beta - 1));
		assert(diepte <= DEPTH_0);

		Move pv[MAX_PLY + 1];
		TTTuple* ttt;
		Key64 sleutel64;
		Move ttZet, zet, besteZet;
		Value besteWaarde, ttWaarde, futilityWaarde, futilityBasis, origAlfa;
		bool geeftSchaak;
		Depth ttDiepte;

		trace_msg("QSearch", diepte, alfa, beta);

		PositionInfo* st = pos.info();

		if (PvNode)
		{
			origAlfa = alfa;
			(st + 1)->pv = pv;
			st->pv[0] = NO_MOVE;
		}

		besteZet = NO_MOVE;

		if (st->zetHerhaling || st->ply >= MAX_PLY)
			return st->ply >= MAX_PLY && !staatSchaak 
				? Evaluation::evaluatie(pos, NO_VALUE, NO_VALUE)
				: RemiseWaarde[pos.side_to_move()];

		assert(0 <= st->ply && st->ply < MAX_PLY);

		ttDiepte = (staatSchaak || diepte == DEPTH_0) ? DEPTH_0 : -PLY;

		sleutel64 = st->sleutel;
		sleutel64 ^= pos.draw50_key();
		ttt = HoofdHash.find_existing(sleutel64);
		ttZet = ttt ? ttt->zet() : NO_MOVE;
		ttWaarde = ttt ? value_from_tt(ttt->waarde(), st->ply) : NO_VALUE;

		if (!PvNode
			&& ttWaarde != NO_VALUE
			&& ttt->diepte() >= ttDiepte
			&& (ttWaarde >= beta ? (ttt->limiet() & LOWER_BOUND) : (ttt->limiet() & UPPER_BOUND)))
		{
			return ttWaarde;
		}

#if 1
		if (TB_Cardinality && TB_ProbeDepth <= 0 && pos.material_or_castling_changed()
			&& pos.all_pieces_count() <= EGTB::MaxPieces_wdl
			&& ComputeEgtbUsefulness(pos) == egtb_useful
			&& !pos.castling_possible(ALL_CASTLING))
		{
			Value waarde = EGTB::EGTB_probe_wdl(pos, alfa, beta);

			if (waarde != NO_VALUE)
				return waarde;
		}
#endif

		if (staatSchaak)
		{
			st->stellingWaarde = NO_VALUE;
			besteWaarde = futilityBasis = -HIGHEST_VALUE;
		}
		else
		{
			if (ttt && ttt->eval() != NO_VALUE)
			{
				st->stellingWaarde = besteWaarde = ttt->eval();
				st->sterkeDreiging = ttt->dreiging();

				trace_eval(st->stellingWaarde);

				if (ttWaarde != NO_VALUE)
					if (ttt->limiet() & (ttWaarde > besteWaarde ? LOWER_BOUND : UPPER_BOUND))
						besteWaarde = ttWaarde;

				if (besteWaarde >= beta)
					return besteWaarde;
			}
			else
			{
				if (st->vorigeZet != NULL_MOVE)
					besteWaarde = Evaluation::evaluatie(pos, PvNode ? NO_VALUE : alfa - LazyMarginQSearchLow,
						PvNode ? NO_VALUE : beta + LazyMarginQSearchHigh);
				else
					besteWaarde = Evaluation::evaluation_after_nullmove((st - 1)->stellingWaarde);

				st->stellingWaarde = besteWaarde;
				if (st->eval_is_exact)
					return besteWaarde;

				trace_eval(st->stellingWaarde);

				if (besteWaarde >= beta)
				{
					ttt = HoofdHash.find_replace(sleutel64);
					ttt->save(sleutel64, value_to_tt(besteWaarde, st->ply), LOWER_BOUND + st->sterkeDreiging,
						NO_DEPTH, NO_MOVE, st->stellingWaarde, HoofdHash.generatie());
					return besteWaarde;
				}
			}

			if (PvNode && besteWaarde > alfa)
				alfa = besteWaarde;

			futilityBasis = besteWaarde;
		}

		MoveSelection::init_qsearch(pos, ttZet, diepte, to_square(st->vorigeZet));
		//ZetKeuze::init_qsearch(pos, ttZet, diepte, naar_veld(st->vorigeZet), alfa - besteWaarde);

		//Kleur JIJ = ~pos.aan_zet();
		//bool ver_eindspel = pos.aantal(JIJ, PION) <= 2
		//	|| pos.aantal(JIJ, PAARD) + pos.aantal(JIJ, LOPER) + pos.aantal(JIJ, TOREN) + pos.aantal(JIJ, DAME) <= 2;

		//int zetNummer = 0;

		while ((zet = MoveSelection::next_move(pos)) != NO_MOVE)
		{
			assert(is_ok(zet));

			geeftSchaak = zet < Move(CASTLING) && !pos.discovered_check_possible()
				? st->schaakVelden[piece_type(pos.moved_piece(zet))] & to_square(zet)
				: pos.gives_check(zet);

			if (!staatSchaak
				&& !geeftSchaak
				&&  futilityBasis > -WIN_VALUE
				&& !pos.advanced_pawn(zet))
			{
				assert(move_type(zet) != ENPASSANT);

				Piece slag = pos.piece_on_square(to_square(zet));
				futilityWaarde = futilityBasis + QSearchFutilityWaarde[slag];
				futilityWaarde += Value(pos.ti()->slagGeschiedenis[slag][to_square(zet)] / 32);

				//if (ver_eindspel)
				//	futilityWaarde += WAARDE_PION;

				if (futilityWaarde <= alfa)
				{
					besteWaarde = std::max(besteWaarde, futilityWaarde);
					continue;
				}

				if (futilityBasis + Value(102) <= alfa)
				{
					if (!pos.see_test(zet, SeeValue(1)))
					{
						besteWaarde = std::max(besteWaarde, futilityBasis + Value(102));
						continue;
					}
					goto skip_see_test;
				}
			}

			if (staatSchaak)
			{
				if (besteWaarde > -LONGEST_MATE_VALUE
					&& !pos.is_capture(zet)
					&& zet < Move(PROMOTION_N))
				{
					Piece bewogenStuk = pos.moved_piece(zet);

					if (!geeftSchaak)
					{
						int offset = CounterMoveValues::compute_offset(bewogenStuk, to_square(zet));

						SortValue statsWaarde = SortValue(pos.ti()->evasionHistory.valueAtOffset(offset))
							+ (st->counterZetWaarden ? SortValue(st->counterZetWaarden->valueAtOffset(offset)) : SORT_ZERO)
							+ ((st - 1)->counterZetWaarden ? SortValue((st - 1)->counterZetWaarden->valueAtOffset(offset)) : SORT_ZERO)
							+ ((st - 3)->counterZetWaarden ? SortValue((st - 3)->counterZetWaarden->valueAtOffset(offset)) : SORT_ZERO);

						if (statsWaarde < SortValue(-12000))
							continue;
					}

					if (piece_type(bewogenStuk) != KING  // 80% zijn K-zetten, geen SEE nodig
						&& !pos.see_test(zet, SEE_0))
						continue;
				}
			}
			else
			{
				if (zet < Move(PROMOTION_N)
					&& !pos.see_test(zet, SEE_0))
					continue;
			}

			skip_see_test:

			if (!pos.legal_move(zet))
				continue;

			pos.do_move(zet, geeftSchaak);
			Value waarde = geeftSchaak ? -qsearch<NT, true>(pos, -beta, -alfa, diepte - PLY)
				: -qsearch<NT, false>(pos, -beta, -alfa, diepte - PLY);
			pos.undo_move(zet);

			if ((st + 1)->geslagenStuk)
			{
				int offset = MoveValueStats::compute_offset((st + 1)->geslagenStuk, to_square(zet));
				if (waarde > alfa)
					pos.ti()->slagGeschiedenis.updatePlus(offset, SortValue(1000));
				else
					pos.ti()->slagGeschiedenis.updateMinus(offset, SortValue(2000));
			}

			if (waarde > besteWaarde)
			{
				besteWaarde = waarde;

				if (waarde > alfa)
				{
					if (PvNode)
						copy_PV(st->pv, zet, (st + 1)->pv);

					if (PvNode && waarde < beta)
					{
						alfa = waarde;
						besteZet = zet;
					}
					else
					{
						ttt = HoofdHash.find_replace(sleutel64);
						ttt->save(sleutel64, value_to_tt(waarde, st->ply), LOWER_BOUND + st->sterkeDreiging,
							ttDiepte, zet, st->stellingWaarde, HoofdHash.generatie());

						return waarde;
					}
				}
			}
		}

		if (staatSchaak && besteWaarde == -HIGHEST_VALUE)
			return mated_in(st->ply);

		ttt = HoofdHash.find_replace(sleutel64);
		ttt->save(sleutel64, value_to_tt(besteWaarde, st->ply),
			(PvNode && besteWaarde > origAlfa ? EXACT_VALUE : UPPER_BOUND) + st->sterkeDreiging,
			ttDiepte, besteZet, st->stellingWaarde, HoofdHash.generatie());

		assert(besteWaarde > -HIGHEST_VALUE && besteWaarde < HIGHEST_VALUE);

		return besteWaarde;
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


	void copy_PV(Move* pv, Move zet, Move* pvLager)
	{
		*pv++ = zet;
		if (pvLager)
			while (*pvLager != NO_MOVE)
				*pv++ = *pvLager++;
		*pv = NO_MOVE;
	}


	void update_stats(const Position& pos, bool staatSchaak, Move zet,
		Depth diepte, Move* rustigeZetten, int rustigAantal)
	{
		PositionInfo* st = pos.info();

		CounterMoveValues* cmh = st->counterZetWaarden;
		CounterMoveValues* fmh = (st - 1)->counterZetWaarden;
		CounterMoveValues* fmh2 = (st - 3)->counterZetWaarden;
		ThreadInfo* ti = pos.ti();
		MoveValueStats& history = staatSchaak ? ti->evasionHistory : ti->history;

		if (!pos.slag_of_promotie(zet))
		{
			if (st->killers[0] != zet)
			{
				st->killers[1] = st->killers[0];
				st->killers[0] = zet;
			}

			if (cmh)
				ti->counterZetten.update(st->bewogenStuk, to_square(st->vorigeZet), zet);

			if (cmh && fmh)
				ti->counterfollowupZetten.update((st - 1)->bewogenStuk, to_square((st - 1)->vorigeZet), 
					st->bewogenStuk, to_square(st->vorigeZet), zet);

			if (diepte < 18 * PLY)
			{
				SortValue bonus = counter_zet_bonus(diepte);
				SortValue hist_bonus = history_bonus(diepte);
				int offset = MoveValueStats::compute_offset(pos.moved_piece(zet), to_square(zet));
				history.updatePlus(offset, hist_bonus);
				if (cmh)
					cmh->updatePlus(offset, bonus);

				if (fmh)
					fmh->updatePlus(offset, bonus);

				if (fmh2)
					fmh2->updatePlus(offset, bonus);

				for (int i = 0; i < rustigAantal; ++i)
				{
					offset = MoveValueStats::compute_offset(pos.moved_piece(rustigeZetten[i]), to_square(rustigeZetten[i]));
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

		if ((st - 1)->zetNummer == 1 && !st->geslagenStuk)
		{
			if (diepte < 18 * PLY)
			{
				SortValue bonus = counter_zet_bonus(diepte + PLY);
				int offset = MoveValueStats::compute_offset(st->bewogenStuk, to_square(st->vorigeZet));

				if ((st - 1)->counterZetWaarden)
					(st - 1)->counterZetWaarden->updateMinus(offset, bonus);

				if ((st - 2)->counterZetWaarden)
					(st - 2)->counterZetWaarden->updateMinus(offset, bonus);

				if ((st - 4)->counterZetWaarden)
					(st - 4)->counterZetWaarden->updateMinus(offset, bonus);
			}
		}
	}


	void update_stats_quiet(const Position& pos, bool staatSchaak, Depth diepte, Move* rustigeZetten, int rustigAantal)
	{
		PositionInfo* st = pos.info();
		CounterMoveValues* cmh = st->counterZetWaarden;
		CounterMoveValues* fmh = (st - 1)->counterZetWaarden;
		CounterMoveValues* fmh2 = (st - 3)->counterZetWaarden;
		ThreadInfo* ti = pos.ti();
		MoveValueStats& history = staatSchaak ? ti->evasionHistory : ti->history;
		if (diepte < 18 * PLY)
		{
			SortValue bonus = SortValue(diepte);
			SortValue hist_bonus = SortValue(diepte);
			for (int i = 0; i < rustigAantal; ++i)
			{
				int offset = MoveValueStats::compute_offset(pos.moved_piece(rustigeZetten[i]), to_square(rustigeZetten[i]));
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


	void update_stats_minus(const Position& pos, bool staatSchaak, Move zet, Depth diepte)
	{
		PositionInfo* st = pos.info();
		CounterMoveValues* cmh = st->counterZetWaarden;
		CounterMoveValues* fmh = (st - 1)->counterZetWaarden;
		CounterMoveValues* fmh2 = (st - 3)->counterZetWaarden;
		ThreadInfo* ti = pos.ti();
		MoveValueStats& history = staatSchaak ? ti->evasionHistory : ti->history;

		if (!pos.slag_of_promotie(zet))
		{
			if (diepte < 18 * PLY)
			{
				SortValue bonus = counter_zet_bonus(diepte);
				SortValue hist_bonus = history_bonus(diepte);
				int offset = MoveValueStats::compute_offset(pos.moved_piece(zet), to_square(zet));
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
		int64_t elapsed = Tijdscontrole.elapsed();

		if (elapsed - vorigeInfoTijd >= 1000)
		{
			vorigeInfoTijd = (elapsed + 100) / 1000 * 1000;
			uint64_t nodes = Threads.visited_nodes();
			uint64_t nps = elapsed ? nodes / elapsed * 1000 : 0;
			uint64_t tbHits = Threads.tb_hits();
			sync_cout << "info time " << elapsed << " nodes " << nodes << " nps " << nps
				<< " tbhits " << tbHits << " hashfull " << HoofdHash.compute_hashfull() << sync_endl;
		}

		if (Klok.ponder)
			return;

		if ((Klok.uses_time_calculation() && elapsed > Tijdscontrole.maximum() - 10)
			|| (Klok.movetime && elapsed >= Klok.movetime)
			|| (Klok.nodes && Threads.visited_nodes() >= Klok.nodes))
			Signalen.stopAnalyse = true;
	}
} // namespace


std::string UCI::pv(const Position& pos, Value alfa, Value beta, int actievePV, int actieveZet) {

  std::stringstream ss;
  int elapsed = (int)Tijdscontrole.elapsed() + 1;
  const RootMoves& rootZetten = pos.my_thread()->rootZetten;
  int multiPV = std::min(Threads.multiPV, rootZetten.zetAantal);
  if (Threads.speelSterkte != 100)
	  multiPV = 1;

  uint64_t visited_nodes = Threads.visited_nodes();
  uint64_t tbHits = Threads.tb_hits();
  int hashFull = elapsed > 1000 ? HoofdHash.compute_hashfull() : 0;

  for (int i = 0; i < multiPV; ++i)
  {
	  const RootMove& rootZet = rootZetten[i == actievePV ? actieveZet : i];

	  int iter = rootZet.diepte / MAIN_THREAD_INC;
	  if (iter < 1)
          continue;

      Value v = i <= actievePV ? rootZet.score : rootZet.vorigeScore;

      bool tb = TB_RootInTB && abs(v) < EGTB_WIN_VALUE;
      v = tb ? TB_Score : v;

	  if (ss.rdbuf()->in_avail()) // Not at first line
		  ss << "\n";

	  // gebruik st->pionSleutel om maximale zoekdiepte te berekenen voor "seldepth" UCI output
	  int selDepth = 0;
	  PositionInfo * st = Threads.main()->rootStelling->info();
	  for (selDepth = 0; selDepth < MAX_PLY; selDepth++)
		  if ((st + selDepth)->pionSleutel == 0)
			  break;

      ss << "info multipv " << i + 1
		 << " depth "    << iter
         << " seldepth " << selDepth
         << " score "    << UCI::waarde(v);

      if (!tb && i == actievePV)
          ss << (v >= beta ? " lowerbound" : v <= alfa ? " upperbound" : "");

      ss << " time "     << elapsed
         << " nodes "    << visited_nodes
         << " nps "      << visited_nodes / elapsed * 1000
	     << " tbhits "   << tbHits;

      if (hashFull)
          ss << " hashfull " << hashFull;

      ss << " pv";

	  // limit the length of the pv (the final moves usually are poor anyways)
	  int pvLength = rootZet.pv.size();
	  if (pvLength > iter)
		  pvLength = std::max(iter, pvLength - 4);
	  for (int n = 0; n < pvLength; n++)
		  ss << " " << UCI::zet(rootZet.pv[n], pos);
  }

  return ss.str();
}


bool RootMove::find_ponder_move_in_tt(Position& pos)
{
    assert(pv.size() == 1);
	if (!pv[0])
		return false;

    pos.do_move(pv[0]);
    TTTuple* ttt = HoofdHash.find_existing(pos.sleutel() ^ pos.draw50_key());

    if (ttt)
    {
        Move zet = ttt->zet(); // Local copy to be SMP safe
		if (LegalMoveListContainsMove(pos, zet))
			pv.add(zet);
    }
	pos.undo_move(pv[0]);

    return pv.size() > 1;
}

void RootMove::complete_pv_with_tt(Position& pos)
{
	Key64 keys[MAX_PLY], key;
	int aantal = 0;

	assert(pv.size() == 1);
	Move zet = pv[0];
	keys[aantal++] = pos.sleutel();

	while (true)
	{
		pos.do_move(zet);
		key = pos.sleutel();

		bool repeated = false;
		for (int i = aantal - 2; i >= 0; i -= 2)
			if (keys[i] == key)
			{
				repeated = true;
				break;
			}
		if (repeated)
			break;

		keys[aantal++] = key;

		TTTuple* ttt = HoofdHash.find_existing(pos.sleutel() ^ pos.draw50_key());
		if (!ttt)
			break;
		zet = ttt->zet();
		if (!zet || !LegalMoveListContainsMove(pos, zet))
			break;
		pv.add(zet);
	}
	for (int i = pv.size(); i > 0; )
		pos.undo_move(pv[--i]);
}


namespace
{
	Depth ComputeEgtbUsefulness(const Position& pos)
	{
		int TotAantal = pos.all_pieces_count();
		const int WD = pos.aantal(WHITE, QUEEN);
		const int ZD = pos.aantal(BLACK, QUEEN);
		const int WT = pos.aantal(WHITE, ROOK);
		const int ZT = pos.aantal(BLACK, ROOK);
		const int WL = pos.aantal(WHITE, BISHOP);
		const int ZL = pos.aantal(BLACK, BISHOP);
		const int WP = pos.aantal(WHITE, KNIGHT);
		const int ZP = pos.aantal(BLACK, KNIGHT);
		const int WPion = pos.aantal(WHITE, PAWN);
		const int ZPion = pos.aantal(BLACK, PAWN);
		if (TotAantal == 4)
		{
			if (
				   WD == 1 && ZPion == 1 || ZD == 1 && WPion == 1  // KQKP
				|| WT == 1 && ZPion == 1 || ZT == 1 && WPion == 1  // KRKP
				|| WD == 1 && ZT == 1 || ZD == 1 && WT == 1  // KQKR
				|| WPion == 1 && ZPion == 1  // KPKP
				)
				return egtb_useful;
		}
		else if (TotAantal == 5)
		{
			if (
				   WD == 1 && ZD == 1 && WPion + ZPion == 1  // KQPKQ
				|| WT == 1 && ZT == 1 && WPion + ZPion == 1  // KRPKR
				|| WL + WP == 1 && ZL + ZP == 1 && WPion + ZPion == 1  // KBPKB, KNPKN, KBPKN, KNPKB
				|| WP == 2 && ZPion == 1 // KNNKP wit
				|| ZP == 2 && WPion == 1 // KNNKP zwart
				|| WD == 1 && ZT == 1 && ZPion == 1  // KQKRP wit
				|| ZD == 1 && WT == 1 && WPion == 1  // KQKRP zwart
				|| WPion == 2 && ZPion == 1  // KPPKP
				|| WPion == 1 && ZPion == 2  // KPPKP
				|| WL + WP + ZL + ZP == 1 && WPion == 1 && ZPion == 1 // KBPKP, KNPKP
				)
				return egtb_useful;
		}
		else if (TotAantal == 6)
		{
			if (
				   WD == 1 && ZD == 1 && WPion + ZPion == 2  // KQPKQP, KQPPKQ
				|| WT == 1 && ZT == 1 && WPion + ZPion == 2  // KRPKRP, KRPPKR
				|| WL + WP == 1 && ZL + ZP == 1 && WPion + ZPion == 2  // KBPPKB, KBPKBP, KNPPKN, KNPKNP, KBPPKN, KBPKNP, KNPPKB
				|| (WT == 1 && ZL + ZP == 1 || WL + WP == 1 && ZT == 1) && WPion == 1 && ZPion == 1  // KRPKBP, KRPKNP
				|| WD == 1 && ZT == 1 && ZPion == 2  // KQKRPP wit
				|| ZD == 1 && WT == 1 && WPion == 2  // KQKRPP zwart
				|| WPion == 2 && ZPion == 2  // KPPKPP
				|| WD == 1 && ZL == 1 && ZP == 1 && ZPion == 1  // KQKBNP wit
				|| ZD == 1 && WL == 1 && WP == 1 && WPion == 1  // KQKBNP wit
				)
				return egtb_useful;
		}
		return egtb_not_useful;
	}

	void FilterRootMoves(Position& pos, RootMoves& rootZetten)
	{
		int aantal_stukken = pos.all_pieces_count();

		// init de st->ply waarden (nodig voor juiste matwaarden)
		PositionInfo* st = pos.info();
		for (int n = 0; n <= MAX_PLY; n++)
			(st + n)->ply = n + 1;

		TB_Score = NO_VALUE;
		if (aantal_stukken <= EGTB::MaxPieces_dtm)
			TB_Score = EGTB::EGTB_probe_dtm(pos, -MATE_VALUE, MATE_VALUE);
		if (TB_Score == NO_VALUE && aantal_stukken <= EGTB::MaxPieces_dtz)
			TB_Score = EGTB::EGTB_probe_dtz(pos, -MATE_VALUE, MATE_VALUE);
		bool TB_afstand_gekend = TB_Score != NO_VALUE;
		if (TB_Score == NO_VALUE && aantal_stukken <= EGTB::MaxPieces_wdl)
			TB_Score = EGTB::EGTB_probe_wdl(pos, -MATE_VALUE, MATE_VALUE);
		
		TB_RootInTB = TB_Score != NO_VALUE;
		if (!TB_RootInTB)
			return;

		if (TB_Score < -EGTB_WIN_VALUE)
		{
			// verloren stelling, doe een normale Search
			TB_Cardinality = 0;
		}
		else if (TB_Score > EGTB_WIN_VALUE && TB_afstand_gekend)
		{
			// winnende stelling -> kies de zet met de laagste DTM of DTZ
			Value beste_waarde = -HIGHEST_VALUE;
			int beste_index = -1;

			for (int i = 0; i < rootZetten.zetAantal; i++)
			{
				Move zet = rootZetten[i].pv[0];
				pos.do_move(zet);
				if (!pos.info()->zetHerhaling)
				{
					Value EGTB_score;
					if (aantal_stukken <= EGTB::MaxPieces_dtm)
						EGTB_score = EGTB::EGTB_probe_dtm(pos, -MATE_VALUE, MATE_VALUE);
					else if (pos.fifty_moves_counter() == 0)
					{
						// pionzet of slagzet die wint => DTZ = 0 !
						EGTB_score = EGTB::EGTB_probe_wdl(pos, -MATE_VALUE, MATE_VALUE);
					}
					else
					{
						EGTB_score = EGTB::EGTB_probe_dtz(pos, -MATE_VALUE, MATE_VALUE);
						// een winnende zet kan een drievoudige zetherhaling zijn, test dit hier...
						//if (EGTB_score != GEEN_WAARDE && -EGTB_score > LANGSTE_MAT_WAARDE
						//	&& Heeft_ZetHerhaling(pos))
						//	EGTB_score = REMISE_WAARDE;
					}
					if (EGTB_score != NO_VALUE && -EGTB_score > beste_waarde)
					{
						beste_waarde = -EGTB_score;
						beste_index = i;
					}
				}
				pos.undo_move(zet);
			}

			if (beste_waarde != -HIGHEST_VALUE)
			{
				RootMove rootZet = rootZetten[beste_index];
				rootZet.score = beste_waarde;
				rootZetten.zetAantal = 0;
				rootZetten.add(rootZet);
				TB_Cardinality = 0;
			}
		}
		else
		{
			// behoud enkel zetten die dezelfde uitkomst behouden als in de root
			int aantal = 0;
			for (int i = 0; i < rootZetten.zetAantal; i++)
			{
				Move zet = rootZetten[i].pv[0];
				pos.do_move(zet);

				Value EGTB_score = NO_VALUE;
				if (pos.info()->zetHerhaling)
					EGTB_score = DRAW_VALUE;
				else if (aantal_stukken <= EGTB::MaxPieces_dtm)
					EGTB_score = EGTB::EGTB_probe_dtm(pos, -MATE_VALUE, MATE_VALUE);
				if (EGTB_score == NO_VALUE)
				{
					if (TB_afstand_gekend)
						EGTB_score = EGTB::EGTB_probe_dtz(pos, -MATE_VALUE, MATE_VALUE);
					else
						EGTB_score = EGTB::EGTB_probe_wdl(pos, -MATE_VALUE, MATE_VALUE);
				}

				pos.undo_move(zet);

				if (EGTB_score != NO_VALUE &&
					((TB_Score > EGTB_WIN_VALUE && -EGTB_score < EGTB_WIN_VALUE)
						|| -EGTB_score < -EGTB_WIN_VALUE))
				{
					// verwijder deze zet
				}
				else
					rootZetten.zetten[aantal++] = rootZetten.zetten[i];
			}
			if (aantal > 0)
			{
				rootZetten.zetAantal = aantal;
				TB_Cardinality = 0;
			}
		}

	}
}