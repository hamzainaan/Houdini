/*
*/

#include "houdini.h"
#include "bitboard.h"
#include "eval_pawnstruct.h"
#include "position.h"
#include "search_smp.h"
#include "uci.h"


namespace 
{
	Bitboard FileBehind(Bitboard x)
	{
		x = x | (x >> 8);
		x = x | (x >> 16);
		x = x | (x >> 32);
		return x;
	}

	Bitboard FileInFront(Bitboard x)
	{
		x = x | (x << 8);
		x = x | (x << 16);
		x = x | (x << 32);
		return x;
	}

	Bitboard FileInFrontBehind(Bitboard x)
	{
		x = x | (x >> 8) | (x << 8);
		x = x | (x >> 16) | (x << 16);
		x = x | (x >> 32) | (x << 32);
		return x;
	}

	static const Bitboard CENTER_BOX = 0x00003C3C3C3C0000;
	static const Bitboard BOARD_EDGE = 0xFFFFC3C3C3C3FFFF;
	static const Bitboard FILE_AH = 0x8181818181818181;
	static const Bitboard NOT_FILE_AH = 0x7E7E7E7E7E7E7E7E;
	static const Bitboard NOT_FILE_A = 0xFEFEFEFEFEFEFEFE;
	static const Bitboard NOT_FILE_H = 0x7F7F7F7F7F7F7F7F;

	static const Bitboard RANKS_1_TO_4[2] = { 0x00000000FFFFFFFF, 0xFFFFFFFF00000000 };
	static const int PionSchild_Constanten[8] = { 0, 27, 22, 10, 5, 0, 0, 0 };
	static const int PionStorm_Constanten[8] = { -2, 0, 0, -6, -13, -40, 0, 0 };

#define S(mg, eg) make_score(mg, eg) * 4 / 16
	static const Score VrijPionWaarden4[8] =
	{
		S(0, 0), S(28, 47), S(28, 47), S(127, 188),
		S(325, 470), S(622, 893), S(1018, 1457), S(0, 0)
	};
#undef S
#define S(mg, eg) make_score(mg, eg) * 8 / 16
	static const Score VrijPionWaarden8[8] =
	{
		S(0, 0), S(28, 47), S(28, 47), S(127, 188),
		S(325, 470), S(622, 893), S(1018, 1457), S(0, 0)
	};
#undef S

	Score Score_K_Pion_afstand[17][8];
	EvalValue Score_PionSchild[8];
	EvalValue Score_PionBestorming[8];
	EvalValue Score_StormHalfOpenLijn[8];  // Score_PionBestorming + 1/8
	EvalValue Score_Aanvalslijnen[8];

	static const Bitboard AanvalsLijnen[FILE_N] =
	{
		FileABB | FileBBB | FileCBB,
		FileABB | FileBBB | FileCBB,
		FileBBB | FileCBB | FileDBB,
		FileCBB | FileDBB | FileEBB,
		FileDBB | FileEBB | FileFBB,
		FileEBB | FileFBB | FileGBB,
		FileFBB | FileGBB | FileHBB,
		FileFBB | FileGBB | FileHBB
	};

	template <Color Us>
	EvalValue safety_on_square(const Position& pos, const Pawns::Tuple* e, Square veldK)
	{
		const Color Them = Us == WHITE ? BLACK : WHITE;
		EvalValue result = EVAL_0;

		// pionnenschild voor koning
		Bitboard UsPawns = pos.pieces(Us, PAWN);
		Bitboard lijnen_rond_K = AanvalsLijnen[lijn(veldK)];

		Bitboard bb_schild = UsPawns & lijnen_rond_K & ~bb_ranks_forward(Them, rij(veldK));
		while (bb_schild)
		{
			Square veld = pop_lsb(&bb_schild);
			EvalValue v = Score_PionSchild[relative_rank(Us, veld)];
			if (lijn(veld) == lijn(veldK))
				result += v;
			result += v;
		}

		// bestorming, het veld voor de koning telt niet mee (hier dient de aanvallende pion als schild!)
		Bitboard bb_storm = pos.pieces(Them, PAWN) & lijnen_rond_K;
		//Bord bb_storm = pos.stukken(JIJ, PION) & lijnen_rond_K & ~shift_up<Us>(pos.stukken(Us, KONING));
		while (bb_storm)
		{
			Square veld = pop_lsb(&bb_storm);
			if (bb_forward(Them, veld) & UsPawns)
				result += Score_PionBestorming[relative_rank(Them, veld)];
			else
				result += Score_StormHalfOpenLijn[relative_rank(Them, veld)];
		}
		// aantal halfopen lijnen voor aanval
		result += Score_Aanvalslijnen[popcount(lijnen_rond_K & e->halfOpenLijnen[Them])];

		return result;
	}

#if 0
	template <Color Us>
	Score evaluatie(const Position& pos, Pawns::Tuple* e, const Bitboard bb_pionlijnen[2])
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);

		const Bitboard bb_mijnPion = pos.pieces(Us, PAWN);
		const Bitboard bb_jouwPion = pos.pieces(Them, PAWN);
		const Bitboard RANK56 = Us == WHITE ? 0x0000FFFF00000000 : 0x00000000FFFF0000;
		const Bitboard TweedeRij = Us == WHITE ? 0x000000000000FF00 : 0x00FF000000000000;
		const Square Up = Us == WHITE ? UP : DOWN;

		Score result;

		e->pionnenOpKleur[Us][BLACK] = popcount(bb_mijnPion & DarkSquares);
		e->pionnenOpKleur[Us][WHITE] = pos.aantal(Us, PAWN) - e->pionnenOpKleur[Us][BLACK];
		e->halfOpenLijnen[Us] = (int)~bb_pionlijnen[Us] & 0xFF;
		Bitboard b = e->halfOpenLijnen[Us] ^ 0xFF;
		e->pionBereik[Us] = b ? int(msb(b) - lsb(b)) : 0;

		Bitboard BuurVelden = ((bb_mijnPion << 1) & NOT_FILE_A) | ((bb_mijnPion >> 1) & NOT_FILE_H);
		Bitboard GeisoleerdeLijnen = ~((bb_pionlijnen[Us] << 1) & NOT_FILE_A | (bb_pionlijnen[Us] >> 1) & NOT_FILE_H);

		// pionaanval op rij 5 of 6
		result = make_score(89, 78) * popcount(bb_mijnPion & RANK56 & e->pionAanval[Them]);

		// KwetsbaarVoorToren = aangevallen door vijandelijke toren op de 8ste rij
		Bitboard KwetsbaarVoorToren = Us == WHITE ? ~FileBehind(bb_jouwPion | (bb_mijnPion >> 8)) : ~FileInFront(bb_jouwPion | (bb_mijnPion << 8));

		// dubbelpionnen (voorste op de lijn)
		Bitboard bb_dubbelpionnen = Us == WHITE ? bb_mijnPion & ~FileBehind(bb_mijnPion >> 8) & FileInFront(bb_mijnPion << 8) :
			bb_mijnPion & ~FileInFront(bb_mijnPion << 8) & FileBehind(bb_mijnPion >> 8);

		result += (popcount(bb_mijnPion) - popcount(bb_pionlijnen[Us] & 0xFF)) * make_score(-42, -65);    // dubbelpionnen
		result += popcount(bb_dubbelpionnen & ~e->pionAanval[Us]) * make_score(-36, -50); // niet gedekte dubbelpionnen

		if (bb_mijnPion & GeisoleerdeLijnen)
		{
			result += popcount(bb_mijnPion & GeisoleerdeLijnen & ~bb_pionlijnen[Them] & NOT_FILE_AH) * make_score(-121, -136);  // geisoleerde pion op halfopen lijn, niet aan de rand
			result += popcount(bb_mijnPion & GeisoleerdeLijnen & ~bb_pionlijnen[Them] & FILE_AH) * make_score(-75, -87);  // geisoleerde randpion op halfopen lijn
			result += popcount(bb_mijnPion & GeisoleerdeLijnen & bb_pionlijnen[Them] & NOT_FILE_AH) * make_score(-34, -40);  // geisoleerde pion niet op halfopen lijn, niet aan de rand
			result += popcount(bb_mijnPion & GeisoleerdeLijnen & bb_pionlijnen[Them] & FILE_AH) * make_score(-31, -26);  // geisoleerde randpion niet op halfopen lijn
		}

		if (bb_dubbelpionnen & GeisoleerdeLijnen)
		{
			result += popcount(bb_dubbelpionnen & GeisoleerdeLijnen & ~bb_pionlijnen[Them] & NOT_FILE_AH) * make_score(-24, -47);  // geisoleerde dubbelpion op halfopen lijn, niet aan de rand
			result += popcount(bb_dubbelpionnen & GeisoleerdeLijnen & ~bb_pionlijnen[Them] & FILE_AH) * make_score(-18, -42);  // geisoleerde dubbelpion op halfopen lijn, aan de rand
			result += popcount(bb_dubbelpionnen & GeisoleerdeLijnen & bb_pionlijnen[Them] & NOT_FILE_AH) * make_score(-14, -8);  // geisoleerde dubbelpion niet op halfopen lijn, niet aan de rand
			result += popcount(bb_dubbelpionnen & GeisoleerdeLijnen & bb_pionlijnen[Them] & FILE_AH) * make_score(-10, -5);  // geisoleerde dubbelpion niet op halfopen lijn, aan de rand
		}

		result += popcount(e->pionAanval[Us] & CENTER_BOX) * make_score(9, 11);  // controle van centrum
		result += popcount(e->pionAanval[Us] & BOARD_EDGE) * make_score(6, 8);  // controle van rest van het bord

		result += popcount(bb_mijnPion & BuurVelden & e->pionAanval[Us]) * make_score(2, -5);  // pion ernaast en verdedigd
		result += popcount(bb_mijnPion & e->pionAanval[Us]) * make_score(48, 26);  // pion verdedigd
		result += popcount(bb_mijnPion & BuurVelden) * make_score(22, 23);  // pion ernaast


		// het volgende lijkt heel bizar - houdt er ook geen rekening mee of de hangende pion gedekt is of niet
		// de versie hieronder rekent trouwens enkel randpionnen in (door de vermoedelijk ontbrekende code)
		/*
		Bord bb_hangend = RIJEN_1_TOT_4[Us] & bb_mijnPion & BuurVelden & ~bb_pionlijnen[JIJ];// hangende pionnen op halfopen lijnen op rij 1 tot 4
		while (bb_hangend)
		{
			Square veld = pop_lsb(&bb_hangend);

			Bord bb_hangende_pion = bbVeld[veld];
			Bord buren = (bb_hangende_pion >> 1) & NIET_LIJN_H | (bb_hangende_pion << 1) & NIET_LIJN_A;
			// hier ontbreekt de volgende lijn ?!?
			//buren &= bb_mijnPion;
			if (buren && !meer_dan_een(buren))  // enkel als popcount == 1, dus een pion naast één enkele andere pion
			{
				Bord blokkade = Us == WHITE ? bb_jouwPion & (buren << 16) : bb_jouwPion & (buren >> 16);
				if (!blokkade || meer_dan_een(blokkade))  // 0 of 2 pionnen die opmars hangende pion stoppen
					result -= maak_score(1, 50);
				else // 1 pion stopt opmars hangende pion
					result += maak_score(-1, -25);
			}
		}
		*/

		// misschien toevoegen: een pion die een vijandelijke pion aanvalt (= door een vijandelijke pion aangevallen wordt) is niet achtergebleven
		Bitboard bb_falanx = e->pionAanval[Us] | BuurVelden;
		Bitboard bb_achtergebleven = bb_mijnPion & e->veiligVoorPion[Us] & ~GeisoleerdeLijnen;
		Bitboard bb_keten = e->pionAanval[Us] | BuurVelden & ~bb_dubbelpionnen;  // laatste conditie is gek - voorste dubbelpion is achtergebleven zelfs als er een buur is?
		// misschien omdat pion niet langs achter kan gedekt worden, en de pion pushen na ruil een geïsoleerde dubbelpion oplevert
		Bitboard bb_echt_achtergebleven = bb_achtergebleven & ~bb_keten;
		Bitboard bb_velden_voor_achtergebleven = shift_bb<Up>(bb_echt_achtergebleven);

		result += popcount(bb_velden_voor_achtergebleven & KwetsbaarVoorToren & e->pionAanval[Them]) * make_score(-55, -86);   // achtergebleven halfopen lijn, pionaanval stopt opmars
		result += popcount(bb_velden_voor_achtergebleven & (pos.pieces(PAWN) | e->pionAanval[Them]) & NOT_FILE_AH) * make_score(-30, -32);  // achtergebleven, pion of pionaanval, niet aan de rand
		result += popcount(bb_velden_voor_achtergebleven & (pos.pieces(PAWN) | e->pionAanval[Them]) & FILE_AH) * make_score(-26, -25);  // achtergebleven, pion of pionaanval, aan de rand

		Bitboard bb_begin_blocked = e->pionAanval[Them] & shift_bb<Up>(bb_mijnPion & TweedeRij);
		result += popcount(bb_begin_blocked) * make_score(-11, -12); // geblokkeerde pionnen op beginpositie


		// vrijpionnen
		// ===========
		Bitboard bb_vrijpionnen = bb_mijnPion & e->veiligVoorPion[Them] & KwetsbaarVoorToren;
		Bitboard bb_gedekt_vrijpion = bb_vrijpionnen & bb_falanx;
		while (bb_gedekt_vrijpion)
		{
			Square veld = pop_lsb(&bb_gedekt_vrijpion);
			result += 4 * VrijPionWaarden[relative_rank(Us, veld)] / 16; // gedekte vrijpion
		}

		e->vrijPionnen[Us] = bb_vrijpionnen;
		if (more_than_one(bb_vrijpionnen))
			result += make_score(33, 128);

		// potentiele vrijpionnen
		// voorste pionnen in falanx op halfopen lijn
		Bitboard voorste_pionnen = Us == WHITE ? bb_mijnPion & ~FileBehind(bb_mijnPion >> 8) : bb_mijnPion & ~FileInFront(bb_mijnPion << 8);
		Bitboard bb_potentieel = voorste_pionnen & bb_falanx & KwetsbaarVoorToren & ~bb_vrijpionnen;
		while (bb_potentieel)
		{
			Square veld = pop_lsb(&bb_potentieel);
			Bitboard bb_actief = voorste_pionnen; // toevoegen: & ~bb_echt_achtergebleven  // achtergebleven pionnen kunnen potentiële vrijpion niet helpen
			if (lijn(veld) != FILE_A)
			{
				Square veld_links = Square(veld - 1);
				if (bbVeld[veld_links] & bb_jouwPion)
					bb_actief &= ~bb_file(veld_links);
			}
			if (lijn(veld) != FILE_H)
			{
				Square veld_rechts = Square(veld + 1);
				if (bbVeld[veld_rechts] & bb_jouwPion)
					bb_actief &= ~bb_file(veld_rechts);
			}

			if (popcount(bb_adjacent_files(lijn(veld)) & ~bb_ranks_forward(Us, rij(veld)) & bb_actief) >= 
				popcount(bb_adjacent_files(lijn(veld)) & bb_ranks_forward(Us, rij(veld)) & bb_jouwPion))
				result += 11 * VrijPionWaarden[relative_rank(Us, veld)] / 16;  // potentiele vrijpionnen
		}

		/* contempt stuff
		if (UCI_Contempt > 0 && __popcnt(WPion | ZPion) > 14)
		pionScore[Root_Kleur] -= SCORE32(UCI_Contempt, 0);

		geblokkeerde_pionnen = __popcnt(ZPion & (WPion << 8)) - 1;
		if (geblokkeerde_pionnen > 0)
		pionScore[Root_Kleur] -= SCORE32(5 * UCI_Contempt * geblokkeerde_pionnen, 0);
		*/

		//entry->conversie_is_geschat = 1;

		return result;
	}
#endif

#define V(x) EvalValue(x)
	// Weakness of our pawn shelter in front of the king by [distance from edge][rank]
	//    rij=0 is zonder pion
	//     rij      2      3      4      5       6       7
	const EvalValue ShelterWeakness[][RANK_N] = {
		{ V( 97), V(21), V(26), V(51), V(87), V( 89), V( 99) },    // h/a-lijn
		{ V(120), V( 0), V(28), V(76), V(88), V(103), V(104) },    // g/b-lijn
		{ V(101), V( 7), V(54), V(78), V(77), V( 92), V(101) },    // f/c-lijn
		{ V( 80), V(11), V(44), V(68), V(87), V( 90), V(119) } };  // e/d-lijn

	// Danger of enemy pawns moving toward our king by [type][distance from edge][rank]
	// type= { GeenEigenPion, KanBewegen, AfgestoptDoorPion, AfgestoptDoorKoning };
	const EvalValue StormDanger[][4][RANK_N] = {
		{ { V( 0),  V(67), V(134), V(38), V(32) },     // halfopen lijn
		  { V( 0),  V(57), V(139), V(37), V(22) },
		  { V( 0),  V(43), V(115), V(43), V(27) },
		  { V( 0),  V(68), V(124), V(57), V(32) } },
		{ { V(20),  V(43), V(100), V(56), V(20) },     // beweeglijke pion, Rij=0 is geen pion
		  { V(23),  V(20), V( 98), V(40), V(15) },
		  { V(23),  V(39), V(103), V(36), V(18) },
		  { V(28),  V(19), V(108), V(42), V(26) } },
		{ { V( 0),  V( 0), V( 75), V(14), V( 2) },     // vastgelopen pion
		  { V( 0),  V( 0), V(150), V(30), V( 4) },
		  { V( 0),  V( 0), V(160), V(22), V( 5) },
		  { V( 0),  V( 0), V(166), V(24), V(13) } },
		{ { V( 0),  V(-283), V(-281), V(57), V(31) },   // Kh2/h3 voor een vijandelijke pion!
		  { V( 0),  V(58), V(141), V(39), V(18) },
		  { V( 0),  V(65), V(142), V(48), V(32) },
		  { V( 0),  V(60), V(126), V(51), V(19) } } };

	// Max bonus for king safety. Corresponds to start position with all the pawns
	// in front of the king and no enemy pawn on the horizon.
	const EvalValue MaxSafetyBonus = V(258);
#undef V

#define pawn_make_score(mg, eg) make_score((87 * 177 * mg + 6 * 142 * eg) / (93 * 64), (-13 * 177 * mg + 106 * 142 * eg) / (93 * 64))
#define S(mg, eg) pawn_make_score(mg, eg)

	const Score DubbelPion = S(22, 48);
	const Score DubbelPionAfstand[6] = { SCORE_ZERO, DubbelPion, DubbelPion / 2, DubbelPion / 3, DubbelPion / 4, DubbelPion / 5 };
	const Score DubbelPionScore[FILE_N] =
	{
		S(13, 43), S(20, 48), S(23, 48), S(23, 48),
		S(23, 48), S(23, 48), S(20, 48), S(13, 43)
	};
	const Score DubbelPionScoreAfstand[FILE_N][8] =
	{
		{ SCORE_ZERO, DubbelPionScore[0], DubbelPionScore[0] / 2, DubbelPionScore[0] / 3, DubbelPionScore[0] / 4, DubbelPionScore[0] / 5 },
		{ SCORE_ZERO, DubbelPionScore[1], DubbelPionScore[1] / 2, DubbelPionScore[1] / 3, DubbelPionScore[1] / 4, DubbelPionScore[1] / 5 },
		{ SCORE_ZERO, DubbelPionScore[2], DubbelPionScore[2] / 2, DubbelPionScore[2] / 3, DubbelPionScore[2] / 4, DubbelPionScore[2] / 5 },
		{ SCORE_ZERO, DubbelPionScore[3], DubbelPionScore[3] / 2, DubbelPionScore[3] / 3, DubbelPionScore[3] / 4, DubbelPionScore[3] / 5 },
		{ SCORE_ZERO, DubbelPionScore[3], DubbelPionScore[3] / 2, DubbelPionScore[3] / 3, DubbelPionScore[3] / 4, DubbelPionScore[3] / 5 },
		{ SCORE_ZERO, DubbelPionScore[2], DubbelPionScore[2] / 2, DubbelPionScore[2] / 3, DubbelPionScore[2] / 4, DubbelPionScore[2] / 5 },
		{ SCORE_ZERO, DubbelPionScore[1], DubbelPionScore[1] / 2, DubbelPionScore[1] / 3, DubbelPionScore[1] / 4, DubbelPionScore[1] / 5 },
		{ SCORE_ZERO, DubbelPionScore[0], DubbelPionScore[0] / 2, DubbelPionScore[0] / 3, DubbelPionScore[0] / 4, DubbelPionScore[0] / 5 }
	};

	const Score Isolani[2] = { S(54, 50), S(36, 34) };
	const Score IsolaniScore[2][FILE_N] =
	{
		{ S(37, 45), S(54, 52), S(60, 52), S(60, 52),
		  S(60, 52), S(60, 52), S(54, 52), S(37, 45) },
		{ S(25, 30), S(36, 35), S(40, 35), S(40, 35),
		  S(40, 35), S(40, 35), S(36, 35), S(25, 30) }
	};

	const Score AchtergeblevenScore[2] = { S(67, 42), S(49, 24) };

	const Score PionAanvallerScore[RANK_N] =
	{
		S(0, 0), S(0, 0), S(0, 0), S(0, 0),
		S(20,20), S(40,40), S(0, 0), S(0, 0)
	};

#undef S

	Score KetenScore[2][2][3][RANK_N];

	const Score NietOndersteundScore[2] = 
	{ 
		make_score(79, 24), make_score(53, 17)
	};

	template<Color Us>
	Score sf_evaluate(const Position& pos, Pawns::Tuple* e)
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);
		const Bitboard TweedeRij = Us == WHITE ? Rank2BB : Rank7BB;
		const Bitboard CenterBindMask = (FileDBB | FileEBB) &
			(Us == WHITE ? (Rank5BB | Rank6BB | Rank7BB) : (Rank4BB | Rank3BB | Rank2BB));

		Square veld;
		Score score = SCORE_ZERO;
		const Square* pVeld = pos.piece_list(Us, PAWN);

		Bitboard mijnPionnen = pos.pieces(Us, PAWN);
		Bitboard jouwPionnen = pos.pieces(Them, PAWN);

		e->vrijPionnen[Us] = 0;
		e->koningVeld[Us] = SQ_NONE;
		e->pionnenOpKleur[Us][BLACK] = popcount(mijnPionnen & DarkSquares);
		e->pionnenOpKleur[Us][WHITE] = pos.aantal(Us, PAWN) - e->pionnenOpKleur[Us][BLACK];

		while ((veld = *pVeld++) != SQ_NONE)
		{
			File f = lijn(veld);

			Bitboard buurPionnen = mijnPionnen & bb_adjacent_files(f);
			Bitboard dubbelPionnen = mijnPionnen & bb_forward(Us, veld);
			//bool dubbelPion = mijnPionnen & veld_achter(IK, veld);
			bool geslotenLijn = jouwPionnen & bb_forward(Us, veld);
			Bitboard stoppers = jouwPionnen & passed_pawn_mask(Us, veld);
			Bitboard aanvallers = jouwPionnen & PionAanval[Us][veld];
			Bitboard aanvallersPush = jouwPionnen & PionAanval[Us][square_in_front(Us, veld)];
			Bitboard falanks = buurPionnen & bb_rank(veld);
			Bitboard ondersteund = buurPionnen & bb_rank(square_behind(Us, veld));
			bool keten = ondersteund | falanks;
			bool isolani = !buurPionnen;
			bool achtergebleven;

			// aanvaller 6.5%, isolani 15.9%, keten 60.9%, stoppers 95.5%, dubbelPionnen 5.8 %

			if ((isolani | keten) 
				|| aanvallers
				|| (mijnPionnen & pawn_attack_range(Them, veld))
				|| (relative_rank(Us, veld) >= RANK_5))
				achtergebleven = false;
			else
			{
				Bitboard b = pawn_attack_range(Us, veld) & (mijnPionnen | jouwPionnen);
				b = pawn_attack_range(Us, veld) & bb_rank(rearmost_square(Us, b));
				achtergebleven = (b | shift_up<Us>(b)) & jouwPionnen;
			}

			if (!stoppers && !dubbelPionnen)
			{
				e->vrijPionnen[Us] |= veld;
				if (keten)
					score += VrijPionWaarden4[relative_rank(Us, veld)]; // gedekte vrijpion
			}
			else if (!(stoppers ^ aanvallers ^ aanvallersPush)
				&& !dubbelPionnen
				&& popcount(ondersteund) >= popcount(aanvallers)
				&& popcount(falanks) >= popcount(aanvallersPush))
			{
				score += VrijPionWaarden8[relative_rank(Us, veld)]; // kandidaat vrijpion
			}

			if (keten) // 60.9 %, geslotenLijn 79.2 %, falanks 73.6 %, rijverdeling 62% 24% 11% 3% 0.3%
				score += KetenScore[geslotenLijn][falanks != 0][popcount(ondersteund)][relative_rank(Us, veld)];

			else if (isolani) // 15.8 % waarvan 42% randpion, 58% gesloten lijn
				score -= IsolaniScore[geslotenLijn][f];
				//score -= Isolani[geslotenLijn];

			else if (achtergebleven) // 3.1 % waarvan 56% gesloten lijn
				score -= AchtergeblevenScore[geslotenLijn];

			else  // 20% niet ondersteund, geslotenLijn 70%
				score -= NietOndersteundScore[geslotenLijn];

			if (dubbelPionnen) // 5.8 % waarvan 6% randpion
				score -= DubbelPionScoreAfstand[f][rank_distance(veld, frontmost_square(Us, dubbelPionnen))];
			//if (dubbelPion && ~ondersteund)
			//	score -= DubbelPion;

			if (aanvallers) // 6.5 %
				score += PionAanvallerScore[relative_rank(Us, veld)];
		}

		Bitboard b = e->halfOpenLijnen[Us] ^ 0xFF;
		e->pionBereik[Us] = b ? int(msb(b) - lsb(b)) : 0;

		const Score Score_Centrum_Bind = make_score(41, -6);
		b = shift_upleft<Us>(mijnPionnen) & shift_upright<Us>(mijnPionnen) & CenterBindMask;
		score += Score_Centrum_Bind * popcount(b);

		const Score Score_Meerdere_Vrijpionnen = make_score(33, 128);
		if (more_than_one(e->vrijPionnen[Us]))
			score += Score_Meerdere_Vrijpionnen;

		const Score Score_Tweede_Rij_Vast = make_score(11, 12);
		Bitboard bb_begin_blocked = e->pionAanval[Them] & shift_up<Us>(mijnPionnen & TweedeRij);
		score -= Score_Tweede_Rij_Vast * popcount(bb_begin_blocked); // geblokkeerde pionnen op beginpositie

		return score;
	}
} // namespace

namespace Pawns 
{
	const int Seed[RANK_N]        = { 0,  0, 15, 10, 57,  75, 135, 0 };
	const int falanksSeed[RANK_N] = { 0, 10, 13, 33, 66, 105, 196, 0 };

	void initialize()
	{
		for (int n = 0; n < 17; n++)
			for (int distance = 0; distance < 8; distance++)
				Score_K_Pion_afstand[n][distance] = make_score(0, (int)floor(sqrt((double)n) * 5.0 * distance));

		for (int n = 0; n < 8; n++)
		{
			Score_PionSchild[n] = EvalValue(8 * PionSchild_Constanten[n]);
			Score_PionBestorming[n] = EvalValue(8 * PionStorm_Constanten[n]);
			Score_StormHalfOpenLijn[n] = EvalValue(9 * PionStorm_Constanten[n]);  // Score_PionBestorming + 1/8
			Score_Aanvalslijnen[n] = n * Score_PionBestorming[0];
		}

		const Score Score_Niet_Ondersteund = make_score(53, 17);
		//const Score Score_Apex = maak_score(52, 25);

		for (int geslotenLijn = 0; geslotenLijn <= 1; ++geslotenLijn)
			for (int falanks = 0; falanks <= 1; ++falanks)
				for (int punt = 0; punt <= 2; ++punt)
					for (Rank rij = RANK_2; rij < RANK_8; ++rij)
					{
						int v = (falanks ? falanksSeed[rij] : Seed[rij]) >> geslotenLijn;
						v += (punt == 2 ? 13 : 0);
						KetenScore[geslotenLijn][falanks][punt][rij] = pawn_make_score(3 * v / 2, v)
							//+ (punt == 2 ? Score_Apex : SCORE_ZERO)
							- (punt == 0 ? Score_Niet_Ondersteund : SCORE_ZERO);
					}
	}


	Tuple* probe(const Position& pos)
	{
		Key64 sleutel = pos.pawn_key();
		Tuple* e = pos.ti()->pionnenTabel[sleutel];

		if (e->sleutel64 == sleutel)
			return e;

		e->sleutel64 = sleutel;

		Bitboard bb_pionlijnen[2];

		// bereken pionstelling
		// ====================
		Bitboard WPion = pos.pieces(WHITE, PAWN);
		Bitboard ZPion = pos.pieces(BLACK, PAWN);

		bb_pionlijnen[WHITE] = FileInFrontBehind(WPion);
		bb_pionlijnen[BLACK] = FileInFrontBehind(ZPion);

		e->pionAanval[WHITE] = pion_aanval<WHITE>(WPion);
		e->pionAanval[BLACK] = pion_aanval<BLACK>(ZPion);

		e->veiligVoorPion[WHITE] = ~FileInFront(e->pionAanval[WHITE]);
		e->veiligVoorPion[BLACK] = ~FileBehind(e->pionAanval[BLACK]);

		e->halfOpenLijnen[WHITE] = (int)~bb_pionlijnen[WHITE] & 0xFF;
		e->halfOpenLijnen[BLACK] = (int)~bb_pionlijnen[BLACK] & 0xFF;

		e->asymmetrie = popcount(e->halfOpenLijnen[WHITE] ^ e->halfOpenLijnen[BLACK]);
		//e->open_lijnen = popcount(e->halfOpenLijnen[WIT] & e->halfOpenLijnen[ZWART]);

#if 0
		e->score = evaluatie<WHITE>(pos, e, bb_pionlijnen);
		e->score -= evaluatie<BLACK>(pos, e, bb_pionlijnen);
#else
		e->score = sf_evaluate<WHITE>(pos, e);
		e->score -= sf_evaluate<BLACK>(pos, e);
#endif

		int lijnen = (bb_pionlijnen[WHITE] | bb_pionlijnen[BLACK]) & 0xFF;
		if (lijnen)
			lijnen = msb(lijnen) - lsb(lijnen);
		// waarde tussen 0 en 4
		e->lijn_breedte = std::max(lijnen - 3, 0);

		// geen enkele open lijn -> conversie moeilijk
		// centrale lijn(en) open, geen halfopen lijnen -> conversie moeilijk
		e->conversie_moeilijk = !(e->halfOpenLijnen[WHITE] & e->halfOpenLijnen[BLACK])
			|| ((e->halfOpenLijnen[WHITE] & e->halfOpenLijnen[BLACK] & 0x3c) && !e->asymmetrie);

		// bereken de gemiddelde lijn van pionnen
		Bitboard bb_pionnen = pos.pieces(PAWN);
		e->nPionnen = popcount(bb_pionnen);
		if (bb_pionnen)
		{
			int LijnSom = 0;
			do
			{
				Square veld = pop_lsb(&bb_pionnen);
				LijnSom += veld & 7;
			} while (bb_pionnen);
			e->gemiddeldeLijn = LijnSom / e->nPionnen;
		}

		return e;
	}

#if 1
	const int FILE_FACTOR_MULT = 64;
	const int schild_factor[3] = { 74, 69, 64 };
	const int storm_factor[3] = { 64, 48, 64 };

	template<Color Us>
	EvalValue sf_shelter_storm(const Position& pos, Square veldK) {

		const Color Them = (Us == WHITE ? BLACK : WHITE);

		enum { NoOwnPawn, CanMove, StoppedByPawn, StoppedByKing };

		Bitboard b = bb_ranks_forward(Us, rij(veldK)) | bb_rank(veldK);
		Bitboard mijnPionnen = b & pos.pieces(Us, PAWN);
		Bitboard jouwPionnen = b & pos.pieces(Them, PAWN);
		EvalValue gevaar = EVAL_0;
		File center = std::max(FILE_B, std::min(FILE_G, lijn(veldK)));

		for (File f = center - File(1); f <= center + File(1); ++f)
		{
			b = mijnPionnen & bb_file(f);
			Rank mijnRij = b ? relative_rank(Us, rearmost_square(Us, b)) : RANK_1;

			b = jouwPionnen & bb_file(f);
			Rank jouwRij = b ? relative_rank(Us, frontmost_square(Them, b)) : RANK_1;

			gevaar += ShelterWeakness[std::min(f, FILE_H - f)][mijnRij] * schild_factor[std::abs(f - lijn(veldK))];

			int ttype = 
				(f == lijn(veldK) && jouwRij == relative_rank(Us, veldK) + 1) ? StoppedByKing :
				(mijnRij == RANK_1) ? NoOwnPawn :
				(jouwRij == mijnRij + 1) ? StoppedByPawn :
				CanMove;

			gevaar += StormDanger[ttype][std::min(f, FILE_H - f)][jouwRij] * storm_factor[std::abs(f - lijn(veldK))];
		}
		gevaar /= FILE_FACTOR_MULT;

		return EvalValue(100 + MaxSafetyBonus * 3 - gevaar * 3);
	}
#endif


	template<Color Us>
	Score Tuple::compute_king_safety(const Position& pos)
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);

		Square veldK = pos.koning(Us);
		koningVeld[Us] = veldK;
		rokadeMogelijkheden[Us] = pos.castling_rights(Us);

		EvalValue veilig_bonus = sf_shelter_storm<Us>(pos, veldK);

		if (pos.castling_possible(Us == WHITE ? WHITE_SHORT : BLACK_SHORT))
			veilig_bonus = std::max(veilig_bonus, sf_shelter_storm<Us>(pos, relative_square(Us, SQ_G1)));
		if (pos.castling_possible(Us == WHITE ? WHITE_LONG : BLACK_LONG))
			veilig_bonus = std::max(veilig_bonus, sf_shelter_storm<Us>(pos, relative_square(Us, SQ_C1)));

		const Bitboard THIRD_FOURTH_RANK = Us == WHITE ? Rank3BB | Rank4BB : Rank6BB | Rank5BB;
		const Bitboard FIFTH_RANK = Us == WHITE ? Rank5BB : Rank4BB;

		Bitboard aanvallende_pionnen = AanvalsLijnen[lijn(veldK)] & pos.pieces(Them, PAWN);

		safety[Us] = veilig_bonus / 220 * 8
			- 16 * popcount(aanvallende_pionnen & THIRD_FOURTH_RANK)    // vijandelijke pionnen op onze 3de rij
			- 8 * popcount(aanvallende_pionnen & FIFTH_RANK);         // vijandelijke pionnen op onze 5de rij

		Score result = make_score(veilig_bonus, 0);

		// K op eerste rij achter 3 pionnen -> malus voor mat achter de paaltjes
		Bitboard bb_Koning = pos.pieces(Us, KING);
		const Bitboard FIRST_RANK = Us == WHITE ? Rank1BB : Rank8BB;

		if (bb_Koning & FIRST_RANK)
		{
			Bitboard bb = shift_up<Us>(bb_Koning) | shift_upleft<Us>(bb_Koning) | shift_upright<Us>(bb_Koning);
			if (bb == (pos.pieces(Us, PAWN) & bb))
				result += make_score(-63, -173);
		}

		// koning voor vijandelijke pion?
		if (pos.pieces(Them, PAWN) & shift_down<Us>(bb_Koning))
			result += make_score(0, 27);

		if (nPionnen)
			result -= Score_K_Pion_afstand[nPionnen][abs(gemiddeldeLijn - lijn(pos.koning(Us)))];

		return result;
	}

	template Score Tuple::compute_king_safety<WHITE>(const Position& pos);
	template Score Tuple::compute_king_safety<BLACK>(const Position& pos);
} // namespace
