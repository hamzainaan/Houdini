/*
*/

#include <iostream>

#include "houdini.h"
#include "move_selection.h"
#include "search_smp.h"
#include "uci.h"

namespace
{
	inline void insertion_sort(MoveEx* begin, MoveEx* end)
	{
		MoveEx tmp, *p, *q;

		for (p = begin + 1; p < end; ++p)
		{
			tmp = *p;
			for (q = p; q != begin && *(q - 1) < tmp; --q)
				*q = *(q - 1);
			*q = tmp;
		}
	}

	inline MoveEx* partitie(MoveEx* begin, MoveEx* end, SortValue v)
	{
		MoveEx tmp;

		while (true)
		{
			while (true)
			{

				if (begin == end)
					return begin;
				else if (begin->waarde > v)
					begin++;
				else
					break;
			}
			end--;
			while (true)
			{
				if (begin == end)
					return begin;
				else if (!(end->waarde > v))
					end--;
				else
					break;
			}
			tmp = *begin;
			*begin = *end;
			*end = tmp;
			begin++;
		}
	}

	void partial_insertion_sort(MoveEx* begin, MoveEx* end, SortValue v)
	{
		MoveEx *gesorteerdTot = begin + 1;
		for (MoveEx *p = begin + 1; p < end; ++p)
		{
			if (p->waarde >= v)
			{
				MoveEx tmp = *p, *q;
				*p = *gesorteerdTot;
				for (q = gesorteerdTot; q != begin && *(q - 1) < tmp; --q)
					*q = *(q - 1);
				*q = tmp;
				++gesorteerdTot;
			}
		}
	}

	void quick_sort(MoveEx* left, MoveEx* right) 
	{
		MoveEx* i = left;
		MoveEx* j = right;
		SortValue pivot = left->waarde;

		while (true)
		{
			while (i->waarde > pivot)
				i++;
			while (j->waarde < pivot)
				j--;
			if (i >= j)
				break;

			MoveEx tmp = *i;
			*i = *j;
			*j = tmp;

			i++;
			j--;
			if (i >= j)
				break;
		};

		if (i > left + 9)
			quick_sort(left, i - 1);
		if (right > i + 9)
			quick_sort(i + 1, right);
	}


	inline Move vind_beste_zet(MoveEx* begin, MoveEx* end)
	{
		MoveEx* best = begin;
		for (MoveEx* z = begin + 1; z < end; z++)
			if (z->waarde > best->waarde)
				best = z;
		Move zet = best->zet;
		*best = *begin;
		return zet;
	}

	const int PieceOrder[PIECE_N] =
	{
		0, 6, 1, 2, 3, 4, 5, 0,
		0, 6, 1, 2, 3, 4, 5, 0
	};

	const int CaptureSortValues[PIECE_N] =
	{
		0, 0, 198, 817, 836, 1270, 2521, 0,
		0, 0, 198, 817, 836, 1270, 2521, 0
	};

	unsigned short crc16(const unsigned char* data_p, unsigned char length)
	{
		unsigned char x;
		unsigned short crc = 0xFFFF;

		while (length--) {
			x = crc >> 8 ^ *data_p++;
			x ^= x >> 4;
			crc = (crc << 8) ^ ((unsigned short)(x << 12)) ^ ((unsigned short)(x << 5)) ^ ((unsigned short)x);
		}
		return crc;
	}

	int hash_bitboard(const Bitboard bb)
	{
		unsigned short crc = crc16((const unsigned char *)(&bb), 8);
		return int(crc);
	}
} // namespace


namespace MoveSelection
{
	void init_search(const Position& pos, Move ttZet, Depth diepte, bool alleen_rustige_schaakzetten)
	{
		assert(diepte >= PLY);
		PositionInfo* st = pos.info();
		st->mp_diepte = diepte;
		st->mp_alleen_rustige_schaakzetten = alleen_rustige_schaakzetten;
		st->mp_ttZet = ttZet && pos.geldige_zet(ttZet) ? ttZet : NO_MOVE;

		if (pos.schaak_gevers())
			st->mp_etappe = st->mp_ttZet ? EVADE_CHECK : EVASIONS_GEN;
		else
		{
			st->mp_etappe = st->mp_ttZet ? NORMAL_SEARCH : GOOD_CAPTURES_GEN;
			if (st->counterZetWaarden)
			{
				st->mp_counterZet = Move(pos.ti()->counterZetten.get(st->bewogenStuk, naar_veld(st->vorigeZet)));
				if (!st->mp_ttZet && (st - 1)->counterZetWaarden
					&& (!st->mp_counterZet || !pos.geldige_zet(st->mp_counterZet) || pos.slag_of_promotie(st->mp_counterZet)))
				{
					st->mp_counterZet = pos.ti()->counterfollowupZetten.get((st - 1)->bewogenStuk, naar_veld((st - 1)->vorigeZet),
						st->bewogenStuk, naar_veld(st->vorigeZet));
				}
			}
			else
				st->mp_counterZet = NO_MOVE;
		}
	}

	void init_qsearch(const Position& pos, Move ttZet, Depth diepte, Square veld)
	{
		assert(diepte <= DEPTH_0);
		PositionInfo* st = pos.info();

		if (pos.schaak_gevers())
			st->mp_etappe = EVADE_CHECK;

		else if (diepte == DEPTH_0)
			st->mp_etappe = QSEARCH_WITH_CHECKS;

		//else if (diepte == -PLY && delta < Waarde(50))
		//	st->mp_etappe = QSEARCH_MET_DAMESCHAAK;

		else if (diepte >= -4 * PLY)
			st->mp_etappe = QSEARCH_WITHOUT_CHECKS;

		else
		{
			st->mp_etappe = RECAPTURE_GEN;
			st->mp_slagVeld = veld;
			return;
		}

		st->mp_ttZet = ttZet && pos.geldige_zet(ttZet) ? ttZet : NO_MOVE;
		if (!st->mp_ttZet)
			++st->mp_etappe;
	}

	void init_probcut(const Position& pos, Move ttZet, SeeValue limiet)
	{
		assert(!pos.schaak_gevers());
		PositionInfo* st = pos.info();
		st->mp_threshold = limiet + SeeValue(1);

		st->mp_ttZet = ttZet
			&& pos.geldige_zet(ttZet)
			&& pos.slag_of_promotie(ttZet)
			&& pos.see_test(ttZet, st->mp_threshold) ? ttZet : NO_MOVE;

		st->mp_etappe = st->mp_ttZet ? PROBCUT : PROBCUT_GEN;
	}

	template<>
	void score<GEN_CAPTURES_OR_PROMOTIONS>(const Position& pos)
	{
		PositionInfo* st = pos.info();
		for (MoveEx *z = st->mp_huidigeZet; z < st->mp_eindeLijst; z++)
			z->waarde = SortValue(CaptureSortValues[pos.stuk_op_veld(naar_veld(z->zet))]
				- 200 * relatieve_rij(pos.aan_zet(), naar_veld(z->zet)));
	}

	template<>
	void score<GEN_QUIET_MOVES>(const Position& pos)
	{
		const MoveValueStats& history = pos.ti()->history;

		PositionInfo* st = pos.info();
		const CounterMoveValues* cm = st->counterZetWaarden ? st->counterZetWaarden : &pos.ni()->counterZetStatistieken[NO_PIECE][SQ_A1];
		const CounterMoveValues* fm = (st - 1)->counterZetWaarden ? (st - 1)->counterZetWaarden : &pos.ni()->counterZetStatistieken[NO_PIECE][SQ_A1];
		const CounterMoveValues* f2 = (st - 3)->counterZetWaarden ? (st - 3)->counterZetWaarden : &pos.ni()->counterZetStatistieken[NO_PIECE][SQ_A1];

		Square dreiging = st->mp_diepte < 6 * PLY ? pos.bereken_dreiging() : SQ_NONE;

		for (MoveEx *z = st->mp_huidigeZet; z < st->mp_eindeLijst; z++)
		{
			int offset = MoveValueStats::bereken_offset(pos.bewogen_stuk(z->zet), naar_veld(z->zet));
			z->waarde = SortValue(history.valueAtOffset(offset))
				+ SortValue(cm->valueAtOffset(offset))
				+ SortValue(fm->valueAtOffset(offset))
				+ SortValue(f2->valueAtOffset(offset));
			z->waarde += SortValue(8 * pos.ti()->maxWinstTabel.get(pos.bewogen_stuk(z->zet), z->zet));

			if (van_veld(z->zet) == dreiging)
				z->waarde += SortValue(9000 - 1000 * (st->mp_diepte / PLY));
		}
	}

	template<>
	void score<GEN_EVASIONS>(const Position& pos)
	{
		PositionInfo* st = pos.info();
		const MoveValueStats& history = pos.ti()->evasionHistory;

		for (MoveEx *z = st->mp_huidigeZet; z < st->mp_eindeLijst; z++)
		{
			if (pos.is_slagzet(z->zet))
				z->waarde = SortValue(CaptureSortValues[pos.stuk_op_veld(naar_veld(z->zet))]
					- PieceOrder[pos.bewogen_stuk(z->zet)]) + SORT_MAX;
			//else if (z->zet == st->mp_counterZet)
			//	z->waarde = SorteerWaarde(99999);
			else
			{
				int offset = MoveValueStats::bereken_offset(pos.bewogen_stuk(z->zet), naar_veld(z->zet));
				z->waarde = SortValue(history.valueAtOffset(offset));
			}
		}
	}


	Move geef_zet(const Position& pos)
	{
		PositionInfo* st = pos.info();

		switch (st->mp_etappe)
		{
		case NORMAL_SEARCH: case EVADE_CHECK:
		case QSEARCH_WITH_CHECKS: case QSEARCH_WITHOUT_CHECKS: 
		//case QSEARCH_MET_DAMESCHAAK:
		case PROBCUT:
			st->mp_eindeLijst = (st - 1)->mp_eindeLijst;
			++st->mp_etappe;
			return st->mp_ttZet;

		case GOOD_CAPTURES_GEN:
			st->mp_huidigeZet = (st - 1)->mp_eindeLijst;
			st->mp_eindeSlechteSlag = st->mp_huidigeZet;
			st->mp_uitgesteld_aantal = 0;
			st->mp_eindeLijst = genereerZetten<GEN_CAPTURES_OR_PROMOTIONS>(pos, st->mp_huidigeZet);
			score<GEN_CAPTURES_OR_PROMOTIONS>(pos);
			st->mp_etappe = GOOD_CAPTURES;

		case GOOD_CAPTURES:
			while (st->mp_huidigeZet < st->mp_eindeLijst)
			{
				Move zet = vind_beste_zet(st->mp_huidigeZet++, st->mp_eindeLijst);
				if (zet != st->mp_ttZet)
				{
					if (pos.see_test(zet, SEE_0))
						return zet;

					*st->mp_eindeSlechteSlag++ = zet;
				}
			}

			// first killer
			st->mp_etappe = KILLERS;
			{
				Move zet = st->killers[0];
				if (zet && zet != st->mp_ttZet && pos.geldige_zet(zet) && !pos.slag_of_promotie(zet))
					return zet;
			}

		case KILLERS:
			// second killer
			st->mp_etappe = KILLERS1;
			{
				Move zet = st->killers[1];
				if (zet && zet != st->mp_ttZet && pos.geldige_zet(zet) && !pos.slag_of_promotie(zet))
					return zet;
			}

		case KILLERS1:
			// counter move
			st->mp_etappe = BxN_CAPTURES_GEN;
			{
				Move zet = st->mp_counterZet;
				if (zet && zet != st->mp_ttZet && zet != st->killers[0]
					&& zet != st->killers[1]
					&& pos.geldige_zet(zet) && !pos.slag_of_promotie(zet))
					return zet;
			}

		case BxN_CAPTURES_GEN:
			st->mp_huidigeZet = (st - 1)->mp_eindeLijst;
			st->mp_etappe = BxN_CAPTURES;

		case BxN_CAPTURES:
			while (st->mp_huidigeZet < st->mp_eindeSlechteSlag)
			{
				Move zet = *st->mp_huidigeZet++;
				if (stuk_type(pos.stuk_op_veld(naar_veld(zet))) == KNIGHT && stuk_type(pos.stuk_op_veld(van_veld(zet))) == BISHOP)
				{
					*(st->mp_huidigeZet - 1) = NO_MOVE;
					return zet;
				}
			}

		//case RUSTIGE_ZETTEN_GEN:
			st->mp_huidigeZet = st->mp_eindeSlechteSlag;
			if (st->mp_alleen_rustige_schaakzetten && st->zetNummer >= 1)
			{
				MoveEx* z = st->mp_huidigeZet;
				z = genereerZetten<GEN_QUIET_CHECKS>(pos, z);
				z = genereerZetten<GEN_PAWN_ADVANCES>(pos, z);
				st->mp_eindeLijst = z;
				score<GEN_QUIET_MOVES>(pos);
				insertion_sort(st->mp_huidigeZet, st->mp_eindeLijst);
			}
			else
			{
				st->mp_eindeLijst = genereerZetten<GEN_QUIET_MOVES>(pos, st->mp_huidigeZet);
				score<GEN_QUIET_MOVES>(pos);

				MoveEx* sorteer_tot = st->mp_eindeLijst;
				// op lage diepte: sorteer enkel maar de positieve zetten; dus eerst een partitie maken
				if (st->mp_diepte < 6 * PLY)
					sorteer_tot = partitie(st->mp_huidigeZet, st->mp_eindeLijst, SortValue(6000 - 6000 * (st->mp_diepte / PLY)));
				insertion_sort(st->mp_huidigeZet, sorteer_tot);
				//if (st->mp_diepte < 8 * PLY)
				//	partial_insertion_sort(st->mp_huidigeZet, st->mp_eindeLijst, SorteerWaarde(2000 - 3000 * (st->mp_diepte / PLY)));
				//else
				//	insertion_sort(st->mp_huidigeZet, sorteer_tot);
				//partial_insertion_sort(st->mp_huidigeZet, st->mp_eindeLijst, SorteerWaarde(4000 - 4000 * (st->mp_diepte / PLY)));
			}
			st->mp_etappe = QUIET_MOVES;

		case QUIET_MOVES:
			while (st->mp_huidigeZet < st->mp_eindeLijst)
			{
				Move zet = *st->mp_huidigeZet++;
				if (zet != st->mp_ttZet
					&& zet != st->killers[0]
					&& zet != st->killers[1]
					&& zet != st->mp_counterZet)
				{
					return zet;
				}
			}

			st->mp_huidigeZet = (st - 1)->mp_eindeLijst;
			st->mp_eindeLijst = st->mp_eindeSlechteSlag;
			st->mp_etappe = BAD_CAPTURES;

		case BAD_CAPTURES:
			while (st->mp_huidigeZet < st->mp_eindeLijst)
			{
				Move zet = *st->mp_huidigeZet++;
				if (zet)
					return zet;
			}
			if (!st->mp_uitgesteld_aantal)
				return NO_MOVE;

			st->mp_etappe = POSTPONED_MOVES;
			st->mp_uitgesteld_huidig = 0;

		case POSTPONED_MOVES:
			if (st->mp_uitgesteld_huidig != st->mp_uitgesteld_aantal)
				return Move(st->mp_uitgesteld[st->mp_uitgesteld_huidig++]);
			return NO_MOVE;

		case EVASIONS_GEN:
			st->mp_huidigeZet = (st - 1)->mp_eindeLijst;
			st->mp_eindeLijst = genereerZetten<GEN_EVASIONS>(pos, st->mp_huidigeZet);
			score<GEN_EVASIONS>(pos);
			st->mp_etappe = EVASIONS_LOOP;

		case EVASIONS_LOOP:
			while (st->mp_huidigeZet < st->mp_eindeLijst)
			{
				Move zet = vind_beste_zet(st->mp_huidigeZet++, st->mp_eindeLijst);
				if (zet != st->mp_ttZet)
					return zet;
			}
			return NO_MOVE;

		case QSEARCH_1: case QSEARCH_2: 
		//case QSEARCH_3:
			st->mp_huidigeZet = (st - 1)->mp_eindeLijst;
			st->mp_eindeLijst = genereerZetten<GEN_CAPTURES_OR_PROMOTIONS>(pos, st->mp_huidigeZet);
			score<GEN_CAPTURES_OR_PROMOTIONS>(pos);
			++st->mp_etappe;

		case QSEARCH_1_CAPTURES: case QSEARCH_2_CAPTURES: 
		//case QSEARCH_3_SLAGZETTEN:
			while (st->mp_huidigeZet < st->mp_eindeLijst)
			{
				Move zet = vind_beste_zet(st->mp_huidigeZet++, st->mp_eindeLijst);
				if (zet != st->mp_ttZet)
					return zet;
			}

			if (st->mp_etappe == QSEARCH_2_CAPTURES)
				return NO_MOVE;

			st->mp_huidigeZet = (st - 1)->mp_eindeLijst;
			//if (st->mp_etappe == QSEARCH_3_SLAGZETTEN)
			//	st->mp_eindeLijst = genereerZetten<GEN_DAME_SCHAAK>(pos, st->mp_huidigeZet);
			//else
				st->mp_eindeLijst = genereerZetten<GEN_QUIET_CHECKS>(pos, st->mp_huidigeZet);
			//score<GEN_RUSTIGE_ZETTEN>(pos);
			++st->mp_etappe;

		case QSEARCH_CHECKS: 
		//case QSEARCH_DAMESCHAAKZETTEN:
			while (st->mp_huidigeZet < st->mp_eindeLijst)
			{
				Move zet = *st->mp_huidigeZet++;
				//Zet zet = vind_beste_zet(st->mp_huidigeZet++, st->mp_eindeLijst);
				if (zet != st->mp_ttZet)
					return zet;
			}
			return NO_MOVE;

		case PROBCUT_GEN:
			st->mp_huidigeZet = (st - 1)->mp_eindeLijst;
			st->mp_eindeLijst = genereerZetten<GEN_CAPTURES_OR_PROMOTIONS>(pos, st->mp_huidigeZet);
			score<GEN_CAPTURES_OR_PROMOTIONS>(pos);
			st->mp_etappe = PROBCUT_CAPTURES;

		case PROBCUT_CAPTURES:
			while (st->mp_huidigeZet < st->mp_eindeLijst)
			{
				Move zet = vind_beste_zet(st->mp_huidigeZet++, st->mp_eindeLijst);
				if (zet != st->mp_ttZet && pos.see_test(zet, st->mp_threshold))
					return zet;
			}
			return NO_MOVE;

		case RECAPTURE_GEN:
			st->mp_huidigeZet = (st - 1)->mp_eindeLijst;
			st->mp_eindeLijst = genereerSlagzettenOpVeld(pos, st->mp_huidigeZet, st->mp_slagVeld);
			score<GEN_CAPTURES_OR_PROMOTIONS>(pos);
			st->mp_etappe = RECAPTURE_MOVES;

		case RECAPTURE_MOVES:
			while (st->mp_huidigeZet < st->mp_eindeLijst)
			{
				Move zet = vind_beste_zet(st->mp_huidigeZet++, st->mp_eindeLijst);
				if (naar_veld(zet) == st->mp_slagVeld)
					return zet;
			}
			return NO_MOVE;

		default:
			assert(false);
			return NO_MOVE;
		}
	}
}


int SpecialKillerStats::index_mijn_stukken(const Position &pos, Color kleur)
{
	return hash_bitboard(pos.stukken(kleur));
}

int SpecialKillerStats::index_jouw_stukken(const Position &pos, Color kleur, Square to)
{
	return hash_bitboard(pos.stukken(kleur, stuk_type(pos.stuk_op_veld(to))));
}

