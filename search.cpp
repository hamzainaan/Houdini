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
const Depth egtb_nuttig      = 0 * PLY;
const Depth egtb_niet_nuttig = 10 * PLY;

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
	inline int late_zet_aantal(Depth d, bool vooruitgang)
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
		void wis()
		{
			sleutelNaTweeZetten = 0;
			derdeZetStabiel = 0;
			pv[0] = pv[1] = pv[2] = GEEN_ZET;
		}

		Move verwachte_zet(Key64 sleutel) const
		{
			return sleutelNaTweeZetten == sleutel ? pv[2] : GEEN_ZET;
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

				pos.speel_zet(pvNieuw[0]);
				pos.speel_zet(pvNieuw[1]);
				sleutelNaTweeZetten = pos.sleutel();
				pos.neem_zet_terug(pvNieuw[1]);
				pos.neem_zet_terug(pvNieuw[0]);
			}
		}

		int derdeZetStabiel;
		Key64 sleutelNaTweeZetten;
		Move pv[3];
	};

	EasyMoveManager GemakkelijkeZet;
	Value RemiseWaarde[KLEUR_N];
	uint64_t vorigeInfoTijd;

	template <NodeType NT>
	Value search(Position& pos, Value alpha, Value beta, Depth diepte, bool cutNode);

	template <NodeType NT, bool staatSchaak>
	Value qsearch(Position& pos, Value alfa, Value beta, Depth diepte);

	Value waarde_voor_tt(Value v, int ply);
	Value waarde_uit_tt(Value v, int ply);
	void kopieer_PV(Move* pv, Move zet, Move* pvLager);
	void update_stats(const Position& pos, bool staatSchaak, Move zet, Depth diepte, Move* rustigeZetten, int rustigAantal);
	void update_stats_rustig(const Position& pos, bool staatSchaak, Depth diepte, Move* rustigeZetten, int rustigAantal);
	void update_stats_minus(const Position& pos, bool staatSchaak, Move zet, Depth diepte);
	void tijdscontrole();

	Depth BerekenEgtbNut(const Position& pos);
	void FilterRootZetten(Position& pos, RootMoves& rootZetten);

	const Value QSearchFutilityWaarde[STUK_N] = {
		Value(102), WAARDE_0, Value(308), Value(818), Value(827), Value(1186), Value(2228), WAARDE_0,
		Value(102), WAARDE_0, Value(308), Value(818), Value(827), Value(1186), Value(2228), WAARDE_0
	};

	const Value LazyMarginQSearchLow = Value(480) + WAARDE_1;
	const Value LazyMarginQSearchHigh = Value(480);

	PolyglotBook openingBoek;

} // namespace


void Search::initialisatie()
{
	std::memset(LMReducties, 0, sizeof(LMReducties));

#if !defined(USE_LICENSE) && !defined(IMPORT_TABELLEN)
	for (int d = PLY; d < 64 * PLY; ++d)
		for (int n = 2; n < 64; ++n)
		{
			double rr = log(double(d) / PLY) * log(n) / 2 * int(PLY);
			if (rr < 6.4)
				continue;
			Depth r = Depth(std::lround(rr));

			LMReducties[false][1][d][n] = uint8_t(r);
			LMReducties[false][0][d][n] = uint8_t(r + (r < 2 * PLY ? 0 * PLY : PLY));
			LMReducties[true][1][d][n] = uint8_t(std::max(r - PLY, DIEPTE_0));
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


void Search::wis_alles()
{
	if (!UciOpties["Never Clear Hash"])
		HoofdHash.wis();
	Threads.wis_counter_zet_geschiedenis();

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

	Threads.main()->vorigeRootScore = HOOGSTE_WAARDE;
	Threads.main()->vorigeRootDiepte = 999 * PLY;
	Threads.main()->snelleZetToegelaten = false;
	openingBoek.nieuwe_partij();
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


void bereken_EGTB_mat_PV(Position& pos, Value verwachte_score, PrincipalVariation& pv)
{
	Value score = GEEN_WAARDE;

	for (const auto& zet : LegalMoveList(pos))
	{
		pos.speel_zet(zet);

		int aantal_stukken = pos.alle_stukken_aantal();
		if (aantal_stukken <= EGTB::MaxPieces_dtm)
			score = EGTB::EGTB_probe_dtm(pos, -MAT_WAARDE, MAT_WAARDE);

		if (score == verwachte_score)
		{
			pv.add(zet);
			bereken_EGTB_mat_PV(pos, -score, pv);
			pos.neem_zet_terug(zet);
			return;
		}
		else
			pos.neem_zet_terug(zet);
	}
	return;
}


void kies_niet_te_sterke_zet(Thread* thread)
{
	Value max_waarde, random_score;
	static PRNG rng(nu());

	int max_index = 0;
	if (Threads.speelSterkte <= 10)
		random_score = Value(2 * (200 - 10 * Threads.speelSterkte));
	else if (Threads.speelSterkte <= 30)
		random_score = Value(2 * (120 - 2 * Threads.speelSterkte));
	else if (Threads.speelSterkte <= 60)
		random_score = Value(2 * (90 - Threads.speelSterkte));
	else
		random_score = Value(2 * (60 - Threads.speelSterkte / 2));

	max_waarde = WAARDE_0; // avoid warning
	for (int PV_index = 0; PV_index < Threads.multiPV; PV_index++)
	{
		RootMove zet = thread->rootZetten[PV_index];
		if (zet.pv[0] == GEEN_ZET)
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

void Search::pas_tijd_aan_na_ponderhit()
{
#ifdef PONDER_SMART_HASH
	HoofdHash.nieuwe_generatie();
#endif
	if (Klok.gebruikt_tijd_berekening())
		Tijdscontrole.aanpassing_na_ponderhit();
}


void Search::export_tabel()
{
	FILE *f = fopen("R:\\Schaak\\h6-lmr-export.dat", "wb");
	fwrite(LMReducties, 1, sizeof(LMReducties), f);
	fclose(f);
}


void Search::import_tabel()
{
	FILE *f = fopen("R:\\Schaak\\h6-lmr-export.dat", "rb");
	fread(LMReducties, 1, sizeof(LMReducties), f);
	fclose(f);
}


void MainThread::start_zoeken()
{
	Running = true;
	rootStelling->kopieer_stelling(Threads.rootStelling, nullptr, nullptr);
	const Color ik = rootStelling->aan_zet();
	Tijdscontrole.initialisatie(Klok, ik, rootStelling->partij_ply());
	vorigeInfoTijd = 0;
	interruptTeller = 0;

#ifdef USE_LICENSE
#ifdef CHESSBASE
	if (app_file_size() != 0xC3C3C3C3)
		memset(PST::psq, 0, sizeof(PST::psq));
#endif
#endif

	Threads.contemptKleur = ik;
	Threads.analyseModus = !Klok.gebruikt_tijd_berekening();
	Threads.tactischeModusIndex = UciOpties["Tactical Mode"];
	for (int i = 0; i < Threads.threadCount; ++i)
		Threads.threads[i]->tactischeModus = Threads.tactischeModusIndex < 2 ? Threads.tactischeModusIndex : (i % Threads.tactischeModusIndex) == 0;

#if 1
	Threads.vijftigZettenAfstand = UciOpties["FiftyMoveDistance"];
	Threads.vijftigZettenAfstand = std::min(50, std::max(Threads.vijftigZettenAfstand, rootStelling->vijftig_zetten_teller() / 2 + 5));
	Threads.stukContempt = (Threads.analyseModus && !UciOpties["Analysis Contempt"]) ? 0 : int(UciOpties["Contempt"]);
	if (Threads.stukContempt)
	{
		if (Threads.analyseModus)
			Threads.contemptKleur = WIT;

		int tempContempt = Threads.stukContempt;
		Threads.stukContempt = 0;
		Value v1 = Evaluation::evaluatie(*rootStelling, GEEN_WAARDE, GEEN_WAARDE);
		Threads.stukContempt = tempContempt;
		Value v2 = Evaluation::evaluatie(*rootStelling, GEEN_WAARDE, GEEN_WAARDE);
		if (abs(v1) < WINST_WAARDE && abs(v2) < WINST_WAARDE)
			Threads.rootContemptWaarde = v2 - v1;
		else
			Threads.rootContemptWaarde = WAARDE_0;
	}
	Threads.multiPV = Threads.multiPVmax = UciOpties["MultiPV"];
	Threads.multiPV_cp = UciOpties["MultiPV_cp"];
	if (Threads.multiPV_cp > 0)
		Threads.multiPV = 2;  // beginwaarde, wordt bijgewerkt door algoritme
	if (UciOpties["UCI_LimitStrength"])
		Threads.speelSterkte = UCI::sterkte_voor_elo(int(UciOpties["UCI_Elo"]) - 200);
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
		RemiseWaarde[ik] = REMISE_WAARDE;
		RemiseWaarde[~ik] = REMISE_WAARDE;
	}
	else
	{
		RemiseWaarde[ik] = REMISE_WAARDE - DEFAULT_DRAW_VALUE * rootStelling->partij_fase() / MIDDENSPEL_FASE;
		RemiseWaarde[~ik] = REMISE_WAARDE + DEFAULT_DRAW_VALUE * rootStelling->partij_fase() / MIDDENSPEL_FASE;
	}

#ifdef PONDER_SMART_HASH
	if (!Klok.ponder)
#endif
		HoofdHash.nieuwe_generatie();
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
		rootZetten.add(RootMove(GEEN_ZET));
		rootZetten[0].score = rootStelling->schaak_gevers() ? -MAT_WAARDE : REMISE_WAARDE;
		rootZetten[0].diepte = MAIN_THREAD_INC;
		Threads.activeThreadCount = 1;  // enkel MainThread heeft een geïnitialiseerde stelling
		sync_cout << UCI::pv(*rootStelling, -HOOGSTE_WAARDE, HOOGSTE_WAARDE, 999, 0) << sync_endl;
	}
	else
	{
		if (rootStelling->alle_stukken_aantal() <= TB_Cardinality
			&& !rootStelling->rokade_mogelijk(ALLE_ROKADE)
			&& Threads.speelSterkte == 100)
		{
			FilterRootZetten(*rootStelling, rootZetten);

			if (TB_RootInTB && rootZetten.zetAantal == 1)
			{
				rootZetten[0].diepte = MAIN_THREAD_INC;
				rootZetten[0].score = TB_Score;

				if (Threads.analyseModus && abs(TB_Score) > LANGSTE_MAT_WAARDE)
				{
					rootStelling->speel_zet(rootZetten[0].pv[0]);
					bereken_EGTB_mat_PV(*rootStelling, TB_Score, rootZetten[0].pv);
					rootStelling->neem_zet_terug(rootZetten[0].pv[0]);
					rootZetten[0].diepte = MAIN_THREAD_INC * rootZetten[0].pv.size();
				}
				if (Tijdscontrole.verstreken() > 100)
					sync_cout << UCI::pv(*rootStelling, -HOOGSTE_WAARDE, HOOGSTE_WAARDE, 999, 0) << sync_endl;
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
			Threads.threads[i]->maak_wakker(true);

		Thread::start_zoeken();

		close_tracefile();
	}
NO_ANALYSIS:

	if (!Signalen.stopAnalyse && (Klok.ponder || Klok.infinite))
	{
		if (Tijdscontrole.verstreken() <= 102)
			sync_cout << UCI::pv(*rootStelling, -HOOGSTE_WAARDE, HOOGSTE_WAARDE, 999, 0) << sync_endl;

		// diepte = 1 -> some GUIs will not show the output, repeat it with diepte 99
		if (rootZetten[0].diepte == MAIN_THREAD_INC)
		{
			rootZetten[0].diepte = 99 * MAIN_THREAD_INC;
			sync_cout << UCI::pv(*rootStelling, -HOOGSTE_WAARDE, HOOGSTE_WAARDE, 999, 0) << sync_endl;
		}

		Signalen.stopBijPonderhit = true;
		wait(Signalen.stopAnalyse);
	}

	Signalen.stopAnalyse = true;

	for (int i = 1; i < Threads.activeThreadCount; ++i)
		Threads.threads[i]->wacht_op_einde_zoeken();

	// kies beste thread (hogere diepte EN betere score)
	Thread* bestThread = this;

	if (Threads.speelSterkte != 100)
		kies_niet_te_sterke_zet(this);

	else if (!this->snelleZetGespeeld
		&& Threads.multiPV == 1
		&& !Klok.depth
		&&  rootZetten[0].pv[0] != GEEN_ZET)
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

	if (bestThread != this || Tijdscontrole.verstreken() <= 102 || Signalen.stopBijPonderhit)
	{
		// gebruik diepte van de main Thread (om geen sprongen te hebben in de output)
		bestThread->rootZetten[0].diepte = rootZetten[0].diepte;
		sync_cout << UCI::pv(*bestThread->rootStelling, -HOOGSTE_WAARDE, HOOGSTE_WAARDE, 999, 0) << sync_endl;
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

	if (bestThread->rootZetten[0].pv.size() > 1 || bestThread->rootZetten[0].vind_ponder_zet_in_tt(*rootStelling))
		std::cout << " ponder " << UCI::zet(bestThread->rootZetten[0].pv[1], *rootStelling);

	std::cout << sync_endl;

	Threads.totaleAnalyseTijd += int(Tijdscontrole.verstreken());

	Running = false;
}


void Thread::start_zoeken()
{
	Value besteWaarde, alfa, beta, delta_alfa, delta_beta;
	Move snelleZet = GEEN_ZET;
	MainThread* mainThread = (this == Threads.main() ? Threads.main() : nullptr);
	//Diepte last_smart_fail_high_diepte = 15 * PLY;

	if (!mainThread)
	{
		rootStelling->kopieer_stelling(Threads.rootStelling, this, Threads.rootStellingInfo);
		rootZetten = Threads.rootZetten;
	}

	PositionInfo* st = rootStelling->info();

	std::memset(st + 1, 0, 2 * sizeof(PositionInfo));
	st->killers[0] = st->killers[1] = GEEN_ZET;
	st->vorigeZet = GEEN_ZET;
	(st - 2)->stellingWaarde = WAARDE_0;
	(st - 1)->stellingWaarde = WAARDE_0;
	(st - 1)->evalPositioneel = GEEN_EVAL;
	(st - 1)->zetNummer = 0;
	(st - 4)->counterZetWaarden = (st - 3)->counterZetWaarden = (st - 2)->counterZetWaarden = (st - 1)->counterZetWaarden = st->counterZetWaarden = nullptr;
	(st - 1)->mp_eindeLijst = rootStelling->ti()->zettenLijst;
	//(st - 1)->statsWaarde = SORT_MAX;

	for (int n = 0; n <= MAX_PLY; n++)
	{
		(st + n)->doeGeenVroegePruning = false;
		(st + n)->uitgeslotenZet = GEEN_ZET;
		(st + n)->lmr_reductie = 0;
		(st + n)->ply = n + 1;
	}

	besteWaarde = delta_alfa = delta_beta = alfa = -HOOGSTE_WAARDE;
	beta = HOOGSTE_WAARDE;
	afgewerkteDiepte = 0 * PLY;

	if (mainThread)
	{
		snelleZet = GemakkelijkeZet.verwachte_zet(rootStelling->sleutel());
		GemakkelijkeZet.wis();
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
		TTTuple* ttt = HoofdHash.zoek_bestaand(rootStelling->sleutel());
		if (ttt && ttt->limiet() == EXACTE_WAARDE)
		{
			Value ttWaarde = waarde_uit_tt(ttt->waarde(), st->ply);
			Move ttZet = ttt->zet();
			Depth ttDiepte = ttt->diepte();

			if (ttDiepte >= mainThread->vorigeRootDiepte - 3 * PLY
				&& ttZet
				&& rootStelling->legale_zet(ttZet)
				&& abs(ttWaarde) < WINST_WAARDE)
			{
				Depth depth_singular = std::max(mainThread->vorigeRootDiepte / 2, mainThread->vorigeRootDiepte - 8 * PLY);
				Value v_singular = ttWaarde - Value(102);
				st->uitgeslotenZet = ttZet;
				st->stellingWaarde = Evaluation::evaluatie(*rootStelling, GEEN_WAARDE, GEEN_WAARDE);
				mainThread->snelleZetEvaluatieBezig = true;
				Value v = search<NonPV>(*rootStelling, v_singular - WAARDE_1, v_singular, depth_singular, false);
				mainThread->snelleZetEvaluatieBezig = false;
				st->uitgeslotenZet = GEEN_ZET;

				if (!mainThread->snelleZetEvaluatieAfgebroken && v < v_singular)
				{
					Signalen.stopAnalyse = true;
					rootZetten[0].score = ttWaarde;
					rootZetten[0].pv.resize(1);
					rootZetten[0].pv[0] = ttZet;
					rootZetten[0].vervolledig_pv_met_tt(*rootStelling);
					rootZetten[0].diepte = ttDiepte;
					mainThread->snelleZetToegelaten = false;
					mainThread->snelleZetGespeeld = true;
					GemakkelijkeZet.wis();
					afgewerkteDiepte = mainThread->vorigeRootDiepte - 2 * PLY;
					sync_cout << UCI::pv(*rootStelling, -HOOGSTE_WAARDE, HOOGSTE_WAARDE, 999, 0) << sync_endl;
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
			rootStelling->speel_zet(zet);
			(st + 1)->doeGeenVroegePruning = true;
			(st + 1)->pv = pv;
			(st + 1)->pv[0] = GEEN_ZET;
			rootZetten[i].startWaarde = -::search<PV>(*rootStelling, -HOOGSTE_WAARDE, HOOGSTE_WAARDE, PLY, false);
			(st + 1)->doeGeenVroegePruning = false;
			rootStelling->neem_zet_terug(zet);
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

			if (Threads.speelSterkte != 100 && Threads.bezochte_knopen() >= Threads.speelSterkteMaxKnopen)
				Signalen.stopAnalyse = true;
		}

		if (Signalen.stopAnalyse)
			break;

		if (mainThread)
		{
			mainThread->besteZetVerandert /= 2;
			mainThread->failedLow = false;
		}

		if (mainThread && Tijdscontrole.verstreken() > 1000)
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
				alfa = std::max(rootZetten[actievePV].vorigeScore - delta_alfa, -HOOGSTE_WAARDE);
				beta = std::min(rootZetten[actievePV].vorigeScore + delta_beta, HOOGSTE_WAARDE);
			}

			while (true)
			{
				if (alfa < -20 * WAARDE_PION)
					alfa = -HOOGSTE_WAARDE;
				if (beta > 20 * WAARDE_PION)
					beta = HOOGSTE_WAARDE;

//#define SMART_FAIL_HIGH
#ifdef SMART_FAIL_HIGH
				if (failHighCount == 2 && Threads.analyseModus && rootZetten[actievePV].pv[0] != prevBestMove 
					&& rootDiepte > last_smart_fail_high_diepte)
				{
					last_smart_fail_high_diepte = rootDiepte;
					bool showOutput = mainThread && Tijdscontrole.verstreken() > 4000;
					if (showOutput)
						sync_cout << "info string smart fail high - attempt reducing search depth to " << zoekIteratie - 5 << sync_endl;

					Move m = rootZetten[actievePV].pv[0];
					Move pv[MAX_PLY + 1];
					(st + 1)->pv = pv;

					rootStelling->speel_zet(m);

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

					rootStelling->neem_zet_terug(m);
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
					&& Tijdscontrole.verstreken() > Tijdscontrole.optimum() * 124 / 1024)
				{
					bool speelGemakkelijkeZet = rootZetten[0].pv[0] == snelleZet && mainThread->besteZetVerandert < 31;

					if (speelGemakkelijkeZet)
						failHighOplossen = false;

					else if (Tijdscontrole.verstreken() > Tijdscontrole.optimum() * 420 / 1024)
					{
						int verbeteringFactor = std::max(420, std::min(1304,
							652 + 160 * mainThread->failedLow - 12 * int(besteWaarde - mainThread->vorigeRootScore)));
						int onstabielFactor = 1024 + mainThread->besteZetVerandert;
						if (Tijdscontrole.verstreken() > Tijdscontrole.optimum() * onstabielFactor / 1024 * verbeteringFactor / 1024)
							failHighOplossen = false;
					}
				}

				if (besteWaarde <= alfa)
				{
					beta = (alfa + beta) / 2;
					alfa = std::max(besteWaarde - delta_alfa, -HOOGSTE_WAARDE);
					failHighCount = 0;

					if (mainThread)
					{
						mainThread->failedLow = true;
						Signalen.stopBijPonderhit = false;
					}
				}
				else if (besteWaarde >= beta && (failHighOplossen || besteWaarde >= WAARDE_PION * 8))
				{
					alfa = (alfa + beta) / 2;
					beta = std::min(besteWaarde + delta_beta, HOOGSTE_WAARDE);
					++failHighCount;
				}
				else
					break;

				delta_alfa += delta_alfa / 4 + Value(4);
				delta_beta += delta_beta / 4 + Value(4);

				assert(alfa >= -HOOGSTE_WAARDE && beta <= HOOGSTE_WAARDE);
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
					((actievePV + 1 == Threads.multiPV && Tijdscontrole.verstreken() > 100) || Tijdscontrole.verstreken() > 4000))
					sync_cout << UCI::pv(*rootStelling, alfa, beta, actievePV, actievePV) << sync_endl;
			}
		}

		if (!Signalen.stopAnalyse)
			afgewerkteDiepte = rootDiepte;

		if (!mainThread)
			continue;

		if (Klok.mate
			&& besteWaarde >= LANGSTE_MAT_WAARDE
			&& MAT_WAARDE - besteWaarde <= 2 * Klok.mate)
			Signalen.stopAnalyse = true;

		if (!Threads.analyseModus && !Klok.ponder && besteWaarde > MAT_WAARDE - 32
			&& rootDiepte >= (MAT_WAARDE - besteWaarde + 10) * PLY)
			Signalen.stopAnalyse = true;

		if (!Threads.analyseModus && !Klok.ponder && besteWaarde < -MAT_WAARDE + 32
			&& rootDiepte >= (MAT_WAARDE + besteWaarde + 10) * PLY)
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
					&& Tijdscontrole.verstreken() > Tijdscontrole.optimum() * 124 / 1024;

				if ((rootZetten.zetAantal == 1 && zoekIteratie > 10)
					|| Tijdscontrole.verstreken() > Tijdscontrole.optimum() * onstabielFactor / 1024 * verbeteringFactor / 1024
					|| (mainThread->snelleZetGespeeld = speelGemakkelijkeZet))
				{
					// If we are allowed to ponder do not stop the search now but
					// keep pondering until the GUI sends "ponderhit" or "stop".
					if (Klok.ponder)
						Signalen.stopBijPonderhit = true;
					else
						Signalen.stopAnalyse = true;
#ifdef TRACE_TM
					trace_tm_msg(1 + rootStelling->partij_ply() / 2, Time.verstreken(), Time.optimum(), mainThread->snelleZetGespeeld ? "Easy Move" : "");
#endif
				}
#if 0
				else if (zoekIteratie >= 12 && mainThread && !mainThread->snelleZetGetest
					&& (Time.verstreken() >= Time.optimum() / 4 || zoekIteratie >= mainThread->vorige_root_iteration - 1))
				{
					mainThread->snelleZetGetest = true;

					int iteration_singular = std::max(zoekIteratie / 2, zoekIteratie - 8);
					Value v_singular = besteWaarde - Value(320);
					st->uitgeslotenZet = rootZetten[0].pv[0];
					st->doeGeenVroegePruning = true;
					Value v = ::search<NonPV>(rootStelling, v_singular - 1, v_singular, rootDepthForIteration(iteration_singular), false);
					st->uitgeslotenZet = GEEN_ZET;
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
				GemakkelijkeZet.wis();
		}
	}

	if (!mainThread)
		return;

	// Clear any candidate easy move that wasn't stable for the last search
	// iterations; the second condition prevents consecutive fast moves.
	if (GemakkelijkeZet.derdeZetStabiel < 6 || mainThread->snelleZetGespeeld)
		GemakkelijkeZet.wis();
}


namespace
{
	template <NodeType NT>
	Value search(Position& pos, Value alfa, Value beta, Depth diepte, bool cutNode)
	{
		const bool PvNode = NT == PV;
		const int MAX_RUSTIGE_ZETTEN = 64;

		assert(-HOOGSTE_WAARDE <= alfa && alfa < beta && beta <= HOOGSTE_WAARDE);
		assert(PvNode || (alfa == beta - 1));
		assert(diepte >= PLY && diepte < MAX_DIEPTE);

		Move pv[MAX_PLY + 1], rustigeZetten[MAX_RUSTIGE_ZETTEN];
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
		Thread* mijnThread = pos.mijn_thread();
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
					int64_t verstreken = Tijdscontrole.verstreken();
					if (verstreken > 1000 || verstreken > Tijdscontrole.optimum() / 16)
					{
						mainThread->snelleZetEvaluatieAfgebroken = true;
						return alfa;
					}
				}
				tijdscontrole();
				mainThread->interruptTeller = 0;
			}
		}

		if (!rootKnoop)
		{
			// Step 2. Check for aborted search and immediate draw
			if (Signalen.stopAnalyse.load(std::memory_order_relaxed) || st->zetHerhaling || st->ply >= MAX_PLY)
				return st->ply >= MAX_PLY && !staatSchaak 
					? Evaluation::evaluatie(pos, GEEN_WAARDE, GEEN_WAARDE)
					: RemiseWaarde[pos.aan_zet()];

			// Step 3. Mate distance pruning
			alfa = std::max(staat_mat(st->ply), alfa);
			beta = std::min(geeft_mat(st->ply + 1), beta);
			if (alfa >= beta)
				return alfa;
		}

		assert(1 <= st->ply && st->ply < MAX_PLY);

		besteZet = GEEN_ZET;
		(st + 2)->killers[0] = (st + 2)->killers[1] = GEEN_ZET;
		st->statsWaarde = SORT_MAX;
		//uint64_t knopen = pos.bezochte_knopen();

		// Step 4. Transposition table lookup
		if (st->uitgeslotenZet)
		{
			// st->stellingwaarde is nog juist, van de callende routine
			ttt = nullptr;
			ttZet = GEEN_ZET;
			ttWaarde = GEEN_WAARDE;  // overbodig, maar vermijdt warning van compiler
			sleutel64 = 0;  // overbodig, maar vermijdt warning van compiler
			// sla ook IID over, want we slaan toch geen zetten op in de hashtabel
			goto bekijk_alle_zetten;
		}

		//sleutel64 = st->uitgeslotenZet ? st->sleutel ^ pos.uitzondering_sleutel(st->uitgeslotenZet) : st->sleutel;
		sleutel64 = st->sleutel;
		sleutel64 ^= pos.remise50_sleutel();
		ttt = HoofdHash.zoek_bestaand(sleutel64);
		ttWaarde = ttt ? waarde_uit_tt(ttt->waarde(), st->ply) : GEEN_WAARDE;
		ttZet = rootKnoop ? mijnThread->rootZetten[mijnThread->actievePV].pv[0]
			: ttt ? ttt->zet() : GEEN_ZET;

		if (!PvNode
			&& ttWaarde != GEEN_WAARDE
			&& ttt->diepte() >= diepte
			&& (ttWaarde >= beta ? (ttt->limiet() & ONDERGRENS)
				: (ttt->limiet() & BOVENGRENS)))
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
		if (!rootKnoop && TB_Cardinality && diepte >= TB_ProbeDepth && pos.materiaal_of_rokade_gewijzigd())
		{
			int aantalStukken = pos.alle_stukken_aantal();

			if (aantalStukken <= TB_Cardinality
				//&& (/*aantalStukken < TB_Cardinality ||*/ diepte >= TB_ProbeDepth + BerekenEgtbNut(pos))
				&& diepte >= TB_ProbeDepth
				&& !pos.rokade_mogelijk(ALLE_ROKADE))
			{
				Value waarde;
				if (aantalStukken <= EGTB::MaxPieces_wdl)
					waarde = EGTB::EGTB_probe_wdl(pos, alfa, beta);
				else if (aantalStukken <= EGTB::MaxPieces_dtm)
					waarde = EGTB::EGTB_probe_dtm(pos, alfa, beta);
				else
					waarde = GEEN_WAARDE;

				if (waarde != GEEN_WAARDE)
				{
					ttt = HoofdHash.zoek_vervang(sleutel64);
					ttt->bewaar(sleutel64, waarde_voor_tt(waarde, st->ply), EXACTE_WAARDE, 
						std::min(MAX_DIEPTE - PLY, diepte + 6 * PLY), 
						GEEN_ZET, GEEN_WAARDE, HoofdHash.generatie());

					return waarde;
				}
#if 0
				// dit is Joseph Ellis manier om TB scores eventueel te laten overschrijven door een gevonden mat 
				// zie https://github.com/official-stockfish/Stockfish/compare/master...jhellis3:tb_fix_2
				if (waarde == REMISE_WAARDE
					|| !ttt || (waarde < REMISE_WAARDE && ttWaarde > -EGTB_WIN_WAARDE) || (waarde > REMISE_WAARDE && ttWaarde < EGTB_WIN_WAARDE))
				{
					ttt = HoofdHash.zoek_vervang(sleutel64);
					ttt->bewaar(sleutel64, waarde_voor_tt(waarde, st->ply),
						waarde > REMISE_WAARDE ? ONDERGRENS : waarde < REMISE_WAARDE ? BOVENGRENS : EXACTE_WAARDE,
						diepte, GEEN_ZET, GEEN_WAARDE, HoofdHash.generatie());

					if (waarde == REMISE_WAARDE)
						return waarde;
				}
#endif
			}
		}

		// Step 5. Evaluate the position statically
		if (staatSchaak)
		{
			st->stellingWaarde = GEEN_WAARDE;
			goto bekijk_alle_zetten;
		}

		if (ttt && ttt->eval() != GEEN_WAARDE)
		{
			eval = st->stellingWaarde = ttt->eval();
			st->sterkeDreiging = ttt->dreiging();

			if (ttWaarde != GEEN_WAARDE)
				if (ttt->limiet() & (ttWaarde > eval ? ONDERGRENS : BOVENGRENS))
					eval = ttWaarde;
		}
		else
		{
			if (st->vorigeZet != NULL_ZET)
				eval = Evaluation::evaluatie(pos, GEEN_WAARDE, GEEN_WAARDE);
				//eval = Evaluatie::evaluatie(pos, 
					//PvNode || diepte >= 4 * PLY ? GEEN_WAARDE : alfa - LazyMarginQSearchLow - Waarde(120) * (diepte / PLY),
					//PvNode || diepte >= 4 * PLY ? GEEN_WAARDE : beta + LazyMarginQSearchHigh + Waarde(120) * (diepte / PLY));
			else
				eval = Evaluation::evaluatie_na_nullzet((st - 1)->stellingWaarde);

			st->stellingWaarde = eval;
			if (st->eval_is_exact && !rootKnoop)
				return eval;

			ttt = HoofdHash.zoek_vervang(sleutel64);
			ttt->bewaar(sleutel64, GEEN_WAARDE, GEEN_LIMIET + st->sterkeDreiging, GEEN_DIEPTE, GEEN_ZET,
				st->stellingWaarde, HoofdHash.generatie());
		}

		trace_eval(st->stellingWaarde);

		if (st->vorigeZet != NULL_ZET
			/*&& st->stellingWaarde != GEEN_WAARDE*/ && (st - 1)->stellingWaarde != GEEN_WAARDE
			&& st->materiaalSleutel == (st - 1)->materiaalSleutel)
		{
			const Value MAX_GAIN = Value(500);
			Value gain = -st->stellingWaarde - (st - 1)->stellingWaarde + 2 * WAARDE_TEMPO;
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
				return qsearch<NonPV, false>(pos, alfa, beta, DIEPTE_0);

			Value ralpha = alfa - razer_margin(diepte);
			Value v = qsearch<NonPV, false>(pos, ralpha, ralpha + WAARDE_1, DIEPTE_0);
			if (v <= ralpha)
				return v;
		}

		// Step 7. Futility pruning: child node
		if (!rootKnoop
			&& diepte < 7 * PLY
			&& eval - futility_marge1(diepte) >= beta
			&& eval < WINST_WAARDE  // Do not return unproven wins
			&& st->nietPionMateriaal[pos.aan_zet()])
			return eval - futility_marge1(diepte);

		// Step 8. Null move search with verification search
		if (!PvNode
			&& diepte >= 2 * PLY
			&& eval >= beta + 2 * WAARDE_TEMPO
			&& (!st->sterkeDreiging || diepte >= 8 * PLY)
			&& (st->stellingWaarde >= beta || diepte >= 12 * PLY)
			&& st->nietPionMateriaal[pos.aan_zet()]
			//&& pos.stukken(pos.aan_zet(), PION)
			&& (!Threads.analyseModus || diepte < 8 * PLY || Evaluation::minstens_twee_mobiele_stukken(pos))
			)   // goede test positie voor minstens_twee_mobiele_stukken: 8/6B1/p5p1/Pp4kp/1P5r/5P1Q/4q1PK/8 w - - 0 32 bm Qxh4
		{
			assert(eval - beta >= 0);

			Depth R;
			if (mijnThread->tactischeModus)
				R = diepte < 4 * PLY ? diepte :
				(540 + 66 * ((unsigned int)diepte / PLY)
					+ std::max(std::min(310 * (eval - beta) / Value(204) - 20 - 15 * cutNode - 15 * (ttZet != GEEN_ZET), 3 * 256), 0)
					) / 256 * PLY;
			else
				R = diepte < 4 * PLY ? diepte :
				(480 + 76 * ((unsigned int)diepte / PLY)
				//(480 + 76 * std::min((unsigned int)diepte / PLY, (unsigned int)diepte / PLY / 2 + 6)
					+ std::max(std::min(310 * (eval - beta) / Value(204) - 20 - 15 * cutNode - 15 * (ttZet != GEEN_ZET), 3 * 256), 0)
					) / 256 * PLY;

			st->mp_eindeLijst = (st - 1)->mp_eindeLijst;
			pos.speel_null_zet();
			(st + 1)->doeGeenVroegePruning = true;
			Value waarde = diepte - R < PLY ? -qsearch<NonPV, false>(pos, -beta, -beta + WAARDE_1, DIEPTE_0)
				: -search<NonPV>(pos, -beta, -beta + WAARDE_1, diepte - R, !cutNode);
			(st + 1)->doeGeenVroegePruning = false;
			pos.neem_null_terug();

			if ((st - 1)->lmr_reductie && (waarde < beta - Value(100) || waarde < -LANGSTE_MAT_WAARDE))
			{
				if ((st - 1)->lmr_reductie <= 2 * PLY)
					return beta - WAARDE_1;
				diepte += 2 * PLY;

				//int delta = std::min(int((st - 1)->lmr_reductie), int(2 * PLY));
				//(st - 1)->lmr_reductie -= delta;
				//diepte += Diepte(delta);
			}

			if (waarde >= beta)
			{
				if (waarde >= LANGSTE_MAT_WAARDE)
					waarde = beta;

				if (diepte < 12 * PLY && abs(beta) < WINST_WAARDE)
					return waarde;

				st->doeGeenVroegePruning = true;
				Value v = diepte - R < PLY ? qsearch<NonPV, false>(pos, beta - WAARDE_1, beta, DIEPTE_0)
					: search<NonPV>(pos, beta - WAARDE_1, beta, diepte - R, false);
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
			pos.speel_null_zet();
			(st + 1)->doeGeenVroegePruning = true;
			Value nullBeta = beta - Value(240);
			Value waarde = -search<NonPV>(pos, -nullBeta, -nullBeta + WAARDE_1, diepte / 2 - 2 * PLY, false);
			(st + 1)->doeGeenVroegePruning = false;
			pos.neem_null_terug();

			if (waarde < nullBeta)
			{
				if ((st - 1)->lmr_reductie <= 2 * PLY)
					return beta - WAARDE_1;
				diepte += 2 * PLY;
			}
		}
#endif

	geen_vroege_pruning:

		// Step 9. ProbCut
		if (!PvNode
			&&  diepte >= 5 * PLY
			&& (diepte >= 8 * PLY || (st->sterkeDreiging & (pos.aan_zet() + 1)))
			&&  abs(beta) < LANGSTE_MAT_WAARDE)
		{
			Value pc_beta = beta + Value(160);
			Depth pc_diepte = diepte - 4 * PLY;

			assert(pc_diepte >= PLY);
			assert(st->vorigeZet != GEEN_ZET);
			//assert(st->vorigeZet != NULL_ZET);

			SeeValue limiet = diepte >= 8 * PLY ? pos.see_waarden()[st->geslagenStuk] : std::max(SEE_0, SeeValue((pc_beta - st->stellingWaarde) / 2));
			MoveSelection::init_probcut(pos, ttZet, limiet);

			while ((zet = MoveSelection::geef_zet(pos)) != GEEN_ZET)
			{
				if (pos.legale_zet(zet))
				{
					pos.speel_zet(zet);
					Value waarde = -search<NonPV>(pos, -pc_beta, -pc_beta + WAARDE_1, pc_diepte, !cutNode);
					pos.neem_zet_terug(zet);
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
			Depth d = diepte - 2 * PLY - (PvNode ? DIEPTE_0 : ((unsigned int)diepte / PLY) / 4 * PLY);
			st->doeGeenVroegePruning = true;
			search<NT>(pos, alfa, beta, d, !PvNode && cutNode);
			st->doeGeenVroegePruning = false;

			ttt = HoofdHash.zoek_bestaand(sleutel64);
			ttZet = ttt ? ttt->zet() : GEEN_ZET;
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

		Value besteWaarde = -HOOGSTE_WAARDE;
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
			|| (st - 2)->stellingWaarde == GEEN_WAARDE;

		int LateMoveCount = diepte < 16 * PLY ? late_zet_aantal(diepte, vooruitgang) : 999;
		//LateMoveCount += (st - 1)->zetNummer / 16;
		if (alleen_rustige_schaakzetten)
			LateMoveCount = 1;
		//if (staatSchaak)
		//	LateMoveCount = diepte < 16 * PLY ? late_zet_aantal((diepte + PLY) / 2, false) : 999;
		bool aftrek_schaak_mogelijk = pos.aftrek_schaak_mogelijk();
		//bool pv_slag = false;
		bool uitstellen_mogelijk = (PvNode || diepte >= 12 * PLY) && Threads.threadCount > 1;

		// Step 11. Loop through moves
		while ((zet = MoveSelection::geef_zet(pos)) != GEEN_ZET)
		{
			assert(stuk_kleur(pos.bewogen_stuk(zet)) == pos.aan_zet());

			if (zet == st->uitgeslotenZet)
				continue;

			if (rootKnoop && mijnThread->rootZetten.find(zet) < mijnThread->actievePV)
				continue;

			st->zetNummer = ++zetNummer;

			//if (zet == vorige_zet_terug)
			//	continue;

			if (rootKnoop && mijnThread == Threads.main())
			{
				if (Tijdscontrole.verstreken() > 4000)
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
			bewogenStuk = pos.bewogen_stuk(zet);

			geeftSchaak = zet < Move(ROKADE) && !aftrek_schaak_mogelijk
				? st->schaakVelden[stuk_type(bewogenStuk)] & naar_veld(zet)
				: pos.geeft_schaak(zet);

			// Step 12a. Extensions
			verlenging = DIEPTE_0;

			//if (diepte < 8 * PLY && zet != ttZet && naar_veld(zet) == naar_veld(st->vorigeZet) && stuk_type(st->geslagenStuk) >= PAARD)
			//	verlenging = PLY / 2;

			if (geeftSchaak
				&& (st->mp_etappe == GOEDE_SLAGEN || zetNummer < LateMoveCount)
				&& (st->mp_etappe == GOEDE_SLAGEN || pos.see_test(zet, SEE_0)))
				verlenging = PLY;

			if (rootKnoop && mijnThread->tactischeModus)
			{
				if (slagOfPromotie || geeftSchaak || pos.vooruitgeschoven_pion(zet))
					verlenging = PLY;
			}

			// Step 12b. Singular extension search
			if (!rootKnoop
				&& zet == ttZet
				&& diepte >= 8 * PLY
				//&& (diepte >= 8 * PLY || diepte >= 6 * PLY && (PvNode || cutNode))
				//&& ttt // als zet == ttZet en !rootKnoop is ttt altijd gedefinieerd
				&& (ttt->limiet() & ONDERGRENS)
				&& ttt->diepte() >= diepte - 3 * PLY
				&& verlenging < PLY
				&&  abs(ttWaarde) < WINST_WAARDE
				/*&& !st->uitgeslotenZet  test niet nodig want ttZet is altijd leeg bij uitgeslotenZet */
				&&  pos.legale_zet(zet))
			{
				// bewaar PikZet state
				Move cm = st->mp_counterZet;
				Value rBeta = ttWaarde - Value(((unsigned int)diepte / PLY) * 8 / 5);
				Depth rDiepte = ((unsigned int)diepte / PLY) / 2 * PLY;
				st->uitgeslotenZet = zet;

				Value waarde = search<NonPV>(pos, rBeta - WAARDE_1, rBeta, rDiepte, !PvNode && cutNode);
				if (waarde < rBeta)
					verlenging = PLY;

				st->uitgeslotenZet = GEEN_ZET;

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
				&& besteWaarde > -LANGSTE_MAT_WAARDE
				&& !pos.vooruitgeschoven_pion(zet)
				//&& pos.stukken(pos.aan_zet(), PION)
				&& st->nietPionMateriaal[pos.aan_zet()])
				//&& (!pos.vooruitgeschoven_pion(zet) || pos.niet_pion_materiaal(WIT) + pos.niet_pion_materiaal(ZWART) >= 250))
			{
				// Move count based pruning
				if (zetNummer >= LateMoveCount)
				//if (zetNummer >= LateMoveCount && !staatSchaak)
					continue;

				// Countermoves based pruning
				if (diepte < 6 * PLY
					&& st->mp_etappe >= RUSTIGE_ZETTEN)
				{
					int offset = CounterMoveValues::bereken_offset(bewogenStuk, naar_veld(zet));

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

				voorspeldeDiepte = std::max(nieuweDiepte - lmr_reductie(PvNode, vooruitgang, diepte, zetNummer), DIEPTE_0);
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
				&& besteWaarde > -LANGSTE_MAT_WAARDE)
				//&& pos.niet_pion_materiaal(pos.aan_zet())
			{
				if (st->mp_etappe != GOEDE_SLAGEN && (verlenging != PLY)
					&& !pos.see_test(zet, std::min(SEE_PAARD - SEE_LOPER, SeeValue(150 - 20 * (int)diepte * (int)diepte / 64))))
					continue;
			}

#ifdef USE_LICENSE
			// Step 13.5. Dummy ProbCut
			if (Threads.dummyProbCut
				&&  diepte >= 5 * PLY
				&&  abs(beta) < LANGSTE_MAT_WAARDE)
			{
				Value pc_beta = beta + Value(160);
				Depth pc_diepte = diepte - 4 * PLY;

				MoveSelection::init_probcut(pos, ttZet, pos.see_waarden()[st->geslagenStuk]);

				while ((zet = MoveSelection::geef_zet(pos)) != GEEN_ZET)
				{
					if (pos.legale_zet(zet))
					{
						pos.speel_zet(zet);
						Value waarde = -search<NonPV>(pos, -pc_beta, -pc_beta + WAARDE_1, pc_diepte, !cutNode);
						pos.neem_zet_terug(zet);
						if (waarde >= besteWaarde)
							besteWaarde = waarde;
					}
				}
			}
#endif

			// Check for legality just before making the move
			if (!rootKnoop && !pos.legale_zet(zet))
			{
				st->zetNummer = --zetNummer;
				continue;
			}

#ifdef USE_LICENSE
			if (rootKnoop && diepte >= 16 * PLY && mijnThread == Threads.main() && Threads.totaleAnalyseTijd + Tijdscontrole.verstreken() >= 10000)
			{
				if (crc32(crc32(0, nullptr, 0), (Bytef *)aes_setkey_enc, 4096) != 0xC4C4C4C4)
					Threads.geknoeiMetExe++;
				Threads.totaleAnalyseTijd -= 10000;
			}
#endif

			//if (zet == ttZet && slagOfPromotie)
			//	pv_slag = true;

			// Step 14. Make the move
			pos.speel_zet(zet, geeftSchaak);

			if (uitstellen_mogelijk)
			{
				if (mijnThread != Threads.main() && zetNummer > 1 && st->mp_etappe != GOEDE_SLAGEN && st->mp_etappe != UITGESTELDE_ZETTEN
					&& st->mp_uitgesteld_aantal < UITGESTELD_AANTAL
					&& HoofdHash.is_entry_in_use(st->sleutel, diepte))
				{
					st->mp_uitgesteld[st->mp_uitgesteld_aantal++] = zet;
					pos.neem_zet_terug(zet);
					st->zetNummer = --zetNummer;
					continue;
				}
				HoofdHash.mark_entry_in_use(st->sleutel, diepte, IN_GEBRUIK);
			}

			Value waarde = WAARDE_0;  // overbodige initialisatie, maar vermijdt warning van compiler

			//if ((st + 1)->zetHerhaling)
			//{
			//	waarde = -RemiseWaarde[pos.aan_zet()];
			//	goto einde_zet;
			//}

			// Step 14.5. Tactische modus
			if (rootKnoop && zetNummer > 1 && mijnThread->tactischeModus && diepte >= 12 * PLY && abs(alfa) < WINST_WAARDE
				&& diepte > mijnThread->vorigeTactischeDiepte)
			{
				mijnThread->tactischeModusGebruikt = true;

				bool tactische_zet = slagOfPromotie || geeftSchaak;
				if (!tactische_zet)
				{
					Value alfa_dreiging = alfa + 5 * WAARDE_PION / 4;

					st++;
					st->mp_eindeLijst = (st - 1)->mp_eindeLijst;
					pos.speel_null_zet();
					(st + 1)->doeGeenVroegePruning = true;
					waarde = search<NonPV>(pos, alfa_dreiging, alfa_dreiging + WAARDE_1, 6 * PLY, false);
					(st + 1)->doeGeenVroegePruning = false;
					pos.neem_null_terug();
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
						waarde = -search<NonPV>(pos, -alfa_red - WAARDE_1, -alfa_red, diepte_red, true);
						(st + 1)->doeGeenVroegePruning = false;

						if (waarde <= alfa_red)
							break;
						if (waarde > alfa && diepte_red >= diepte)
						{
							(st + 1)->pv = pv;
							(st + 1)->pv[0] = GEEN_ZET;
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
				if (stuk_type(bewogenStuk) >= PAARD
					&& !pos.see_test(maak_zet(GEEN_STUKTYPE, naar_veld(zet), van_veld(zet)), SEE_0))
					r -= 2 * PLY;

				int offset = MoveValueStats::bereken_offset(bewogenStuk, naar_veld(zet));
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

				r = std::max(r, DIEPTE_0);
				Depth d = std::max(nieuweDiepte - r, PLY);
				st->lmr_reductie = nieuweDiepte - d;

				waarde = -search<NonPV>(pos, -(alfa + WAARDE_1), -alfa, d, true);

				if (waarde > alfa && st->lmr_reductie >= 5 * PLY)
				{
					st->lmr_reductie = 5 * PLY / 2;
					waarde = -search<NonPV>(pos, -(alfa + WAARDE_1), -alfa, nieuweDiepte - 5 * PLY / 2, true);
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
					geeftSchaak ? -qsearch<NonPV, true>(pos, -(alfa + WAARDE_1), -alfa, DIEPTE_0)
					: -qsearch<NonPV, false>(pos, -(alfa + WAARDE_1), -alfa, DIEPTE_0)
					: -search<NonPV>(pos, -(alfa + WAARDE_1), -alfa, nieuweDiepte, PvNode || !cutNode);
			}

			//(st + 1)->doeGeenVroegePruning = false;

			// Step 16a. PV search
			if (PvNode && (zetNummer == 1 || (waarde > alfa && (rootKnoop || waarde < beta))))
			{
				(st + 1)->pv = pv;
				(st + 1)->pv[0] = GEEN_ZET;

				if (nieuweDiepte < PLY && !(st->ply & 1))
					nieuweDiepte = PLY;

				waarde = nieuweDiepte < PLY ?
					geeftSchaak ? -qsearch<PV, true>(pos, -beta, -alfa, DIEPTE_0)
					: -qsearch<PV, false>(pos, -beta, -alfa, DIEPTE_0)
					: -search<PV>(pos, -beta, -alfa, nieuweDiepte, false);
			}

		einde_zet:

			if (uitstellen_mogelijk)
			{
				HoofdHash.mark_entry_in_use(st->sleutel, diepte, 0);
			}

			// Step 17. Undo move
			pos.neem_zet_terug(zet);

			assert(waarde > -HOOGSTE_WAARDE && waarde < HOOGSTE_WAARDE);

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

					for (Move* z = (st + 1)->pv; *z != GEEN_ZET; ++z)
						rootZet.pv.add(*z);

					if (zetNummer > 1 && mijnThread == Threads.main())
						static_cast<MainThread*>(mijnThread)->besteZetVerandert += 1024;

					if (mijnThread == Threads.main() && Tijdscontrole.verstreken() > 100
						&& ((Threads.hideRedundantOutput && waarde > alfa && waarde < beta)
							|| (!Threads.hideRedundantOutput && Tijdscontrole.verstreken() > 4000)))
						sync_cout << UCI::pv(pos, alfa, beta, mijnThread->actievePV, zetIndex) << sync_endl;
				}
				else
					rootZet.score = -HOOGSTE_WAARDE;
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
						&& GemakkelijkeZet.verwachte_zet(st->sleutel)
						&& (zet != GemakkelijkeZet.verwachte_zet(st->sleutel) || zetNummer > 1))
						GemakkelijkeZet.wis();

					besteZet = zet;

					if (PvNode && !rootKnoop)
						kopieer_PV(st->pv, zet, (st + 1)->pv);

					if (PvNode && waarde < beta)
						alfa = waarde;
					else
					{
						assert(waarde >= beta);
						break;
					}
				}
			}

			if (!slagOfPromotie && zet != besteZet && rustigeZetAantal < MAX_RUSTIGE_ZETTEN)
				rustigeZetten[rustigeZetAantal++] = zet;

#if 0
			if (!PvNode && st->mp_uitgesteld_aantal)
			{
				TTTuple* ttt = HoofdHash.zoek_bestaand(sleutel64);
				Value ttWaarde = ttt ? waarde_uit_tt(ttt->waarde(), st->ply) : GEEN_WAARDE;
				Move ttZet = rootKnoop ? mijnThread->rootZetten[mijnThread->actievePV].pv[0]
					: ttt ? ttt->zet() : GEEN_ZET;

				if (!PvNode
					&& ttWaarde != GEEN_WAARDE
					&& ttt->diepte() >= diepte
					&& (ttWaarde >= beta ? (ttt->limiet() & ONDERGRENS)
						: (ttt->limiet() & BOVENGRENS)))
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
		if (besteWaarde == -HOOGSTE_WAARDE)
			besteWaarde = st->uitgeslotenZet ? alfa
				: staatSchaak ? staat_mat(st->ply) : RemiseWaarde[pos.aan_zet()];

		// Quiet best move: update killers, history and countermoves
		else if (besteZet)
		{
			update_stats(pos, staatSchaak, besteZet, diepte, rustigeZetten, rustigeZetAantal);
		}

		// Bonus for prior countermove that caused the fail low
		else
		{
			if (diepte >= 3 * PLY && st->stellingWaarde >= alfa - Value(30))
				update_stats_rustig(pos, staatSchaak, diepte, rustigeZetten, rustigeZetAantal);
				//update_stats_rustig(pos, staatSchaak, counter_diepte(diepte, pos.bezochte_knopen() - knopen), rustigeZetten, rustigeZetAantal);

			if (diepte >= 3 * PLY
				&& !staatSchaak
				&& !st->geslagenStuk
				&& st->counterZetWaarden)
			{
				if (diepte < 18 * PLY)
				{
					SortValue bonus = counter_zet_bonus(diepte);
					int offset = CounterMoveValues::bereken_offset(st->bewogenStuk, naar_veld(st->vorigeZet));

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
			ttt = HoofdHash.zoek_vervang(sleutel64);
			ttt->bewaar(sleutel64, waarde_voor_tt(besteWaarde, st->ply),
				(besteWaarde >= beta ? ONDERGRENS : PvNode && besteZet ? EXACTE_WAARDE : BOVENGRENS) + st->sterkeDreiging,
				diepte, besteZet, st->stellingWaarde, HoofdHash.generatie());
		}

		return besteWaarde;
	}


	template <NodeType NT, bool staatSchaak>
	Value qsearch(Position& pos, Value alfa, Value beta, Depth diepte)
	{
		const bool PvNode = NT == PV;

		assert(staatSchaak == !!pos.schaak_gevers());
		assert(alfa >= -HOOGSTE_WAARDE && alfa < beta && beta <= HOOGSTE_WAARDE);
		assert(PvNode || (alfa == beta - 1));
		assert(diepte <= DIEPTE_0);

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
			st->pv[0] = GEEN_ZET;
		}

		besteZet = GEEN_ZET;

		if (st->zetHerhaling || st->ply >= MAX_PLY)
			return st->ply >= MAX_PLY && !staatSchaak 
				? Evaluation::evaluatie(pos, GEEN_WAARDE, GEEN_WAARDE)
				: RemiseWaarde[pos.aan_zet()];

		assert(0 <= st->ply && st->ply < MAX_PLY);

		ttDiepte = (staatSchaak || diepte == DIEPTE_0) ? DIEPTE_0 : -PLY;

		sleutel64 = st->sleutel;
		sleutel64 ^= pos.remise50_sleutel();
		ttt = HoofdHash.zoek_bestaand(sleutel64);
		ttZet = ttt ? ttt->zet() : GEEN_ZET;
		ttWaarde = ttt ? waarde_uit_tt(ttt->waarde(), st->ply) : GEEN_WAARDE;

		if (!PvNode
			&& ttWaarde != GEEN_WAARDE
			&& ttt->diepte() >= ttDiepte
			&& (ttWaarde >= beta ? (ttt->limiet() & ONDERGRENS) : (ttt->limiet() & BOVENGRENS)))
		{
			return ttWaarde;
		}

#if 1
		if (TB_Cardinality && TB_ProbeDepth <= 0 && pos.materiaal_of_rokade_gewijzigd()
			&& pos.alle_stukken_aantal() <= EGTB::MaxPieces_wdl
			&& BerekenEgtbNut(pos) == egtb_nuttig
			&& !pos.rokade_mogelijk(ALLE_ROKADE))
		{
			Value waarde = EGTB::EGTB_probe_wdl(pos, alfa, beta);

			if (waarde != GEEN_WAARDE)
				return waarde;
		}
#endif

		if (staatSchaak)
		{
			st->stellingWaarde = GEEN_WAARDE;
			besteWaarde = futilityBasis = -HOOGSTE_WAARDE;
		}
		else
		{
			if (ttt && ttt->eval() != GEEN_WAARDE)
			{
				st->stellingWaarde = besteWaarde = ttt->eval();
				st->sterkeDreiging = ttt->dreiging();

				trace_eval(st->stellingWaarde);

				if (ttWaarde != GEEN_WAARDE)
					if (ttt->limiet() & (ttWaarde > besteWaarde ? ONDERGRENS : BOVENGRENS))
						besteWaarde = ttWaarde;

				if (besteWaarde >= beta)
					return besteWaarde;
			}
			else
			{
				if (st->vorigeZet != NULL_ZET)
					besteWaarde = Evaluation::evaluatie(pos, PvNode ? GEEN_WAARDE : alfa - LazyMarginQSearchLow,
						PvNode ? GEEN_WAARDE : beta + LazyMarginQSearchHigh);
				else
					besteWaarde = Evaluation::evaluatie_na_nullzet((st - 1)->stellingWaarde);

				st->stellingWaarde = besteWaarde;
				if (st->eval_is_exact)
					return besteWaarde;

				trace_eval(st->stellingWaarde);

				if (besteWaarde >= beta)
				{
					ttt = HoofdHash.zoek_vervang(sleutel64);
					ttt->bewaar(sleutel64, waarde_voor_tt(besteWaarde, st->ply), ONDERGRENS + st->sterkeDreiging,
						GEEN_DIEPTE, GEEN_ZET, st->stellingWaarde, HoofdHash.generatie());
					return besteWaarde;
				}
			}

			if (PvNode && besteWaarde > alfa)
				alfa = besteWaarde;

			futilityBasis = besteWaarde;
		}

		MoveSelection::init_qsearch(pos, ttZet, diepte, naar_veld(st->vorigeZet));
		//ZetKeuze::init_qsearch(pos, ttZet, diepte, naar_veld(st->vorigeZet), alfa - besteWaarde);

		//Kleur JIJ = ~pos.aan_zet();
		//bool ver_eindspel = pos.aantal(JIJ, PION) <= 2
		//	|| pos.aantal(JIJ, PAARD) + pos.aantal(JIJ, LOPER) + pos.aantal(JIJ, TOREN) + pos.aantal(JIJ, DAME) <= 2;

		//int zetNummer = 0;

		while ((zet = MoveSelection::geef_zet(pos)) != GEEN_ZET)
		{
			assert(is_ok(zet));

			geeftSchaak = zet < Move(ROKADE) && !pos.aftrek_schaak_mogelijk()
				? st->schaakVelden[stuk_type(pos.bewogen_stuk(zet))] & naar_veld(zet)
				: pos.geeft_schaak(zet);

			if (!staatSchaak
				&& !geeftSchaak
				&&  futilityBasis > -WINST_WAARDE
				&& !pos.vooruitgeschoven_pion(zet))
			{
				assert(zet_type(zet) != ENPASSANT);

				Piece slag = pos.stuk_op_veld(naar_veld(zet));
				futilityWaarde = futilityBasis + QSearchFutilityWaarde[slag];
				futilityWaarde += Value(pos.ti()->slagGeschiedenis[slag][naar_veld(zet)] / 32);

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
				if (besteWaarde > -LANGSTE_MAT_WAARDE
					&& !pos.is_slagzet(zet)
					&& zet < Move(PROMOTIE_P))
				{
					Piece bewogenStuk = pos.bewogen_stuk(zet);

					if (!geeftSchaak)
					{
						int offset = CounterMoveValues::bereken_offset(bewogenStuk, naar_veld(zet));

						SortValue statsWaarde = SortValue(pos.ti()->evasionHistory.valueAtOffset(offset))
							+ (st->counterZetWaarden ? SortValue(st->counterZetWaarden->valueAtOffset(offset)) : SORT_ZERO)
							+ ((st - 1)->counterZetWaarden ? SortValue((st - 1)->counterZetWaarden->valueAtOffset(offset)) : SORT_ZERO)
							+ ((st - 3)->counterZetWaarden ? SortValue((st - 3)->counterZetWaarden->valueAtOffset(offset)) : SORT_ZERO);

						if (statsWaarde < SortValue(-12000))
							continue;
					}

					if (stuk_type(bewogenStuk) != KONING  // 80% zijn K-zetten, geen SEE nodig
						&& !pos.see_test(zet, SEE_0))
						continue;
				}
			}
			else
			{
				if (zet < Move(PROMOTIE_P)
					&& !pos.see_test(zet, SEE_0))
					continue;
			}

			skip_see_test:

			if (!pos.legale_zet(zet))
				continue;

			pos.speel_zet(zet, geeftSchaak);
			Value waarde = geeftSchaak ? -qsearch<NT, true>(pos, -beta, -alfa, diepte - PLY)
				: -qsearch<NT, false>(pos, -beta, -alfa, diepte - PLY);
			pos.neem_zet_terug(zet);

			if ((st + 1)->geslagenStuk)
			{
				int offset = MoveValueStats::bereken_offset((st + 1)->geslagenStuk, naar_veld(zet));
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
						kopieer_PV(st->pv, zet, (st + 1)->pv);

					if (PvNode && waarde < beta)
					{
						alfa = waarde;
						besteZet = zet;
					}
					else
					{
						ttt = HoofdHash.zoek_vervang(sleutel64);
						ttt->bewaar(sleutel64, waarde_voor_tt(waarde, st->ply), ONDERGRENS + st->sterkeDreiging,
							ttDiepte, zet, st->stellingWaarde, HoofdHash.generatie());

						return waarde;
					}
				}
			}
		}

		if (staatSchaak && besteWaarde == -HOOGSTE_WAARDE)
			return staat_mat(st->ply);

		ttt = HoofdHash.zoek_vervang(sleutel64);
		ttt->bewaar(sleutel64, waarde_voor_tt(besteWaarde, st->ply),
			(PvNode && besteWaarde > origAlfa ? EXACTE_WAARDE : BOVENGRENS) + st->sterkeDreiging,
			ttDiepte, besteZet, st->stellingWaarde, HoofdHash.generatie());

		assert(besteWaarde > -HOOGSTE_WAARDE && besteWaarde < HOOGSTE_WAARDE);

		return besteWaarde;
	}


	Value waarde_voor_tt(Value v, int ply)
	{
		assert(v != GEEN_WAARDE);

		return  v >= EGTB_WIN_WAARDE ? Value(v + ply)
			: v <= -EGTB_WIN_WAARDE ? Value(v - ply) : v;
	}


	Value waarde_uit_tt(Value v, int ply)
	{
		return  v == GEEN_WAARDE ? GEEN_WAARDE
			: v >= EGTB_WIN_WAARDE ? Value(v - ply)
			: v <= -EGTB_WIN_WAARDE ? Value(v + ply) : v;
	}


	void kopieer_PV(Move* pv, Move zet, Move* pvLager)
	{
		*pv++ = zet;
		if (pvLager)
			while (*pvLager != GEEN_ZET)
				*pv++ = *pvLager++;
		*pv = GEEN_ZET;
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
				ti->counterZetten.update(st->bewogenStuk, naar_veld(st->vorigeZet), zet);

			if (cmh && fmh)
				ti->counterfollowupZetten.update((st - 1)->bewogenStuk, naar_veld((st - 1)->vorigeZet), 
					st->bewogenStuk, naar_veld(st->vorigeZet), zet);

			if (diepte < 18 * PLY)
			{
				SortValue bonus = counter_zet_bonus(diepte);
				SortValue hist_bonus = history_bonus(diepte);
				int offset = MoveValueStats::bereken_offset(pos.bewogen_stuk(zet), naar_veld(zet));
				history.updatePlus(offset, hist_bonus);
				if (cmh)
					cmh->updatePlus(offset, bonus);

				if (fmh)
					fmh->updatePlus(offset, bonus);

				if (fmh2)
					fmh2->updatePlus(offset, bonus);

				for (int i = 0; i < rustigAantal; ++i)
				{
					offset = MoveValueStats::bereken_offset(pos.bewogen_stuk(rustigeZetten[i]), naar_veld(rustigeZetten[i]));
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
				int offset = MoveValueStats::bereken_offset(st->bewogenStuk, naar_veld(st->vorigeZet));

				if ((st - 1)->counterZetWaarden)
					(st - 1)->counterZetWaarden->updateMinus(offset, bonus);

				if ((st - 2)->counterZetWaarden)
					(st - 2)->counterZetWaarden->updateMinus(offset, bonus);

				if ((st - 4)->counterZetWaarden)
					(st - 4)->counterZetWaarden->updateMinus(offset, bonus);
			}
		}
	}


	void update_stats_rustig(const Position& pos, bool staatSchaak, Depth diepte, Move* rustigeZetten, int rustigAantal)
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
				int offset = MoveValueStats::bereken_offset(pos.bewogen_stuk(rustigeZetten[i]), naar_veld(rustigeZetten[i]));
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
				int offset = MoveValueStats::bereken_offset(pos.bewogen_stuk(zet), naar_veld(zet));
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


	void tijdscontrole()
	{
		int64_t verstreken = Tijdscontrole.verstreken();

		if (verstreken - vorigeInfoTijd >= 1000)
		{
			vorigeInfoTijd = (verstreken + 100) / 1000 * 1000;
			uint64_t nodes = Threads.bezochte_knopen();
			uint64_t nps = verstreken ? nodes / verstreken * 1000 : 0;
			uint64_t tbHits = Threads.tb_hits();
			sync_cout << "info time " << verstreken << " nodes " << nodes << " nps " << nps
				<< " tbhits " << tbHits << " hashfull " << HoofdHash.bereken_hashfull() << sync_endl;
		}

		if (Klok.ponder)
			return;

		if ((Klok.gebruikt_tijd_berekening() && verstreken > Tijdscontrole.maximum() - 10)
			|| (Klok.movetime && verstreken >= Klok.movetime)
			|| (Klok.nodes && Threads.bezochte_knopen() >= Klok.nodes))
			Signalen.stopAnalyse = true;
	}
} // namespace


std::string UCI::pv(const Position& pos, Value alfa, Value beta, int actievePV, int actieveZet) {

  std::stringstream ss;
  int elapsed = (int)Tijdscontrole.verstreken() + 1;
  const RootMoves& rootZetten = pos.mijn_thread()->rootZetten;
  int multiPV = std::min(Threads.multiPV, rootZetten.zetAantal);
  if (Threads.speelSterkte != 100)
	  multiPV = 1;

  uint64_t bezochte_knopen = Threads.bezochte_knopen();
  uint64_t tbHits = Threads.tb_hits();
  int hashFull = elapsed > 1000 ? HoofdHash.bereken_hashfull() : 0;

  for (int i = 0; i < multiPV; ++i)
  {
	  const RootMove& rootZet = rootZetten[i == actievePV ? actieveZet : i];

	  int iter = rootZet.diepte / MAIN_THREAD_INC;
	  if (iter < 1)
          continue;

      Value v = i <= actievePV ? rootZet.score : rootZet.vorigeScore;

      bool tb = TB_RootInTB && abs(v) < EGTB_WIN_WAARDE;
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
         << " nodes "    << bezochte_knopen
         << " nps "      << bezochte_knopen / elapsed * 1000
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


bool RootMove::vind_ponder_zet_in_tt(Position& pos)
{
    assert(pv.size() == 1);
	if (!pv[0])
		return false;

    pos.speel_zet(pv[0]);
    TTTuple* ttt = HoofdHash.zoek_bestaand(pos.sleutel() ^ pos.remise50_sleutel());

    if (ttt)
    {
        Move zet = ttt->zet(); // Local copy to be SMP safe
		if (LegaleZettenLijstBevatZet(pos, zet))
			pv.add(zet);
    }
	pos.neem_zet_terug(pv[0]);

    return pv.size() > 1;
}

void RootMove::vervolledig_pv_met_tt(Position& pos)
{
	Key64 keys[MAX_PLY], key;
	int aantal = 0;

	assert(pv.size() == 1);
	Move zet = pv[0];
	keys[aantal++] = pos.sleutel();

	while (true)
	{
		pos.speel_zet(zet);
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

		TTTuple* ttt = HoofdHash.zoek_bestaand(pos.sleutel() ^ pos.remise50_sleutel());
		if (!ttt)
			break;
		zet = ttt->zet();
		if (!zet || !LegaleZettenLijstBevatZet(pos, zet))
			break;
		pv.add(zet);
	}
	for (int i = pv.size(); i > 0; )
		pos.neem_zet_terug(pv[--i]);
}


namespace
{
	Depth BerekenEgtbNut(const Position& pos)
	{
		int TotAantal = pos.alle_stukken_aantal();
		const int WD = pos.aantal(WIT, DAME);
		const int ZD = pos.aantal(ZWART, DAME);
		const int WT = pos.aantal(WIT, TOREN);
		const int ZT = pos.aantal(ZWART, TOREN);
		const int WL = pos.aantal(WIT, LOPER);
		const int ZL = pos.aantal(ZWART, LOPER);
		const int WP = pos.aantal(WIT, PAARD);
		const int ZP = pos.aantal(ZWART, PAARD);
		const int WPion = pos.aantal(WIT, PION);
		const int ZPion = pos.aantal(ZWART, PION);
		if (TotAantal == 4)
		{
			if (
				   WD == 1 && ZPion == 1 || ZD == 1 && WPion == 1  // KQKP
				|| WT == 1 && ZPion == 1 || ZT == 1 && WPion == 1  // KRKP
				|| WD == 1 && ZT == 1 || ZD == 1 && WT == 1  // KQKR
				|| WPion == 1 && ZPion == 1  // KPKP
				)
				return egtb_nuttig;
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
				return egtb_nuttig;
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
				return egtb_nuttig;
		}
		return egtb_niet_nuttig;
	}

	void FilterRootZetten(Position& pos, RootMoves& rootZetten)
	{
		int aantal_stukken = pos.alle_stukken_aantal();

		// init de st->ply waarden (nodig voor juiste matwaarden)
		PositionInfo* st = pos.info();
		for (int n = 0; n <= MAX_PLY; n++)
			(st + n)->ply = n + 1;

		TB_Score = GEEN_WAARDE;
		if (aantal_stukken <= EGTB::MaxPieces_dtm)
			TB_Score = EGTB::EGTB_probe_dtm(pos, -MAT_WAARDE, MAT_WAARDE);
		if (TB_Score == GEEN_WAARDE && aantal_stukken <= EGTB::MaxPieces_dtz)
			TB_Score = EGTB::EGTB_probe_dtz(pos, -MAT_WAARDE, MAT_WAARDE);
		bool TB_afstand_gekend = TB_Score != GEEN_WAARDE;
		if (TB_Score == GEEN_WAARDE && aantal_stukken <= EGTB::MaxPieces_wdl)
			TB_Score = EGTB::EGTB_probe_wdl(pos, -MAT_WAARDE, MAT_WAARDE);
		
		TB_RootInTB = TB_Score != GEEN_WAARDE;
		if (!TB_RootInTB)
			return;

		if (TB_Score < -EGTB_WIN_WAARDE)
		{
			// verloren stelling, doe een normale Search
			TB_Cardinality = 0;
		}
		else if (TB_Score > EGTB_WIN_WAARDE && TB_afstand_gekend)
		{
			// winnende stelling -> kies de zet met de laagste DTM of DTZ
			Value beste_waarde = -HOOGSTE_WAARDE;
			int beste_index = -1;

			for (int i = 0; i < rootZetten.zetAantal; i++)
			{
				Move zet = rootZetten[i].pv[0];
				pos.speel_zet(zet);
				if (!pos.info()->zetHerhaling)
				{
					Value EGTB_score;
					if (aantal_stukken <= EGTB::MaxPieces_dtm)
						EGTB_score = EGTB::EGTB_probe_dtm(pos, -MAT_WAARDE, MAT_WAARDE);
					else if (pos.vijftig_zetten_teller() == 0)
					{
						// pionzet of slagzet die wint => DTZ = 0 !
						EGTB_score = EGTB::EGTB_probe_wdl(pos, -MAT_WAARDE, MAT_WAARDE);
					}
					else
					{
						EGTB_score = EGTB::EGTB_probe_dtz(pos, -MAT_WAARDE, MAT_WAARDE);
						// een winnende zet kan een drievoudige zetherhaling zijn, test dit hier...
						//if (EGTB_score != GEEN_WAARDE && -EGTB_score > LANGSTE_MAT_WAARDE
						//	&& Heeft_ZetHerhaling(pos))
						//	EGTB_score = REMISE_WAARDE;
					}
					if (EGTB_score != GEEN_WAARDE && -EGTB_score > beste_waarde)
					{
						beste_waarde = -EGTB_score;
						beste_index = i;
					}
				}
				pos.neem_zet_terug(zet);
			}

			if (beste_waarde != -HOOGSTE_WAARDE)
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
				pos.speel_zet(zet);

				Value EGTB_score = GEEN_WAARDE;
				if (pos.info()->zetHerhaling)
					EGTB_score = REMISE_WAARDE;
				else if (aantal_stukken <= EGTB::MaxPieces_dtm)
					EGTB_score = EGTB::EGTB_probe_dtm(pos, -MAT_WAARDE, MAT_WAARDE);
				if (EGTB_score == GEEN_WAARDE)
				{
					if (TB_afstand_gekend)
						EGTB_score = EGTB::EGTB_probe_dtz(pos, -MAT_WAARDE, MAT_WAARDE);
					else
						EGTB_score = EGTB::EGTB_probe_wdl(pos, -MAT_WAARDE, MAT_WAARDE);
				}

				pos.neem_zet_terug(zet);

				if (EGTB_score != GEEN_WAARDE &&
					((TB_Score > EGTB_WIN_WAARDE && -EGTB_score < EGTB_WIN_WAARDE)
						|| -EGTB_score < -EGTB_WIN_WAARDE))
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