/*
*/

#include "houdini.h"
#include "bitboard.h"
#include "eval_endgames.h"
#include "move_generation.h"
#include "search_smp.h"

using std::string;

namespace Zobrist 
{
	extern Key64 psq[PIECE_N][SQUARE_N];
}

namespace 
{

#define V(x) Value(x * 4 / 5)

	const Value DrijfNaarKant[SQUARE_N] =
	{
		V(100), V(90), V(80), V(70), V(70), V(80), V(90), V(100),
		V(90), V(70), V(60), V(50), V(50), V(60), V(70),  V(90),
		V(80), V(60), V(40), V(30), V(30), V(40), V(60),  V(80),
		V(70), V(50), V(30), V(20), V(20), V(30), V(50),  V(70),
		V(70), V(50), V(30), V(20), V(20), V(30), V(50),  V(70),
		V(80), V(60), V(40), V(30), V(30), V(40), V(60),  V(80),
		V(90), V(70), V(60), V(50), V(50), V(60), V(70),  V(90),
		V(100), V(90), V(80), V(70), V(70), V(80), V(90), V(100)
	};

	const Value DrijfNaarHoek[SQUARE_N] =
	{
		V(100),  V(90),  V(80),  V(70),  V(60),  V(50),  V(40),  V(30),
		V(90),  V(80),  V(70),  V(60),  V(50),  V(40),  V(30),  V(40),
		V(80),  V(70),  V(55),  V(40),  V(40),  V(25),  V(40),  V(50),
		V(70),  V(60),  V(40),  V(20),  V(10),  V(40),  V(50),  V(60),
		V(60),  V(50),  V(40),  V(10),  V(20),  V(40),  V(60),  V(70),
		V(50),  V(40),  V(25),  V(40),  V(40),  V(55),  V(70),  V(80),
		V(40),  V(30),  V(40),  V(50),  V(60),  V(70),  V(80),  V(90),
		V(30),  V(40),  V(50),  V(60),  V(70),  V(80),  V(90), V(100)
	};

	const Value DrijfDichter[8] = { V(0), V(0), V(100), V(80), V(60), V(40), V(20), V(10) };
	const Value DrijfVerder[8] = { V(0), V(5), V(20), V(40), V(60), V(80), V(90), V(100) };

#undef V

	const int KRPPKRPScaleFactors[RANK_N] = { 0, 14, 16, 22, 33, 69, 0, 0 };

#ifndef NDEBUG
	bool tel_materiaal(const Position& pos, Color kleur, MaterialValue npm, int pionnen)
	{
		return pos.niet_pion_materiaal(kleur) == npm && pos.aantal(kleur, PAWN) == pionnen;
	}
#endif

	Square normaliseer_pion_zijde(const Position& pos, Color sterkeZijde, Square veld)
	{
		assert(pos.aantal(sterkeZijde, PAWN) == 1);

		if (lijn(pos.stuk_veld(sterkeZijde, PAWN)) >= FILE_E)
			veld = Square(veld ^ 7);

		if (sterkeZijde == BLACK)
			veld = ~veld;

		return veld;
	}

	Key64 materiaal_sleutel(Color kleur, const char* pieces)
	{
		Key64 materiaalSleutel = 0;
		for (PieceType stuk = KING; stuk <= QUEEN; ++stuk)
		{
			int aantal = int(pieces[stuk] - '0');
			for (int cnt = 0; cnt < aantal; ++cnt)
				materiaalSleutel ^= Zobrist::psq[maak_stuk(kleur, stuk)][cnt];
			aantal = int(pieces[stuk + 8] - '0');
			for (int cnt = 0; cnt < aantal; ++cnt)
				materiaalSleutel ^= Zobrist::psq[maak_stuk(~kleur, stuk)][cnt];
		}

		return materiaalSleutel;
	}

	Bitboard aanval_koning_inc(Square s) { return LegeAanval[KING][s] | s; }
} // namespace


Endgames::Endgames() {}

const Bitboard BlackModifier = 0xa4489c56;

void Endgames::add_waarde(const char* stukken, endgame_value_fx fW, endgame_value_fx fB)
{
	mapWaarde[materiaal_sleutel(WHITE, stukken)] = waarde_aantal;
	waarde_functies[waarde_aantal++] = fW;
	mapWaarde[materiaal_sleutel(BLACK, stukken)] = waarde_aantal;
	waarde_functies[waarde_aantal++] = fB;
}

void Endgames::add_schaalfactor(const char* stukken, endgame_scalefactor_fx fW, endgame_scalefactor_fx fB)
{
	mapSchaalFactor[materiaal_sleutel(WHITE, stukken)] = factor_aantal;
	factor_functies[factor_aantal++] = fW;
	mapSchaalFactor[materiaal_sleutel(BLACK, stukken) ^ BlackModifier] = factor_aantal;
	factor_functies[factor_aantal++] = fB;
}

int Endgames::probe_waarde(Key64 sleutel)
{
	FunctionIndexMap::const_iterator iter = mapWaarde.find(sleutel);
	return iter == mapWaarde.end() ? -1 : iter->second;
}

int Endgames::probe_schaalfactor(Key64 sleutel, Color& sterkeZijde)
{
	FunctionIndexMap::const_iterator iter = mapSchaalFactor.find(sleutel);
	if (iter != mapSchaalFactor.end())
		return sterkeZijde = WHITE, iter->second;
	iter = mapSchaalFactor.find(sleutel ^ BlackModifier);
	if (iter != mapSchaalFactor.end())
		return sterkeZijde = BLACK, iter->second;
	return -1;
}


template <Color sterkeZijde>
Value endgameValue_KXK(const Position& pos) 
{
	const Color zwakkeZijde = ~sterkeZijde;
	assert(tel_materiaal(pos, zwakkeZijde, MATL_0, 0));
	assert(!pos.schaak_gevers()); // Eval is never called when in check

	if (pos.aan_zet() == zwakkeZijde && !MinstensEenLegaleZet(pos))
		return DRAW_VALUE;

	Square sterkeK = pos.koning(sterkeZijde);
	Square zwakkeK = pos.koning(zwakkeZijde);

	Value result = WaardeVanMateriaal(pos.niet_pion_materiaal(sterkeZijde))
		+ pos.aantal(sterkeZijde, PAWN) * VALUE_PAWN
		+ DrijfNaarKant[zwakkeK]
		+ DrijfDichter[afstand(sterkeK, zwakkeK)];

	if (result < WIN_VALUE)
		if (pos.aantal(sterkeZijde, QUEEN) + pos.aantal(sterkeZijde, ROOK)
			|| (pos.aantal(sterkeZijde, BISHOP) && pos.aantal(sterkeZijde, KNIGHT))
			|| (pos.stukken(sterkeZijde, BISHOP) & DarkSquares && pos.stukken(sterkeZijde, BISHOP) & ~DarkSquares))
			result += WIN_VALUE;

	return sterkeZijde == pos.aan_zet() ? result : -result;
}


template <Color sterkeZijde>
Value endgameValue_KBNK(const Position& pos) 
{
	const Color zwakkeZijde = ~sterkeZijde;
	assert(tel_materiaal(pos, sterkeZijde, MATL_KNIGHT + MATL_BISHOP, 0));
	assert(tel_materiaal(pos, zwakkeZijde, MATL_0, 0));

	Square sterkeK = pos.koning(sterkeZijde);
	Square zwakkeK = pos.koning(zwakkeZijde);
	Square loper = pos.stuk_veld(sterkeZijde, BISHOP);

	if (verschillende_kleur(loper, SQ_A1))
	{
		sterkeK = ~sterkeK;
		zwakkeK = ~zwakkeK;
	}

	Value result = WIN_VALUE
		+ DrijfDichter[afstand(sterkeK, zwakkeK)]
		+ DrijfNaarHoek[zwakkeK];

	return sterkeZijde == pos.aan_zet() ? result : -result;
}


template <Color sterkeZijde>
Value endgameValue_KPK(const Position& pos) 
{
	const Color zwakkeZijde = ~sterkeZijde;
	assert(tel_materiaal(pos, sterkeZijde, MATL_0, 1));
	assert(tel_materiaal(pos, zwakkeZijde, MATL_0, 0));

	Square sterkeK = normaliseer_pion_zijde(pos, sterkeZijde, pos.koning(sterkeZijde));
	Square zwakkeK = normaliseer_pion_zijde(pos, sterkeZijde, pos.koning(zwakkeZijde));
	Square pion = normaliseer_pion_zijde(pos, sterkeZijde, pos.stuk_veld(sterkeZijde, PAWN));

	Color ik = sterkeZijde == pos.aan_zet() ? WHITE : BLACK;

	if (!Bitbases::probe(sterkeK, pion, zwakkeK, ik))
	{
		pos.info()->eval_is_exact = true;
		return DRAW_VALUE;
	}

	Value result = WIN_VALUE + Value(40) * int(rij(pion));

	return sterkeZijde == pos.aan_zet() ? result : -result;
}


/*
test posities voor KR_KP (randgevallen)
=======================================
remise
R7/8/8/7K/4p3/4k3/8/8 b - - 0 1
4R3/1K6/8/2k1p3/8/8/8/8 b - - 0 1
R7/2K5/8/2k1p3/8/8/8/8 b - - 0 1
R7/7K/8/4p3/2k5/8/8/8 b - - 0 1
6R1/5K2/8/3p4/2k5/8/8/8 b - - 0 1
3R4/2K5/8/8/2p5/2k5/8/8 w - - 0 1
K5R1/8/6p1/5k2/8/8/8/8 b - - 0 1
=> require 14 - 18 - 16 - 13 - 14 - 14 - 15 plies om remise zonder KR_KP extensie te herkennen

winst
4R3/8/8/8/4p2K/3k4/8/8 b - - 0 1
4R3/2K5/8/2k1p3/8/8/8/8 b - - 0 1
R7/7K/8/2k1p3/8/8/8/8 b - - 0 1
2R5/1K6/8/8/1p6/1k6/8/8 w - - 0 1
K5R1/8/6p1/5k2/8/8/8/8 w - - 0 1
*/
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KRKP(const Position& pos)
{
	const Color zwakkeZijde = ~sterkeZijde;
	assert(tel_materiaal(pos, sterkeZijde, MATL_ROOK, 0));
	assert(tel_materiaal(pos, zwakkeZijde, MATL_0, 1));

	Square witte_koning = relatief_veld(sterkeZijde, pos.koning(sterkeZijde));
	Square zwarte_koning = relatief_veld(sterkeZijde, pos.koning(zwakkeZijde));
	Square toren = relatief_veld(sterkeZijde, pos.stuk_veld(sterkeZijde, ROOK));
	Square pion = relatief_veld(sterkeZijde, pos.stuk_veld(zwakkeZijde, PAWN));

	Square promotie = maak_veld(lijn(pion), RANK_1);

	int promotie_afstand, WK_afstand, ZK_afstand, WK_afstand2;
	bool pion_aangevallen, toren_achter_pion;

	if (!((pos.aanval_van<KING>(witte_koning) | pos.stukken(sterkeZijde, KING)) & bb_rijen_voorwaarts(zwakkeZijde, pion)) &&
		(rij(pion) <= RANK_3 || rij(zwarte_koning) <= rij(pion)))
	{
		ZK_afstand = VeldAfstand[zwarte_koning][pion];
		pion_aangevallen = (pos.aanval_van<ROOK>(pos.stuk_veld(sterkeZijde, ROOK)) | pos.aanval_van<KING>(pos.koning(sterkeZijde)))
			& pos.stuk_veld(zwakkeZijde, PAWN);

		if (ZK_afstand <= 1 + (pos.aan_zet() == zwakkeZijde || !pion_aangevallen))
		{
			toren_achter_pion = (lijn(pion) == lijn(toren));
			promotie_afstand = rij(pion) + VeldAfstand[zwarte_koning][promotie] - 2 + (toren_achter_pion);
			WK_afstand = VeldAfstand[witte_koning][promotie] - (pos.aan_zet() == sterkeZijde);
			if ((lijn(witte_koning) < lijn(pion) && lijn(zwarte_koning) < lijn(pion) ||
				lijn(witte_koning) > lijn(pion) && lijn(zwarte_koning) > lijn(pion)) &&
				rij(witte_koning) >= rij(zwarte_koning))
				WK_afstand++;

			if ((lijn(witte_koning) < lijn(zwarte_koning) && lijn(zwarte_koning) < lijn(pion) ||
				lijn(witte_koning) > lijn(zwarte_koning) && lijn(zwarte_koning) > lijn(pion)) &&
				rij(witte_koning) >= rij(zwarte_koning))
				WK_afstand++;

			if (WK_afstand > promotie_afstand)
			{
				WK_afstand2 = VeldAfstand[witte_koning][pion] - (pos.aan_zet() == sterkeZijde);
				if (WK_afstand2 > ZK_afstand ||
					rij(witte_koning) - (pos.aan_zet() == sterkeZijde) > rij(pion))
					return DRAW_FACTOR;
			}
		}
	}
	return NO_FACTOR;
}


/*
test posities voor KQ_KP
========================
remise
7Q/8/8/4K3/8/8/p7/1k6 w - - 0 1
Q7/8/8/8/8/8/pk3K2/8 w - - 0 1
2Q5/8/4K3/8/8/8/1kp5/8 w - - 0 1
7Q/8/8/4K3/8/8/2pk4/8 w - - 0 1
without KQ_KP extension even at 30 ply evaluations are higher than +6
4Q3/8/8/3K4/8/8/2pk4/8 w - - 0 1  => uitzonderingspositie op de regel van Euwe !!!

winst
8/8/8/8/8/8/pk3KQ1/8 w - - 0 1
Q7/8/8/K7/8/8/pk6/8 w - - 0 1
7Q/8/8/3K4/8/8/2pk4/8 w - - 0 1
8/8/1Q6/1K6/8/8/1kp5/8 w - - 0 1
*/
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KQKP(const Position& pos) 
{
	const Color zwakkeZijde = ~sterkeZijde;
	assert(tel_materiaal(pos, sterkeZijde, MATL_QUEEN, 0));
	assert(tel_materiaal(pos, zwakkeZijde, MATL_0, 1));

	if (!(pos.stukken(zwakkeZijde, PAWN) & (sterkeZijde == WHITE ? bb4(SQ_A2, SQ_C2, SQ_F2, SQ_H2) : bb4(SQ_A7, SQ_C7, SQ_F7, SQ_H7))))
		return NO_FACTOR;

	Square witte_koning = relatief_veld(sterkeZijde, pos.koning(sterkeZijde));
	Square zwarte_koning = relatief_veld(sterkeZijde, pos.koning(zwakkeZijde));
	Square dame = relatief_veld(sterkeZijde, pos.stuk_veld(sterkeZijde, QUEEN));
	Square pion = relatief_veld(sterkeZijde, pos.stuk_veld(zwakkeZijde, PAWN));

	Bitboard aanval = pos.aanval_van<QUEEN>(pos.stuk_veld(sterkeZijde, QUEEN)) | pos.aanval_van<KING>(pos.koning(sterkeZijde));
	bool wit_aan_zet = pos.aan_zet() == sterkeZijde;
	Bitboard ZK_schaak = pos.schaak_gevers();

	int flop = sterkeZijde == WHITE ? 0 : 070;
	bool tempo = !wit_aan_zet || !(aanval & Square(pion ^ flop));
	if (dame != pion - 8 && rij(zwarte_koning) <= RANK_2 + tempo
		&& (VeldAfstand[zwarte_koning][pion] <= 1 + tempo || pion == SQ_C2 && zwarte_koning == SQ_A1 || pion == SQ_F2 && zwarte_koning == SQ_H1))
	{
		if (lijn(pion) >= FILE_E)
		{
			flop ^= 07;
			pion = Square(pion ^ 07);
			witte_koning = Square(witte_koning ^ 07);
			zwarte_koning = Square(zwarte_koning ^ 07);
			dame = Square(dame ^ 07);
		}
		if (pion == SQ_A2 && (lijn(witte_koning) > FILE_E || witte_koning > SQ_D5))
			return DRAW_FACTOR;
		if (pion == SQ_C2 && lijn(zwarte_koning) < FILE_C && (lijn(witte_koning) > FILE_E || witte_koning > SQ_C4))
			return DRAW_FACTOR;
		if (pion == SQ_C2 && lijn(zwarte_koning) >= FILE_C && (lijn(witte_koning) > FILE_G || witte_koning > SQ_D5))
			return DRAW_FACTOR;

		if (!wit_aan_zet && !ZK_schaak)
		{
			if (pion == SQ_A2 && (VeldAfstand[SQ_A1][zwarte_koning] == 1 || lijn(dame) != FILE_A && !(aanval & Square(SQ_A1 ^ flop))))
				return DRAW_FACTOR;

			if (pion == SQ_C2 && (VeldAfstand[SQ_C1][zwarte_koning] == 1 || lijn(dame) != FILE_C && !(aanval & Square(SQ_C1 ^ flop))))
				return DRAW_FACTOR;
		}
	}
	return NO_FACTOR;
}


template <Color sterkeZijde>
Value endgameValue_KQKR(const Position& pos)
{
	const Color zwakkeZijde = ~sterkeZijde;
	assert(tel_materiaal(pos, sterkeZijde, MATL_QUEEN, 0));
	assert(tel_materiaal(pos, zwakkeZijde, MATL_ROOK, 0));

	Square sterkeK = pos.koning(sterkeZijde);
	Square zwakkeK = pos.koning(zwakkeZijde);

	Value result = 4 * VALUE_PAWN
		+ DrijfNaarKant[zwakkeK]
		+ DrijfDichter[afstand(sterkeK, zwakkeK)];

	return sterkeZijde == pos.aan_zet() ? result : -result;
}


template <Color sterkeZijde>
Value endgameValue_KNNK(const Position& pos)
{ 
	return DRAW_VALUE;
}


/*
test posities
remise
k7/8/K7/P7/8/2B5/P7/8 w - - 0 1
8/Bk6/1P6/2K5/8/8/8/8 w - - 0 1 Ponziani
1k6/p7/P2K4/8/4B3/8/8/8 w - - 0 1
1k6/p7/P2K4/8/Pp2B3/8/8/8 w - - 0 1
2k5/1p6/1P2K3/8/5B2/8/8/8 w - - 0 1
2k5/1p6/1Pp1K3/8/1P3B2/8/8/8 w - - 0 1
2k5/1p6/1P2K3/8/4B3/8/8/8 w - - 0 1
KBP v KP: 8/8/6K1/8/p7/P4k2/8/4B3 w - - OK!
KBP v KP: 1b6/7p/8/1K5P/8/8/8/5k2 b - - OK!
KBP v KP: 4b3/8/p7/P3K3/8/8/8/2k5 b - - OK!
KBPP v KPP: 2k5/7p/6pP/6P1/5B2/3K4/8/8 w - - OK!
win
1k6/p7/Pp1K4/8/P3B3/8/8/8 w - - 0 1
8/Bpk5/8/P2K4/8/8/8/8 w - - 0 1
*/
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KBPsK(const Position& pos) 
{
	const Color zwakkeZijde = ~sterkeZijde;
	assert(pos.niet_pion_materiaal(sterkeZijde) == MATL_BISHOP);
	assert(pos.aantal(sterkeZijde, PAWN) >= 1);

	// kan laatste pion geslagen worden?
	Bitboard bb_zwart_aanval = aanval_koning_inc(pos.koning(zwakkeZijde));
	Bitboard bb_wit_aanval = pos.aanval_van<KING>(pos.koning(sterkeZijde)) | pos.aanval_van<BISHOP>(pos.stuk_veld(sterkeZijde, BISHOP));
	if (pos.aantal(sterkeZijde, PAWN) == 1 && pos.aan_zet() == zwakkeZijde
		&& pos.stukken(sterkeZijde, PAWN) & bb_zwart_aanval & ~bb_wit_aanval)
		return DRAW_FACTOR;

	Square witte_koning = relatief_veld(sterkeZijde, pos.koning(sterkeZijde));
	Square zwarte_koning = relatief_veld(sterkeZijde, pos.koning(zwakkeZijde));
	Square loper = relatief_veld(sterkeZijde, pos.stuk_veld(sterkeZijde, BISHOP));
	Square pion = relatief_veld(sterkeZijde, pos.stuk_lijst(sterkeZijde, PAWN)[0]);
	Bitboard witte_pionnen = 0, zwarte_pionnen = 0;

	if (DarkSquares & loper)
	{
		witte_koning = Square(witte_koning ^ 07);
		zwarte_koning = Square(zwarte_koning ^ 07);
		loper = Square(loper ^ 07);
		pion = Square(pion ^ 07);
		for (int i = 0; i < pos.aantal(sterkeZijde, PAWN); i++)
			witte_pionnen |= relatief_veld(sterkeZijde, Square(pos.stuk_lijst(sterkeZijde, PAWN)[i] ^ 07));
		for (int i = 0; i < pos.aantal(zwakkeZijde, PAWN); i++)
			zwarte_pionnen |= relatief_veld(sterkeZijde, Square(pos.stuk_lijst(zwakkeZijde, PAWN)[i] ^ 07));
	}
	else
	{
		for (int i = 0; i < pos.aantal(sterkeZijde, PAWN); i++)
			witte_pionnen |= relatief_veld(sterkeZijde, pos.stuk_lijst(sterkeZijde, PAWN)[i]);
		for (int i = 0; i < pos.aantal(zwakkeZijde, PAWN); i++)
			zwarte_pionnen |= relatief_veld(sterkeZijde, pos.stuk_lijst(zwakkeZijde, PAWN)[i]);
	}
	// vanaf hier hebben we de situatie met een witte loper op de lichte velden

	Bitboard bb_koning_dekt = aanval_koning_inc(zwarte_koning);
	if (pos.aantal(sterkeZijde, PAWN) > 1)
		pion = SQ_NONE;

	if (!(witte_pionnen & ~FileHBB))
	{
		if (bb_koning_dekt & bb(SQ_H8))
		{
			if (witte_pionnen & bb(SQ_H5) && (zwarte_pionnen & bb2(SQ_G7, SQ_H6)) == bb2(SQ_G7, SQ_H6))
				;
			else
				return DRAW_FACTOR;
		}
		if (!(zwarte_pionnen & FileGBB)
			&& VeldAfstand[SQ_G7][zwarte_koning] < VeldAfstand[SQ_G7][witte_koning] + 1 - (pos.aan_zet() == sterkeZijde)
			&& VeldAfstand[SQ_H8][zwarte_koning] < 7 - rij(msb(witte_pionnen)) - (pos.aan_zet() == sterkeZijde))
			return ScaleFactor(NORMAL_FACTOR / 4);
		if (!(witte_pionnen & ~bb2(SQ_H2, SQ_H3)) && (zwarte_pionnen & bb(SQ_H4)))
			return ScaleFactor(NORMAL_FACTOR / 4);
	}
	if (pion == SQ_G6 && loper == SQ_H7 && bb_koning_dekt & bb(SQ_H8))
		return DRAW_FACTOR;
	if (!(witte_pionnen & ~FileABB) && witte_pionnen & bb(SQ_A6) &&
		zwarte_pionnen & bb(SQ_A7) && bb_koning_dekt & bb(SQ_B8) &&
		(!(zwarte_pionnen & FileBBB) ||
			rij(lsb(witte_pionnen & FileABB)) >= rij(msb(zwarte_pionnen & FileBBB))))
		return DRAW_FACTOR;
	if (!(witte_pionnen & ~FileGBB) && witte_pionnen & bb(SQ_G6) &&
		zwarte_pionnen & bb(SQ_G7) && bb_koning_dekt & bb2(SQ_G8, SQ_F8))
		return DRAW_FACTOR;
	if (pion == SQ_B6 && zwarte_pionnen & bb(SQ_B7) && bb_koning_dekt & bb2(SQ_B8, SQ_C8))
		return DRAW_FACTOR;
	// witte pionnen a6-b5-c4 geblokkeerd door een keten van a7-b6-c5
	if (witte_pionnen == bb2(SQ_A6, SQ_B5)
		&& (zwarte_pionnen & (bb2(SQ_A7, SQ_B6) | bb3(SQ_B7, SQ_C7, SQ_C6))) == bb2(SQ_A7, SQ_B6)
		&& bb_koning_dekt & bb(SQ_B8))
		return DRAW_FACTOR;
	if (witte_pionnen == bb3(SQ_A6, SQ_B5, SQ_C4)
		&& (zwarte_pionnen & (bb3(SQ_A7, SQ_B6, SQ_C5) | bb3(SQ_B7, SQ_C7, SQ_C6) | bb3(SQ_D7, SQ_D6, SQ_D5))) == bb3(SQ_A7, SQ_B6, SQ_C5)
		&& bb_koning_dekt & bb(SQ_B8))
		return DRAW_FACTOR;

	return NO_FACTOR;
}

/*
remise
8/4kp2/4r3/8/5QK1/8/8/8 w - - 0 1
winst
8/6kp/6r1/8/4KQ2/8/8/8 w - - 0 1
*/
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KQKRPs(const Position& pos) 
{
	const Color zwakkeZijde = ~sterkeZijde;
	assert(tel_materiaal(pos, sterkeZijde, MATL_QUEEN, 0));
	assert(pos.aantal(zwakkeZijde, ROOK) == 1);
	assert(pos.aantal(zwakkeZijde, PAWN) >= 1);

	Square kingSq = pos.koning(zwakkeZijde);
	Square rsq = pos.stuk_veld(zwakkeZijde, ROOK);

	if (relatieve_rij(zwakkeZijde, kingSq) <= RANK_2
		&&  relatieve_rij(zwakkeZijde, pos.koning(sterkeZijde)) >= RANK_4
		&&  relatieve_rij(zwakkeZijde, rsq) == RANK_3
		&& (pos.stukken(zwakkeZijde, PAWN)
			& ~FileABB & ~FileHBB // a en h pion maken geen vesting !
			& pos.aanval_van<KING>(kingSq)
			& pos.aanval_van<PAWN>(rsq, sterkeZijde)))
		return DRAW_FACTOR;

	return ONE_PAWN_FACTOR;
}


/// KRP vs KR. This function knows a handful of the most important classes of
/// drawn positions, but is far from perfect. It would probably be a good idea
/// to add more knowledge in the future.
///
/// It would also be nice to rewrite the actual code for this function,
/// which is mostly copied from Glaurung 1.x, and isn't very pretty.
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KRPKR(const Position& pos) {

  const Color zwakkeZijde = ~sterkeZijde;
  assert(tel_materiaal(pos, sterkeZijde, MATL_ROOK, 1));
  assert(tel_materiaal(pos, zwakkeZijde, MATL_ROOK, 0));

  // Assume sterkeZijde is white and the pawn is on files A-D
  Square wksq = normaliseer_pion_zijde(pos, sterkeZijde, pos.koning(sterkeZijde));
  Square bksq = normaliseer_pion_zijde(pos, sterkeZijde, pos.koning(zwakkeZijde));
  Square wrsq = normaliseer_pion_zijde(pos, sterkeZijde, pos.stuk_veld(sterkeZijde, ROOK));
  Square wpsq = normaliseer_pion_zijde(pos, sterkeZijde, pos.stuk_veld(sterkeZijde, PAWN));
  Square brsq = normaliseer_pion_zijde(pos, sterkeZijde, pos.stuk_veld(zwakkeZijde, ROOK));

  File f = lijn(wpsq);
  Rank r = rij(wpsq);
  Square queeningSq = maak_veld(f, RANK_8);
  int tempo = (pos.aan_zet() == sterkeZijde);

  // If the pawn is not too far advanced and the defending king defends the
  // queening square, use the third-rank defence.
  if (   r <= RANK_5
      && afstand(bksq, queeningSq) <= 1
      && wksq <= SQ_H5
      && (rij(brsq) == RANK_6 || (r <= RANK_3 && rij(wrsq) != RANK_6)))
      return DRAW_FACTOR;

  // The defending side saves a draw by checking from behind in case the pawn
  // has advanced to the 6th rank with the king behind.
  if (   r == RANK_6
      && afstand(bksq, queeningSq) <= 1
      && rij(wksq) + tempo <= RANK_6
      && (rij(brsq) == RANK_1 || (!tempo && lijn_afstand(brsq, wpsq) >= 3)))
      return DRAW_FACTOR;

  if (   r >= RANK_6
      && bksq == queeningSq
      && rij(brsq) == RANK_1
      && (!tempo || afstand(wksq, wpsq) >= 2))
      return DRAW_FACTOR;

  // White pawn on a7 and rook on a8 is a draw if black's king is on g7 or h7
  // and the black rook is behind the pawn.
  if (   wpsq == SQ_A7
      && wrsq == SQ_A8
      && (bksq == SQ_H7 || bksq == SQ_G7)
      && lijn(brsq) == FILE_A
      && (rij(brsq) <= RANK_3 || lijn(wksq) >= FILE_D || rij(wksq) <= RANK_5))
      return DRAW_FACTOR;

  // If the defending king blocks the pawn and the attacking king is too far
  // away, it's a draw.
  if (   r <= RANK_5
      && bksq == wpsq + UP
      && afstand(wksq, wpsq) - tempo >= 2
      && afstand(wksq, brsq) - tempo >= 2)
      return DRAW_FACTOR;

  // Pawn on the 7th rank supported by the rook from behind usually wins if the
  // attacking king is closer to the queening square than the defending king,
  // and the defending king cannot gain tempi by threatening the attacking rook.
  if (   r == RANK_7
      && f != FILE_A
      && lijn(wrsq) == f
      && wrsq != queeningSq
      && (afstand(wksq, queeningSq) < afstand(bksq, queeningSq) - 2 + tempo)
      && (afstand(wksq, queeningSq) < afstand(bksq, wrsq) + tempo))
      return ScaleFactor(MAX_FACTOR - 3 * afstand(wksq, queeningSq));

  // Similar to the above, but with the pawn further back
  if (   f != FILE_A
      && lijn(wrsq) == f
      && wrsq < wpsq
      && (afstand(wksq, queeningSq) < afstand(bksq, queeningSq) - 2 + tempo)
      && (afstand(wksq, wpsq + UP) < afstand(bksq, wpsq + UP) - 2 + tempo)
      && (  afstand(bksq, wrsq) + tempo >= 3
          || (    afstand(wksq, queeningSq) < afstand(bksq, wrsq) + tempo
              && (afstand(wksq, wpsq + UP) < afstand(bksq, wrsq) + tempo))))
      return ScaleFactor(MAX_FACTOR
                         - 12 * afstand(wpsq, queeningSq)
                         - 3 * afstand(wksq, queeningSq));

  // If the pawn is not far advanced and the defending king is somewhere in
  // the pawn's path, it's probably a draw.
  if (r <= RANK_4 && bksq > wpsq)
  {
      if (lijn(bksq) == lijn(wpsq))
          return ScaleFactor(16);
      if (   lijn_afstand(bksq, wpsq) == 1
          && afstand(wksq, bksq) > 2)
          return ScaleFactor(38 - 3 * afstand(wksq, bksq));
  }
  return NO_FACTOR;
}

template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KRPKB(const Position& pos) {

  const Color zwakkeZijde = ~sterkeZijde;
  assert(tel_materiaal(pos, sterkeZijde, MATL_ROOK, 1));
  assert(tel_materiaal(pos, zwakkeZijde, MATL_BISHOP, 0));

  // Test for a rook pawn
  if (pos.stukken(PAWN) & (FileABB | FileHBB))
  {
      Square veldK = pos.koning(zwakkeZijde);
      Square bsq = pos.stuk_veld(zwakkeZijde, BISHOP);
      Square psq = pos.stuk_veld(sterkeZijde, PAWN);
      Rank rk = relatieve_rij(sterkeZijde, psq);
      Square push = pion_vooruit(sterkeZijde);

      // If the pawn is on the 5th rank and the pawn (currently) is on
      // the same color square as the bishop then there is a chance of
      // a fortress. Depending on the king position give a moderate
      // reduction or a stronger one if the defending king is near the
      // corner but not trapped there.
      if (rk == RANK_5 && !verschillende_kleur(bsq, psq))
      {
          int d = afstand(psq + 3 * push, veldK);

          if (d <= 2 && !(d == 0 && veldK == pos.koning(sterkeZijde) + 2 * push))
              return ScaleFactor(38);
          else
              return ScaleFactor(75);
      }

      // When the pawn has moved to the 6th rank we can be fairly sure
      // it's drawn if the bishop attacks the square in front of the
      // pawn from a reasonable distance and the defending king is near
      // the corner
      if (   rk == RANK_6
          && afstand(psq + 2 * push, veldK) <= 1
          && (LegeAanval[BISHOP][bsq] & (psq + push))
          && lijn_afstand(bsq, psq) >= 2)
          return ScaleFactor(12);
  }

  return NO_FACTOR;
}

/// KRPP vs KRP. There is just a single rule: if the stronger side has no passed
/// pawns and the defending king is actively placed, the position is drawish.
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KRPPKRP(const Position& pos) {

  const Color zwakkeZijde = ~sterkeZijde;
  assert(tel_materiaal(pos, sterkeZijde, MATL_ROOK, 2));
  assert(tel_materiaal(pos, zwakkeZijde, MATL_ROOK, 1));

  Square wpsq1 = pos.stuk_lijst(sterkeZijde, PAWN)[0];
  Square wpsq2 = pos.stuk_lijst(sterkeZijde, PAWN)[1];
  Square bksq = pos.koning(zwakkeZijde);

  // Does the stronger side have a passed pawn?
  if (pos.is_vrijpion(sterkeZijde, wpsq1) || pos.is_vrijpion(sterkeZijde, wpsq2))
      return NO_FACTOR;

  Rank r = std::max(relatieve_rij(sterkeZijde, wpsq1), relatieve_rij(sterkeZijde, wpsq2));

  if (   lijn_afstand(bksq, wpsq1) <= 1
      && lijn_afstand(bksq, wpsq2) <= 1
      && relatieve_rij(sterkeZijde, bksq) > r)
  {
      assert(r > RANK_1 && r < RANK_7);
      return ScaleFactor(KRPPKRPScaleFactors[r]);
  }
  return NO_FACTOR;
}


template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KPsK(const Position& pos) 
{
	const Color zwakkeZijde = ~sterkeZijde;
	assert(pos.niet_pion_materiaal(sterkeZijde) == MATL_0);
	assert(pos.aantal(sterkeZijde, PAWN) >= 2);
	assert(tel_materiaal(pos, zwakkeZijde, MATL_0, 0));

	Square veldK = pos.koning(zwakkeZijde);
	Bitboard pawns = pos.stukken(sterkeZijde, PAWN);

	if (!(pawns & ~bb_rijen_voorwaarts(zwakkeZijde, rij(veldK)))
		&& !((pawns & ~FileABB) && (pawns & ~FileHBB))
		&& lijn_afstand(veldK, lsb(pawns)) <= 1)
		return DRAW_FACTOR;

	return NO_FACTOR;
}


/*
test posities voor KBP_KB_identical
===================================
remise
4k3/8/2KP4/5b2/8/8/4B3/8 b - - 0 1
8/2K5/3P4/2k2b2/8/8/4B3/8 w - - 0 1

winst
4k3/8/2KP4/5b2/8/8/4B3/8 w - - 0 1

test posities voor KBP_KB_opposite
==================================
remise
8/4B3/3P4/4Kb2/6k1/8/8/8 w - - 0 1
8/4B3/3P4/4K3/b7/8/8/7k w - - 0 1
*/
const Bitboard AverbakhRule = 0xfff7e3c180000000;

template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KBPKB(const Position& pos)
{
	const Color zwakkeZijde = ~sterkeZijde;
	assert(tel_materiaal(pos, sterkeZijde, MATL_BISHOP, 1));
	assert(tel_materiaal(pos, zwakkeZijde, MATL_BISHOP, 0));

	int weakSideToMove = pos.aan_zet() == zwakkeZijde;
	if (weakSideToMove
		&& pos.aanval_van<BISHOP>(pos.stuk_veld(sterkeZijde, PAWN)) & pos.stukken(zwakkeZijde, BISHOP)) //& ~ai.gepend[sterkeZijde])
		return DRAW_FACTOR;

	if (verschillende_kleur(pos.stuk_veld(sterkeZijde, BISHOP), pos.stuk_veld(zwakkeZijde, BISHOP)))
	{
		Square witte_koning = pos.koning(sterkeZijde);
		Square witte_loper = pos.stuk_veld(sterkeZijde, BISHOP);
		Square pion = pos.stuk_veld(sterkeZijde, PAWN);
		Square zwarte_koning = pos.koning(zwakkeZijde);
		Square zwarte_loper = pos.stuk_veld(zwakkeZijde, BISHOP);
		Bitboard wit_aanval = pos.aanval_van<PAWN>(pion, WHITE) | pos.aanval_van<BISHOP>(witte_loper) | pos.aanval_van<KING>(witte_koning);
		//Bord zwart_aanval = pos.aanval_van<LOPER>(zwarte_loper) | pos.aanval_van<KONING>(zwarte_koning);
		if (relatieve_rij(sterkeZijde, pion) <= RANK_5)
			return DRAW_FACTOR;
		if (bb_voorwaarts(sterkeZijde, pion) & zwarte_koning && verschillende_kleur(zwarte_koning, witte_loper))
			return DRAW_FACTOR;
		if (bb_voorwaarts(sterkeZijde, pion) & (pos.aanval_van<BISHOP>(zwarte_loper) | zwarte_loper)
			&& (weakSideToMove || (!(wit_aanval & zwarte_loper))))
			return DRAW_FACTOR;
	}
	else
	{
		int flop = sterkeZijde == WHITE ? ((pos.stukken(WHITE, BISHOP) & DarkSquares) ? 0 : 07)
			: ((pos.stukken(BLACK, BISHOP) & ~DarkSquares) ? 070 : 077);

		Square pion = Square(pos.stuk_veld(sterkeZijde, PAWN) ^ flop);
		Square witte_koning = Square(pos.koning(sterkeZijde) ^ flop);
		Square zwarte_koning = Square(pos.koning(zwakkeZijde) ^ flop);
		//Veld witte_loper = Veld(pos.stuk_veld(sterkeZijde, LOPER) ^ flop);
		// hier aangekomen hebben we lopers op donkere velden, en wit is de sterke partij

		if (zwarte_koning > pion && lijn(pion) == lijn(zwarte_koning) && ~DarkSquares & zwarte_koning)
			return DRAW_FACTOR;

		if (VeldAfstand[pion][zwarte_koning] == 1 && rij(zwarte_koning) < rij(pion)
			&& (abs(lijn(zwarte_koning) - lijn(witte_koning)) <= weakSideToMove)
			&& !(AverbakhRule & pion))
			return DRAW_FACTOR;
	}

	return NO_FACTOR;
}


/// KBPP vs KB. It detects a few basic draws with opposite-colored bishops
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KBPPKB(const Position& pos)
{
  const Color zwakkeZijde = ~sterkeZijde;
  assert(tel_materiaal(pos, sterkeZijde, MATL_BISHOP, 2));
  assert(tel_materiaal(pos, zwakkeZijde, MATL_BISHOP, 0));

  Square wbsq = pos.stuk_veld(sterkeZijde, BISHOP);
  Square bbsq = pos.stuk_veld(zwakkeZijde, BISHOP);

  if (!verschillende_kleur(wbsq, bbsq))
      return NO_FACTOR;

  Square veldK = pos.koning(zwakkeZijde);
  Square psq1 = pos.stuk_lijst(sterkeZijde, PAWN)[0];
  Square psq2 = pos.stuk_lijst(sterkeZijde, PAWN)[1];
  Square blockSq1, blockSq2;

  if (relatieve_rij(sterkeZijde, psq1) > relatieve_rij(sterkeZijde, psq2))
  {
      blockSq1 = psq1 + pion_vooruit(sterkeZijde);
      blockSq2 = maak_veld(lijn(psq2), rij(psq1));
  }
  else
  {
      blockSq1 = psq2 + pion_vooruit(sterkeZijde);
      blockSq2 = maak_veld(lijn(psq1), rij(psq2));
  }

  switch (lijn_afstand(psq1, psq2))
  {
  case 0:
    // Both pawns are on the same file. It's an easy draw if the defender firmly
    // controls some square in the frontmost pawn's path.
    if (   lijn(veldK) == lijn(blockSq1)
        && relatieve_rij(sterkeZijde, veldK) >= relatieve_rij(sterkeZijde, blockSq1)
        && verschillende_kleur(veldK, wbsq))
        return DRAW_FACTOR;
    else
        return NO_FACTOR;

  case 1:
    // Pawns on adjacent files. It's a draw if the defender firmly controls the
    // square in front of the frontmost pawn's path, and the square diagonally
    // behind this square on the file of the other pawn.
    if (   veldK == blockSq1
        && verschillende_kleur(veldK, wbsq)
        && (   bbsq == blockSq2
            || (pos.aanval_van<BISHOP>(blockSq2) & pos.stukken(zwakkeZijde, BISHOP))
            || rij_afstand(psq1, psq2) >= 2))
        return DRAW_FACTOR;

    else if (   veldK == blockSq2
             && verschillende_kleur(veldK, wbsq)
             && (   bbsq == blockSq1
                 || (pos.aanval_van<BISHOP>(blockSq1) & pos.stukken(zwakkeZijde, BISHOP))))
        return DRAW_FACTOR;
    else
        return NO_FACTOR;

  default:
    // The pawns are not on the same file or adjacent files. No scaling.
    return NO_FACTOR;
  }
}


/// KBP vs KN. There is a single rule: If the defending king is somewhere along
/// the path of the pawn, and the square of the king is not of the same color as
/// the stronger side's bishop, it's a draw.
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KBPKN(const Position& pos) 
{
  const Color zwakkeZijde = ~sterkeZijde;
  assert(tel_materiaal(pos, sterkeZijde, MATL_BISHOP, 1));
  assert(tel_materiaal(pos, zwakkeZijde, MATL_KNIGHT, 0));

  Square pawnSq = pos.stuk_veld(sterkeZijde, PAWN);
  Square strongBishopSq = pos.stuk_veld(sterkeZijde, BISHOP);
  Square weakKingSq = pos.koning(zwakkeZijde);

  if (   lijn(weakKingSq) == lijn(pawnSq)
      && relatieve_rij(sterkeZijde, pawnSq) < relatieve_rij(sterkeZijde, weakKingSq)
      && (   verschillende_kleur(weakKingSq, strongBishopSq)
          || relatieve_rij(sterkeZijde, weakKingSq) <= RANK_6))
      return DRAW_FACTOR;

  return NO_FACTOR;
}


/*
test posities
remise
k7/P7/2N5/8/8/8/8/7K w - - 0 1
K7/P2k4/8/8/8/8/7N/8 b - - 0 1
K7/P2k4/8/8/8/8/8/7N b - - 0 1
*/
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KNPK(const Position& pos) 
{
  const Color zwakkeZijde = ~sterkeZijde;
  assert(tel_materiaal(pos, sterkeZijde, MATL_KNIGHT, 1));
  assert(tel_materiaal(pos, zwakkeZijde, MATL_0, 0));

  // Assume sterkeZijde is white and the pawn is on files A-D
  Square witte_koning = normaliseer_pion_zijde(pos, sterkeZijde, pos.koning(sterkeZijde));
  Square paard = normaliseer_pion_zijde(pos, sterkeZijde, pos.stuk_veld(sterkeZijde, KNIGHT));
  Square pion = normaliseer_pion_zijde(pos, sterkeZijde, pos.stuk_veld(sterkeZijde, PAWN));
  Square zwarte_koning = normaliseer_pion_zijde(pos, sterkeZijde, pos.koning(zwakkeZijde));

  if (pion == SQ_A7 && afstand(SQ_A8, zwarte_koning) <= 1)
      return DRAW_FACTOR;

  if (pion == SQ_A7 && witte_koning == SQ_A8 &&
	  (zwarte_koning == SQ_C7 && (!(pos.aan_zet() == sterkeZijde) ^ !(~DarkSquares & paard)) ||
		  zwarte_koning == SQ_C8 && (!(pos.aan_zet() == sterkeZijde) ^ !(DarkSquares & paard))))
	  return DRAW_FACTOR;

  return NO_FACTOR;
}


/// KNP vs KB. If knight can block bishop from taking pawn, it's a win.
/// Otherwise the position is drawn.
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KNPKB(const Position& pos) 
{
  const Color zwakkeZijde = ~sterkeZijde;
  Square pawnSq = pos.stuk_veld(sterkeZijde, PAWN);
  Square bishopSq = pos.stuk_veld(zwakkeZijde, BISHOP);
  Square weakKingSq = pos.koning(zwakkeZijde);

  // King needs to get close to promoting pawn to prevent knight from blocking.
  // Rules for this are very tricky, so just approximate.
  if (bb_voorwaarts(sterkeZijde, pawnSq) & pos.aanval_van<BISHOP>(bishopSq))
      return ScaleFactor(2 * afstand(weakKingSq, pawnSq));

  return NO_FACTOR;
}


/// KP vs KP. This is done by removing the weakest side's pawn and probing the
/// KP vs K bitbase: If the weakest side has a draw without the pawn, it probably
/// has at least a draw with the pawn as well. The exception is when the stronger
/// side's pawn is far advanced and not on a rook file; in this case it is often
/// possible to win (e.g. 8/4k3/3p4/3P4/6K1/8/8/8 w - - 0 1).
template <Color sterkeZijde>
ScaleFactor endgameScaleFactor_KPKP(const Position& pos) 
{
  const Color zwakkeZijde = ~sterkeZijde;
  assert(tel_materiaal(pos, sterkeZijde, MATL_0, 1));
  assert(tel_materiaal(pos, zwakkeZijde, MATL_0, 1));

  // Assume sterkeZijde is white and the pawn is on files A-D
  Square wksq = normaliseer_pion_zijde(pos, sterkeZijde, pos.koning(sterkeZijde));
  Square bksq = normaliseer_pion_zijde(pos, sterkeZijde, pos.koning(zwakkeZijde));
  Square psq  = normaliseer_pion_zijde(pos, sterkeZijde, pos.stuk_veld(sterkeZijde, PAWN));

  Color us = sterkeZijde == pos.aan_zet() ? WHITE : BLACK;

  // If the pawn has advanced to the fifth rank or further, and is not a
  // rook pawn, it's too dangerous to assume that it's at least a draw.
  if (rij(psq) >= RANK_5 && lijn(psq) != FILE_A)
      return NO_FACTOR;

  // Probe the KPK bitbase with the weakest side's pawn removed. If it's a draw,
  // it's probably at least a draw even with the pawn.
  return Bitbases::probe(wksq, psq, bksq, us) ? NO_FACTOR : DRAW_FACTOR;
}


void Endgames::initialisatie()
{
	if (!mapWaarde.empty())
		return;

	waarde_aantal = factor_aantal = 0;

	// end games with complete evaluation
	waarde_functies[waarde_aantal++] = &endgameValue_KXK<WHITE>;    // index 0
	waarde_functies[waarde_aantal++] = &endgameValue_KXK<BLACK>;  // index 1
	//           KPNBRQ  KPNBRQ
	add_waarde("0110000 0100000", &endgameValue_KPK<WHITE>,  &endgameValue_KPK<BLACK>);
	add_waarde("0102000 0100000", &endgameValue_KNNK<WHITE>, &endgameValue_KNNK<BLACK>);
	add_waarde("0101100 0100000", &endgameValue_KBNK<WHITE>, &endgameValue_KBNK<BLACK>);
	add_waarde("0100001 0100010", &endgameValue_KQKR<WHITE>, &endgameValue_KQKR<BLACK>);
	//addValue("0100010 0101000", &endgameValue_KRKN<WHITE>, &endgameValue_KRKN<BLACK>);
	//addValue("0100010 0100100", &endgameValue_KRKB<WHITE>, &endgameValue_KRKB<BLACK>);

	// end games with scale factor
	factor_functies[factor_aantal++] = &endgameScaleFactor_KBPsK<WHITE>;     // index 0 KB and pawns vs K
	factor_functies[factor_aantal++] = &endgameScaleFactor_KBPsK<BLACK>;   // index 1
	factor_functies[factor_aantal++] = &endgameScaleFactor_KQKRPs<WHITE>;    // index 2 KQ vs KR and pawns
	factor_functies[factor_aantal++] = &endgameScaleFactor_KQKRPs<BLACK>;  // index 3
	factor_functies[factor_aantal++] = &endgameScaleFactor_KPsK<WHITE>;      // index 4 K and pawns vs K
	factor_functies[factor_aantal++] = &endgameScaleFactor_KPsK<BLACK>;    // index 5
	factor_functies[factor_aantal++] = &endgameScaleFactor_KPKP<WHITE>;      // index 6 KP vs KP
	factor_functies[factor_aantal++] = &endgameScaleFactor_KPKP<BLACK>;    // index 7
	//                 KPNBRQ  KPNBRQ
	add_schaalfactor("0100001 0110000", &endgameScaleFactor_KQKP<WHITE>,    &endgameScaleFactor_KQKP<BLACK>);
	add_schaalfactor("0100010 0110000", &endgameScaleFactor_KRKP<WHITE>,    &endgameScaleFactor_KRKP<BLACK>);
	add_schaalfactor("0110010 0100010", &endgameScaleFactor_KRPKR<WHITE>,   &endgameScaleFactor_KRPKR<BLACK>);
	add_schaalfactor("0120010 0110010", &endgameScaleFactor_KRPPKRP<WHITE>, &endgameScaleFactor_KRPPKRP<BLACK>);
	add_schaalfactor("0110100 0100100", &endgameScaleFactor_KBPKB<WHITE>,   &endgameScaleFactor_KBPKB<BLACK>);
	add_schaalfactor("0120100 0100100", &endgameScaleFactor_KBPPKB<WHITE>,  &endgameScaleFactor_KBPPKB<BLACK>);
	add_schaalfactor("0111000 0100000", &endgameScaleFactor_KNPK<WHITE>,    &endgameScaleFactor_KNPK<BLACK>);
	add_schaalfactor("0111000 0100100", &endgameScaleFactor_KNPKB<WHITE>,   &endgameScaleFactor_KNPKB<BLACK>);
	add_schaalfactor("0110100 0101000", &endgameScaleFactor_KBPKN<WHITE>,   &endgameScaleFactor_KBPKN<BLACK>);
	add_schaalfactor("0110010 0100100", &endgameScaleFactor_KRPKB<WHITE>,   &endgameScaleFactor_KRPKB<BLACK>);
}


template Value endgameValue_KXK<WHITE>(const Position& pos);
template Value endgameValue_KXK<BLACK>(const Position& pos);
template ScaleFactor endgameScaleFactor_KBPsK<WHITE>(const Position& pos);
template ScaleFactor endgameScaleFactor_KBPsK<BLACK>(const Position& pos);
template ScaleFactor endgameScaleFactor_KQKRPs<WHITE>(const Position& pos);
template ScaleFactor endgameScaleFactor_KQKRPs<BLACK>(const Position& pos);
template ScaleFactor endgameScaleFactor_KPsK<WHITE>(const Position& pos);
template ScaleFactor endgameScaleFactor_KPsK<BLACK>(const Position& pos);
template ScaleFactor endgameScaleFactor_KPKP<WHITE>(const Position& pos);
template ScaleFactor endgameScaleFactor_KPKP<BLACK>(const Position& pos);
