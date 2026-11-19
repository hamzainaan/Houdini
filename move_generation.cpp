/*
*/

#include "houdini.h"
#include "move_generation.h"
#include "position.h"
#include "util_tools.h"

namespace 
{
	template<CastlingRight rokade, bool AlleenSchaakZetten, bool Chess960>
	MoveEx* schrijf_rokade(const Position& pos, MoveEx* zetten)
	{
		const Color IK = rokade <= WIT_LANG ? WIT : ZWART;
		const Color JIJ = (IK == WIT ? ZWART : WIT);
		const bool korte_rokade = (rokade == WIT_KORT || rokade == ZWART_KORT);

		if (pos.rokade_verhinderd(rokade) || !pos.rokade_mogelijk(rokade))
			return zetten;

		const Square vanK = Chess960 ? pos.koning(IK) : relatief_veld(IK, SQ_E1);
		const Square naarK = relatief_veld(IK, korte_rokade ? SQ_G1 : SQ_C1);
		const Square richting = naarK > vanK ? LINKS : RECHTS;

		assert(!pos.schaak_gevers());

		if (Chess960)
		{
			for (Square veld = naarK; veld != vanK; veld += richting)
				if (pos.aanvallers_naar(veld) & pos.stukken(JIJ))
					return zetten;

			Square vanT = pos.rokade_toren_veld(naarK);
			if (aanval_bb_toren(naarK, pos.stukken() ^ vanT) & pos.stukken(JIJ, TOREN, DAME))
				return zetten;
		}
		else
		{
			if (pos.aanvallers_naar(naarK) & pos.stukken(JIJ))
				return zetten;
			if (pos.aanvallers_naar(naarK + richting) & pos.stukken(JIJ))
				return zetten;
		}

		Move zet = maak_zet(ROKADE, vanK, naarK);

		if (AlleenSchaakZetten && !pos.geeft_schaak(zet))
			return zetten;

		*zetten++ = zet;
		return zetten;
	}


	template<Color IK, MoveGeneration Type, Square Delta>
	MoveEx* schrijf_promoties(const Position& pos, MoveEx* zetten, Square naar)
	{
		const Color JIJ = (IK == WIT ? ZWART : WIT);

		if (Type == GEN_SLAG_OF_PROMOTIE || Type == GEN_GA_UIT_SCHAAK || Type == GEN_ALLE_ZETTEN)
			*zetten++ = maak_zet(PROMOTIE_D, naar - Delta, naar);

		if (Type == GEN_RUSTIGE_ZETTEN || Type == GEN_GA_UIT_SCHAAK || Type == GEN_ALLE_ZETTEN)
		{
			*zetten++ = maak_zet(PROMOTIE_T, naar - Delta, naar);
			*zetten++ = maak_zet(PROMOTIE_L, naar - Delta, naar);
			*zetten++ = maak_zet(PROMOTIE_P, naar - Delta, naar);
		}

		if (Type == GEN_RUSTIGE_SCHAAKS && (LegeAanval[PAARD][naar] & pos.koning(JIJ)))
			*zetten++ = maak_zet(PROMOTIE_P, naar - Delta, naar);

		return zetten;
	}


	template<Color IK, MoveGeneration Type>
	MoveEx* zetten_voor_pion(const Position& pos, MoveEx* zetten, Bitboard target)
	{
		const Color JIJ = (IK == WIT ? ZWART : WIT);
		const Bitboard AchtsteRij = (IK == WIT ? Rank8BB : Rank1BB);
		const Bitboard ZevendeRij = (IK == WIT ? Rank7BB : Rank2BB);
		const Bitboard DerdeRij = (IK == WIT ? Rank3BB : Rank6BB);
		const Square Vooruit = (IK == WIT ? BOVEN : ONDER);
		const Square SlagRechts = (IK == WIT ? RECHTSBOVEN : LINKSONDER);
		const Square SlagLinks = (IK == WIT ? LINKSBOVEN : RECHTSONDER);

		Bitboard legeVelden;
		Bitboard pionnen7deRij = pos.stukken(IK, PION) &  ZevendeRij;
		Bitboard pionnenNiet7deRij = pos.stukken(IK, PION) & ~ZevendeRij;

		Bitboard jouwStukken = (Type == GEN_GA_UIT_SCHAAK ? pos.stukken(JIJ) & target :
			Type == GEN_SLAG_OF_PROMOTIE ? target : pos.stukken(JIJ));

		if (Type != GEN_SLAG_OF_PROMOTIE)
		{
			legeVelden = (Type == GEN_RUSTIGE_ZETTEN || Type == GEN_RUSTIGE_SCHAAKS ? target : ~pos.stukken());

			Bitboard bbEnkeleZet = shift_up<IK>(pionnenNiet7deRij) & legeVelden;
			Bitboard bbDubbeleZet = shift_up<IK>(bbEnkeleZet & DerdeRij) & legeVelden;

			if (Type == GEN_GA_UIT_SCHAAK)
			{
				bbEnkeleZet &= target;
				bbDubbeleZet &= target;
			}

			if (Type == GEN_RUSTIGE_SCHAAKS)
			{
				bbEnkeleZet &= pos.aanval_van<PION>(pos.koning(JIJ), JIJ);
				bbDubbeleZet &= pos.aanval_van<PION>(pos.koning(JIJ), JIJ);

				Bitboard aftrekSchaak = pos.info()->xRay[~pos.aan_zet()];
				if (pionnenNiet7deRij & aftrekSchaak)
				{
					Bitboard aftrekVoorwaarts = shift_up<IK>(pionnenNiet7deRij & aftrekSchaak) & legeVelden & ~bb_lijn(pos.koning(JIJ));
					Bitboard aftrekDubbel = shift_up<IK>(aftrekVoorwaarts & DerdeRij) & legeVelden;

					bbEnkeleZet |= aftrekVoorwaarts;
					bbDubbeleZet |= aftrekDubbel;
				}
			}

			while (bbEnkeleZet)
			{
				Square naar = pop_lsb(&bbEnkeleZet);
				*zetten++ = maak_zet(PION, naar - Vooruit, naar);
			}

			while (bbDubbeleZet)
			{
				Square naar = pop_lsb(&bbDubbeleZet);
				*zetten++ = maak_zet(PION, naar - Vooruit - Vooruit, naar);
			}
		}

		if (pionnen7deRij && (Type != GEN_GA_UIT_SCHAAK || (target & AchtsteRij)))
		{
			if (Type == GEN_SLAG_OF_PROMOTIE)
				legeVelden = ~pos.stukken();

			if (Type == GEN_GA_UIT_SCHAAK)
				legeVelden &= target;

			Bitboard promotieRechts = shift_bb<SlagRechts>(pionnen7deRij) & jouwStukken;
			Bitboard promotieLinks = shift_bb<SlagLinks>(pionnen7deRij) & jouwStukken;
			Bitboard promotieVoorwaarts = shift_up<IK>(pionnen7deRij) & legeVelden;

			while (promotieRechts)
				zetten = schrijf_promoties<IK, Type, SlagRechts>(pos, zetten, pop_lsb(&promotieRechts));

			while (promotieLinks)
				zetten = schrijf_promoties<IK, Type, SlagLinks>(pos, zetten, pop_lsb(&promotieLinks));

			while (promotieVoorwaarts)
				zetten = schrijf_promoties<IK, Type, Vooruit>(pos, zetten, pop_lsb(&promotieVoorwaarts));
		}

		if (Type == GEN_SLAG_OF_PROMOTIE || Type == GEN_GA_UIT_SCHAAK || Type == GEN_ALLE_ZETTEN)
		{
			Bitboard slagRechts = shift_bb<SlagRechts>(pionnenNiet7deRij) & jouwStukken;
			Bitboard slagLinks = shift_bb<SlagLinks>(pionnenNiet7deRij) & jouwStukken;

			while (slagRechts)
			{
				Square naar = pop_lsb(&slagRechts);
				*zetten++ = maak_zet(PION, naar - SlagRechts, naar);
			}

			while (slagLinks)
			{
				Square naar = pop_lsb(&slagLinks);
				*zetten++ = maak_zet(PION, naar - SlagLinks, naar);
			}

			if (pos.enpassant_veld() != SQ_NONE)
			{
				assert(rij(pos.enpassant_veld()) == relatieve_rij(IK, RIJ_6));

				if (Type == GEN_GA_UIT_SCHAAK && !(target & (pos.enpassant_veld() - Vooruit)))
					return zetten;

				slagRechts = pionnenNiet7deRij & pos.aanval_van<PION>(pos.enpassant_veld(), JIJ);

				assert(slagRechts);

				while (slagRechts)
					*zetten++ = maak_zet(ENPASSANT, pop_lsb(&slagRechts), pos.enpassant_veld());
			}
		}

		return zetten;
	}


	template<Color IK, PieceType stuk, bool AlleenSchaakZetten>
	MoveEx* zetten_voor_stuk(const Position& pos, MoveEx* zetten, Bitboard target)
	{
		assert(stuk != KONING && stuk != PION);

		const Square* pl = pos.stuk_lijst(IK, stuk);

		for (Square van = *pl; van != SQ_NONE; van = *++pl)
		{
			if (AlleenSchaakZetten)
			{
				if ((stuk == LOPER || stuk == TOREN || stuk == DAME)
					&& !(LegeAanval[stuk][van] & target & pos.info()->schaakVelden[stuk]))
					continue;

				// aftrekschaaks worden in genereerZetten<GEN_RUSTIGE_SCHAAKS> geschreven
				if (pos.info()->xRay[~pos.aan_zet()] & van)
					continue;
			}

			Bitboard velden = pos.aanval_van<stuk>(van) & target;

			if (AlleenSchaakZetten)
				velden &= pos.info()->schaakVelden[stuk];

			while (velden)
				*zetten++ = maak_zet(stuk, van, pop_lsb(&velden));
		}

		return zetten;
	}


	template<Color IK, MoveGeneration Type>
	MoveEx* zetten_voor_alle_stukken(const Position& pos, MoveEx* zetten, Bitboard target)
	{
		const bool AlleenSchaakZetten = Type == GEN_RUSTIGE_SCHAAKS;

		if (Type != GEN_ROKADE)
		{
			zetten = zetten_voor_pion<IK, Type>(pos, zetten, target);
			zetten = zetten_voor_stuk<IK, PAARD, AlleenSchaakZetten>(pos, zetten, target);
			zetten = zetten_voor_stuk<IK, LOPER, AlleenSchaakZetten>(pos, zetten, target);
			zetten = zetten_voor_stuk<IK, TOREN, AlleenSchaakZetten>(pos, zetten, target);
			zetten = zetten_voor_stuk<IK, DAME, AlleenSchaakZetten>(pos, zetten, target);

			if (Type != GEN_RUSTIGE_SCHAAKS && Type != GEN_GA_UIT_SCHAAK)
			{
				Square veldK = pos.koning(IK);
				Bitboard velden = pos.aanval_van<KONING>(veldK) & target;
				while (velden)
					*zetten++ = maak_zet(KONING, veldK, pop_lsb(&velden));
			}
		}

		if (Type != GEN_SLAG_OF_PROMOTIE && Type != GEN_GA_UIT_SCHAAK && pos.rokade_mogelijkheden(IK))
		{
			if (pos.is_chess960())
			{
				zetten = schrijf_rokade<IK == WIT ? WIT_KORT : ZWART_KORT, AlleenSchaakZetten, true>(pos, zetten);
				zetten = schrijf_rokade<IK == WIT ? WIT_LANG : ZWART_LANG, AlleenSchaakZetten, true>(pos, zetten);
			}
			else
			{
				zetten = schrijf_rokade<IK == WIT ? WIT_KORT : ZWART_KORT, AlleenSchaakZetten, false>(pos, zetten);
				zetten = schrijf_rokade<IK == WIT ? WIT_LANG : ZWART_LANG, AlleenSchaakZetten, false>(pos, zetten);
			}
		}

		return zetten;
	}


	template<Color IK>
	MoveEx* genereer_pion_opmars(const Position& pos, MoveEx* zetten)
	{
		const Bitboard RIJ67 = IK == WIT ? Rank6BB | Rank7BB : Rank3BB | Rank2BB;

		Bitboard velden = shift_up<IK>(pos.stukken(IK, PION)) & RIJ67 & ~pos.stukken();
		while (velden)
		{
			Square naar = pop_lsb(&velden);
			*zetten++ = maak_zet(PION, naar - pion_vooruit(IK), naar);
		}

		return zetten;
	}

	
} // namespace


template<MoveGeneration Type>
MoveEx* genereerZetten(const Position& pos, MoveEx* zetten) 
{
	assert(Type == GEN_SLAG_OF_PROMOTIE || Type == GEN_RUSTIGE_ZETTEN || Type == GEN_ALLE_ZETTEN || Type == GEN_ROKADE);
	assert(!pos.schaak_gevers());

	Color ik = pos.aan_zet();

	Bitboard target = Type == GEN_SLAG_OF_PROMOTIE ? pos.stukken(~ik)
		: Type == GEN_RUSTIGE_ZETTEN ? ~pos.stukken()
		: Type == GEN_ALLE_ZETTEN ? ~pos.stukken(ik) : 0;

	return ik == WIT ? zetten_voor_alle_stukken<WIT, Type>(pos, zetten, target)
		: zetten_voor_alle_stukken<ZWART, Type>(pos, zetten, target);
}

template MoveEx* genereerZetten<GEN_SLAG_OF_PROMOTIE>(const Position&, MoveEx*);
template MoveEx* genereerZetten<GEN_RUSTIGE_ZETTEN>(const Position&, MoveEx*);
template MoveEx* genereerZetten<GEN_ALLE_ZETTEN>(const Position&, MoveEx*);


template<>
MoveEx* genereerZetten<GEN_RUSTIGE_SCHAAKS>(const Position& pos, MoveEx* zetten) 
{
	assert(!pos.schaak_gevers());

	Color ik = pos.aan_zet();
	Bitboard aftrekSchaak = pos.aftrek_schaak_mogelijk();

	while (aftrekSchaak)
	{
		Square van = pop_lsb(&aftrekSchaak);
		PieceType stuk = stuk_type(pos.stuk_op_veld(van));

		if (stuk == PION)
			continue;

		Bitboard velden = pos.aanval_van(stuk, van) & ~pos.stukken();

		if (stuk == KONING)
			velden &= ~LegeAanval[DAME][pos.koning(~ik)];

		while (velden)
			*zetten++ = maak_zet(stuk, van, pop_lsb(&velden));
	}

	return ik == WIT ? zetten_voor_alle_stukken<WIT, GEN_RUSTIGE_SCHAAKS>(pos, zetten, ~pos.stukken())
		: zetten_voor_alle_stukken<ZWART, GEN_RUSTIGE_SCHAAKS>(pos, zetten, ~pos.stukken());
}


template<>
MoveEx* genereerZetten<GEN_PION_OPMARS>(const Position& pos, MoveEx* zetten)
{
	assert(!pos.schaak_gevers());

	Color ik = pos.aan_zet();
	return ik == WIT ? genereer_pion_opmars<WIT>(pos, zetten)
		: genereer_pion_opmars<ZWART>(pos, zetten);
}


template<>
MoveEx* genereerZetten<GEN_DAME_SCHAAK>(const Position& pos, MoveEx* zetten)
{
	return pos.aan_zet() == WIT 
		? zetten_voor_stuk<WIT, DAME, true>(pos, zetten, ~pos.stukken())
		: zetten_voor_stuk<ZWART, DAME, true>(pos, zetten, ~pos.stukken());
}

template<>
MoveEx* genereerZetten<GEN_GA_UIT_SCHAAK>(const Position& pos, MoveEx* zetten) 
{
	assert(pos.schaak_gevers());

	Color ik = pos.aan_zet();
	Square veldK = pos.koning(ik);
	Bitboard aangevallenVelden = 0;
	Bitboard verreAanvallers = pos.schaak_gevers() & ~pos.stukken(PAARD, PION);

	while (verreAanvallers)
	{
		Square schaakveld = pop_lsb(&verreAanvallers);
		aangevallenVelden |= bbVerbinding[schaakveld][veldK] ^ schaakveld;
	}

	Bitboard velden = pos.aanval_van<KONING>(veldK) & ~pos.stukken(ik) & ~aangevallenVelden;
	while (velden)
		*zetten++ = maak_zet(KONING, veldK, pop_lsb(&velden));

	if (meer_dan_een(pos.schaak_gevers()))
		return zetten;

	Square schaakVeld = lsb(pos.schaak_gevers());
	Bitboard target = bb_tussen(schaakVeld, veldK) | schaakVeld;

	return ik == WIT ? zetten_voor_alle_stukken<WIT, GEN_GA_UIT_SCHAAK>(pos, zetten, target)
		: zetten_voor_alle_stukken<ZWART, GEN_GA_UIT_SCHAAK>(pos, zetten, target);
}


MoveEx* genereerLegaleZetten(const Position& pos, MoveEx* zetten) 
{
	Bitboard gepend = pos.gepende_stukken();
	Square veldK = pos.koning(pos.aan_zet());
	MoveEx* pZet = zetten;

	zetten = pos.schaak_gevers() ? genereerZetten<GEN_GA_UIT_SCHAAK>(pos, zetten)
		: genereerZetten<GEN_ALLE_ZETTEN>(pos, zetten);
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

	return pos.aan_zet() == WIT
		? zetten_voor_alle_stukken<WIT, GEN_SLAG_OF_PROMOTIE>(pos, zetten, target)
		: zetten_voor_alle_stukken<ZWART, GEN_SLAG_OF_PROMOTIE>(pos, zetten, target);
}


bool LegaleZettenLijstBevatZet(const Position & pos, Move zet)
{
	MoveEx zetten[MAX_ZETTEN];
	MoveEx* einde = pos.schaak_gevers() ? genereerZetten<GEN_GA_UIT_SCHAAK>(pos, zetten)
		: genereerZetten<GEN_ALLE_ZETTEN>(pos, zetten);

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
	MoveEx zetten[MAX_ZETTEN];
	MoveEx* einde = pos.schaak_gevers() ? genereerZetten<GEN_GA_UIT_SCHAAK>(pos, zetten)
		: genereerZetten<GEN_ALLE_ZETTEN>(pos, zetten);

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
	MoveEx zetten[MAX_ZETTEN];
	if (pos.schaak_gevers())
		return false;

	MoveEx* einde = genereerZetten<GEN_ROKADE>(pos, zetten);

	MoveEx* pZet = zetten;
	while (pZet != einde)
	{
		if (pZet->zet == zet)
			return true;
		pZet++;
	}
	return false;
}
