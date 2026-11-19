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
	static const int PawnShield_Constants[8] = { 0, 27, 22, 10, 5, 0, 0, 0 };
	static const int PawnStorm_Constants[8] = { -2, 0, 0, -6, -13, -40, 0, 0 };

#define S(mg, eg) make_score(mg, eg) * 4 / 16
	static const Score PassedPawnValues4[8] =
	{
		S(0, 0), S(28, 47), S(28, 47), S(127, 188),
		S(325, 470), S(622, 893), S(1018, 1457), S(0, 0)
	};
#undef S
#define S(mg, eg) make_score(mg, eg) * 8 / 16
	static const Score PassedPawnValues8[8] =
	{
		S(0, 0), S(28, 47), S(28, 47), S(127, 188),
		S(325, 470), S(622, 893), S(1018, 1457), S(0, 0)
	};
#undef S

	Score Score_K_Pawn_distance[17][8];
	EvalValue Score_PawnShield[8];
	EvalValue Score_PawnStorming[8];
	EvalValue Score_StormHalfOpenFile[8];  // Score_PionBestorming + 1/8
	EvalValue Score_AttackFiles[8];

	static const Bitboard AttackFiles[FILE_N] =
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
	EvalValue safety_on_square(const Position& pos, const Pawns::Tuple* e, Square squareK)
	{
		const Color Them = Us == WHITE ? BLACK : WHITE;
		EvalValue result = EVAL_0;

		// pionnenschild voor koning
		Bitboard UsPawns = pos.pieces(Us, PAWN);
		Bitboard files_around_K = AttackFiles[file_of(squareK)];

		Bitboard bb_shield = UsPawns & files_around_K & ~bb_ranks_forward(Them, rank_of(squareK));
		while (bb_shield)
		{
			Square square = pop_lsb(&bb_shield);
			EvalValue v = Score_PawnShield[relative_rank(Us, square)];
			if (file_of(square) == file_of(squareK))
				result += v;
			result += v;
		}

		// bestorming, het veld voor de koning telt niet mee (hier dient de aanvallende pion als schild!)
		Bitboard bb_storm = pos.pieces(Them, PAWN) & files_around_K;
		//Bord bb_storm = pos.stukken(JIJ, PION) & lijnen_rond_K & ~shift_up<Us>(pos.stukken(Us, KONING));
		while (bb_storm)
		{
			Square square = pop_lsb(&bb_storm);
			if (bb_forward(Them, square) & UsPawns)
				result += Score_PawnStorming[relative_rank(Them, square)];
			else
				result += Score_StormHalfOpenFile[relative_rank(Them, square)];
		}
		// aantal halfopen lijnen voor aanval
		result += Score_AttackFiles[popcount(files_around_K & e->halfOpenFiles[Them])];

		return result;
	}

#if 0
	template <Color Us>
	Score evaluation(const Position& pos, Pawns::Tuple* e, const Bitboard bb_pawn_files[2])
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);

		const Bitboard bb_ourPawn = pos.pieces(Us, PAWN);
		const Bitboard bb_theirPawn = pos.pieces(Them, PAWN);
		const Bitboard RANK56 = Us == WHITE ? 0x0000FFFF00000000 : 0x00000000FFFF0000;
		const Bitboard SecondRank = Us == WHITE ? 0x000000000000FF00 : 0x00FF000000000000;
		const Square Up = Us == WHITE ? UP : DOWN;

		Score result;

		e->pawnsOnColor[Us][BLACK] = popcount(bb_ourPawn & DarkSquares);
		e->pawnsOnColor[Us][WHITE] = pos.count(Us, PAWN) - e->pawnsOnColor[Us][BLACK];
		e->halfOpenFiles[Us] = (int)~bb_pawn_files[Us] & 0xFF;
		Bitboard b = e->halfOpenFiles[Us] ^ 0xFF;
		e->pawnRange[Us] = b ? int(msb(b) - lsb(b)) : 0;

		Bitboard NeighborSquares = ((bb_ourPawn << 1) & NOT_FILE_A) | ((bb_ourPawn >> 1) & NOT_FILE_H);
		Bitboard IsolatedFiles = ~((bb_pawn_files[Us] << 1) & NOT_FILE_A | (bb_pawn_files[Us] >> 1) & NOT_FILE_H);

		// pionaanval op rij 5 of 6
		result = make_score(89, 78) * popcount(bb_ourPawn & RANK56 & e->pawnAttack[Them]);

		// KwetsbaarVoorToren = aangevallen door vijandelijke toren op de 8ste rij
		Bitboard VulnerableToRook = Us == WHITE ? ~FileBehind(bb_theirPawn | (bb_ourPawn >> 8)) : ~FileInFront(bb_theirPawn | (bb_ourPawn << 8));

		// dubbelpionnen (voorste op de lijn)
		Bitboard bb_doubled_pawns = Us == WHITE ? bb_ourPawn & ~FileBehind(bb_ourPawn >> 8) & FileInFront(bb_ourPawn << 8) :
			bb_ourPawn & ~FileInFront(bb_ourPawn << 8) & FileBehind(bb_ourPawn >> 8);

		result += (popcount(bb_ourPawn) - popcount(bb_pawn_files[Us] & 0xFF)) * make_score(-42, -65);    // dubbelpionnen
		result += popcount(bb_doubled_pawns & ~e->pawnAttack[Us]) * make_score(-36, -50); // niet gedekte dubbelpionnen

		if (bb_ourPawn & IsolatedFiles)
		{
			result += popcount(bb_ourPawn & IsolatedFiles & ~bb_pawn_files[Them] & NOT_FILE_AH) * make_score(-121, -136);  // geisoleerde pion op halfopen lijn, niet aan de rand
			result += popcount(bb_ourPawn & IsolatedFiles & ~bb_pawn_files[Them] & FILE_AH) * make_score(-75, -87);  // geisoleerde randpion op halfopen lijn
			result += popcount(bb_ourPawn & IsolatedFiles & bb_pawn_files[Them] & NOT_FILE_AH) * make_score(-34, -40);  // geisoleerde pion niet op halfopen lijn, niet aan de rand
			result += popcount(bb_ourPawn & IsolatedFiles & bb_pawn_files[Them] & FILE_AH) * make_score(-31, -26);  // geisoleerde randpion niet op halfopen lijn
		}

		if (bb_doubled_pawns & IsolatedFiles)
		{
			result += popcount(bb_doubled_pawns & IsolatedFiles & ~bb_pawn_files[Them] & NOT_FILE_AH) * make_score(-24, -47);  // geisoleerde dubbelpion op halfopen lijn, niet aan de rand
			result += popcount(bb_doubled_pawns & IsolatedFiles & ~bb_pawn_files[Them] & FILE_AH) * make_score(-18, -42);  // geisoleerde dubbelpion op halfopen lijn, aan de rand
			result += popcount(bb_doubled_pawns & IsolatedFiles & bb_pawn_files[Them] & NOT_FILE_AH) * make_score(-14, -8);  // geisoleerde dubbelpion niet op halfopen lijn, niet aan de rand
			result += popcount(bb_doubled_pawns & IsolatedFiles & bb_pawn_files[Them] & FILE_AH) * make_score(-10, -5);  // geisoleerde dubbelpion niet op halfopen lijn, aan de rand
		}

		result += popcount(e->pawnAttack[Us] & CENTER_BOX) * make_score(9, 11);  // controle van centrum
		result += popcount(e->pawnAttack[Us] & BOARD_EDGE) * make_score(6, 8);  // controle van rest van het bord

		result += popcount(bb_ourPawn & NeighborSquares & e->pawnAttack[Us]) * make_score(2, -5);  // pion ernaast en verdedigd
		result += popcount(bb_ourPawn & e->pawnAttack[Us]) * make_score(48, 26);  // pion verdedigd
		result += popcount(bb_ourPawn & NeighborSquares) * make_score(22, 23);  // pion ernaast


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
		Bitboard bb_phalanx = e->pawnAttack[Us] | NeighborSquares;
		Bitboard bb_backward = bb_ourPawn & e->safeFromPawn[Us] & ~IsolatedFiles;
		Bitboard bb_chain = e->pawnAttack[Us] | NeighborSquares & ~bb_doubled_pawns;  // laatste conditie is gek - voorste dubbelpion is achtergebleven zelfs als er een buur is?
		// misschien omdat pion niet langs achter kan gedekt worden, en de pion pushen na ruil een geïsoleerde dubbelpion oplevert
		Bitboard bb_truly_backward = bb_backward & ~bb_chain;
		Bitboard bb_squares_in_front_of_backward = shift_bb<Up>(bb_truly_backward);

		result += popcount(bb_squares_in_front_of_backward & VulnerableToRook & e->pawnAttack[Them]) * make_score(-55, -86);   // achtergebleven halfopen lijn, pionaanval stopt opmars
		result += popcount(bb_squares_in_front_of_backward & (pos.pieces(PAWN) | e->pawnAttack[Them]) & NOT_FILE_AH) * make_score(-30, -32);  // achtergebleven, pion of pionaanval, niet aan de rand
		result += popcount(bb_squares_in_front_of_backward & (pos.pieces(PAWN) | e->pawnAttack[Them]) & FILE_AH) * make_score(-26, -25);  // achtergebleven, pion of pionaanval, aan de rand

		Bitboard bb_begin_blocked = e->pawnAttack[Them] & shift_bb<Up>(bb_ourPawn & SecondRank);
		result += popcount(bb_begin_blocked) * make_score(-11, -12); // geblokkeerde pionnen op beginpositie


		// vrijpionnen
		// ===========
		Bitboard bb_passed_pawns = bb_ourPawn & e->safeFromPawn[Them] & VulnerableToRook;
		Bitboard bb_protected_passed_pawn = bb_passed_pawns & bb_phalanx;
		while (bb_protected_passed_pawn)
		{
			Square square = pop_lsb(&bb_protected_passed_pawn);
			result += 4 * PassedPawnValues[relative_rank(Us, square)] / 16; // gedekte vrijpion
		}

		e->passedPawns[Us] = bb_passed_pawns;
		if (more_than_one(bb_passed_pawns))
			result += make_score(33, 128);

		// potentiele vrijpionnen
		// voorste pionnen in falanx op halfopen lijn
		Bitboard front_pawns = Us == WHITE ? bb_ourPawn & ~FileBehind(bb_ourPawn >> 8) : bb_ourPawn & ~FileInFront(bb_ourPawn << 8);
		Bitboard bb_potential = front_pawns & bb_phalanx & VulnerableToRook & ~bb_passed_pawns;
		while (bb_potential)
		{
			Square square = pop_lsb(&bb_potential);
			Bitboard bb_active = front_pawns; // toevoegen: & ~bb_echt_achtergebleven  // achtergebleven pionnen kunnen potentiële vrijpion niet helpen
			if (file_of(square) != FILE_A)
			{
				Square square_left = Square(square - 1);
				if (bbSquare[square_left] & bb_theirPawn)
					bb_active &= ~bb_file(square_left);
			}
			if (file_of(square) != FILE_H)
			{
				Square square_right = Square(square + 1);
				if (bbSquare[square_right] & bb_theirPawn)
					bb_active &= ~bb_file(square_right);
			}

			if (popcount(bb_adjacent_files(file_of(square)) & ~bb_ranks_forward(Us, rank_of(square)) & bb_active) >= 
				popcount(bb_adjacent_files(file_of(square)) & bb_ranks_forward(Us, rank_of(square)) & bb_theirPawn))
				result += 11 * PassedPawnValues[relative_rank(Us, square)] / 16;  // potentiele vrijpionnen
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

	const Score DoubledPawn = S(22, 48);
	const Score DoubledPawnDistance[6] = { SCORE_ZERO, DoubledPawn, DoubledPawn / 2, DoubledPawn / 3, DoubledPawn / 4, DoubledPawn / 5 };
	const Score DoubledPawnScore[FILE_N] =
	{
		S(13, 43), S(20, 48), S(23, 48), S(23, 48),
		S(23, 48), S(23, 48), S(20, 48), S(13, 43)
	};
	const Score DoubledPawnScoreDistance[FILE_N][8] =
	{
		{ SCORE_ZERO, DoubledPawnScore[0], DoubledPawnScore[0] / 2, DoubledPawnScore[0] / 3, DoubledPawnScore[0] / 4, DoubledPawnScore[0] / 5 },
		{ SCORE_ZERO, DoubledPawnScore[1], DoubledPawnScore[1] / 2, DoubledPawnScore[1] / 3, DoubledPawnScore[1] / 4, DoubledPawnScore[1] / 5 },
		{ SCORE_ZERO, DoubledPawnScore[2], DoubledPawnScore[2] / 2, DoubledPawnScore[2] / 3, DoubledPawnScore[2] / 4, DoubledPawnScore[2] / 5 },
		{ SCORE_ZERO, DoubledPawnScore[3], DoubledPawnScore[3] / 2, DoubledPawnScore[3] / 3, DoubledPawnScore[3] / 4, DoubledPawnScore[3] / 5 },
		{ SCORE_ZERO, DoubledPawnScore[3], DoubledPawnScore[3] / 2, DoubledPawnScore[3] / 3, DoubledPawnScore[3] / 4, DoubledPawnScore[3] / 5 },
		{ SCORE_ZERO, DoubledPawnScore[2], DoubledPawnScore[2] / 2, DoubledPawnScore[2] / 3, DoubledPawnScore[2] / 4, DoubledPawnScore[2] / 5 },
		{ SCORE_ZERO, DoubledPawnScore[1], DoubledPawnScore[1] / 2, DoubledPawnScore[1] / 3, DoubledPawnScore[1] / 4, DoubledPawnScore[1] / 5 },
		{ SCORE_ZERO, DoubledPawnScore[0], DoubledPawnScore[0] / 2, DoubledPawnScore[0] / 3, DoubledPawnScore[0] / 4, DoubledPawnScore[0] / 5 }
	};

	const Score Isolani[2] = { S(54, 50), S(36, 34) };
	const Score IsolaniScore[2][FILE_N] =
	{
		{ S(37, 45), S(54, 52), S(60, 52), S(60, 52),
		  S(60, 52), S(60, 52), S(54, 52), S(37, 45) },
		{ S(25, 30), S(36, 35), S(40, 35), S(40, 35),
		  S(40, 35), S(40, 35), S(36, 35), S(25, 30) }
	};

	const Score BackwardScore[2] = { S(67, 42), S(49, 24) };

	const Score PawnAttackerScore[RANK_N] =
	{
		S(0, 0), S(0, 0), S(0, 0), S(0, 0),
		S(20,20), S(40,40), S(0, 0), S(0, 0)
	};

#undef S

	Score ChainScore[2][2][3][RANK_N];

	const Score UnsupportedScore[2] = 
	{ 
		make_score(79, 24), make_score(53, 17)
	};

	template<Color Us>
	Score sf_evaluate(const Position& pos, Pawns::Tuple* e)
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);
		const Bitboard SecondRank = Us == WHITE ? Rank2BB : Rank7BB;
		const Bitboard CenterBindMask = (FileDBB | FileEBB) &
			(Us == WHITE ? (Rank5BB | Rank6BB | Rank7BB) : (Rank4BB | Rank3BB | Rank2BB));

		Square square;
		Score score = SCORE_ZERO;
		const Square* pSquare = pos.piece_list(Us, PAWN);

		Bitboard ourPawns = pos.pieces(Us, PAWN);
		Bitboard theirPawns = pos.pieces(Them, PAWN);

		e->passedPawns[Us] = 0;
		e->kingSquare[Us] = SQ_NONE;
		e->pawnsOnColor[Us][BLACK] = popcount(ourPawns & DarkSquares);
		e->pawnsOnColor[Us][WHITE] = pos.count(Us, PAWN) - e->pawnsOnColor[Us][BLACK];

		while ((square = *pSquare++) != SQ_NONE)
		{
			File f = file_of(square);

			Bitboard neighborPawns = ourPawns & bb_adjacent_files(f);
			Bitboard doubledPawns = ourPawns & bb_forward(Us, square);
			//bool dubbelPion = mijnPionnen & veld_achter(IK, veld);
			bool closedFile = theirPawns & bb_forward(Us, square);
			Bitboard stoppers = theirPawns & passed_pawn_mask(Us, square);
			Bitboard attackers = theirPawns & PawnAttack[Us][square];
			Bitboard attackersPush = theirPawns & PawnAttack[Us][square_in_front(Us, square)];
			Bitboard phalanx = neighborPawns & bb_rank(square);
			Bitboard supported = neighborPawns & bb_rank(square_behind(Us, square));
			bool chain = supported | phalanx;
			bool isolani = !neighborPawns;
			bool backward;

			// aanvaller 6.5%, isolani 15.9%, keten 60.9%, stoppers 95.5%, dubbelPionnen 5.8 %

			if ((isolani | chain) 
				|| attackers
				|| (ourPawns & pawn_attack_range(Them, square))
				|| (relative_rank(Us, square) >= RANK_5))
				backward = false;
			else
			{
				Bitboard b = pawn_attack_range(Us, square) & (ourPawns | theirPawns);
				b = pawn_attack_range(Us, square) & bb_rank(rearmost_square(Us, b));
				backward = (b | shift_up<Us>(b)) & theirPawns;
			}

			if (!stoppers && !doubledPawns)
			{
				e->passedPawns[Us] |= square;
				if (chain)
					score += PassedPawnValues4[relative_rank(Us, square)]; // gedekte vrijpion
			}
			else if (!(stoppers ^ attackers ^ attackersPush)
				&& !doubledPawns
				&& popcount(supported) >= popcount(attackers)
				&& popcount(phalanx) >= popcount(attackersPush))
			{
				score += PassedPawnValues8[relative_rank(Us, square)]; // kandidaat vrijpion
			}

			if (chain) // 60.9 %, geslotenLijn 79.2 %, falanks 73.6 %, rijverdeling 62% 24% 11% 3% 0.3%
				score += ChainScore[closedFile][phalanx != 0][popcount(supported)][relative_rank(Us, square)];

			else if (isolani) // 15.8 % waarvan 42% randpion, 58% gesloten lijn
				score -= IsolaniScore[closedFile][f];
				//score -= Isolani[geslotenLijn];

			else if (backward) // 3.1 % waarvan 56% gesloten lijn
				score -= BackwardScore[closedFile];

			else  // 20% niet ondersteund, geslotenLijn 70%
				score -= UnsupportedScore[closedFile];

			if (doubledPawns) // 5.8 % waarvan 6% randpion
				score -= DoubledPawnScoreDistance[f][rank_distance(square, frontmost_square(Us, doubledPawns))];
			//if (dubbelPion && ~ondersteund)
			//	score -= DubbelPion;

			if (attackers) // 6.5 %
				score += PawnAttackerScore[relative_rank(Us, square)];
		}

		Bitboard b = e->halfOpenFiles[Us] ^ 0xFF;
		e->pawnRange[Us] = b ? int(msb(b) - lsb(b)) : 0;

		const Score Score_Center_Bind = make_score(41, -6);
		b = shift_upleft<Us>(ourPawns) & shift_upright<Us>(ourPawns) & CenterBindMask;
		score += Score_Center_Bind * popcount(b);

		const Score Score_Multiple_PassedPawns = make_score(33, 128);
		if (more_than_one(e->passedPawns[Us]))
			score += Score_Multiple_PassedPawns;

		const Score Score_Second_Rank_Fixed = make_score(11, 12);
		Bitboard bb_begin_blocked = e->pawnAttack[Them] & shift_up<Us>(ourPawns & SecondRank);
		score -= Score_Second_Rank_Fixed * popcount(bb_begin_blocked); // geblokkeerde pionnen op beginpositie

		return score;
	}
} // namespace

namespace Pawns 
{
	const int Seed[RANK_N]        = { 0,  0, 15, 10, 57,  75, 135, 0 };
	const int phalanxSeed[RANK_N] = { 0, 10, 13, 33, 66, 105, 196, 0 };

	void initialize()
	{
		for (int n = 0; n < 17; n++)
			for (int distance = 0; distance < 8; distance++)
				Score_K_Pawn_distance[n][distance] = make_score(0, (int)floor(sqrt((double)n) * 5.0 * distance));

		for (int n = 0; n < 8; n++)
		{
			Score_PawnShield[n] = EvalValue(8 * PawnShield_Constants[n]);
			Score_PawnStorming[n] = EvalValue(8 * PawnStorm_Constants[n]);
			Score_StormHalfOpenFile[n] = EvalValue(9 * PawnStorm_Constants[n]);  // Score_PionBestorming + 1/8
			Score_AttackFiles[n] = n * Score_PawnStorming[0];
		}

		const Score Score_Not_Supported = make_score(53, 17);
		//const Score Score_Apex = maak_score(52, 25);

		for (int closedFile = 0; closedFile <= 1; ++closedFile)
			for (int phalanx = 0; phalanx <= 1; ++phalanx)
				for (int point = 0; point <= 2; ++point)
					for (Rank rank = RANK_2; rank < RANK_8; ++rank)
					{
						int v = (phalanx ? phalanxSeed[rank] : Seed[rank]) >> closedFile;
						v += (point == 2 ? 13 : 0);
						ChainScore[closedFile][phalanx][point][rank] = pawn_make_score(3 * v / 2, v)
							//+ (punt == 2 ? Score_Apex : SCORE_ZERO)
							- (point == 0 ? Score_Not_Supported : SCORE_ZERO);
					}
	}


	Tuple* probe(const Position& pos)
	{
		Key64 key = pos.pawn_key();
		Tuple* e = pos.ti()->pawnsTable[key];

		if (e->key64 == key)
			return e;

		e->key64 = key;

		Bitboard bb_pawn_files[2];

		// bereken pionstelling
		// ====================
		Bitboard WPawn = pos.pieces(WHITE, PAWN);
		Bitboard BPawn = pos.pieces(BLACK, PAWN);

		bb_pawn_files[WHITE] = FileInFrontBehind(WPawn);
		bb_pawn_files[BLACK] = FileInFrontBehind(BPawn);

		e->pawnAttack[WHITE] = pawn_attack<WHITE>(WPawn);
		e->pawnAttack[BLACK] = pawn_attack<BLACK>(BPawn);

		e->safeFromPawn[WHITE] = ~FileInFront(e->pawnAttack[WHITE]);
		e->safeFromPawn[BLACK] = ~FileBehind(e->pawnAttack[BLACK]);

		e->halfOpenFiles[WHITE] = (int)~bb_pawn_files[WHITE] & 0xFF;
		e->halfOpenFiles[BLACK] = (int)~bb_pawn_files[BLACK] & 0xFF;

		e->asymmetry = popcount(e->halfOpenFiles[WHITE] ^ e->halfOpenFiles[BLACK]);
		//e->open_lijnen = popcount(e->halfOpenLijnen[WIT] & e->halfOpenLijnen[ZWART]);

#if 0
		e->score = evaluation<WHITE>(pos, e, bb_pawn_files);
		e->score -= evaluation<BLACK>(pos, e, bb_pawn_files);
#else
		e->score = sf_evaluate<WHITE>(pos, e);
		e->score -= sf_evaluate<BLACK>(pos, e);
#endif

		int files = (bb_pawn_files[WHITE] | bb_pawn_files[BLACK]) & 0xFF;
		if (files)
			files = msb(files) - lsb(files);
		// waarde tussen 0 en 4
		e->file_width = std::max(files - 3, 0);

		// geen enkele open lijn -> conversie moeilijk
		// centrale lijn(en) open, geen halfopen lijnen -> conversie moeilijk
		e->conversion_difficult = !(e->halfOpenFiles[WHITE] & e->halfOpenFiles[BLACK])
			|| ((e->halfOpenFiles[WHITE] & e->halfOpenFiles[BLACK] & 0x3c) && !e->asymmetry);

		// bereken de gemiddelde lijn van pionnen
		Bitboard bb_pawns = pos.pieces(PAWN);
		e->nPawns = popcount(bb_pawns);
		if (bb_pawns)
		{
			int FileSum = 0;
			do
			{
				Square square = pop_lsb(&bb_pawns);
				FileSum += square & 7;
			} while (bb_pawns);
			e->averageFile = FileSum / e->nPawns;
		}

		return e;
	}

#if 1
	const int FILE_FACTOR_MULT = 64;
	const int shield_factor[3] = { 74, 69, 64 };
	const int storm_factor[3] = { 64, 48, 64 };

	template<Color Us>
	EvalValue sf_shelter_storm(const Position& pos, Square squareK) {

		const Color Them = (Us == WHITE ? BLACK : WHITE);

		enum { NoOwnPawn, CanMove, StoppedByPawn, StoppedByKing };

		Bitboard b = bb_ranks_forward(Us, rank_of(squareK)) | bb_rank(squareK);
		Bitboard ourPawns = b & pos.pieces(Us, PAWN);
		Bitboard theirPawns = b & pos.pieces(Them, PAWN);
		EvalValue danger = EVAL_0;
		File center = std::max(FILE_B, std::min(FILE_G, file_of(squareK)));

		for (File f = center - File(1); f <= center + File(1); ++f)
		{
			b = ourPawns & bb_file(f);
			Rank ourRank = b ? relative_rank(Us, rearmost_square(Us, b)) : RANK_1;

			b = theirPawns & bb_file(f);
			Rank theirRank = b ? relative_rank(Us, frontmost_square(Them, b)) : RANK_1;

			danger += ShelterWeakness[std::min(f, FILE_H - f)][ourRank] * shield_factor[std::abs(f - file_of(squareK))];

			int ttype = 
				(f == file_of(squareK) && theirRank == relative_rank(Us, squareK) + 1) ? StoppedByKing :
				(ourRank == RANK_1) ? NoOwnPawn :
				(theirRank == ourRank + 1) ? StoppedByPawn :
				CanMove;

			danger += StormDanger[ttype][std::min(f, FILE_H - f)][theirRank] * storm_factor[std::abs(f - file_of(squareK))];
		}
		danger /= FILE_FACTOR_MULT;

		return EvalValue(100 + MaxSafetyBonus * 3 - danger * 3);
	}
#endif


	template<Color Us>
	Score Tuple::compute_king_safety(const Position& pos)
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);

		Square squareK = pos.king(Us);
		kingSquare[Us] = squareK;
		castlingRights[Us] = pos.castling_rights(Us);

		EvalValue safe_bonus = sf_shelter_storm<Us>(pos, squareK);

		if (pos.castling_possible(Us == WHITE ? WHITE_SHORT : BLACK_SHORT))
			safe_bonus = std::max(safe_bonus, sf_shelter_storm<Us>(pos, relative_square(Us, SQ_G1)));
		if (pos.castling_possible(Us == WHITE ? WHITE_LONG : BLACK_LONG))
			safe_bonus = std::max(safe_bonus, sf_shelter_storm<Us>(pos, relative_square(Us, SQ_C1)));

		const Bitboard THIRD_FOURTH_RANK = Us == WHITE ? Rank3BB | Rank4BB : Rank6BB | Rank5BB;
		const Bitboard FIFTH_RANK = Us == WHITE ? Rank5BB : Rank4BB;

		Bitboard attacking_pawns = AttackFiles[file_of(squareK)] & pos.pieces(Them, PAWN);

		safety[Us] = safe_bonus / 220 * 8
			- 16 * popcount(attacking_pawns & THIRD_FOURTH_RANK)    // vijandelijke pionnen op onze 3de rij
			- 8 * popcount(attacking_pawns & FIFTH_RANK);         // vijandelijke pionnen op onze 5de rij

		Score result = make_score(safe_bonus, 0);

		// K op eerste rij achter 3 pionnen -> malus voor mat achter de paaltjes
		Bitboard bb_King = pos.pieces(Us, KING);
		const Bitboard FIRST_RANK = Us == WHITE ? Rank1BB : Rank8BB;

		if (bb_King & FIRST_RANK)
		{
			Bitboard bb = shift_up<Us>(bb_King) | shift_upleft<Us>(bb_King) | shift_upright<Us>(bb_King);
			if (bb == (pos.pieces(Us, PAWN) & bb))
				result += make_score(-63, -173);
		}

		// koning voor vijandelijke pion?
		if (pos.pieces(Them, PAWN) & shift_down<Us>(bb_King))
			result += make_score(0, 27);

		if (nPawns)
			result -= Score_K_Pawn_distance[nPawns][abs(averageFile - file_of(pos.king(Us)))];

		return result;
	}

	template Score Tuple::compute_king_safety<WHITE>(const Position& pos);
	template Score Tuple::compute_king_safety<BLACK>(const Position& pos);
} // namespace
