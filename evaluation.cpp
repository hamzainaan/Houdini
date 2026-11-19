/*
*/

#include "houdini.h"
#include "bitboard.h"
#include "evaluation.h"
#include "eval_material.h"
#include "eval_pawnstruct.h"
#include "uci.h"
#include "search_smp.h"


namespace Evaluation
{
	Score VeiligheidTabel[1024];
}

namespace
{
	unsigned int MobiMult_P[64], MobiMult_L1[64], MobiMult_L2[64], MobiMult_T[64], MobiMult_D[64];
	Score mobi_P[256], mobi_L1[256], mobi_L2[256], mobi_T[256], mobi_D[256];

	Score Score_Afstand_P_K[8], Score_Afstand_L_K[8];
	Score Score_PionOpKleurLoper[9], Score_PionAndereKleurLoper[9];
	Score Score_PionLijnBreedte[9];

	Score Score_Vrijpion_vrije_doorgang[8];
	Score Score_Vrijpion_opmars_ondersteund[8];
	Score Score_Vrijpion_opmars_gestuit[8];
	Score Score_Vrijpion_niet_DvD[8];
	Score Score_Vrijpion_DvD[8];

	Score Score_Dreigingen[9];

	static const int KoningGevaar[256] = {
		0, 1, 3, 6, 11, 17, 25, 34, 44, 55, 68, 81, 96, 112, 129, 147,
		166, 185, 205, 227, 248, 270, 293, 317, 340, 364, 388, 413, 438, 463, 487, 512,
		537, 562, 587, 611, 636, 660, 684, 708, 731, 754, 777, 799, 821, 842, 863, 884,
		904, 924, 943, 962, 980, 998, 1016, 1033, 1049, 1065, 1081, 1096, 1111, 1125, 1139, 1152,
		1165, 1178, 1190, 1201, 1213, 1224, 1234, 1245, 1254, 1264, 1273, 1282, 1291, 1299, 1307, 1314,
		1322, 1329, 1336, 1342, 1349, 1355, 1360, 1366, 1371, 1377, 1382, 1387, 1391, 1396, 1400, 1404,
		1408, 1412, 1415, 1419, 1422, 1425, 1428, 1431, 1434, 1437, 1439, 1442, 1444, 1447, 1449, 1451,
		1453, 1455, 1457, 1459, 1460, 1462, 1464, 1465, 1467, 1468, 1469, 1471, 1472, 1473, 1474, 1475,
		1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476,
		1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476,
		1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476,
		1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476,
		1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476,
		1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476,
		1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476,
		1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476, 1476
	};

	static const Bitboard bb_L_domineert_P[2][64] = 
	{
		{
			0, 0, 0, 0, 0, 0, 0, 0,
			bb(SQ_A5), bb(SQ_B5), bb(SQ_C5), bb(SQ_D5), bb(SQ_E5), bb(SQ_F5), bb(SQ_G5), bb(SQ_H5),
			bb(SQ_A6), bb(SQ_B6), bb(SQ_C6), bb(SQ_D6), bb(SQ_E6), bb(SQ_F6), bb(SQ_G6), bb(SQ_H6),
			bb(SQ_A7), bb(SQ_B7), bb(SQ_C7), bb(SQ_D7), bb(SQ_E7), bb(SQ_F7), bb(SQ_G7), bb(SQ_H7),
			bb(SQ_A8), bb(SQ_B8), bb(SQ_C8), bb(SQ_D8), bb(SQ_E8), bb(SQ_F8), bb(SQ_G8), bb(SQ_H8),
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0
		},
		{
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			bb(SQ_A1), bb(SQ_B1), bb(SQ_C1), bb(SQ_D1), bb(SQ_E1), bb(SQ_F1), bb(SQ_G1), bb(SQ_H1),
			bb(SQ_A2), bb(SQ_B2), bb(SQ_C2), bb(SQ_D2), bb(SQ_E2), bb(SQ_F2), bb(SQ_G2), bb(SQ_H2),
			bb(SQ_A3), bb(SQ_B3), bb(SQ_C3), bb(SQ_D3), bb(SQ_E3), bb(SQ_F3), bb(SQ_G3), bb(SQ_H3),
			bb(SQ_A4), bb(SQ_B4), bb(SQ_C4), bb(SQ_D4), bb(SQ_E4), bb(SQ_F4), bb(SQ_G4), bb(SQ_H4),
			0, 0, 0, 0, 0, 0, 0, 0
		}
	};

	static const Bitboard opgesloten_loper_b3_c2[2][64] =
	{
		{
			bb(SQ_B2), bb(SQ_C2), 0, 0, 0, 0, bb(SQ_F2), bb(SQ_G2),
			bb(SQ_B3), 0, 0, 0, 0, 0, 0, bb(SQ_G3),
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			bb(SQ_B5), 0, 0, 0, 0, 0, 0, bb(SQ_G5),
			bb(SQ_B6), 0, 0, 0, 0, 0, 0, bb(SQ_G6),
			bb(SQ_B7), bb(SQ_C7), 0, 0, 0, 0, bb(SQ_F7), bb(SQ_G7)
		},
		{
			bb(SQ_B2), bb(SQ_C2), 0, 0, 0, 0, bb(SQ_F2), bb(SQ_G2),
			bb(SQ_B3), 0, 0, 0, 0, 0, 0, bb(SQ_G3),
			bb(SQ_B4), 0, 0, 0, 0, 0, 0, bb(SQ_G4),
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			bb(SQ_B6), 0, 0, 0, 0, 0, 0, bb(SQ_G6),
			bb(SQ_B7), bb(SQ_C7), 0, 0, 0, 0, bb(SQ_F7), bb(SQ_G7)
		} 
	};
	static const Bitboard opgesloten_loper_b3_c2_extra[64] =
	{
		bb(SQ_B3), bb(SQ_B3), 0, 0, 0, 0, bb(SQ_G3), bb(SQ_G3),
		bb(SQ_C2), 0, 0, 0, 0, 0, 0, bb(SQ_F2),
		bb(SQ_C3), 0, 0, 0, 0, 0, 0, bb(SQ_F3),
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		bb(SQ_C6), 0, 0, 0, 0, 0, 0, bb(SQ_F6),
		bb(SQ_C7), 0, 0, 0, 0, 0, 0, bb(SQ_F7),
		bb(SQ_B6), bb(SQ_B6), 0, 0, 0, 0, bb(SQ_G6), bb(SQ_G6)
	};

	/*
	static const Bord opgesloten_loper_b4_d2[2][64] =
	{
		{
			bb2(SQ_B4, SQ_C3), 0, bb2(SQ_B4, SQ_C3), 0, 0, bb2(SQ_G4, SQ_F3), 0, bb2(SQ_G4, SQ_F3),
			0, bb2(SQ_B4, SQ_C3), 0, 0, 0, 0, bb2(SQ_G4, SQ_F3), 0,
			bb2(SQ_B4, SQ_C3), 0, 0, 0, 0, 0, 0, bb2(SQ_G4, SQ_F3),
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0
		},
		{
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			bb2(SQ_B5, SQ_C6), 0, 0, 0, 0, 0, 0, bb2(SQ_G5, SQ_F6),
			0, bb2(SQ_B5, SQ_C6), 0, 0, 0, 0, bb2(SQ_G5, SQ_F6), 0,
			bb2(SQ_B5, SQ_C6), 0, bb2(SQ_B5, SQ_C6), 0, 0, bb2(SQ_G5, SQ_F6), 0, bb2(SQ_G5, SQ_F6)
		}
	};
	static const Bord opgesloten_loper_b4_d2_extra[64] =
	{
		bb2(SQ_D2, SQ_E3), 0, bb2(SQ_D2, SQ_E3), 0, 0, bb2(SQ_E2, SQ_D3), 0, bb2(SQ_E2, SQ_D3),
		0, bb2(SQ_D2, SQ_E3), 0, 0, 0, 0, bb2(SQ_E2, SQ_D3), 0,
		bb2(SQ_D2, SQ_E3), 0, 0, 0, 0, 0, 0, bb2(SQ_E2, SQ_D3),
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		bb2(SQ_D7, SQ_E6), 0, 0, 0, 0, 0, 0, bb2(SQ_E7, SQ_D6),
		0, bb2(SQ_D7, SQ_E6), 0, 0, 0, 0, bb2(SQ_E7, SQ_D6), 0,
		bb2(SQ_D7, SQ_E6), 0, bb2(SQ_D7, SQ_E6), 0, 0, bb2(SQ_E7, SQ_D6), 0, bb2(SQ_E7, SQ_D6)
	};

	static const Bord opgesloten_toren[64] =
	{
		0, bb2(SQ_A1, SQ_A2), bb4(SQ_A1, SQ_A2, SQ_B1, SQ_B2), 0, 0, bb4(SQ_H1, SQ_H2, SQ_G1, SQ_G2), bb2(SQ_H1, SQ_H2), 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, bb2(SQ_A8, SQ_A7), bb4(SQ_A8, SQ_A7, SQ_B8, SQ_B7), 0, 0, bb4(SQ_H8, SQ_H7, SQ_G8, SQ_G7), bb2(SQ_H8, SQ_H7), 0
	};
	*/


	ScaleFactor compute_scalefactor(const Position& pos, const Material::Tuple* materiaalTuple, EvalValue waarde) {

		Color sterkeZijde = waarde > DRAW_EVAL ? WHITE : BLACK;
		ScaleFactor sf = materiaalTuple->scalefactor_from_function(pos, sterkeZijde);

		if (abs(waarde) <= BISHOP_EVAL && (sf == NORMAL_FACTOR || sf == ONE_PAWN_FACTOR))
		{
			if (pos.opposite_bishops())
			{
				if (pos.non_pawn_material(WHITE) == MATL_BISHOP
					&& pos.non_pawn_material(BLACK) == MATL_BISHOP)
					sf = pos.aantal(sterkeZijde, PAWN) > 1 ? ScaleFactor(50) : ScaleFactor(12);

				else
					sf = ScaleFactor(3 * sf / 4);
			}
			else if (pos.aantal(sterkeZijde, PAWN) <= 2
				&& !pos.is_passed_pawn(~sterkeZijde, pos.koning(~sterkeZijde)))
				sf = ScaleFactor(58 + 11 * pos.aantal(sterkeZijde, PAWN));
		}

		return sf;
	}

#define S(mg, eg) make_score(mg, eg)
	const Score StukDreiging[PIECE_TYPE_N] = {
		S(0, 0), S(0, 0), S(7, 110), S(161, 125), S(165, 138), S(266, 328), S(188, 375)
	};
	const Score TorenDreiging[PIECE_TYPE_N] = {
		S(0, 0), S(0, 0), S(5, 83), S(148, 191), S(148, 181), S(7, 113), S(128, 146)
	};
	const Score PionDreiging[PIECE_TYPE_N] = {
		S(0, 0), S(0, 0), S(0, 0), S(623, 393), S(469, 371), S(780, 640), S(732, 636)
	};

	const Score Score_Loper_Penning[COLOR_N][PIECE_N] = {
		{ S(0, 0), S(0, 0), S(0, 0), S(90, 90), S(0, 0), S(60, 60), S(60, 60), S(0, 0),
		  S(0, 0), S(0, 0), S(0, 0), S(120,120), S(0, 0), S(225,255), S(225,255), S(0, 0)
		},
		{ S(0, 0), S(0, 0), S(0, 0), S(120,120), S(0, 0), S(225,255), S(225,255), S(0, 0),
		  S(0, 0), S(0, 0), S(0, 0), S(90, 90), S(0, 0), S(60, 60), S(60, 60), S(0, 0),
		}
	};
	const Score Score_Toren_Penning[COLOR_N][PIECE_N] = {
		{ S(0, 0), S(0, 0), S(0, 0), S(100, 100), S(100, 100), S(0, 0), S(0, 0), S(0, 0),
		  S(0, 0), S(0, 0), S(0, 0), S(100, 100), S(100, 100), S(0, 0), S(0, 0), S(0, 0)
		},
		{ S(0, 0), S(0, 0), S(0, 0), S(100, 100), S(100, 100), S(0, 0), S(0, 0), S(0, 0),
		  S(0, 0), S(0, 0), S(0, 0), S(100, 100), S(100, 100), S(0, 0), S(0, 0), S(0, 0)
		}
	};
#undef S

	const int KingAttackWeights[PIECE_TYPE_N] = { 0, 0, 35, 125, 80, 80, 72 };

	const int QueenContactCheck = 89;
	const int QueenCheck = 50;
	const int RookCheck = 45;
	const int BishopCheck = 6;
	const int KnightCheck = 14;

	static const int VrijpionNabijheid[8] = { 13, 9, 6, 4, 3, 2, 1, 0 };
	static const Score Score_Pion_Vrijpion_ifvLijn[8] = {
		make_score(0, 60 * 32 / 35), make_score(0, 20 * 32 / 35), make_score(0, -20 * 32 / 35), make_score(0, -60 * 32 / 35),
		make_score(0, -60 * 32 / 35), make_score(0, -20 * 32 / 35), make_score(0, 20 * 32 / 35), make_score(0, 60 * 32 / 35)
	};

	Score Score_Vrijpion_Mijn_K[6][8];
	Score Score_Vrijpion_Jouw_K[6][8];

	//Bord bb_centrum_pivot_W[LIJN_N], bb_centrum_pivot_Z[LIJN_N];
	
	Bitboard bb_koning_flankaanval[COLOR_N][FILE_N] = {
		{
			(Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB) & (FileABB | FileBBB | FileCBB | FileDBB),
			(Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB) & (FileABB | FileBBB | FileCBB | FileDBB),
			(Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB) & (FileABB | FileBBB | FileCBB | FileDBB),
			(Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB) & (FileCBB | FileDBB | FileEBB | FileFBB),
			(Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB) & (FileCBB | FileDBB | FileEBB | FileFBB),
			(Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB) & (FileEBB | FileFBB | FileGBB | FileHBB),
			(Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB) & (FileEBB | FileFBB | FileGBB | FileHBB),
			(Rank5BB | Rank4BB | Rank3BB | Rank2BB | Rank1BB) & (FileEBB | FileFBB | FileGBB | FileHBB)
		},
		{
			(Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB) & (FileABB | FileBBB | FileCBB | FileDBB),
			(Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB) & (FileABB | FileBBB | FileCBB | FileDBB),
			(Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB) & (FileABB | FileBBB | FileCBB | FileDBB),
			(Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB) & (FileCBB | FileDBB | FileEBB | FileFBB),
			(Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB) & (FileCBB | FileDBB | FileEBB | FileFBB),
			(Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB) & (FileEBB | FileFBB | FileGBB | FileHBB),
			(Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB) & (FileEBB | FileFBB | FileGBB | FileHBB),
			(Rank4BB | Rank5BB | Rank6BB | Rank7BB | Rank8BB) & (FileEBB | FileFBB | FileGBB | FileHBB)
		}
	};

	//Bord bb_koning_flank[LIJN_N] = {
	//	FileABB | FileBBB | FileCBB | FileDBB,
	//	FileABB | FileBBB | FileCBB | FileDBB,
	//	FileABB | FileBBB | FileCBB | FileDBB,
	//	FileCBB | FileDBB | FileEBB | FileFBB,
	//	FileCBB | FileDBB | FileEBB | FileFBB,
	//	FileEBB | FileFBB | FileGBB | FileHBB,
	//	FileEBB | FileFBB | FileGBB | FileHBB,
	//	FileEBB | FileFBB | FileGBB | FileHBB
	//};

	//static const int P_GEMIDDELD_MOBILITEIT = 8;
	//static const int L_GEMIDDELD_MOBILITEIT = 6;
	//static const int T_GEMIDDELD_MOBILITEIT = 7;
	//static const int D_GEMIDDELD_MOBILITEIT = 11;

	const Bitboard ThreeFiles = FileABB | FileBBB | FileCBB;

	const Bitboard KoningVleugelLijnen[FILE_N] =
	{
		ThreeFiles, ThreeFiles, ThreeFiles << 1, ThreeFiles << 2,
		ThreeFiles << 3, ThreeFiles << 4, ThreeFiles << 5, ThreeFiles << 5
	};

} // namespace


template<Color Us>
Bitboard compute_attack(const Position& pos)
{
	const Square* pVeld;
	Square veld;
	Bitboard aanval;

	aanval = pos.attack_from<KING>(pos.koning(Us));
	aanval |= pion_aanval<Us>(pos.pieces(Us, PAWN));

	pVeld = pos.piece_list(Us, KNIGHT);
	while ((veld = *pVeld++) != SQ_NONE)
		aanval |= pos.attack_from<KNIGHT>(veld);

	pVeld = pos.piece_list(Us, BISHOP);
	while ((veld = *pVeld++) != SQ_NONE)
		aanval |= pos.attack_from<BISHOP>(veld);

	pVeld = pos.piece_list(Us, ROOK);
	while ((veld = *pVeld++) != SQ_NONE)
		aanval |= pos.attack_from<ROOK>(veld);

	pVeld = pos.piece_list(Us, QUEEN);
	while ((veld = *pVeld++) != SQ_NONE)
		aanval |= pos.attack_from<QUEEN>(veld);

	return aanval;
}


template<Color Us>
bool at_least_two_mobile_pieces(const Position& pos)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Square* pVeld;
	Square veld;

	Bitboard jouwAanval = compute_attack<Them>(pos);
	Bitboard gepend = pos.info()->xRay[Us];

	Bitboard veilig = ~pos.pieces(Us) & ~jouwAanval;
	bool mobiel = false;

	Bitboard aanval = pos.attack_from<KING>(pos.koning(Us));
	if (aanval & veilig)
	{
		// koning die aangevallen stuk verdedigt is niet mobiel
		if (!(aanval & pos.pieces(Us) & jouwAanval))
			mobiel = true;
	}

	pVeld = pos.piece_list(Us, KNIGHT);
	while ((veld = *pVeld++) != SQ_NONE)
	{
		if (gepend & veld)
			continue;
		if (pos.attack_from<KNIGHT>(veld) & veilig)
		{
			if (mobiel)
				return true;
			mobiel = true;
		}
	}

	pVeld = pos.piece_list(Us, BISHOP);
	while ((veld = *pVeld++) != SQ_NONE)
	{
		if (gepend & veld)
			continue;
		if (pos.attack_from<BISHOP>(veld) & veilig)
		{
			if (mobiel)
				return true;
			mobiel = true;
		}
	}

	pVeld = pos.piece_list(Us, ROOK);
	while ((veld = *pVeld++) != SQ_NONE)
	{
		if (gepend & veld)
			continue;
		if (pos.attack_from<ROOK>(veld) & veilig)
		{
			if (mobiel)
				return true;
			mobiel = true;
		}
	}

	pVeld = pos.piece_list(Us, QUEEN);
	while ((veld = *pVeld++) != SQ_NONE)
	{
		if (gepend & veld)
			continue;
		if (pos.attack_from<QUEEN>(veld) & veilig)
		{
			if (mobiel)
				return true;
			mobiel = true;
		}
	}

	return false;
}

bool Evaluation::at_least_two_mobile_pieces(const Position& pos)
{
	if (pos.side_to_move() == WHITE)
		return ::at_least_two_mobile_pieces<WHITE>(pos);
	else
		return ::at_least_two_mobile_pieces<BLACK>(pos);
}


void Evaluation::export_table()
{
	FILE *f = fopen("R:\\Schaak\\h6-veilig-export.dat", "wb");
	fwrite(VeiligheidTabel, 1, sizeof(VeiligheidTabel), f);
	fclose(f);
}


void Evaluation::import_table()
{
	FILE *f = fopen("R:\\Schaak\\h6-veilig-export.dat", "rb");
	fread(VeiligheidTabel, 1, sizeof(VeiligheidTabel), f);
	fclose(f);
}


template<Color Us>
inline Score Threats(const Position& pos, const AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Score Score_NietVerdedigdePionnen = make_score(-15, -30);
	const Score Score_StukkenMoetenVerdedigen = make_score(17, 42);

	Bitboard bb_niet_verdedigd_Z = pos.pieces(Them) & ~ai.aanval[Them][ALL_PIECES];

	Bitboard ZP_Pion_aangevallen_L = ai.aanval[Us][BISHOP] & pos.pieces(Them, KNIGHT, PAWN);
	Bitboard ZLP_Pion_aangevallen_T = ai.aanval[Us][ROOK] & pos.pieces(Them, BISHOP, KNIGHT, PAWN);
	Bitboard ZL_Pion_aangevallen_P = ai.aanval[Us][KNIGHT] & pos.pieces(Them, BISHOP, PAWN);

	// zwarte stukken aangevallen door WPion, ZD aangevallend door WT, ZD of ZT aangevallend door WL of WP (waardevol aangevallen door minder waardevol)
	int dreigingenW = popcount((pos.pieces(Them) ^ pos.pieces(Them, PAWN)) & ai.aanval[Us][PAWN]
		| pos.pieces(Them, QUEEN) & ai.aanval[Us][ROOK]
		| pos.pieces(Them, QUEEN, ROOK) & (ai.aanval[Us][BISHOP] | ai.aanval[Us][KNIGHT]));
	Score score = Score_Dreigingen[dreigingenW];

	// niet verdedigde zwarte stukken aangevallen door waardevoller stuk
	dreigingenW += popcount(bb_niet_verdedigd_Z & (ai.aanval[Us][KING] | ZP_Pion_aangevallen_L | ZL_Pion_aangevallen_P | ZLP_Pion_aangevallen_T
		| ai.aanval[Us][QUEEN] & (pos.pieces(Them) ^ pos.pieces(Them, QUEEN))));
	score += Score_Dreigingen[dreigingenW];

	score += Score_NietVerdedigdePionnen * popcount(pos.pieces(Us, PAWN) & ~ai.aanval[Us][PIECES_WITHOUT_KING]);
	score += Score_StukkenMoetenVerdedigen * popcount((ai.aanval[Them][PIECES_WITHOUT_KING] ^ ai.aanval[Them][PAWN]) & ai.aanval[Us][PIECES_WITHOUT_KING] & pos.pieces(Them));

	return score;
}

template<Color Us>
Score sf_Dreigingen(const Position& pos, AttackInfo& ai) 
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Bitboard RANK2 = (Us == WHITE ? Rank2BB : Rank7BB);
	const Bitboard RANK7 = (Us == WHITE ? Rank7BB : Rank2BB);

	const Score HangingPawnThreat = make_score(249, 182);
	//const Score PionDreigingAvg = maak_score(650, 510);
	const Score KingThreatSingle = make_score(24, 206);
	const Score KingThreatMultiple = make_score(62, 458);
	const Score HangingPieces = make_score(167, 74);
	const Score PawnAdvance = make_score(108, 50);

	Score score = SCORE_ZERO;
	ai.sterkeDreiging[Us] = false;

	Bitboard pionDreigingen = pos.pieces_except(Them, PAWN) & ai.aanval[Us][PAWN];
	if (pionDreigingen)
	{
		Bitboard veiligePionnen = pos.pieces(Us, PAWN) & (~ai.aanval[Them][ALL_PIECES] | ai.aanval[Us][ALL_PIECES]);
		Bitboard veiligeDreigingen = pion_aanval<Us>(veiligePionnen) & pionDreigingen;

		if (pionDreigingen ^ veiligeDreigingen)
			score += HangingPawnThreat;

		if (veiligeDreigingen)
		{
			ai.sterkeDreiging[Us] = true;
			//score += PionDreigingAvg * popcount(veiligeDreigingen);
		}

		while (veiligeDreigingen)
			score += PionDreiging[piece_type(pos.piece_on_square(pop_lsb(&veiligeDreigingen)))];
	}

	Bitboard ondersteundeStukken = pos.pieces_except(Them, PAWN) & ai.aanval[Them][PAWN] & ai.aanval[Us][ALL_PIECES];
	Bitboard zwakkeStukken = pos.pieces(Them) & ~ai.aanval[Them][PAWN] & ai.aanval[Us][ALL_PIECES];
	//zwakkeStukken |= ai.gepend[JIJ];

	if (ondersteundeStukken | zwakkeStukken)
	{
		Bitboard b = (ondersteundeStukken | zwakkeStukken) & (ai.aanval[Us][KNIGHT] | ai.aanval[Us][BISHOP]);
		if (b & pos.pieces(Them, ROOK, QUEEN))
			ai.sterkeDreiging[Us] = true;
		while (b)
			score += StukDreiging[piece_type(pos.piece_on_square(pop_lsb(&b)))];

		b = (pos.pieces(Them, QUEEN) | zwakkeStukken) & ai.aanval[Us][ROOK];
		if (b & pos.pieces(Them, QUEEN))
			ai.sterkeDreiging[Us] = true;
		while (b)
			score += TorenDreiging[piece_type(pos.piece_on_square(pop_lsb(&b)))];

		b = zwakkeStukken & ~ai.aanval[Them][ALL_PIECES];
		if (b & pos.pieces_except(Them, PAWN))
			ai.sterkeDreiging[Us] = true;
		score += HangingPieces * popcount(b);

		b = zwakkeStukken & ai.aanval[Us][KING];
		if (b)
			score += more_than_one(b) ? KingThreatMultiple : KingThreatSingle;
	}

	Bitboard b = pos.pieces(Us, PAWN) & ~RANK7;
	b = shift_up<Us>(b | (shift_up<Us>(b & RANK2) & ~pos.pieces()));
	//b = shift_up<IK>(b | (shift_up<IK>(b & RANK2) & ~pos.stukken() & ~ai.aanval[JIJ][PION]));

	b &= ~pos.pieces()
		& ~ai.aanval[Them][PAWN]
		& (ai.aanval[Us][ALL_PIECES] | ~ai.aanval[Them][ALL_PIECES]);

	b = pion_aanval<Us>(b)
		&  pos.pieces(Them)
		& ~ai.aanval[Us][PAWN];

	score += PawnAdvance * popcount(b);

	return score;
}

template<Color Us>
inline Score PassedPawns(const Position& pos, const AttackInfo& ai, Bitboard bb_vrijpionnen)
{
	assert(bb_vrijpionnen != 0);
	const Color Them = (Us == WHITE ? BLACK : WHITE);

	Score score = SCORE_ZERO;
	while (bb_vrijpionnen)
	{
		Square vrijpion = pop_lsb(&bb_vrijpionnen);

		score += Score_Pion_Vrijpion_ifvLijn[lijn(vrijpion)];

		int pion_rij = relative_rank(Us, vrijpion) - 1;
		if (pos.non_pawn_material(WHITE) == MATL_QUEEN && pos.non_pawn_material(BLACK) == MATL_QUEEN)
			score += Score_Vrijpion_DvD[pion_rij];
		else
			score += Score_Vrijpion_niet_DvD[pion_rij];

		if (pion_rij > 1)
		{
			// afstand tot veld voor pion
			Square veld_voor_pion = vrijpion + pawn_forward(Us);
			int mijnAfstand = distance(veld_voor_pion, pos.koning(Us));
			int jouwAfstand = distance(veld_voor_pion, pos.koning(Them));

			score += score_muldiv(Score_Vrijpion_Mijn_K[pion_rij][mijnAfstand], 3, 4);
			score += score_muldiv(Score_Vrijpion_Jouw_K[pion_rij][jouwAfstand], 3, 4);

			// afstand tot promotieveld
			Square promotie_veld = Square(make_square(lijn(vrijpion), Rank(7 * Them)));
			mijnAfstand = distance(promotie_veld, pos.koning(Us));
			jouwAfstand = distance(promotie_veld, pos.koning(Them));

			score += score_muldiv(Score_Vrijpion_Mijn_K[pion_rij][mijnAfstand], 2, 4);
			score += score_muldiv(Score_Vrijpion_Jouw_K[pion_rij][jouwAfstand], 2, 4);

			if (pion_rij > 2)
			{
				Bitboard bb_achter_vrijpion = bb_forward(Them, vrijpion);
				// toren achter pion
				if (bb_achter_vrijpion & pos.pieces(Us, ROOK))
					score += make_score(62, 274);
				if (bb_achter_vrijpion & pos.pieces(Them, ROOK))
					score -= make_score(62, 274);
			}

			if (!(pos.pieces() & veld_voor_pion))
			{
				// vrijpion kan voorwaarts
				Bitboard vrijpion_pad = bb_forward(Us, vrijpion);
				Bitboard bb_opmars_gestuit = vrijpion_pad & (pos.pieces(Them) | ai.aanval[Them][ALL_PIECES]);

				// jouw T of D achter vrijpion -> hele vrijpion_pad is aangevallen
				Bitboard aangevallen = pos.pieces(Them, ROOK, QUEEN) & bb_forward(Them, vrijpion);
				if (aangevallen)
				{
					Square veld = frontmost_square(Us, aangevallen);
					if (!(pos.pieces() & bb_between(vrijpion, veld)))
						bb_opmars_gestuit = vrijpion_pad;
				}

				if (!bb_opmars_gestuit)
					score += Score_Vrijpion_vrije_doorgang[pion_rij];
				else if (bb_opmars_gestuit & ~ai.aanval[Us][ALL_PIECES])
					score += Score_Vrijpion_opmars_gestuit[pion_rij];
				else // pad is aangevallen, maar ook verdedigd
					score += Score_Vrijpion_opmars_ondersteund[pion_rij];
			}
		}
	}

	return score;
}

template<Color Us>
inline Score KingAttack(const Position& pos, const AttackInfo& ai, int AanvalScore)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);

	int aanvalIndex = AanvalScore;

	aanvalIndex += 16 * popcount(ai.aanval[Them][KING] & ai.aanval[Us][ALL_PIECES] & ~ai.aanval[Them][PIECES_WITHOUT_KING]);

	if (ai.gepend[Them]) // penningen of aftrekschaak
		aanvalIndex += 12;

	// niet verdedigde aangevallen velden 2 rijen voor de koning
	aanvalIndex += 11 * popcount(shift_down<Us>(ai.aanval[Them][KING]) & ~ai.aanval[Them][ALL_PIECES] & ai.aanval[Us][ALL_PIECES] & ~pos.pieces(Us));

	// veilige schaaks
	const Square veldK = pos.koning(Them);
	Bitboard Schaak_OK = ~pos.pieces(Us);
	
	Bitboard Schaakvelden_T = pos.attack_from<ROOK>(veldK) & Schaak_OK;
	Bitboard Schaakvelden_L = pos.attack_from<BISHOP>(veldK) & Schaak_OK;
	Bitboard Schaakvelden_P = pos.attack_from<KNIGHT>(veldK) & Schaak_OK;

	// P, L, T
	Bitboard veilig_PLT = ~ai.aanval[Them][ALL_PIECES] | (ai.dubbelAanval[Us] & ~ai.dubbelAanval[Them] & (ai.aanval[Them][KING] | ai.aanval[Them][QUEEN]));
	if (Schaakvelden_P & ai.aanval[Us][KNIGHT])
	{
		if (Schaakvelden_P & ai.aanval[Us][KNIGHT] & veilig_PLT)
			aanvalIndex += 70;
		else
			aanvalIndex += 30;
	}
	if (Schaakvelden_L & ai.aanval[Us][BISHOP])
	{
		if (Schaakvelden_L & ai.aanval[Us][BISHOP] & veilig_PLT)
			aanvalIndex += 54;
		else
			aanvalIndex += 22;
	}
	if (Schaakvelden_T & ai.aanval[Us][ROOK])
	{
		if (Schaakvelden_T & ai.aanval[Us][ROOK] & veilig_PLT)
			aanvalIndex += 70;
		else
			aanvalIndex += 30;
	}

	// Dame schaaks / contact schaaks
	Bitboard dame_schaak = (Schaakvelden_L | Schaakvelden_T) & ai.aanval[Us][QUEEN];
	if (dame_schaak)
	{
		if (dame_schaak & ~ai.aanval[Them][KING])
		{
			if (dame_schaak & ~ai.aanval[Them][ALL_PIECES])
				aanvalIndex += 86;
			else
				aanvalIndex += 38;
		}
		dame_schaak &= ai.aanval[Them][KING] & (pos.pieces(Them, QUEEN) | ai.dubbelAanval[Us]) & ~ai.aanval[Them][PIECES_WITHOUT_KING];
		if (dame_schaak)
			aanvalIndex += 120;
	}

	if (aanvalIndex < 0)
		aanvalIndex = 0;
	if (aanvalIndex > 1000)
		aanvalIndex = 1000;

	return Evaluation::VeiligheidTabel[aanvalIndex];
}


inline EvalValue Initiative(const Position& pos, const Pawns::Tuple *pionTuple, EvalValue eg)
{
	int k_afstand = file_distance(pos.koning(WHITE), pos.koning(BLACK))
		- rank_distance(pos.koning(WHITE), pos.koning(BLACK));

	int initiatief = (2 * pionTuple->asymmetrie + k_afstand + 3 * pos.aantal(eg < 0 ? B_PAWN : W_PAWN) - 15) * 38;

	int bonus = ((eg > 0) - (eg < 0)) * std::max(initiatief, -abs(eg / 2));

	return EvalValue(bonus);
}


template<Color Us>
inline Score StrongSquares(const Position& pos, const AttackInfo& ai, const Pawns::Tuple *pionTuple)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Bitboard RANK456 = (Us == WHITE ? 0x3C3C3C000000 : 0x3C3C3C0000);
	const Score Score_PL_achter_Pion       = make_score(32,  8);
	const Score Score_Ondersteunde_Stukken = make_score(55, 29);
	const Score Score_SterkVeld_PL         = make_score(62, 70);
	const Score Score_SterkVeld_PL_Extra   = make_score(156, 74);
	const Score Score_VeiligVoorPion_TLP   = make_score(35, 27);
	const Score Score_SterkP_voor_Pion     = make_score(14, 34);

	Score score = SCORE_ZERO;
	score += Score_VeiligVoorPion_TLP * popcount(pionTuple->safe_from_pawn(Them) & pos.pieces(Us, KNIGHT, BISHOP, ROOK));
	score += Score_SterkP_voor_Pion * popcount(pionTuple->safe_from_pawn(Them) & pos.pieces(Us, KNIGHT) & shift_down<Us>(pos.pieces(Them, PAWN)));

	Bitboard sterkePL = pionTuple->safe_from_pawn(Them) & ai.aanval[Us][PAWN] & pos.pieces(Us, KNIGHT, BISHOP) & RANK456;
	if (sterkePL)
	{
		score += Score_SterkVeld_PL * popcount(sterkePL);
		if (!pos.pieces(Them, KNIGHT))
		{
			do
			{
				Square veld = pop_lsb(&sterkePL);
				Bitboard velden_zelfde_kleur = (DarkSquares & veld) ? DarkSquares : ~DarkSquares;
				if (!(pos.pieces(Them, BISHOP) & velden_zelfde_kleur))
					score += Score_SterkVeld_PL_Extra;
			} while (sterkePL);
		}
	}

	// P en L achter eigen pion
	score += Score_PL_achter_Pion * popcount(pos.pieces(Us, KNIGHT) & pion_aanval<Them>(pos.pieces(Us, PAWN)));
	score += Score_PL_achter_Pion * popcount(pos.pieces(Us, KNIGHT, BISHOP) & shift_down<Us>(pos.pieces(Us, PAWN)));
	score += Score_Ondersteunde_Stukken * popcount(pos.pieces_except(Us, PAWN) & ai.aanval[Us][PAWN]);

	return score;
}


template<Color Us>
inline Bitboard ZoneControl(const Position& pos, const AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Bitboard Rank2to4 = Us == WHITE ? 0x00000000ffffff00 : 0x00ffffff00000000;

	Bitboard veilig = Rank2to4 & ~(pos.pieces(Us, PAWN) | ai.aanval[Them][PAWN]
		| (ai.aanval[Them][ALL_PIECES] & ~ai.aanval[Us][ALL_PIECES]));

	Bitboard afgeschermd = pos.pieces(Us, PAWN);
	afgeschermd |= (Us == WHITE ? afgeschermd >> 8 : afgeschermd << 8);
	afgeschermd |= (Us == WHITE ? afgeschermd >> 16 : afgeschermd << 16);
	afgeschermd &= veilig;

	return veilig | (Us == WHITE ? afgeschermd << 32 : afgeschermd >> 32);
}


template<Color Us>
inline Score Space(const Position& pos, const AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Bitboard CenterZone = Us == WHITE ? 0x000000003c3c3c00 : 0x003c3c3c00000000;

	Bitboard veilig = CenterZone & ~(pos.pieces(Us, PAWN) | ai.aanval[Them][PAWN]
		| (ai.aanval[Them][ALL_PIECES] & ~ai.aanval[Us][ALL_PIECES]));

	Bitboard afgeschermd = pos.pieces(Us, PAWN);
	afgeschermd |= (Us == WHITE ? afgeschermd >> 8 : afgeschermd << 8);
	afgeschermd |= (Us == WHITE ? afgeschermd >> 16 : afgeschermd << 16);
	afgeschermd &= veilig;

	unsigned int beheersing = popcount(veilig | (Us == WHITE ? afgeschermd << 32 : afgeschermd >> 32));
	unsigned int weight = popcount(pos.pieces(Us));
	Score score = remake_score(EvalValue(weight * weight * 3 / 16), EVAL_0);
	return score * beheersing;
}


template<Color Us>
inline void eval_init(const Position& pos, AttackInfo& ai, const Pawns::Tuple *pionTuple)
{
	ai.aanval[Us][KING] = pos.attack_from<KING>(pos.koning(Us));
	ai.aanval[Us][PAWN] = pionTuple->pion_aanval(Us);
	ai.aanval[Us][KNIGHT] = 0;
	ai.aanval[Us][BISHOP] = 0;
	ai.aanval[Us][ROOK] = 0;
	ai.aanval[Us][QUEEN] = 0;
	ai.aanval[Us][PIECES_WITHOUT_KING] = ai.aanval[Us][PAWN];
	ai.dubbelAanval[Us] = 0;

	ai.gepend[Us] = pos.info()->xRay[Us];
	ai.KZone[Us] = KoningZone[pos.koning(Us)];
	//ai.KZoneExtra[IK] = ai.KZone[IK] | shift_up<IK>(ai.KZone[IK]);

	ai.KAanvalScore[Us] = 0;
	//ai.mobiliteit[IK] = 0;
}


template<Color Us>
inline Score eval_Knights(const Position& pos, AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	Score score = SCORE_ZERO;

	Bitboard velden = pos.pieces(Us, KNIGHT);
	assert(velden);
	do
	{
		Square veld = pop_lsb(&velden);
		score += Score_Afstand_P_K[distance(veld, pos.koning(Us))];
		Bitboard aanval = pos.attack_from<KNIGHT>(veld);
		if (aanval & ai.KZone[Them])
			ai.KAanvalScore[Us] += 3 * 8;
		//else if (aanval & ai.KZoneExtra[JIJ])
		//	ai.KAanvalScore[IK] += 8;
		ai.aanval[Us][KNIGHT] |= aanval;
		ai.dubbelAanval[Us] |= ai.aanval[Us][PIECES_WITHOUT_KING] & aanval;
		ai.aanval[Us][PIECES_WITHOUT_KING] |= aanval;

		unsigned int mobiliteit;
		if (ai.gepend[Us] & veld)
			mobiliteit = 0;
		else
		{
			aanval &= ai.mobiliteitMask[Us];
			// voorwaartse mobiliteit telt dubbel
			mobiliteit = popcount(aanval) + popcount(aanval & bb_ranks_forward(Us, veld));
		}
		score += mobi_P[(mobiliteit * MobiMult_P[relative_square(Us, veld)] + 16) / 32];
		//ai.mobiliteit[IK] += mobiliteit - P_GEMIDDELD_MOBILITEIT;
	} while (velden);

	return score;
}


template<Color Us>
inline Score eval_Bishops(const Position& pos, AttackInfo& ai, const Pawns::Tuple *pionTuple)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	Score score = SCORE_ZERO;

	Bitboard velden = pos.pieces(Us, BISHOP);
	assert(velden);

	if (shift_up<Us>(pos.pieces(Us, KING)) & velden)
		score += make_score(73, 0);
		//score += maak_score(33, 0);
	if (Us == WHITE)
	{
		if (velden & bb2(SQ_A1, SQ_H1))
		{
			if ((pos.pieces(Us) & (velden << 9)) & SQ_B2)
				score -= make_score(63, 96);
			if ((pos.pieces(Us) & (velden << 7)) & SQ_G2)
				score -= make_score(63, 96);
		}
	}
	else
	{
		if (velden & bb2(SQ_A8, SQ_H8))
		{
			if ((pos.pieces(Us) & (velden >> 7)) & SQ_B7)
				score -= make_score(63, 96);
			if ((pos.pieces(Us) & (velden >> 9)) & SQ_G7)
				score -= make_score(63, 96);
		}
	}

	do
	{
		Square veld = pop_lsb(&velden);
		score += Score_Afstand_L_K[distance(veld, pos.koning(Us))];

		Bitboard bb_penning_TD = LegeAanval[BISHOP][veld] & pos.pieces(Them, ROOK, QUEEN);
		while (bb_penning_TD)
		{
			Square veld_TD = pop_lsb(&bb_penning_TD);
			Bitboard b = bb_between(veld_TD, veld) & pos.pieces();
			if (b && !more_than_one(b))
				score += Score_Loper_Penning[Us][pos.piece_on_square(lsb(b))];
		}

		Bitboard aanval = attack_bb_bishop(veld, pos.pieces(PAWN));
		score += mobi_L1[(popcount(aanval) * MobiMult_L1[relative_square(Us, veld)] + 16) / 32];
		//score += mobi_L1[std::min((int)(popcount(aanval) * MobiMult_L1[relatief_veld(Us, veld)] + 16) / 32, 255)];

		if (pos.pieces(PAWN) & opgesloten_loper_b3_c2[Us][veld])
		{
			if (pos.pieces(PAWN) & opgesloten_loper_b3_c2_extra[veld])
				score -= make_score(665, 665);
			else
				score -= make_score(315, 315);
		}
#if 0
		else if (opgesloten_loper_b4_d2[Us][veld] && (pos.pieces(PAWN) & opgesloten_loper_b4_d2[Us][veld]) == opgesloten_loper_b4_d2[Us][veld])
		{
			if (pos.pieces(PAWN) & opgesloten_loper_b4_d2_extra[veld])
				score -= make_score(600, 600);
			else
				score -= make_score(300, 300);
		}
#endif
#if 0
		else
		{
			aanval &= ~(ai.aanval[Them][PAWN] | pos.pieces(Us, PAWN));
			if (!more_than_one(aanval))
			{
				if (aanval == 0)
					score -= make_score(250, 350);
				else
					score -= make_score(80, 135);
			}
		}
#endif

		//aanval = pos.aanval_van<LOPER>(veld);
		aanval = attack_bb_bishop(veld, pos.pieces() ^ pos.pieces(Us, QUEEN));
		if (aanval & ai.KZone[Them])
			ai.KAanvalScore[Us] += 8;
		//else if (aanval & ai.KZoneExtra[JIJ])
		//	ai.KAanvalScore[IK] += 4;
		ai.aanval[Us][BISHOP] |= aanval;
		ai.dubbelAanval[Us] |= ai.aanval[Us][PIECES_WITHOUT_KING] & aanval;
		ai.aanval[Us][PIECES_WITHOUT_KING] |= aanval;

		aanval &= ai.mobiliteitMask[Us];
		if (ai.gepend[Us] & veld)
			aanval &= bb_between(pos.koning(Us), veld);
		unsigned int mobiliteit = popcount(aanval);
		score += mobi_L2[(mobiliteit * MobiMult_L2[relative_square(Us, veld)] + 16) / 32];
		//ai.mobiliteit[IK] += mobiliteit - L_GEMIDDELD_MOBILITEIT;

		Score pionnenOpKleur = Score_PionOpKleurLoper[pionTuple->pawns_on_color(Us, veld)];
		score += pionnenOpKleur;
		score += Score_PionAndereKleurLoper[pionTuple->pawns_not_on_color(Us, veld)];

		// als ik loperpaar heb en jij hebt geen loper op dezelfde kleur
		// na ruil van lopers blijf ik zitten met slechte loper
		Bitboard velden_zelfde_kleur = (DarkSquares & veld) ? DarkSquares : ~DarkSquares;
		if ((pos.pieces(Us, BISHOP) & ~velden_zelfde_kleur) && !(velden_zelfde_kleur & pos.pieces(Them, BISHOP)))
			score += pionnenOpKleur;

		if (pos.pieces(Them, KNIGHT) & bb_L_domineert_P[Us][veld])
			// extra score voor vijandelijk Paard dat 3 rijen voor de loper staat (wordt gedomineerd door Loper)
			score += make_score(20, 19);
	} while (velden);

	return score;
}


template<Color Us>
inline Score eval_Rooks(const Position& pos, AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	Score score = SCORE_ZERO;
	Bitboard velden = pos.pieces(Us, ROOK);
	assert(velden);

	// T opgesloten naast K
#if 1
	if (pos.pieces(Us, KING) & (Us == WHITE ? bb2(SQ_F1, SQ_G1) : bb2(SQ_F8, SQ_G8)) && velden & (Us == WHITE ? 0xC0C0 : 0xC0C0000000000000))
		score -= make_score(282, 101);
#else
	Bitboard bbx = opgesloten_toren[pos.veld<KING>(Us)] & velden;
	if (bbx)
	{
		Square veld = rearmost_square(Us, bbx);
		bbx = bb_forward(Us, veld) & pos.pieces(Us, PAWN);
		if (bbx)
		{
			veld = rearmost_square(Us, bbx);
			Rank r = relative_rank(Us, veld);
			if (!pos.castling_possible(Us))  // test nodig voor Chess960
				score -= make_score(110 * (6 - r), 0);
			else
				score -= make_score(100 + 20 * (6 - r), 0);
		}
	}
#endif

	do
	{
		Square veld = pop_lsb(&velden);

		//Bord bb_penning_D = LegeAanval[TOREN][veld] & pos.stukken(JIJ, DAME);
		//while (bb_penning_D)
		//{
		//	Veld veld_D = pop_lsb(&bb_penning_D);
		//	Bord b = bb_tussen(veld_D, veld) & pos.stukken();
		//	if (b && !meer_dan_een(b))
		//		score += Score_Toren_Penning[IK][pos.stuk_op_veld(lsb(b))];
		//}

		Bitboard aanval = attack_bb_rook(veld, pos.pieces() ^ pos.pieces(Us, ROOK, QUEEN));
		if (aanval & ai.KZone[Them])
			ai.KAanvalScore[Us] += 8;
		//else if (aanval & ai.KZoneExtra[JIJ])
		//	ai.KAanvalScore[IK] += 4;
		ai.aanval[Us][ROOK] |= aanval;
		ai.dubbelAanval[Us] |= ai.aanval[Us][PIECES_WITHOUT_KING] & aanval;
		ai.aanval[Us][PIECES_WITHOUT_KING] |= aanval;

		aanval &= ai.mobiliteitMask[Us];
		if (ai.gepend[Us] & veld)
			aanval &= bb_between(pos.koning(Us), veld);
		unsigned int mobiliteit = popcount(aanval);
		score += mobi_T[(mobiliteit * MobiMult_T[relative_square(Us, veld)] + 16) / 32];
		//ai.mobiliteit[IK] += mobiliteit - T_GEMIDDELD_MOBILITEIT;

		// koning op achtste rij, T op zevende
		const Bitboard EIGHTH_RANK = Us == WHITE ? Rank8BB : Rank1BB;
		if (relative_rank(Us, veld) == RANK_7 && pos.pieces(Them, KING) & EIGHTH_RANK)
			score += make_score(64, 163);

		// T op lijn K, als er geen eigen pionnen tussen staan
		if (lijn(veld) == lijn(pos.koning(Them)) && !(pos.pieces(Us, PAWN) & bb_between(pos.koning(Them), veld)))
			ai.KAanvalScore[Us] += 2 * 8;

		if (!(bb_file(veld) & pos.pieces(Us, PAWN)))
		{
			Bitboard pion = pos.pieces(Them, PAWN) & bb_file(veld);
			if (!pion)
				// open lijn
				score += make_score(185, 157);
			else if (pion & ai.aanval[Them][PAWN])
				// halfopen verdedigd door pion
				score += make_score(20, 44);
			else
				// halfopen lijn
				score += make_score(112, 94);
		}
	} while (velden);

	return score;
}


template<Color Us>
inline Score eval_Queens(const Position& pos, AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	Score score = SCORE_ZERO;

	Bitboard velden = pos.pieces(Us, QUEEN);
	assert(velden);

	Bitboard mobiliteit_mask_D = ~(ai.aanval[Them][BISHOP] | ai.aanval[Them][ROOK] | pos.pieces(Us, KING, PAWN) | ai.aanval[Them][PAWN] | ai.aanval[Them][KNIGHT]);
	ai.KAanvalScore[Us] += 3 * 8;

	do
	{
		Square veld = pop_lsb(&velden);
		//Bord aanval = pos.aanval_van<DAME>(veld);
		Bitboard aanval = attack_bb_rook(veld, pos.pieces() ^ pos.pieces(Us, QUEEN)) | attack_bb_bishop(veld, pos.pieces() ^ pos.pieces(Us, QUEEN));
		//Bord aanval = aanval_bb<TOREN>(veld, pos.stukken() ^ pos.stukken(IK, TOREN, DAME))
		//	| aanval_bb<LOPER>(veld, pos.stukken());
		if (aanval & ai.KZone[Them])
			ai.KAanvalScore[Us] += 8;
		//else if (aanval & ai.KZoneExtra[JIJ])
		//	ai.KAanvalScore[IK] += 4;
		ai.aanval[Us][QUEEN] |= aanval;
		ai.dubbelAanval[Us] |= ai.aanval[Us][PIECES_WITHOUT_KING] & aanval;
		ai.aanval[Us][PIECES_WITHOUT_KING] |= aanval;

		aanval &= mobiliteit_mask_D;
		if (ai.gepend[Us] & veld)
			aanval &= bb_between(pos.koning(Us), veld);

		const Bitboard CENTER_BOX = 0x00003C3C3C3C0000;
		unsigned int mobiliteit = popcount(aanval) + popcount(aanval & CENTER_BOX);
		score += mobi_D[(mobiliteit * MobiMult_D[relative_square(Us, veld)] + 32) / 64];
		//ai.mobiliteit[IK] += mobiliteit - D_GEMIDDELD_MOBILITEIT;
	} while (velden);

	return score;
}


Value Evaluation::evaluatie(const Position& pos, Value alfa, Value beta)
{
	assert(!pos.check_givers());

	Material::Tuple* materiaalTuple;
	Pawns::Tuple* pionTuple;

	materiaalTuple = Material::probe(pos);

	PositionInfo* st = pos.info();
	st->eval_is_exact = false;

	if (materiaalTuple->has_value_function())
	{
		st->sterkeDreiging = 0;
		return materiaalTuple->value_from_function(pos);
	}

	bool doLazyEval = beta < WIN_VALUE && (st - 1)->evalPositioneel != NO_EVAL && alfa > -WIN_VALUE
		//&& st->geslagenStuk
		&& pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) > 2 * MATL_BISHOP
		&& !(pos.pieces(WHITE, PAWN) & Rank7BB) && !(pos.pieces(BLACK, PAWN) & Rank2BB);

	if (doLazyEval)
	{
		EvalValue v = (st - 1)->evalPositioneel;
		int evalFactor = (st - 1)->evalFactor;
#if 0
		// laat meerdere lazyEvals achter elkaar toe
		st->evalPositioneel = v;
		st->evalFactor = evalFactor;
#endif
		v += materiaalTuple->waarde * evalFactor / MAX_FACTOR;

		if (pos.side_to_move() == BLACK)
			v = -v;

		Value lazyResult = ValueFromEval(v) + VALUE_TEMPO;

		if (lazyResult <= alfa || lazyResult >= beta)
		{
			st->sterkeDreiging = 0;
			return lazyResult;
		}
	}

	pionTuple = Pawns::probe(pos);

#if 0
	const EvalValue InstantEvalLimit = 6 * PAWN_EVAL;
	EvalValue instantWaarde = materiaalTuple->waarde * materiaalTuple->conversie / MAX_FACTOR +
		mg_value(pionTuple->pawns_score() + pos.psq_score());
	if (abs(instantWaarde) > InstantEvalLimit)
		return ValueFromEval(pos.side_to_move() == BLACK ? -instantWaarde : instantWaarde);
#endif

	Score koningVeiligheid = pionTuple->koning_veiligheid<WHITE>(pos);
	koningVeiligheid -= pionTuple->koning_veiligheid<BLACK>(pos);


	// evaluatie stukken begint hier
	// =============================
	AttackInfo ai;
	
	eval_init<WHITE>(pos, ai, pionTuple);
	eval_init<BLACK>(pos, ai, pionTuple);

	ai.mobiliteitMask[WHITE] = ~(ai.aanval[BLACK][PAWN] | (pos.pieces(WHITE, PAWN) & shift_down<WHITE>(pos.pieces())))
		| pos.pieces_except(BLACK, PAWN);
	ai.mobiliteitMask[BLACK] = ~(ai.aanval[WHITE][PAWN] | (pos.pieces(BLACK, PAWN) & shift_down<BLACK>(pos.pieces())))
		| pos.pieces_except(WHITE, PAWN);

	Score evalScore = pos.psq_score();

	// aftrekschaak
	//const Score Score_Aftrekschaak = maak_score(250, 250);
	//if (ai.gepend[ZWART] & pos.stukken(WIT, PAARD, LOPER, TOREN))
	//	evalScore += Score_Aftrekschaak;
	//if (ai.gepend[WIT] & pos.stukken(ZWART, PAARD, LOPER, TOREN))
	//	evalScore -= Score_Aftrekschaak;

	// paarden
	if (pos.pieces(WHITE, KNIGHT))
		evalScore += eval_Knights<WHITE>(pos, ai);
	if (pos.pieces(BLACK, KNIGHT))
		evalScore -= eval_Knights<BLACK>(pos, ai);

	// lopers
	if (pos.pieces(WHITE, BISHOP))
		evalScore += eval_Bishops<WHITE>(pos, ai, pionTuple);
	if (pos.pieces(BLACK, BISHOP))
		evalScore -= eval_Bishops<BLACK>(pos, ai, pionTuple);

	// torens
	if (pos.pieces(WHITE, ROOK))
		evalScore += eval_Rooks<WHITE>(pos, ai);
	if (pos.pieces(BLACK, ROOK))
		evalScore -= eval_Rooks<BLACK>(pos, ai);
	
	// dames
	if (pos.pieces(WHITE, QUEEN))
		evalScore += eval_Queens<WHITE>(pos, ai);
	if (pos.pieces(BLACK, QUEEN))
		evalScore -= eval_Queens<BLACK>(pos, ai);

	ai.dubbelAanval[WHITE] |= ai.aanval[WHITE][PIECES_WITHOUT_KING] & ai.aanval[WHITE][KING];
	ai.aanval[WHITE][ALL_PIECES] = ai.aanval[WHITE][PIECES_WITHOUT_KING] | ai.aanval[WHITE][KING];
	ai.dubbelAanval[BLACK] |= ai.aanval[BLACK][PIECES_WITHOUT_KING] & ai.aanval[BLACK][KING];
	ai.aanval[BLACK][ALL_PIECES] = ai.aanval[BLACK][PIECES_WITHOUT_KING] | ai.aanval[BLACK][KING];

	//evalScore += maak_score(-12, 0) * (popcount<Full>(ai.attack[WHITE][ALLE_STUKKEN]) - popcount<Full>(ai.attack[BLACK][ALLE_STUKKEN]));

	// score koningsaanval
	// ===================
#if 0
	Bitboard zoneBeheersing[2];
	zoneBeheersing[WHITE] = ZoneControl<WHITE>(pos, ai);
	zoneBeheersing[BLACK] = ZoneControl<BLACK>(pos, ai);

	int ruimte_voordeel = popcount(zoneBeheersing[WHITE] & KoningVleugelLijnen[lijn(pos.koning(BLACK))]) 
		- popcount(zoneBeheersing[BLACK] & KoningVleugelLijnen[lijn(pos.koning(BLACK))]);

	evalScore += KingAttack<WHITE>(pos, ai, ai.KAanvalScore[WHITE] - pionTuple->safety[BLACK] 
		+ ruimte_voordeel / 2
		//+ ruimte_voordeel * popcount(pos.stukken_uitgezonderd(WIT, PION)) / 8
		);

	if (KoningVleugelLijnen[lijn(pos.koning(WHITE))] != KoningVleugelLijnen[lijn(pos.koning(BLACK))])
		ruimte_voordeel = popcount(zoneBeheersing[WHITE] & KoningVleugelLijnen[lijn(pos.koning(WHITE))]) 
			- popcount(zoneBeheersing[BLACK] & KoningVleugelLijnen[lijn(pos.koning(WHITE))]);

	evalScore -= KingAttack<BLACK>(pos, ai, ai.KAanvalScore[BLACK] - pionTuple->safety[WHITE]
		- ruimte_voordeel / 2
		//- ruimte_voordeel * popcount(pos.stukken_uitgezonderd(ZWART, PION)) / 8
		);
#else
	evalScore += KingAttack<WHITE>(pos, ai, ai.KAanvalScore[WHITE] - pionTuple->safety[BLACK]);
	evalScore -= KingAttack<BLACK>(pos, ai, ai.KAanvalScore[BLACK] - pionTuple->safety[WHITE]);
#endif

	// dreigingen Wit
	// ==============
	evalScore += sf_Dreigingen<WHITE>(pos, ai);
	evalScore -= sf_Dreigingen<BLACK>(pos, ai);
	st->sterkeDreiging = ai.sterkeDreiging[WHITE] + 2 * ai.sterkeDreiging[BLACK];

	if (pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) >= 2 * MATL_QUEEN + 4 * MATL_ROOK + 2 * MATL_KNIGHT)
	{
		evalScore += Space<WHITE>(pos, ai);
		evalScore -= Space<BLACK>(pos, ai);
	}

	// vrijpionnen
	// ===========
	if (pionTuple->vrijpionnen(WHITE))
		evalScore += PassedPawns<WHITE>(pos, ai, pionTuple->vrijpionnen(WHITE));
	if (pionTuple->vrijpionnen(BLACK))
		evalScore -= PassedPawns<BLACK>(pos, ai, pionTuple->vrijpionnen(BLACK));

	// sterke velden voor P en L
	// =========================
	evalScore += StrongSquares<WHITE>(pos, ai, pionTuple);
	evalScore -= StrongSquares<BLACK>(pos, ai, pionTuple);

	// geblokkeerde pionnen
	const Score Score_Geblokkeerde_Pionnen = score_muldiv(make_score(43, 167), 128, 256);
	evalScore -= Score_Geblokkeerde_Pionnen * popcount(pos.pieces(WHITE, PAWN) & shift_down<WHITE>(pos.pieces()));
	evalScore += Score_Geblokkeerde_Pionnen * popcount(pos.pieces(BLACK, PAWN) & shift_down<BLACK>(pos.pieces()));
	//const Score Score_Geremde_Pionnen = maak_score(21, 42);
	//evalScore -= Score_Geremde_Pionnen * popcount(pos.stukken(WIT, PION) & shift_down<WIT>(
	//	ai.aanval[ZWART][ALLE_STUKKEN] & ~ai.aanval[WIT][ALLE_STUKKEN] & ~pos.stukken()));
	//evalScore += Score_Geremde_Pionnen * popcount(pos.stukken(ZWART, PION) & shift_down<ZWART>(
	//	ai.aanval[WIT][ALLE_STUKKEN] & ~ai.aanval[ZWART][ALLE_STUKKEN] & ~pos.stukken()));

	// beheersing flank koning
	Bitboard bb_flank = ai.aanval[WHITE][PIECES_WITHOUT_KING] & bb_koning_flankaanval[BLACK][lijn(pos.koning(BLACK))]
		& ~ai.aanval[BLACK][KING] & ~ai.aanval[BLACK][PAWN];
	bb_flank = (bb_flank >> 4) | (bb_flank & ai.dubbelAanval[WHITE]);
	evalScore += popcount(bb_flank) * make_score(18, 0);

	bb_flank = ai.aanval[BLACK][PIECES_WITHOUT_KING] & bb_koning_flankaanval[WHITE][lijn(pos.koning(WHITE))]
		& ~ai.aanval[WHITE][KING] & ~ai.aanval[WHITE][PAWN];
	bb_flank = (bb_flank << 4) | (bb_flank & ai.dubbelAanval[BLACK]);
	evalScore -= popcount(bb_flank) * make_score(18, 0);

	// flank zonder pion
	//if (!(pos.stukken(PION) & bb_koning_flank[lijn(pos.koning(WIT))]))
	//	evalScore -= maak_score(0, 250);
	//if (!(pos.stukken(PION) & bb_koning_flank[lijn(pos.koning(ZWART))]))
	//	evalScore += maak_score(0, 250);

#if 0
	// beheersing aanvalsvelden
	const Score Score_Aanvalsvelden = make_score(20, 0);
	Bitboard bb_centrum_pivot = bb_centrum_pivot_W[lijn(pos.koning(WHITE))] | bb_centrum_pivot_Z[lijn(pos.koning(BLACK))];
	evalScore += Score_Aanvalsvelden * popcount(bb_centrum_pivot & ai.aanval[WHITE][PIECES_WITHOUT_KING]);
	evalScore -= Score_Aanvalsvelden * popcount(bb_centrum_pivot & ai.aanval[BLACK][PIECES_WITHOUT_KING]);
#endif

#if 0
	const Score Score_L_KVleugel = make_score(35, 0);
	const Score Score_Pion_KVleugel = make_score(8, 0);
	const Bitboard KingSide = 0xF0F0F0F0F0F0F0F0;
	const Bitboard QueenSide = 0x0F0F0F0F0F0F0F0F;

	if (pos.pieces(WHITE, KING) & QueenSide)
	{
		if (pos.pieces(BLACK, KING) & QueenSide)
		{
			if (pos.pieces(WHITE, BISHOP) & DarkSquares)
				evalScore += Score_L_KVleugel;
			if (pos.pieces(BLACK, BISHOP) & ~DarkSquares)
				evalScore -= Score_L_KVleugel;
			Bitboard DP = pos.pieces(WHITE, QUEEN, KNIGHT) & QueenSide;
			if (DP)
			{
				evalScore += Score_L_KVleugel;
				if (more_than_one(DP))
					evalScore += Score_L_KVleugel;
			}
			DP = pos.pieces(BLACK, QUEEN, KNIGHT) & QueenSide;
			if (DP)
			{
				evalScore -= Score_L_KVleugel;
				if (more_than_one(DP))
					evalScore -= Score_L_KVleugel;
			}
			//evalScore += Score_Pion_KVleugel * popcount(pos.stukken(WIT, PION) & DVleugel);
			//evalScore -= Score_Pion_KVleugel * popcount(pos.stukken(ZWART, PION) & DVleugel);
		}
	}
	else if (pos.pieces(BLACK, KING) & KingSide)
	{
		if (pos.pieces(WHITE, BISHOP) & ~DarkSquares)
			evalScore += Score_L_KVleugel;
		if (pos.pieces(BLACK, BISHOP) & DarkSquares)
			evalScore -= Score_L_KVleugel;
		Bitboard DP = pos.pieces(WHITE, QUEEN, KNIGHT) & KingSide;
		if (DP)
		{
			evalScore += Score_L_KVleugel;
			if (more_than_one(DP))
				evalScore += Score_L_KVleugel;
		}
		DP = pos.pieces(BLACK, QUEEN, KNIGHT) & KingSide;
		if (DP)
		{
			evalScore -= Score_L_KVleugel;
			if (more_than_one(DP))
				evalScore -= Score_L_KVleugel;
		}
		//evalScore += Score_Pion_KVleugel * popcount(pos.stukken(WIT, PION) & KVleugel);
		//evalScore -= Score_Pion_KVleugel * popcount(pos.stukken(ZWART, PION) & KVleugel);
	}
#endif

	//evalScore += (5 - 2 * pionTuple->lijn_breedte) * maak_score(0, 40) * (pos.count<PAARD>(WHITE) - pos.count<PAARD>(BLACK));

	// positionele eval wordt maal (140/128) gedaan
	int mul = (Threads.tactischeModusIndex == 1) ? 50 : 35;
	Score score = pionTuple->pawns_score() + koningVeiligheid + score_muldiv(evalScore, mul, 32);

	// in laat eindspel (zonder dames, hoogstens 2 torens)
	if (pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) <= 4 * MATL_BISHOP)
		score += Score_PionLijnBreedte[pionTuple->pawn_range(WHITE)] - Score_PionLijnBreedte[pionTuple->pawn_range(BLACK)];

	EvalValue mg = (106 * mg_value(score) - 6 * eg_value(score)) / 100;
	EvalValue eg = (13 * mg_value(score) + 87 * eg_value(score)) / 100;

	ScaleFactor sf = compute_scalefactor(pos, materiaalTuple, materiaalTuple->waarde + eg);
	ScaleFactor conversie = materiaalTuple->conversie;

	eg += Initiative(pos, pionTuple, materiaalTuple->waarde + eg);

	if (materiaalTuple->conversie_is_geschat && !pos.pieces(QUEEN) && !(pionTuple->vrijpionnen(WHITE) | pionTuple->vrijpionnen(BLACK)))
		conversie = ScaleFactor(conversie * 115 / 128); // ongeveer 0.9
	if (pionTuple->conversie_moeilijk)
		conversie = ScaleFactor(conversie * 115 / 128); // ongeveer 0.9

	ScaleFactor evalFactor = sf == NORMAL_FACTOR ? conversie : ScaleFactor(std::min((int)conversie, (int)2 * sf));

	int fase = materiaalTuple->game_phase();
	EvalValue v = (mg * conversie / MAX_FACTOR * fase + eg * evalFactor / MAX_FACTOR * int(MIDDLEGAME_PHASE - fase)) / int(MIDDLEGAME_PHASE);
	st->evalPositioneel = v;
	st->evalFactor = evalFactor;
	v += materiaalTuple->waarde * evalFactor / MAX_FACTOR;

#if 1
	if (Threads.stukContempt)
	{
		// aantal stukken
		int contempt_aantal = 2 * pos.aantal(Threads.contemptKleur, PAWN)
			+ 2 * pos.aantal(Threads.contemptKleur, KNIGHT) + 3 * pos.aantal(Threads.contemptKleur, BISHOP)
			+ 4 * pos.aantal(Threads.contemptKleur, ROOK) + 8 * pos.aantal(Threads.contemptKleur, QUEEN);

		// vastgelopen pionnen
		//int vastgelopen_pionnen = popcount(pos.stukken(ZWART, PION) & (pos.stukken(WIT, PION) << 8)) - 2;
		//if (vastgelopen_pionnen > 0)
		//	contempt_aantal -= 5 * vastgelopen_pionnen * fase / int(MIDDENSPEL_FASE);

		int contemptScore = 4 * Threads.stukContempt * contempt_aantal * evalFactor / MAX_FACTOR;
		if (Threads.contemptKleur == WHITE)
			v += EvalValue(contemptScore);
		else
			v -= EvalValue(contemptScore);
	}
#endif

	if (pos.side_to_move() == BLACK)
		v = -v;

	// tot hier is de score in 1/1600 pion (EvalWaarde)
	// uiteindelijk resultaat in 1/200 pion (Value)
	//Waarde result = ValueFromEval(v) + Waarde((unsigned int)WAARDE_TEMPO * (unsigned int)evalFactor / MAX_FACTOR);
	Value result = ValueFromEval(v) + VALUE_TEMPO;

	// 50 zet regel
	if (pos.fifty_moves_counter() > Threads.vijftigZettenAfstand)
		result = result * (5 * (2 * Threads.vijftigZettenAfstand - pos.fifty_moves_counter()) + 6) / 256;

	// test op pat als enkel K + pionnen
	if (!pos.non_pawn_material(pos.side_to_move()))
	{
		if (pos.side_to_move() == WHITE)
		{
			if ((pos.attack_from<KING>(pos.koning(WHITE)) & ~pos.pieces(WHITE) & ~ai.aanval[BLACK][ALL_PIECES]) == 0
				&& ((pos.pieces(WHITE, PAWN) << 8) & ~pos.pieces()) == 0
				&& (((pos.pieces(WHITE, PAWN) & ~FileABB) << 7) & pos.pieces(BLACK)) == 0
				&& (((pos.pieces(WHITE, PAWN) & ~FileHBB) << 9) & pos.pieces(BLACK)) == 0)
			{
				result = DRAW_VALUE;
				st->eval_is_exact = true;
			}
		}
		else
			if ((pos.attack_from<KING>(pos.koning(BLACK)) & ~pos.pieces(BLACK) & ~ai.aanval[WHITE][ALL_PIECES]) == 0
				&& ((pos.pieces(BLACK, PAWN) >> 8) & ~pos.pieces()) == 0
				&& (((pos.pieces(BLACK, PAWN) & ~FileABB) >> 9) & pos.pieces(WHITE)) == 0
				&& (((pos.pieces(BLACK, PAWN) & ~FileHBB) >> 7) & pos.pieces(WHITE)) == 0)
			{
				result = DRAW_VALUE;
				st->eval_is_exact = true;
			}
	}

	return result;
}

Value Evaluation::evaluation_after_nullmove(Value eval)
{
	Value result = -eval + 2 * VALUE_TEMPO;

	return result;
}


void Evaluation::initialize()
{
	// Mobiliteit multiplier
	const int MobiMultConst[64] = {
		 1, 1, 1, 1, 1, 1, 1, 1,
		 1, 1, 1, 1, 1, 1, 1, 1,
		 1, 1, 1, 1, 1, 1, 1, 1,
		 1, 1, 1, 1, 1, 1, 1, 1,
		 1, 1, 1, 1, 1, 1, 1, 1,
		 1, 1, 1, 1, 1, 1, 1, 1,
		 1, 1, 1, 1, 1, 1, 1, 1,
		 1, 1, 1, 1, 1, 1, 1, 1
	};
	const int MobiMultRankQuad[64] = {
		-9,-9,-9,-9,-9,-9,-9,-9,
		-3,-3,-3,-3,-3,-3,-3,-3,
		1, 1, 1, 1, 1, 1, 1, 1,
		3, 3, 3, 3, 3, 3, 3, 3,
		3, 3, 3, 3, 3, 3, 3, 3,
		1, 1, 1, 1, 1, 1, 1, 1,
		-3,-3,-3,-3,-3,-3,-3,-3,
		-9,-9,-9,-9,-9,-9,-9,-9
	};
	const int MobiMultFileQuad[64] = {
		-9,-3, 1, 3, 3, 1,-3,-9,
		-9,-3, 1, 3, 3, 1,-3,-9,
		-9,-3, 1, 3, 3, 1,-3,-9,
		-9,-3, 1, 3, 3, 1,-3,-9,
		-9,-3, 1, 3, 3, 1,-3,-9,
		-9,-3, 1, 3, 3, 1,-3,-9,
		-9,-3, 1, 3, 3, 1,-3,-9,
		-9,-3, 1, 3, 3, 1,-3,-9
	};
	const int MobiMultCenter[64] = {
		4, 3, 2, 1, 1, 2, 3, 4,
		3, 2, 1, 0, 0, 1, 2, 3,
		2, 1, 0,-1,-1, 0, 1, 2,
		1, 0,-1,-2,-2,-1, 0, 1,
		1, 0,-1,-2,-2,-1, 0, 1,
		2, 1, 0,-1,-1, 0, 1, 2,
		3, 2, 1, 0, 0, 1, 2, 3,
		4, 3, 2, 1, 1, 2, 3, 4
	};
	const int MobiMultRank[64] = {
		-3,-3,-3,-3,-3,-3,-3,-3,
		-2,-2,-2,-2,-2,-2,-2,-2,
		-1,-1,-1,-1,-1,-1,-1,-1,
		 0, 0, 0, 0, 0, 0, 0, 0,
		 1, 1, 1, 1, 1, 1, 1, 1,
		 2, 2, 2, 2, 2, 2, 2, 2,
		 3, 3, 3, 3, 3, 3, 3, 3,
		 4, 4, 4, 4, 4, 4, 4, 4
	};
	const int MobiMultRand[64] = {
		-3,-3,-3,-3,-3,-3,-3,-3,
		-3,-1,-1,-1,-1,-1,-1,-3,
		-3,-1, 1, 1, 1, 1,-1,-3,
		-3,-1, 1, 3, 3, 1,-1,-3,
		-3,-1, 1, 3, 3, 1,-1,-3,
		-3,-1, 1, 1, 1, 1,-1,-3,
		-3,-1,-1,-1,-1,-1,-1,-3,
		-3,-3,-3,-3,-3,-3,-3,-3
	};
	for (int n = 0; n < 64; n++)
	{
		MobiMult_P[n] = 270 * MobiMultConst[n] + (0 * MobiMultRankQuad[n] + 6 * MobiMultFileQuad[n]) / 2
			- 2 * MobiMultCenter[n] + 0 * MobiMultRank[n] + 5 * MobiMultRand[n];
		MobiMult_L1[n] = 256;
		//MobiMult_L1[n] = 271 * MobiMultConst[n] + (-5 * MobiMultRankQuad[n] + 0 * MobiMultFileQuad[n]) / 2
		//	+ 0 * MobiMultCenter[n] + 0 * MobiMultRank[n] + 0 * MobiMultRand[n];
		MobiMult_L2[n] = 249 * MobiMultConst[n] + (-8 * MobiMultRankQuad[n] - 3 * MobiMultFileQuad[n]) / 2
			+ 0 * MobiMultCenter[n] - 3 * MobiMultRank[n] - 4 * MobiMultRand[n];
		MobiMult_T[n] = 255 * MobiMultConst[n] + (1 * MobiMultRankQuad[n] - 5 * MobiMultFileQuad[n]) / 2
			- 6 * MobiMultCenter[n] - 1 * MobiMultRank[n] - 2 * MobiMultRand[n];
		MobiMult_D[n] = 272 * MobiMultConst[n] + (-2 * MobiMultRankQuad[n] + 4 * MobiMultFileQuad[n]) / 2
			+ 1 * MobiMultCenter[n] - 2 * MobiMultRank[n] - 8 * MobiMultRand[n];

		//int MobiMult = TUNE_1 * MobiMultConst[n] + (TUNE_2 * MobiMultRankQuad[n] + TUNE_3 * MobiMultFileQuad[n]) / 2
		//	+ TUNE_4 * MobiMultCenter[n] + TUNE_5 * MobiMultRank[n] + TUNE_6 * MobiMultRand[n];
		//MobiMult_L1[n] = std::max(std::min(MobiMult, 512), 64);
	}
	for (int n = 0; n < 256; n++)
	{
		double v38 = sqrt(0.125 * n + 1.5) - sqrt(1.5);
		mobi_P[n] = make_score(std::lround(v38 * 207.32 - 417.0), std::lround(v38 * 252.68 - 509.0));
		mobi_T[n] = make_score(std::lround(v38 * 125.90 - 190.0) * 7 / 8, std::lround(v38 * 218.96 - 331.0) * 7 / 8);
		mobi_L2[n] = make_score(std::lround(v38 * 221.48 - 374.0), std::lround(v38 * 203.99 - 344.0));
		mobi_L1[n] = make_score(std::lround(v38 * 92.43 - 171.0), std::lround(v38 * 104.75 - 194.0));
		v38 = sqrt(0.25 * n + 1.5) - sqrt(1.5);
		mobi_D[n] = make_score(std::lround(v38 * 203.42 - 616.0), std::lround(v38 * 165.33 - 555.0));
	}

	for (int n = 0; n < 8; n++)
	{
		Score_Afstand_P_K[n] = make_score(11, 7) * (3 - n);
		Score_Afstand_L_K[n] = make_score(1, 6) * (3 - n);
	}

	for (int n = 0; n < 9; n++)
	{
		Score_PionOpKleurLoper[n] = make_score(-17, -27) * (n - 2);
		Score_PionAndereKleurLoper[n] = make_score(29, 11) * (n - 2);
		Score_PionLijnBreedte[n] = make_score(0, 2 * (n > 5 ? 9 * n - 36 : n * n - 16));
		Score_Dreigingen[n] = make_score(138, 123) * (n > 1 ? n + 2 : n);
	}

	for (int n = 0; n <= 5; n++)
	{
		Score_Vrijpion_niet_DvD[n] = make_score(49 * (n - 1) * n, 34 * (n * n + 1));
		Score_Vrijpion_DvD[n]      = make_score(46 * (n - 1) * n, 43 * (n * n + 1));

		Score_Vrijpion_vrije_doorgang[n]     = make_score(1, 3) * (n - 1) * n + score_muldiv(make_score(10, 60) * (n - 1) * n, 272, 256);
		Score_Vrijpion_opmars_ondersteund[n] = make_score(1, 3) * (n - 1) * n + score_muldiv(make_score(10, 36) * (n - 1) * n, 304, 256);
		Score_Vrijpion_opmars_gestuit[n]     = make_score(1, 3) * (n - 1) * n;

		for (int distance = 0; distance < 8; distance++)
		{
			int ondersteuning = 30 * VrijpionNabijheid[distance];
			double vrije_baan = (sqrt(distance + 1.0) - 1.0) * (n - 1) * n;
			Score_Vrijpion_Mijn_K[n][distance] = make_score(0, (ondersteuning - std::lround(vrije_baan * 40)) * 32 / 35);
			Score_Vrijpion_Jouw_K[n][distance] = make_score(0, (std::lround(vrije_baan * 76) - ondersteuning) * 32 / 35);
		}
	}

#if !defined(USE_LICENSE) && !defined(IMPORT_TABLES)
#if 1
	int vprev = 0;
	for (int n = 0; n < 128; n++)
	{
		int v = 13 * KoningGevaar[n] / 2;
		VeiligheidTabel[8 * n] = make_score(v, 0);
		if (n > 0)
			for (int i = 1; i < 8; i++)
				VeiligheidTabel[8 * n - 8 + i] = make_score((i * v + (8 - i) * vprev) / 8, 0);
		vprev = v;
	}
#else
	const double MaxSlope = 25 * 6.5 / 8;
	const double InflectionPoint = 29 * 8;
	const int MaxValue = std::lround(1400 * 6.5);
	int prev_waarde = 0;
	for (int n = 0; n < 1024; n++)
	{
		int waarde;
		if (n > InflectionPoint)
			waarde = prev_waarde + std::lround(MaxSlope);
		else {
			const double d2 = 3.1416 / 4 / InflectionPoint;
			waarde = std::lround(d2 * MaxSlope * pow(sin(d2 * n) / d2, 2));
		}
		VeiligheidTabel[n] = make_score(std::min(MaxValue, waarde), 0);
		prev_waarde = waarde;
	}
#endif
#endif

#if 0
	for (File lijn = FILE_A; lijn <= FILE_H; ++lijn)
	{
		bb_centrum_pivot_W[lijn] = bb4(SQ_D4, SQ_D5, SQ_E4, SQ_E5)
			| (lijn >= FILE_E ? bb3(SQ_F4, SQ_E3, SQ_F3) : bb3(SQ_C4, SQ_D3, SQ_C3));
		bb_centrum_pivot_Z[lijn] = bb4(SQ_D4, SQ_D5, SQ_E4, SQ_E5)
			| (lijn >= FILE_E ? bb3(SQ_F5, SQ_E6, SQ_F6) : bb3(SQ_C5, SQ_D6, SQ_C6));
	}
#endif
}

void Evaluation::init_tune()
{
	Evaluation::initialize();
	Pawns::initialize();
	PST::initialize();
	Search::initialize();
}
