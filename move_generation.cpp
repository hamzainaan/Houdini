/*
*/

#include "houdini.h"
#include "move_generation.h"
#include "position.h"
#include "util_tools.h"

namespace 
{
	template<CastlingRight rokade, bool OnlyCheckMoves, bool Chess960>
	MoveEx* schrijf_rokade(const Position& pos, MoveEx* zetten)
	{
		const Color Us = rokade <= WHITE_LONG ? WHITE : BLACK;
		const Color Them = (Us == WHITE ? BLACK : WHITE);
		const bool korte_rokade = (rokade == WHITE_SHORT || rokade == BLACK_SHORT);

		if (pos.rokade_verhinderd(rokade) || !pos.rokade_mogelijk(rokade))
			return zetten;

		const Square vanK = Chess960 ? pos.koning(Us) : relatief_veld(Us, SQ_E1);
		const Square naarK = relatief_veld(Us, korte_rokade ? SQ_G1 : SQ_C1);
		const Square richting = naarK > vanK ? LEFT : RIGHT;

		assert(!pos.schaak_gevers());

		if (Chess960)
		{
			for (Square veld = naarK; veld != vanK; veld += richting)
				if (pos.aanvallers_naar(veld) & pos.stukken(Them))
					return zetten;

			Square vanT = pos.rokade_toren_veld(naarK);
			if (aanval_bb_toren(naarK, pos.stukken() ^ vanT) & pos.stukken(Them, ROOK, QUEEN))
				return zetten;
		}
		else
		{
			if (pos.aanvallers_naar(naarK) & pos.stukken(Them))
				return zetten;
			if (pos.aanvallers_naar(naarK + richting) & pos.stukken(Them))
				return zetten;
		}

		Move zet = maak_zet(CASTLING, vanK, naarK);

		if (OnlyCheckMoves && !pos.geeft_schaak(zet))
			return zetten;

		*zetten++ = zet;
		return zetten;
	}


	template<Color Us, MoveGeneration Type, Square Delta>
	MoveEx* schrijf_promoties(const Position& pos, MoveEx* zetten, Square naar)
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);

		if (Type == GEN_CAPTURES_OR_PROMOTIONS || Type == GEN_EVASIONS || Type == GEN_ALL_MOVES)
			*zetten++ = maak_zet(PROMOTION_Q, naar - Delta, naar);

		if (Type == GEN_QUIET_MOVES || Type == GEN_EVASIONS || Type == GEN_ALL_MOVES)
		{
			*zetten++ = maak_zet(PROMOTION_R, naar - Delta, naar);
			*zetten++ = maak_zet(PROMOTION_B, naar - Delta, naar);
			*zetten++ = maak_zet(PROMOTION_N, naar - Delta, naar);
		}

		if (Type == GEN_QUIET_CHECKS && (LegeAanval[KNIGHT][naar] & pos.koning(Them)))
			*zetten++ = maak_zet(PROMOTION_N, naar - Delta, naar);

		return zetten;
	}


	template<Color Us, MoveGeneration Type>
	MoveEx* zetten_voor_pion(const Position& pos, MoveEx* zetten, Bitboard target)
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);
		const Bitboard AchtsteRij = (Us == WHITE ? Rank8BB : Rank1BB);
		const Bitboard ZevendeRij = (Us == WHITE ? Rank7BB : Rank2BB);
		const Bitboard ThirdRank = (Us == WHITE ? Rank3BB : Rank6BB);
		const Square Forward = (Us == WHITE ? UP : DOWN);
		const Square SlagRechts = (Us == WHITE ? UP_RIGHT : DOWN_LEFT);
		const Square SlagLinks = (Us == WHITE ? UP_LEFT : DOWN_RIGHT);

		Bitboard legeVelden;
		Bitboard pionnen7deRij = pos.stukken(Us, PAWN) &  ZevendeRij;
		Bitboard pionnenNiet7deRij = pos.stukken(Us, PAWN) & ~ZevendeRij;

		Bitboard jouwStukken = (Type == GEN_EVASIONS ? pos.stukken(Them) & target :
			Type == GEN_CAPTURES_OR_PROMOTIONS ? target : pos.stukken(Them));

		if (Type != GEN_CAPTURES_OR_PROMOTIONS)
		{
			legeVelden = (Type == GEN_QUIET_MOVES || Type == GEN_QUIET_CHECKS ? target : ~pos.stukken());

			Bitboard bbEnkeleZet = shift_up<Us>(pionnenNiet7deRij) & legeVelden;
			Bitboard bbDubbeleZet = shift_up<Us>(bbEnkeleZet & ThirdRank) & legeVelden;

			if (Type == GEN_EVASIONS)
			{
				bbEnkeleZet &= target;
				bbDubbeleZet &= target;
			}

			if (Type == GEN_QUIET_CHECKS)
			{
				bbEnkeleZet &= pos.aanval_van<PAWN>(pos.koning(Them), Them);
				bbDubbeleZet &= pos.aanval_van<PAWN>(pos.koning(Them), Them);

				Bitboard aftrekSchaak = pos.info()->xRay[~pos.aan_zet()];
				if (pionnenNiet7deRij & aftrekSchaak)
				{
					Bitboard aftrekVoorwaarts = shift_up<Us>(pionnenNiet7deRij & aftrekSchaak) & legeVelden & ~bb_lijn(pos.koning(Them));
					Bitboard aftrekDubbel = shift_up<Us>(aftrekVoorwaarts & ThirdRank) & legeVelden;

					bbEnkeleZet |= aftrekVoorwaarts;
					bbDubbeleZet |= aftrekDubbel;
				}
			}

			while (bbEnkeleZet)
			{
				Square naar = pop_lsb(&bbEnkeleZet);
				*zetten++ = maak_zet(PAWN, naar - Forward, naar);
			}

			while (bbDubbeleZet)
			{
				Square naar = pop_lsb(&bbDubbeleZet);
				*zetten++ = maak_zet(PAWN, naar - Forward - Forward, naar);
			}
		}

		if (pionnen7deRij && (Type != GEN_EVASIONS || (target & AchtsteRij)))
		{
			if (Type == GEN_CAPTURES_OR_PROMOTIONS)
				legeVelden = ~pos.stukken();

			if (Type == GEN_EVASIONS)
				legeVelden &= target;

			Bitboard promotieRechts = shift_bb<SlagRechts>(pionnen7deRij) & jouwStukken;
			Bitboard promotieLinks = shift_bb<SlagLinks>(pionnen7deRij) & jouwStukken;
			Bitboard promotieVoorwaarts = shift_up<Us>(pionnen7deRij) & legeVelden;

			while (promotieRechts)
				zetten = schrijf_promoties<Us, Type, SlagRechts>(pos, zetten, pop_lsb(&promotieRechts));

			while (promotieLinks)
				zetten = schrijf_promoties<Us, Type, SlagLinks>(pos, zetten, pop_lsb(&promotieLinks));

			while (promotieVoorwaarts)
				zetten = schrijf_promoties<Us, Type, Forward>(pos, zetten, pop_lsb(&promotieVoorwaarts));
		}

		if (Type == GEN_CAPTURES_OR_PROMOTIONS || Type == GEN_EVASIONS || Type == GEN_ALL_MOVES)
		{
			Bitboard slagRechts = shift_bb<SlagRechts>(pionnenNiet7deRij) & jouwStukken;
			Bitboard slagLinks = shift_bb<SlagLinks>(pionnenNiet7deRij) & jouwStukken;

			while (slagRechts)
			{
				Square naar = pop_lsb(&slagRechts);
				*zetten++ = maak_zet(PAWN, naar - SlagRechts, naar);
			}

			while (slagLinks)
			{
				Square naar = pop_lsb(&slagLinks);
				*zetten++ = maak_zet(PAWN, naar - SlagLinks, naar);
			}

			if (pos.enpassant_veld() != SQ_NONE)
			{
				assert(rij(pos.enpassant_veld()) == relatieve_rij(Us, RANK_6));

				if (Type == GEN_EVASIONS && !(target & (pos.enpassant_veld() - Forward)))
					return zetten;

				slagRechts = pionnenNiet7deRij & pos.aanval_van<PAWN>(pos.enpassant_veld(), Them);

				assert(slagRechts);

				while (slagRechts)
					*zetten++ = maak_zet(ENPASSANT, pop_lsb(&slagRechts), pos.enpassant_veld());
			}
		}

		return zetten;
	}


	template<Color Us, PieceType stuk, bool OnlyCheckMoves>
	MoveEx* zetten_voor_stuk(const Position& pos, MoveEx* zetten, Bitboard target)
	{
		assert(stuk != KING && stuk != PAWN);

		const Square* pl = pos.stuk_lijst(Us, stuk);

		for (Square van = *pl; van != SQ_NONE; van = *++pl)
		{
			if (OnlyCheckMoves)
			{
				if ((stuk == BISHOP || stuk == ROOK || stuk == QUEEN)
					&& !(LegeAanval[stuk][van] & target & pos.info()->schaakVelden[stuk]))
					continue;

				// aftrekschaaks worden in genereerZetten<GEN_RUSTIGE_SCHAAKS> geschreven
				if (pos.info()->xRay[~pos.aan_zet()] & van)
					continue;
			}

			Bitboard velden = pos.aanval_van<stuk>(van) & target;

			if (OnlyCheckMoves)
				velden &= pos.info()->schaakVelden[stuk];

			while (velden)
				*zetten++ = maak_zet(stuk, van, pop_lsb(&velden));
		}

		return zetten;
	}


	template<Color Us, MoveGeneration Type>
	MoveEx* zetten_voor_alle_stukken(const Position& pos, MoveEx* zetten, Bitboard target)
	{
		const bool OnlyCheckMoves = Type == GEN_QUIET_CHECKS;

		if (Type != GEN_CASTLING)
		{
			zetten = zetten_voor_pion<Us, Type>(pos, zetten, target);
			zetten = zetten_voor_stuk<Us, KNIGHT, OnlyCheckMoves>(pos, zetten, target);
			zetten = zetten_voor_stuk<Us, BISHOP, OnlyCheckMoves>(pos, zetten, target);
			zetten = zetten_voor_stuk<Us, ROOK, OnlyCheckMoves>(pos, zetten, target);
			zetten = zetten_voor_stuk<Us, QUEEN, OnlyCheckMoves>(pos, zetten, target);

			if (Type != GEN_QUIET_CHECKS && Type != GEN_EVASIONS)
			{
				Square veldK = pos.koning(Us);
				Bitboard velden = pos.aanval_van<KING>(veldK) & target;
				while (velden)
					*zetten++ = maak_zet(KING, veldK, pop_lsb(&velden));
			}
		}

		if (Type != GEN_CAPTURES_OR_PROMOTIONS && Type != GEN_EVASIONS && pos.rokade_mogelijkheden(Us))
		{
			if (pos.is_chess960())
			{
				zetten = schrijf_rokade<Us == WHITE ? WHITE_SHORT : BLACK_SHORT, OnlyCheckMoves, true>(pos, zetten);
				zetten = schrijf_rokade<Us == WHITE ? WHITE_LONG : BLACK_LONG, OnlyCheckMoves, true>(pos, zetten);
			}
			else
			{
				zetten = schrijf_rokade<Us == WHITE ? WHITE_SHORT : BLACK_SHORT, OnlyCheckMoves, false>(pos, zetten);
				zetten = schrijf_rokade<Us == WHITE ? WHITE_LONG : BLACK_LONG, OnlyCheckMoves, false>(pos, zetten);
			}
		}

		return zetten;
	}


	template<Color Us>
	MoveEx* genereer_pion_opmars(const Position& pos, MoveEx* zetten)
	{
		const Bitboard RANK67 = Us == WHITE ? Rank6BB | Rank7BB : Rank3BB | Rank2BB;

		Bitboard velden = shift_up<Us>(pos.stukken(Us, PAWN)) & RANK67 & ~pos.stukken();
		while (velden)
		{
			Square naar = pop_lsb(&velden);
			*zetten++ = maak_zet(PAWN, naar - pion_vooruit(Us), naar);
		}

		return zetten;
	}

	
} // namespace


template<MoveGeneration Type>
MoveEx* genereerZetten(const Position& pos, MoveEx* zetten) 
{
	assert(Type == GEN_CAPTURES_OR_PROMOTIONS || Type == GEN_QUIET_MOVES || Type == GEN_ALL_MOVES || Type == GEN_CASTLING);
	assert(!pos.schaak_gevers());

	Color ik = pos.aan_zet();

	Bitboard target = Type == GEN_CAPTURES_OR_PROMOTIONS ? pos.stukken(~ik)
		: Type == GEN_QUIET_MOVES ? ~pos.stukken()
		: Type == GEN_ALL_MOVES ? ~pos.stukken(ik) : 0;

	return ik == WHITE ? zetten_voor_alle_stukken<WHITE, Type>(pos, zetten, target)
		: zetten_voor_alle_stukken<BLACK, Type>(pos, zetten, target);
}

template MoveEx* genereerZetten<GEN_CAPTURES_OR_PROMOTIONS>(const Position&, MoveEx*);
template MoveEx* genereerZetten<GEN_QUIET_MOVES>(const Position&, MoveEx*);
template MoveEx* genereerZetten<GEN_ALL_MOVES>(const Position&, MoveEx*);


template<>
MoveEx* genereerZetten<GEN_QUIET_CHECKS>(const Position& pos, MoveEx* zetten) 
{
	assert(!pos.schaak_gevers());

	Color ik = pos.aan_zet();
	Bitboard aftrekSchaak = pos.aftrek_schaak_mogelijk();

	while (aftrekSchaak)
	{
		Square van = pop_lsb(&aftrekSchaak);
		PieceType stuk = stuk_type(pos.stuk_op_veld(van));

		if (stuk == PAWN)
			continue;

		Bitboard velden = pos.aanval_van(stuk, van) & ~pos.stukken();

		if (stuk == KING)
			velden &= ~LegeAanval[QUEEN][pos.koning(~ik)];

		while (velden)
			*zetten++ = maak_zet(stuk, van, pop_lsb(&velden));
	}

	return ik == WHITE ? zetten_voor_alle_stukken<WHITE, GEN_QUIET_CHECKS>(pos, zetten, ~pos.stukken())
		: zetten_voor_alle_stukken<BLACK, GEN_QUIET_CHECKS>(pos, zetten, ~pos.stukken());
}


template<>
MoveEx* genereerZetten<GEN_PAWN_ADVANCES>(const Position& pos, MoveEx* zetten)
{
	assert(!pos.schaak_gevers());

	Color ik = pos.aan_zet();
	return ik == WHITE ? genereer_pion_opmars<WHITE>(pos, zetten)
		: genereer_pion_opmars<BLACK>(pos, zetten);
}


template<>
MoveEx* genereerZetten<GEN_QUEEN_CHECKS>(const Position& pos, MoveEx* zetten)
{
	return pos.aan_zet() == WHITE 
		? zetten_voor_stuk<WHITE, QUEEN, true>(pos, zetten, ~pos.stukken())
		: zetten_voor_stuk<BLACK, QUEEN, true>(pos, zetten, ~pos.stukken());
}

template<>
MoveEx* genereerZetten<GEN_EVASIONS>(const Position& pos, MoveEx* zetten) 
{
	assert(pos.schaak_gevers());

	Color ik = pos.aan_zet();
	Square veldK = pos.koning(ik);
	Bitboard aangevallenVelden = 0;
	Bitboard verreAanvallers = pos.schaak_gevers() & ~pos.stukken(KNIGHT, PAWN);

	while (verreAanvallers)
	{
		Square schaakveld = pop_lsb(&verreAanvallers);
		aangevallenVelden |= bbVerbinding[schaakveld][veldK] ^ schaakveld;
	}

	Bitboard velden = pos.aanval_van<KING>(veldK) & ~pos.stukken(ik) & ~aangevallenVelden;
	while (velden)
		*zetten++ = maak_zet(KING, veldK, pop_lsb(&velden));

	if (meer_dan_een(pos.schaak_gevers()))
		return zetten;

	Square schaakVeld = lsb(pos.schaak_gevers());
	Bitboard target = bb_tussen(schaakVeld, veldK) | schaakVeld;

	return ik == WHITE ? zetten_voor_alle_stukken<WHITE, GEN_EVASIONS>(pos, zetten, target)
		: zetten_voor_alle_stukken<BLACK, GEN_EVASIONS>(pos, zetten, target);
}


MoveEx* genereerLegaleZetten(const Position& pos, MoveEx* zetten) 
{
	Bitboard gepend = pos.gepende_stukken();
	Square veldK = pos.koning(pos.aan_zet());
	MoveEx* pZet = zetten;

	zetten = pos.schaak_gevers() ? genereerZetten<GEN_EVASIONS>(pos, zetten)
		: genereerZetten<GEN_ALL_MOVES>(pos, zetten);
	while (pZet != zetten)
		if ((gepend || van_veld(*pZet) == veldK || zet_type(*pZet) == ENPASSANT)
			&& !pos.legale_zet(*pZet))
			*pZet = (--zetten)->zet;
		else
			++pZet;

	return zetten;
}


MoveEx* genereerSlagzettenOpVeld(const Position& pos, MoveEx* zetten, Square veld) 
{
	Bitboard target = bbVeld[veld];

	return pos.aan_zet() == WHITE
		? zetten_voor_alle_stukken<WHITE, GEN_CAPTURES_OR_PROMOTIONS>(pos, zetten, target)
		: zetten_voor_alle_stukken<BLACK, GEN_CAPTURES_OR_PROMOTIONS>(pos, zetten, target);
}


bool LegaleZettenLijstBevatZet(const Position & pos, Move zet)
{
	MoveEx zetten[MAX_MOVES];
	MoveEx* einde = pos.schaak_gevers() ? genereerZetten<GEN_EVASIONS>(pos, zetten)
		: genereerZetten<GEN_ALL_MOVES>(pos, zetten);

	MoveEx* pZet = zetten;
	while (pZet != einde)
	{
		if (pZet->zet == zet)
			return pos.legale_zet(zet);
		pZet++;
	}
	return false;
}


bool MinstensEenLegaleZet(const Position & pos)
{
	MoveEx zetten[MAX_MOVES];
	MoveEx* einde = pos.schaak_gevers() ? genereerZetten<GEN_EVASIONS>(pos, zetten)
		: genereerZetten<GEN_ALL_MOVES>(pos, zetten);

	MoveEx* pZet = zetten;
	while (pZet != einde)
	{
		if (pos.legale_zet(pZet->zet))
			return true;
		pZet++;
	}
	return false;
}


bool LegaleZettenLijstBevatRokade(const Position & pos, Move zet)
{
	MoveEx zetten[MAX_MOVES];
	if (pos.schaak_gevers())
		return false;

	MoveEx* einde = genereerZetten<GEN_CASTLING>(pos, zetten);

	MoveEx* pZet = zetten;
	while (pZet != einde)
	{
		if (pZet->zet == zet)
			return true;
		pZet++;
	}
	return false;
}
