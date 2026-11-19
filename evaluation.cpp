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
	Score SafetyTable[1024];
}

namespace
{
	unsigned int MobiMult_N[64], MobiMult_B1[64], MobiMult_B2[64], MobiMult_R[64], MobiMult_Q[64];
	Score mobi_N[256], mobi_B1[256], mobi_B2[256], mobi_R[256], mobi_Q[256];

	Score Score_Distance_N_K[8], Score_Distance_B_K[8];
	Score Score_PawnOnColorBishop[9], Score_PawnOtherColorBishop[9];
	Score Score_PawnFileWidth[9];

	Score Score_PassedPawn_free_passage[8];
	Score Score_PassedPawn_advance_supported[8];
	Score Score_PassedPawn_advance_blocked[8];
	Score Score_PassedPawn_not_QvQ[8];
	Score Score_PassedPawn_QvQ[8];

	Score Score_Threats[9];

	static const int KingDanger[256] = {
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

	static const Bitboard bb_B_dominates_N[2][64] = 
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

	static const Bitboard trapped_bishop_b3_c2[2][64] =
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
	static const Bitboard trapped_bishop_b3_c2_extra[64] =
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
	static const Bitboard trapped_bishop_b4_d2[2][64] =
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
	static const Bitboard trapped_bishop_b4_d2_extra[64] =
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

	static const Bitboard trapped_rook[64] =
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


	ScaleFactor compute_scalefactor(const Position& pos, const Material::Tuple* materialTuple, EvalValue value) {

		Color strongSide = value > DRAW_EVAL ? WHITE : BLACK;
		ScaleFactor sf = materialTuple->scalefactor_from_function(pos, strongSide);

		if (abs(value) <= BISHOP_EVAL && (sf == NORMAL_FACTOR || sf == ONE_PAWN_FACTOR))
		{
			if (pos.opposite_bishops())
			{
				if (pos.non_pawn_material(WHITE) == MATL_BISHOP
					&& pos.non_pawn_material(BLACK) == MATL_BISHOP)
					sf = pos.count(strongSide, PAWN) > 1 ? ScaleFactor(50) : ScaleFactor(12);

				else
					sf = ScaleFactor(3 * sf / 4);
			}
			else if (pos.count(strongSide, PAWN) <= 2
				&& !pos.is_passed_pawn(~strongSide, pos.king(~strongSide)))
				sf = ScaleFactor(58 + 11 * pos.count(strongSide, PAWN));
		}

		return sf;
	}

#define S(mg, eg) make_score(mg, eg)
	const Score PieceThreat[PIECE_TYPE_N] = {
		S(0, 0), S(0, 0), S(7, 110), S(161, 125), S(165, 138), S(266, 328), S(188, 375)
	};
	const Score RookThreat[PIECE_TYPE_N] = {
		S(0, 0), S(0, 0), S(5, 83), S(148, 191), S(148, 181), S(7, 113), S(128, 146)
	};
	const Score PawnThreat[PIECE_TYPE_N] = {
		S(0, 0), S(0, 0), S(0, 0), S(623, 393), S(469, 371), S(780, 640), S(732, 636)
	};

	const Score Score_Bishop_Pin[COLOR_N][PIECE_N] = {
		{ S(0, 0), S(0, 0), S(0, 0), S(90, 90), S(0, 0), S(60, 60), S(60, 60), S(0, 0),
		  S(0, 0), S(0, 0), S(0, 0), S(120,120), S(0, 0), S(225,255), S(225,255), S(0, 0)
		},
		{ S(0, 0), S(0, 0), S(0, 0), S(120,120), S(0, 0), S(225,255), S(225,255), S(0, 0),
		  S(0, 0), S(0, 0), S(0, 0), S(90, 90), S(0, 0), S(60, 60), S(60, 60), S(0, 0),
		}
	};
	const Score Score_Rook_Pin[COLOR_N][PIECE_N] = {
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

	static const int PassedPawnProximity[8] = { 13, 9, 6, 4, 3, 2, 1, 0 };
	static const Score Score_Pawn_PassedPawn_perFile[8] = {
		make_score(0, 60 * 32 / 35), make_score(0, 20 * 32 / 35), make_score(0, -20 * 32 / 35), make_score(0, -60 * 32 / 35),
		make_score(0, -60 * 32 / 35), make_score(0, -20 * 32 / 35), make_score(0, 20 * 32 / 35), make_score(0, 60 * 32 / 35)
	};

	Score Score_PassedPawn_Our_K[6][8];
	Score Score_PassedPawn_Their_K[6][8];

	//Bitboard bb_center_pivot_W[FILE_N], bb_center_pivot_B[FILE_N];
	
	Bitboard bb_king_flank_attack[COLOR_N][FILE_N] = {
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

	//Bitboard bb_king_flank[FILE_N] = {
	//	FileABB | FileBBB | FileCBB | FileDBB,
	//	FileABB | FileBBB | FileCBB | FileDBB,
	//	FileABB | FileBBB | FileCBB | FileDBB,
	//	FileCBB | FileDBB | FileEBB | FileFBB,
	//	FileCBB | FileDBB | FileEBB | FileFBB,
	//	FileEBB | FileFBB | FileGBB | FileHBB,
	//	FileEBB | FileFBB | FileGBB | FileHBB,
	//	FileEBB | FileFBB | FileGBB | FileHBB
	//};

	//static const int N_AVERAGE_MOBILITY = 8;
	//static const int B_AVERAGE_MOBILITY = 6;
	//static const int R_AVERAGE_MOBILITY = 7;
	//static const int Q_AVERAGE_MOBILITY = 11;

	const Bitboard ThreeFiles = FileABB | FileBBB | FileCBB;

	const Bitboard KingFlankFiles[FILE_N] =
	{
		ThreeFiles, ThreeFiles, ThreeFiles << 1, ThreeFiles << 2,
		ThreeFiles << 3, ThreeFiles << 4, ThreeFiles << 5, ThreeFiles << 5
	};

} // namespace


template<Color Us>
Bitboard compute_attack(const Position& pos)
{
	const Square* pSquare;
	Square square;
	Bitboard attack;

	attack = pos.attack_from<KING>(pos.king(Us));
	attack |= pawn_attack<Us>(pos.pieces(Us, PAWN));

	pSquare = pos.piece_list(Us, KNIGHT);
	while ((square = *pSquare++) != SQ_NONE)
		attack |= pos.attack_from<KNIGHT>(square);

	pSquare = pos.piece_list(Us, BISHOP);
	while ((square = *pSquare++) != SQ_NONE)
		attack |= pos.attack_from<BISHOP>(square);

	pSquare = pos.piece_list(Us, ROOK);
	while ((square = *pSquare++) != SQ_NONE)
		attack |= pos.attack_from<ROOK>(square);

	pSquare = pos.piece_list(Us, QUEEN);
	while ((square = *pSquare++) != SQ_NONE)
		attack |= pos.attack_from<QUEEN>(square);

	return attack;
}


template<Color Us>
bool at_least_two_mobile_pieces(const Position& pos)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Square* pSquare;
	Square square;

	Bitboard theirAttack = compute_attack<Them>(pos);
	Bitboard pinned = pos.info()->xRay[Us];

	Bitboard safe = ~pos.pieces(Us) & ~theirAttack;
	bool mobile = false;

	Bitboard attack = pos.attack_from<KING>(pos.king(Us));
	if (attack & safe)
	{
		// king that defends an attacked piece is not mobile
		if (!(attack & pos.pieces(Us) & theirAttack))
			mobile = true;
	}

	pSquare = pos.piece_list(Us, KNIGHT);
	while ((square = *pSquare++) != SQ_NONE)
	{
		if (pinned & square)
			continue;
		if (pos.attack_from<KNIGHT>(square) & safe)
		{
			if (mobile)
				return true;
			mobile = true;
		}
	}

	pSquare = pos.piece_list(Us, BISHOP);
	while ((square = *pSquare++) != SQ_NONE)
	{
		if (pinned & square)
			continue;
		if (pos.attack_from<BISHOP>(square) & safe)
		{
			if (mobile)
				return true;
			mobile = true;
		}
	}

	pSquare = pos.piece_list(Us, ROOK);
	while ((square = *pSquare++) != SQ_NONE)
	{
		if (pinned & square)
			continue;
		if (pos.attack_from<ROOK>(square) & safe)
		{
			if (mobile)
				return true;
			mobile = true;
		}
	}

	pSquare = pos.piece_list(Us, QUEEN);
	while ((square = *pSquare++) != SQ_NONE)
	{
		if (pinned & square)
			continue;
		if (pos.attack_from<QUEEN>(square) & safe)
		{
			if (mobile)
				return true;
			mobile = true;
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
	fwrite(SafetyTable, 1, sizeof(SafetyTable), f);
	fclose(f);
}


void Evaluation::import_table()
{
	FILE *f = fopen("R:\\Schaak\\h6-veilig-export.dat", "rb");
	fread(SafetyTable, 1, sizeof(SafetyTable), f);
	fclose(f);
}


template<Color Us>
inline Score Threats(const Position& pos, const AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Score Score_UndefendedPawns = make_score(-15, -30);
	const Score Score_PiecesMustDefend = make_score(17, 42);

	Bitboard bb_not_defended_B = pos.pieces(Them) & ~ai.attack[Them][ALL_PIECES];

	Bitboard BN_Pawn_attacked_B = ai.attack[Us][BISHOP] & pos.pieces(Them, KNIGHT, PAWN);
	Bitboard BBN_Pawn_attacked_R = ai.attack[Us][ROOK] & pos.pieces(Them, BISHOP, KNIGHT, PAWN);
	Bitboard BB_Pawn_attacked_N = ai.attack[Us][KNIGHT] & pos.pieces(Them, BISHOP, PAWN);

	// black pieces attacked by WPawn, BQ attacked by WR, BQ or BR attacked by WB or WN (valuable attacked by less valuable)
	int threatsW = popcount((pos.pieces(Them) ^ pos.pieces(Them, PAWN)) & ai.attack[Us][PAWN]
		| pos.pieces(Them, QUEEN) & ai.attack[Us][ROOK]
		| pos.pieces(Them, QUEEN, ROOK) & (ai.attack[Us][BISHOP] | ai.attack[Us][KNIGHT]));
	Score score = Score_Threats[threatsW];

	// undefended black pieces attacked by a more valuable piece
	threatsW += popcount(bb_not_defended_B & (ai.attack[Us][KING] | BN_Pawn_attacked_B | BB_Pawn_attacked_N | BBN_Pawn_attacked_R
		| ai.attack[Us][QUEEN] & (pos.pieces(Them) ^ pos.pieces(Them, QUEEN))));
	score += Score_Threats[threatsW];

	score += Score_UndefendedPawns * popcount(pos.pieces(Us, PAWN) & ~ai.attack[Us][PIECES_WITHOUT_KING]);
	score += Score_PiecesMustDefend * popcount((ai.attack[Them][PIECES_WITHOUT_KING] ^ ai.attack[Them][PAWN]) & ai.attack[Us][PIECES_WITHOUT_KING] & pos.pieces(Them));

	return score;
}

template<Color Us>
Score sf_Threats(const Position& pos, AttackInfo& ai) 
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Bitboard RANK2 = (Us == WHITE ? Rank2BB : Rank7BB);
	const Bitboard RANK7 = (Us == WHITE ? Rank7BB : Rank2BB);

	const Score HangingPawnThreat = make_score(249, 182);
	//const Score PawnThreatAvg = make_score(650, 510);
	const Score KingThreatSingle = make_score(24, 206);
	const Score KingThreatMultiple = make_score(62, 458);
	const Score HangingPieces = make_score(167, 74);
	const Score PawnAdvance = make_score(108, 50);

	Score score = SCORE_ZERO;
	ai.strongThreat[Us] = false;

	Bitboard pawnThreats = pos.pieces_except(Them, PAWN) & ai.attack[Us][PAWN];
	if (pawnThreats)
	{
		Bitboard safePawns = pos.pieces(Us, PAWN) & (~ai.attack[Them][ALL_PIECES] | ai.attack[Us][ALL_PIECES]);
		Bitboard safeThreats = pawn_attack<Us>(safePawns) & pawnThreats;

		if (pawnThreats ^ safeThreats)
			score += HangingPawnThreat;

		if (safeThreats)
		{
			ai.strongThreat[Us] = true;
			//score += PawnThreatAvg * popcount(safeThreats);
		}

		while (safeThreats)
			score += PawnThreat[piece_type(pos.piece_on_square(pop_lsb(&safeThreats)))];
	}

	Bitboard supportedPieces = pos.pieces_except(Them, PAWN) & ai.attack[Them][PAWN] & ai.attack[Us][ALL_PIECES];
	Bitboard weakPieces = pos.pieces(Them) & ~ai.attack[Them][PAWN] & ai.attack[Us][ALL_PIECES];
	//weakPieces |= ai.pinned[Them];

	if (supportedPieces | weakPieces)
	{
		Bitboard b = (supportedPieces | weakPieces) & (ai.attack[Us][KNIGHT] | ai.attack[Us][BISHOP]);
		if (b & pos.pieces(Them, ROOK, QUEEN))
			ai.strongThreat[Us] = true;
		while (b)
			score += PieceThreat[piece_type(pos.piece_on_square(pop_lsb(&b)))];

		b = (pos.pieces(Them, QUEEN) | weakPieces) & ai.attack[Us][ROOK];
		if (b & pos.pieces(Them, QUEEN))
			ai.strongThreat[Us] = true;
		while (b)
			score += RookThreat[piece_type(pos.piece_on_square(pop_lsb(&b)))];

		b = weakPieces & ~ai.attack[Them][ALL_PIECES];
		if (b & pos.pieces_except(Them, PAWN))
			ai.strongThreat[Us] = true;
		score += HangingPieces * popcount(b);

		b = weakPieces & ai.attack[Us][KING];
		if (b)
			score += more_than_one(b) ? KingThreatMultiple : KingThreatSingle;
	}

	Bitboard b = pos.pieces(Us, PAWN) & ~RANK7;
	b = shift_up<Us>(b | (shift_up<Us>(b & RANK2) & ~pos.pieces()));
	//b = shift_up<Us>(b | (shift_up<Us>(b & RANK2) & ~pos.pieces() & ~ai.attack[Them][PAWN]));

	b &= ~pos.pieces()
		& ~ai.attack[Them][PAWN]
		& (ai.attack[Us][ALL_PIECES] | ~ai.attack[Them][ALL_PIECES]);

	b = pawn_attack<Us>(b)
		&  pos.pieces(Them)
		& ~ai.attack[Us][PAWN];

	score += PawnAdvance * popcount(b);

	return score;
}

template<Color Us>
inline Score PassedPawns(const Position& pos, const AttackInfo& ai, Bitboard bb_passed_pawns)
{
	assert(bb_passed_pawns != 0);
	const Color Them = (Us == WHITE ? BLACK : WHITE);

	Score score = SCORE_ZERO;
	while (bb_passed_pawns)
	{
		Square passed_pawn = pop_lsb(&bb_passed_pawns);

		score += Score_Pawn_PassedPawn_perFile[file_of(passed_pawn)];

		int pawn_rank = relative_rank(Us, passed_pawn) - 1;
		if (pos.non_pawn_material(WHITE) == MATL_QUEEN && pos.non_pawn_material(BLACK) == MATL_QUEEN)
			score += Score_PassedPawn_QvQ[pawn_rank];
		else
			score += Score_PassedPawn_not_QvQ[pawn_rank];

		if (pawn_rank > 1)
		{
			// distance to the square in front of the pawn
			Square square_in_front_of_pawn = passed_pawn + pawn_forward(Us);
			int ourDistance = distance(square_in_front_of_pawn, pos.king(Us));
			int theirDistance = distance(square_in_front_of_pawn, pos.king(Them));

			score += score_muldiv(Score_PassedPawn_Our_K[pawn_rank][ourDistance], 3, 4);
			score += score_muldiv(Score_PassedPawn_Their_K[pawn_rank][theirDistance], 3, 4);

			// distance to promotion square
			Square promotion_square = Square(make_square(file_of(passed_pawn), Rank(7 * Them)));
			ourDistance = distance(promotion_square, pos.king(Us));
			theirDistance = distance(promotion_square, pos.king(Them));

			score += score_muldiv(Score_PassedPawn_Our_K[pawn_rank][ourDistance], 2, 4);
			score += score_muldiv(Score_PassedPawn_Their_K[pawn_rank][theirDistance], 2, 4);

			if (pawn_rank > 2)
			{
				Bitboard bb_behind_passed_pawn = bb_forward(Them, passed_pawn);
				// rook behind pawn
				if (bb_behind_passed_pawn & pos.pieces(Us, ROOK))
					score += make_score(62, 274);
				if (bb_behind_passed_pawn & pos.pieces(Them, ROOK))
					score -= make_score(62, 274);
			}

			if (!(pos.pieces() & square_in_front_of_pawn))
			{
				// passed pawn can move forward
				Bitboard passed_pawn_path = bb_forward(Us, passed_pawn);
				Bitboard bb_advance_blocked = passed_pawn_path & (pos.pieces(Them) | ai.attack[Them][ALL_PIECES]);

				// their R or Q behind passed pawn -> whole passed_pawn_path is attacked
				Bitboard attacked = pos.pieces(Them, ROOK, QUEEN) & bb_forward(Them, passed_pawn);
				if (attacked)
				{
					Square square = frontmost_square(Us, attacked);
					if (!(pos.pieces() & bb_between(passed_pawn, square)))
						bb_advance_blocked = passed_pawn_path;
				}

				if (!bb_advance_blocked)
					score += Score_PassedPawn_free_passage[pawn_rank];
				else if (bb_advance_blocked & ~ai.attack[Us][ALL_PIECES])
					score += Score_PassedPawn_advance_blocked[pawn_rank];
				else // path is attacked, but also defended
					score += Score_PassedPawn_advance_supported[pawn_rank];
			}
		}
	}

	return score;
}

template<Color Us>
inline Score KingAttack(const Position& pos, const AttackInfo& ai, int AttackScore)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);

	int attackIndex = AttackScore;

	attackIndex += 16 * popcount(ai.attack[Them][KING] & ai.attack[Us][ALL_PIECES] & ~ai.attack[Them][PIECES_WITHOUT_KING]);

	if (ai.pinned[Them]) // pins or discovered check
		attackIndex += 12;

	// undefended attacked squares 2 ranks in front of the king
	attackIndex += 11 * popcount(shift_down<Us>(ai.attack[Them][KING]) & ~ai.attack[Them][ALL_PIECES] & ai.attack[Us][ALL_PIECES] & ~pos.pieces(Us));

	// safe checks
	const Square squareK = pos.king(Them);
	Bitboard Check_OK = ~pos.pieces(Us);
	
	Bitboard CheckSquares_R = pos.attack_from<ROOK>(squareK) & Check_OK;
	Bitboard CheckSquares_B = pos.attack_from<BISHOP>(squareK) & Check_OK;
	Bitboard CheckSquares_N = pos.attack_from<KNIGHT>(squareK) & Check_OK;

	// P, L, T
	Bitboard safe_NBR = ~ai.attack[Them][ALL_PIECES] | (ai.doubleAttack[Us] & ~ai.doubleAttack[Them] & (ai.attack[Them][KING] | ai.attack[Them][QUEEN]));
	if (CheckSquares_N & ai.attack[Us][KNIGHT])
	{
		if (CheckSquares_N & ai.attack[Us][KNIGHT] & safe_NBR)
			attackIndex += 70;
		else
			attackIndex += 30;
	}
	if (CheckSquares_B & ai.attack[Us][BISHOP])
	{
		if (CheckSquares_B & ai.attack[Us][BISHOP] & safe_NBR)
			attackIndex += 54;
		else
			attackIndex += 22;
	}
	if (CheckSquares_R & ai.attack[Us][ROOK])
	{
		if (CheckSquares_R & ai.attack[Us][ROOK] & safe_NBR)
			attackIndex += 70;
		else
			attackIndex += 30;
	}

	// Queen checks / contact checks
	Bitboard queen_check = (CheckSquares_B | CheckSquares_R) & ai.attack[Us][QUEEN];
	if (queen_check)
	{
		if (queen_check & ~ai.attack[Them][KING])
		{
			if (queen_check & ~ai.attack[Them][ALL_PIECES])
				attackIndex += 86;
			else
				attackIndex += 38;
		}
		queen_check &= ai.attack[Them][KING] & (pos.pieces(Them, QUEEN) | ai.doubleAttack[Us]) & ~ai.attack[Them][PIECES_WITHOUT_KING];
		if (queen_check)
			attackIndex += 120;
	}

	if (attackIndex < 0)
		attackIndex = 0;
	if (attackIndex > 1000)
		attackIndex = 1000;

	return Evaluation::SafetyTable[attackIndex];
}


inline EvalValue Initiative(const Position& pos, const Pawns::Tuple *pawnTuple, EvalValue eg)
{
	int k_distance = file_distance(pos.king(WHITE), pos.king(BLACK))
		- rank_distance(pos.king(WHITE), pos.king(BLACK));

	int initiative = (2 * pawnTuple->asymmetry + k_distance + 3 * pos.count(eg < 0 ? B_PAWN : W_PAWN) - 15) * 38;

	int bonus = ((eg > 0) - (eg < 0)) * std::max(initiative, -abs(eg / 2));

	return EvalValue(bonus);
}


template<Color Us>
inline Score StrongSquares(const Position& pos, const AttackInfo& ai, const Pawns::Tuple *pawnTuple)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Bitboard RANK456 = (Us == WHITE ? 0x3C3C3C000000 : 0x3C3C3C0000);
	const Score Score_NB_behind_Pawn       = make_score(32,  8);
	const Score Score_Supported_Pieces = make_score(55, 29);
	const Score Score_StrongSquare_NB         = make_score(62, 70);
	const Score Score_StrongSquare_NB_Extra   = make_score(156, 74);
	const Score Score_SafeFromPawn_RBN   = make_score(35, 27);
	const Score Score_StrongN_before_Pawn     = make_score(14, 34);

	Score score = SCORE_ZERO;
	score += Score_SafeFromPawn_RBN * popcount(pawnTuple->safe_from_pawn(Them) & pos.pieces(Us, KNIGHT, BISHOP, ROOK));
	score += Score_StrongN_before_Pawn * popcount(pawnTuple->safe_from_pawn(Them) & pos.pieces(Us, KNIGHT) & shift_down<Us>(pos.pieces(Them, PAWN)));

	Bitboard strongNB = pawnTuple->safe_from_pawn(Them) & ai.attack[Us][PAWN] & pos.pieces(Us, KNIGHT, BISHOP) & RANK456;
	if (strongNB)
	{
		score += Score_StrongSquare_NB * popcount(strongNB);
		if (!pos.pieces(Them, KNIGHT))
		{
			do
			{
				Square square = pop_lsb(&strongNB);
				Bitboard squares_same_color = (DarkSquares & square) ? DarkSquares : ~DarkSquares;
				if (!(pos.pieces(Them, BISHOP) & squares_same_color))
					score += Score_StrongSquare_NB_Extra;
			} while (strongNB);
		}
	}

	// N and B behind own pawn
	score += Score_NB_behind_Pawn * popcount(pos.pieces(Us, KNIGHT) & pawn_attack<Them>(pos.pieces(Us, PAWN)));
	score += Score_NB_behind_Pawn * popcount(pos.pieces(Us, KNIGHT, BISHOP) & shift_down<Us>(pos.pieces(Us, PAWN)));
	score += Score_Supported_Pieces * popcount(pos.pieces_except(Us, PAWN) & ai.attack[Us][PAWN]);

	return score;
}


template<Color Us>
inline Bitboard ZoneControl(const Position& pos, const AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Bitboard Rank2to4 = Us == WHITE ? 0x00000000ffffff00 : 0x00ffffff00000000;

	Bitboard safe = Rank2to4 & ~(pos.pieces(Us, PAWN) | ai.attack[Them][PAWN]
		| (ai.attack[Them][ALL_PIECES] & ~ai.attack[Us][ALL_PIECES]));

	Bitboard shielded = pos.pieces(Us, PAWN);
	shielded |= (Us == WHITE ? shielded >> 8 : shielded << 8);
	shielded |= (Us == WHITE ? shielded >> 16 : shielded << 16);
	shielded &= safe;

	return safe | (Us == WHITE ? shielded << 32 : shielded >> 32);
}


template<Color Us>
inline Score Space(const Position& pos, const AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	const Bitboard CenterZone = Us == WHITE ? 0x000000003c3c3c00 : 0x003c3c3c00000000;

	Bitboard safe = CenterZone & ~(pos.pieces(Us, PAWN) | ai.attack[Them][PAWN]
		| (ai.attack[Them][ALL_PIECES] & ~ai.attack[Us][ALL_PIECES]));

	Bitboard shielded = pos.pieces(Us, PAWN);
	shielded |= (Us == WHITE ? shielded >> 8 : shielded << 8);
	shielded |= (Us == WHITE ? shielded >> 16 : shielded << 16);
	shielded &= safe;

	unsigned int control = popcount(safe | (Us == WHITE ? shielded << 32 : shielded >> 32));
	unsigned int weight = popcount(pos.pieces(Us));
	Score score = remake_score(EvalValue(weight * weight * 3 / 16), EVAL_0);
	return score * control;
}


template<Color Us>
inline void eval_init(const Position& pos, AttackInfo& ai, const Pawns::Tuple *pawnTuple)
{
	ai.attack[Us][KING] = pos.attack_from<KING>(pos.king(Us));
	ai.attack[Us][PAWN] = pawnTuple->pawn_attack(Us);
	ai.attack[Us][KNIGHT] = 0;
	ai.attack[Us][BISHOP] = 0;
	ai.attack[Us][ROOK] = 0;
	ai.attack[Us][QUEEN] = 0;
	ai.attack[Us][PIECES_WITHOUT_KING] = ai.attack[Us][PAWN];
	ai.doubleAttack[Us] = 0;

	ai.pinned[Us] = pos.info()->xRay[Us];
	ai.KZone[Us] = KingZone[pos.king(Us)];
	//ai.KZoneExtra[Us] = ai.KZone[Us] | shift_up<Us>(ai.KZone[Us]);

	ai.KAttackScore[Us] = 0;
	//ai.mobility[Us] = 0;
}


template<Color Us>
inline Score eval_Knights(const Position& pos, AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	Score score = SCORE_ZERO;

	Bitboard squares = pos.pieces(Us, KNIGHT);
	assert(squares);
	do
	{
		Square square = pop_lsb(&squares);
		score += Score_Distance_N_K[distance(square, pos.king(Us))];
		Bitboard attack = pos.attack_from<KNIGHT>(square);
		if (attack & ai.KZone[Them])
			ai.KAttackScore[Us] += 3 * 8;
		//else if (attack & ai.KZoneExtra[Them])
		//	ai.KAttackScore[Us] += 8;
		ai.attack[Us][KNIGHT] |= attack;
		ai.doubleAttack[Us] |= ai.attack[Us][PIECES_WITHOUT_KING] & attack;
		ai.attack[Us][PIECES_WITHOUT_KING] |= attack;

		unsigned int mobility;
		if (ai.pinned[Us] & square)
			mobility = 0;
		else
		{
			attack &= ai.mobilityMask[Us];
			// forward mobility counts double
			mobility = popcount(attack) + popcount(attack & bb_ranks_forward(Us, square));
		}
		score += mobi_N[(mobility * MobiMult_N[relative_square(Us, square)] + 16) / 32];
		//ai.mobility[Us] += mobility - N_AVERAGE_MOBILITY;
	} while (squares);

	return score;
}


template<Color Us>
inline Score eval_Bishops(const Position& pos, AttackInfo& ai, const Pawns::Tuple *pawnTuple)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	Score score = SCORE_ZERO;

	Bitboard squares = pos.pieces(Us, BISHOP);
	assert(squares);

	if (shift_up<Us>(pos.pieces(Us, KING)) & squares)
		score += make_score(73, 0);
		//score += make_score(33, 0);
	if (Us == WHITE)
	{
		if (squares & bb2(SQ_A1, SQ_H1))
		{
			if ((pos.pieces(Us) & (squares << 9)) & SQ_B2)
				score -= make_score(63, 96);
			if ((pos.pieces(Us) & (squares << 7)) & SQ_G2)
				score -= make_score(63, 96);
		}
	}
	else
	{
		if (squares & bb2(SQ_A8, SQ_H8))
		{
			if ((pos.pieces(Us) & (squares >> 7)) & SQ_B7)
				score -= make_score(63, 96);
			if ((pos.pieces(Us) & (squares >> 9)) & SQ_G7)
				score -= make_score(63, 96);
		}
	}

	do
	{
		Square square = pop_lsb(&squares);
		score += Score_Distance_B_K[distance(square, pos.king(Us))];

		Bitboard bb_pin_RQ = EmptyAttack[BISHOP][square] & pos.pieces(Them, ROOK, QUEEN);
		while (bb_pin_RQ)
		{
			Square square_RQ = pop_lsb(&bb_pin_RQ);
			Bitboard b = bb_between(square_RQ, square) & pos.pieces();
			if (b && !more_than_one(b))
				score += Score_Bishop_Pin[Us][pos.piece_on_square(lsb(b))];
		}

		Bitboard attack = attack_bb_bishop(square, pos.pieces(PAWN));
		score += mobi_B1[(popcount(attack) * MobiMult_B1[relative_square(Us, square)] + 16) / 32];
		//score += mobi_B1[std::min((int)(popcount(attack) * MobiMult_B1[relative_square(Us, square)] + 16) / 32, 255)];

		if (pos.pieces(PAWN) & trapped_bishop_b3_c2[Us][square])
		{
			if (pos.pieces(PAWN) & trapped_bishop_b3_c2_extra[square])
				score -= make_score(665, 665);
			else
				score -= make_score(315, 315);
		}
#if 0
		else if (trapped_bishop_b4_d2[Us][square] && (pos.pieces(PAWN) & trapped_bishop_b4_d2[Us][square]) == trapped_bishop_b4_d2[Us][square])
		{
			if (pos.pieces(PAWN) & trapped_bishop_b4_d2_extra[square])
				score -= make_score(600, 600);
			else
				score -= make_score(300, 300);
		}
#endif
#if 0
		else
		{
			attack &= ~(ai.attack[Them][PAWN] | pos.pieces(Us, PAWN));
			if (!more_than_one(attack))
			{
				if (attack == 0)
					score -= make_score(250, 350);
				else
					score -= make_score(80, 135);
			}
		}
#endif

		//attack = pos.attack_from<BISHOP>(square);
		attack = attack_bb_bishop(square, pos.pieces() ^ pos.pieces(Us, QUEEN));
		if (attack & ai.KZone[Them])
			ai.KAttackScore[Us] += 8;
		//else if (attack & ai.KZoneExtra[Them])
		//	ai.KAttackScore[Us] += 4;
		ai.attack[Us][BISHOP] |= attack;
		ai.doubleAttack[Us] |= ai.attack[Us][PIECES_WITHOUT_KING] & attack;
		ai.attack[Us][PIECES_WITHOUT_KING] |= attack;

		attack &= ai.mobilityMask[Us];
		if (ai.pinned[Us] & square)
			attack &= bb_between(pos.king(Us), square);
		unsigned int mobility = popcount(attack);
		score += mobi_B2[(mobility * MobiMult_B2[relative_square(Us, square)] + 16) / 32];
		//ai.mobility[Us] += mobility - B_AVERAGE_MOBILITY;

		Score pawnsOnColor = Score_PawnOnColorBishop[pawnTuple->pawns_on_color(Us, square)];
		score += pawnsOnColor;
		score += Score_PawnOtherColorBishop[pawnTuple->pawns_not_on_color(Us, square)];

		// if I have the bishop pair and you have no bishop on the same color
		// after exchange of bishops I am left with a bad bishop
		Bitboard squares_same_color = (DarkSquares & square) ? DarkSquares : ~DarkSquares;
		if ((pos.pieces(Us, BISHOP) & ~squares_same_color) && !(squares_same_color & pos.pieces(Them, BISHOP)))
			score += pawnsOnColor;

		if (pos.pieces(Them, KNIGHT) & bb_B_dominates_N[Us][square])
			// extra score for enemy Knight standing 3 ranks in front of the bishop (is dominated by Bishop)
			score += make_score(20, 19);
	} while (squares);

	return score;
}


template<Color Us>
inline Score eval_Rooks(const Position& pos, AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	Score score = SCORE_ZERO;
	Bitboard squares = pos.pieces(Us, ROOK);
	assert(squares);

	// R trapped next to K
#if 1
	if (pos.pieces(Us, KING) & (Us == WHITE ? bb2(SQ_F1, SQ_G1) : bb2(SQ_F8, SQ_G8)) && squares & (Us == WHITE ? 0xC0C0 : 0xC0C0000000000000))
		score -= make_score(282, 101);
#else
	Bitboard bbx = trapped_rook[pos.square<KING>(Us)] & squares;
	if (bbx)
	{
		Square square = rearmost_square(Us, bbx);
		bbx = bb_forward(Us, square) & pos.pieces(Us, PAWN);
		if (bbx)
		{
			square = rearmost_square(Us, bbx);
			Rank r = relative_rank(Us, square);
			if (!pos.castling_possible(Us))  // test needed for Chess960
				score -= make_score(110 * (6 - r), 0);
			else
				score -= make_score(100 + 20 * (6 - r), 0);
		}
	}
#endif

	do
	{
		Square square = pop_lsb(&squares);

		//Bitboard bb_pin_Q = EmptyAttack[ROOK][square] & pos.pieces(Them, QUEEN);
		//while (bb_pin_Q)
		//{
		//	Square square_Q = pop_lsb(&bb_pin_Q);
		//	Bitboard b = bb_between(square_Q, square) & pos.pieces();
		//	if (b && !more_than_one(b))
		//		score += Score_Rook_Pin[Us][pos.piece_on_square(lsb(b))];
		//}

		Bitboard attack = attack_bb_rook(square, pos.pieces() ^ pos.pieces(Us, ROOK, QUEEN));
		if (attack & ai.KZone[Them])
			ai.KAttackScore[Us] += 8;
		//else if (attack & ai.KZoneExtra[Them])
		//	ai.KAttackScore[Us] += 4;
		ai.attack[Us][ROOK] |= attack;
		ai.doubleAttack[Us] |= ai.attack[Us][PIECES_WITHOUT_KING] & attack;
		ai.attack[Us][PIECES_WITHOUT_KING] |= attack;

		attack &= ai.mobilityMask[Us];
		if (ai.pinned[Us] & square)
			attack &= bb_between(pos.king(Us), square);
		unsigned int mobility = popcount(attack);
		score += mobi_R[(mobility * MobiMult_R[relative_square(Us, square)] + 16) / 32];
		//ai.mobility[Us] += mobility - R_AVERAGE_MOBILITY;

		// king on eighth rank, R on seventh
		const Bitboard EIGHTH_RANK = Us == WHITE ? Rank8BB : Rank1BB;
		if (relative_rank(Us, square) == RANK_7 && pos.pieces(Them, KING) & EIGHTH_RANK)
			score += make_score(64, 163);

		// R on file of K, if there are no own pawns in between
		if (file_of(square) == file_of(pos.king(Them)) && !(pos.pieces(Us, PAWN) & bb_between(pos.king(Them), square)))
			ai.KAttackScore[Us] += 2 * 8;

		if (!(bb_file(square) & pos.pieces(Us, PAWN)))
		{
			Bitboard pawn = pos.pieces(Them, PAWN) & bb_file(square);
			if (!pawn)
				// open file
				score += make_score(185, 157);
			else if (pawn & ai.attack[Them][PAWN])
				// half-open defended by pawn
				score += make_score(20, 44);
			else
				// half-open file
				score += make_score(112, 94);
		}
	} while (squares);

	return score;
}


template<Color Us>
inline Score eval_Queens(const Position& pos, AttackInfo& ai)
{
	const Color Them = (Us == WHITE ? BLACK : WHITE);
	Score score = SCORE_ZERO;

	Bitboard squares = pos.pieces(Us, QUEEN);
	assert(squares);

	Bitboard mobility_mask_Q = ~(ai.attack[Them][BISHOP] | ai.attack[Them][ROOK] | pos.pieces(Us, KING, PAWN) | ai.attack[Them][PAWN] | ai.attack[Them][KNIGHT]);
	ai.KAttackScore[Us] += 3 * 8;

	do
	{
		Square square = pop_lsb(&squares);
		//Bitboard attack = pos.attack_from<QUEEN>(square);
		Bitboard attack = attack_bb_rook(square, pos.pieces() ^ pos.pieces(Us, QUEEN)) | attack_bb_bishop(square, pos.pieces() ^ pos.pieces(Us, QUEEN));
		//Bitboard attack = attack_bb<ROOK>(square, pos.pieces() ^ pos.pieces(Us, ROOK, QUEEN))
		//	| attack_bb<BISHOP>(square, pos.pieces());
		if (attack & ai.KZone[Them])
			ai.KAttackScore[Us] += 8;
		//else if (attack & ai.KZoneExtra[Them])
		//	ai.KAttackScore[Us] += 4;
		ai.attack[Us][QUEEN] |= attack;
		ai.doubleAttack[Us] |= ai.attack[Us][PIECES_WITHOUT_KING] & attack;
		ai.attack[Us][PIECES_WITHOUT_KING] |= attack;

		attack &= mobility_mask_Q;
		if (ai.pinned[Us] & square)
			attack &= bb_between(pos.king(Us), square);

		const Bitboard CENTER_BOX = 0x00003C3C3C3C0000;
		unsigned int mobility = popcount(attack) + popcount(attack & CENTER_BOX);
		score += mobi_Q[(mobility * MobiMult_Q[relative_square(Us, square)] + 32) / 64];
		//ai.mobility[Us] += mobility - Q_AVERAGE_MOBILITY;
	} while (squares);

	return score;
}


Value Evaluation::evaluation(const Position& pos, Value alpha, Value beta)
{
	assert(!pos.check_givers());

	Material::Tuple* materialTuple;
	Pawns::Tuple* pawnTuple;

	materialTuple = Material::probe(pos);

	PositionInfo* st = pos.info();
	st->eval_is_exact = false;

	if (materialTuple->has_value_function())
	{
		st->strongThreat = 0;
		return materialTuple->value_from_function(pos);
	}

	bool doLazyEval = beta < WIN_VALUE && (st - 1)->evalPositional != NO_EVAL && alpha > -WIN_VALUE
		//&& st->capturedPiece
		&& pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) > 2 * MATL_BISHOP
		&& !(pos.pieces(WHITE, PAWN) & Rank7BB) && !(pos.pieces(BLACK, PAWN) & Rank2BB);

	if (doLazyEval)
	{
		EvalValue v = (st - 1)->evalPositional;
		int evalFactor = (st - 1)->evalFactor;
#if 0
		// allow multiple lazyEvals in a row
		st->evalPositional = v;
		st->evalFactor = evalFactor;
#endif
		v += materialTuple->value * evalFactor / MAX_FACTOR;

		if (pos.side_to_move() == BLACK)
			v = -v;

		Value lazyResult = ValueFromEval(v) + VALUE_TEMPO;

		if (lazyResult <= alpha || lazyResult >= beta)
		{
			st->strongThreat = 0;
			return lazyResult;
		}
	}

	pawnTuple = Pawns::probe(pos);

#if 0
	const EvalValue InstantEvalLimit = 6 * PAWN_EVAL;
	EvalValue instantValue = materialTuple->value * materialTuple->conversionFactor / MAX_FACTOR +
		mg_value(pawnTuple->pawns_score() + pos.psq_score());
	if (abs(instantValue) > InstantEvalLimit)
		return ValueFromEval(pos.side_to_move() == BLACK ? -instantValue : instantValue);
#endif

	Score kingSafety = pawnTuple->king_safety<WHITE>(pos);
	kingSafety -= pawnTuple->king_safety<BLACK>(pos);


	// evaluation of pieces starts here
	// =============================
	AttackInfo ai;
	
	eval_init<WHITE>(pos, ai, pawnTuple);
	eval_init<BLACK>(pos, ai, pawnTuple);

	ai.mobilityMask[WHITE] = ~(ai.attack[BLACK][PAWN] | (pos.pieces(WHITE, PAWN) & shift_down<WHITE>(pos.pieces())))
		| pos.pieces_except(BLACK, PAWN);
	ai.mobilityMask[BLACK] = ~(ai.attack[WHITE][PAWN] | (pos.pieces(BLACK, PAWN) & shift_down<BLACK>(pos.pieces())))
		| pos.pieces_except(WHITE, PAWN);

	Score evalScore = pos.psq_score();

	// discovered check
	//const Score Score_DiscoveredCheck = make_score(250, 250);
	//if (ai.pinned[BLACK] & pos.pieces(WHITE, KNIGHT, BISHOP, ROOK))
	//	evalScore += Score_DiscoveredCheck;
	//if (ai.pinned[WHITE] & pos.pieces(BLACK, KNIGHT, BISHOP, ROOK))
	//	evalScore -= Score_DiscoveredCheck;

	// knights
	if (pos.pieces(WHITE, KNIGHT))
		evalScore += eval_Knights<WHITE>(pos, ai);
	if (pos.pieces(BLACK, KNIGHT))
		evalScore -= eval_Knights<BLACK>(pos, ai);

	// bishops
	if (pos.pieces(WHITE, BISHOP))
		evalScore += eval_Bishops<WHITE>(pos, ai, pawnTuple);
	if (pos.pieces(BLACK, BISHOP))
		evalScore -= eval_Bishops<BLACK>(pos, ai, pawnTuple);

	// rooks
	if (pos.pieces(WHITE, ROOK))
		evalScore += eval_Rooks<WHITE>(pos, ai);
	if (pos.pieces(BLACK, ROOK))
		evalScore -= eval_Rooks<BLACK>(pos, ai);
	
	// queens
	if (pos.pieces(WHITE, QUEEN))
		evalScore += eval_Queens<WHITE>(pos, ai);
	if (pos.pieces(BLACK, QUEEN))
		evalScore -= eval_Queens<BLACK>(pos, ai);

	ai.doubleAttack[WHITE] |= ai.attack[WHITE][PIECES_WITHOUT_KING] & ai.attack[WHITE][KING];
	ai.attack[WHITE][ALL_PIECES] = ai.attack[WHITE][PIECES_WITHOUT_KING] | ai.attack[WHITE][KING];
	ai.doubleAttack[BLACK] |= ai.attack[BLACK][PIECES_WITHOUT_KING] & ai.attack[BLACK][KING];
	ai.attack[BLACK][ALL_PIECES] = ai.attack[BLACK][PIECES_WITHOUT_KING] | ai.attack[BLACK][KING];

	//evalScore += make_score(-12, 0) * (popcount<Full>(ai.attack[WHITE][ALL_PIECES]) - popcount<Full>(ai.attack[BLACK][ALL_PIECES]));

	// score king attack
	// ===================
#if 0
	Bitboard zoneControl[2];
	zoneControl[WHITE] = ZoneControl<WHITE>(pos, ai);
	zoneControl[BLACK] = ZoneControl<BLACK>(pos, ai);

	int space_advantage = popcount(zoneControl[WHITE] & KingFlankFiles[file_of(pos.king(BLACK))]) 
		- popcount(zoneControl[BLACK] & KingFlankFiles[file_of(pos.king(BLACK))]);

	evalScore += KingAttack<WHITE>(pos, ai, ai.KAttackScore[WHITE] - pawnTuple->safety[BLACK] 
		+ space_advantage / 2
		//+ space_advantage * popcount(pos.pieces_except(WHITE, PAWN)) / 8
		);

	if (KingFlankFiles[file_of(pos.king(WHITE))] != KingFlankFiles[file_of(pos.king(BLACK))])
		space_advantage = popcount(zoneControl[WHITE] & KingFlankFiles[file_of(pos.king(WHITE))]) 
			- popcount(zoneControl[BLACK] & KingFlankFiles[file_of(pos.king(WHITE))]);

	evalScore -= KingAttack<BLACK>(pos, ai, ai.KAttackScore[BLACK] - pawnTuple->safety[WHITE]
		- space_advantage / 2
		//- space_advantage * popcount(pos.pieces_except(BLACK, PAWN)) / 8
		);
#else
	evalScore += KingAttack<WHITE>(pos, ai, ai.KAttackScore[WHITE] - pawnTuple->safety[BLACK]);
	evalScore -= KingAttack<BLACK>(pos, ai, ai.KAttackScore[BLACK] - pawnTuple->safety[WHITE]);
#endif

	// threats White
	// ==============
	evalScore += sf_Threats<WHITE>(pos, ai);
	evalScore -= sf_Threats<BLACK>(pos, ai);
	st->strongThreat = ai.strongThreat[WHITE] + 2 * ai.strongThreat[BLACK];

	if (pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) >= 2 * MATL_QUEEN + 4 * MATL_ROOK + 2 * MATL_KNIGHT)
	{
		evalScore += Space<WHITE>(pos, ai);
		evalScore -= Space<BLACK>(pos, ai);
	}

	// passed pawns
	// ===========
	if (pawnTuple->passed_pawns(WHITE))
		evalScore += PassedPawns<WHITE>(pos, ai, pawnTuple->passed_pawns(WHITE));
	if (pawnTuple->passed_pawns(BLACK))
		evalScore -= PassedPawns<BLACK>(pos, ai, pawnTuple->passed_pawns(BLACK));

	// strong squares for N and B
	// =========================
	evalScore += StrongSquares<WHITE>(pos, ai, pawnTuple);
	evalScore -= StrongSquares<BLACK>(pos, ai, pawnTuple);

	// blocked pawns
	const Score Score_Blocked_Pawns = score_muldiv(make_score(43, 167), 128, 256);
	evalScore -= Score_Blocked_Pawns * popcount(pos.pieces(WHITE, PAWN) & shift_down<WHITE>(pos.pieces()));
	evalScore += Score_Blocked_Pawns * popcount(pos.pieces(BLACK, PAWN) & shift_down<BLACK>(pos.pieces()));
	//const Score Score_Restrained_Pawns = make_score(21, 42);
	//evalScore -= Score_Restrained_Pawns * popcount(pos.pieces(WHITE, PAWN) & shift_down<WHITE>(
	//	ai.attack[BLACK][ALL_PIECES] & ~ai.attack[WHITE][ALL_PIECES] & ~pos.pieces()));
	//evalScore += Score_Restrained_Pawns * popcount(pos.pieces(BLACK, PAWN) & shift_down<BLACK>(
	//	ai.attack[WHITE][ALL_PIECES] & ~ai.attack[BLACK][ALL_PIECES] & ~pos.pieces()));

	// control of king flank
	Bitboard bb_flank = ai.attack[WHITE][PIECES_WITHOUT_KING] & bb_king_flank_attack[BLACK][file_of(pos.king(BLACK))]
		& ~ai.attack[BLACK][KING] & ~ai.attack[BLACK][PAWN];
	bb_flank = (bb_flank >> 4) | (bb_flank & ai.doubleAttack[WHITE]);
	evalScore += popcount(bb_flank) * make_score(18, 0);

	bb_flank = ai.attack[BLACK][PIECES_WITHOUT_KING] & bb_king_flank_attack[WHITE][file_of(pos.king(WHITE))]
		& ~ai.attack[WHITE][KING] & ~ai.attack[WHITE][PAWN];
	bb_flank = (bb_flank << 4) | (bb_flank & ai.doubleAttack[BLACK]);
	evalScore -= popcount(bb_flank) * make_score(18, 0);

	// flank without pawn
	//if (!(pos.pieces(PAWN) & bb_king_flank[file_of(pos.king(WHITE))]))
	//	evalScore -= make_score(0, 250);
	//if (!(pos.pieces(PAWN) & bb_king_flank[file_of(pos.king(BLACK))]))
	//	evalScore += make_score(0, 250);

#if 0
	// control of attack squares
	const Score Score_AttackSquares = make_score(20, 0);
	Bitboard bb_center_pivot = bb_center_pivot_W[file_of(pos.king(WHITE))] | bb_center_pivot_B[file_of(pos.king(BLACK))];
	evalScore += Score_AttackSquares * popcount(bb_center_pivot & ai.attack[WHITE][PIECES_WITHOUT_KING]);
	evalScore -= Score_AttackSquares * popcount(bb_center_pivot & ai.attack[BLACK][PIECES_WITHOUT_KING]);
#endif

#if 0
	const Score Score_B_KingSide = make_score(35, 0);
	const Score Score_Pawn_KingSide = make_score(8, 0);
	const Bitboard KingSide = 0xF0F0F0F0F0F0F0F0;
	const Bitboard QueenSide = 0x0F0F0F0F0F0F0F0F;

	if (pos.pieces(WHITE, KING) & QueenSide)
	{
		if (pos.pieces(BLACK, KING) & QueenSide)
		{
			if (pos.pieces(WHITE, BISHOP) & DarkSquares)
				evalScore += Score_B_KingSide;
			if (pos.pieces(BLACK, BISHOP) & ~DarkSquares)
				evalScore -= Score_B_KingSide;
			Bitboard QN = pos.pieces(WHITE, QUEEN, KNIGHT) & QueenSide;
			if (QN)
			{
				evalScore += Score_B_KingSide;
				if (more_than_one(QN))
					evalScore += Score_B_KingSide;
			}
			QN = pos.pieces(BLACK, QUEEN, KNIGHT) & QueenSide;
			if (QN)
			{
				evalScore -= Score_B_KingSide;
				if (more_than_one(QN))
					evalScore -= Score_B_KingSide;
			}
			//evalScore += Score_Pawn_KingSide * popcount(pos.pieces(WHITE, PAWN) & QueenSide);
			//evalScore -= Score_Pawn_KingSide * popcount(pos.pieces(BLACK, PAWN) & QueenSide);
		}
	}
	else if (pos.pieces(BLACK, KING) & KingSide)
	{
		if (pos.pieces(WHITE, BISHOP) & ~DarkSquares)
			evalScore += Score_B_KingSide;
		if (pos.pieces(BLACK, BISHOP) & DarkSquares)
			evalScore -= Score_B_KingSide;
		Bitboard QN = pos.pieces(WHITE, QUEEN, KNIGHT) & KingSide;
		if (QN)
		{
			evalScore += Score_B_KingSide;
			if (more_than_one(QN))
				evalScore += Score_B_KingSide;
		}
		QN = pos.pieces(BLACK, QUEEN, KNIGHT) & KingSide;
		if (QN)
		{
			evalScore -= Score_B_KingSide;
			if (more_than_one(QN))
				evalScore -= Score_B_KingSide;
		}
		//evalScore += Score_Pawn_KingSide * popcount(pos.pieces(WHITE, PAWN) & KingSide);
		//evalScore -= Score_Pawn_KingSide * popcount(pos.pieces(BLACK, PAWN) & KingSide);
	}
#endif

	//evalScore += (5 - 2 * pawnTuple->file_width) * make_score(0, 40) * (pos.count<KNIGHT>(WHITE) - pos.count<KNIGHT>(BLACK));

	// positional eval is multiplied by (140/128)
	int mul = (Threads.tacticalModeIndex == 1) ? 50 : 35;
	Score score = pawnTuple->pawns_score() + kingSafety + score_muldiv(evalScore, mul, 32);

	// in late endgame (without queens, at most 2 rooks)
	if (pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) <= 4 * MATL_BISHOP)
		score += Score_PawnFileWidth[pawnTuple->pawn_range(WHITE)] - Score_PawnFileWidth[pawnTuple->pawn_range(BLACK)];

	EvalValue mg = (106 * mg_value(score) - 6 * eg_value(score)) / 100;
	EvalValue eg = (13 * mg_value(score) + 87 * eg_value(score)) / 100;

	ScaleFactor sf = compute_scalefactor(pos, materialTuple, materialTuple->value + eg);
	ScaleFactor conversionFactor = materialTuple->conversionFactor;

	eg += Initiative(pos, pawnTuple, materialTuple->value + eg);

	if (materialTuple->conversion_is_estimated && !pos.pieces(QUEEN) && !(pawnTuple->passed_pawns(WHITE) | pawnTuple->passed_pawns(BLACK)))
		conversionFactor = ScaleFactor(conversionFactor * 115 / 128); // approximately 0.9
	if (pawnTuple->conversion_difficult)
		conversionFactor = ScaleFactor(conversionFactor * 115 / 128); // approximately 0.9

	ScaleFactor evalFactor = sf == NORMAL_FACTOR ? conversionFactor : ScaleFactor(std::min((int)conversionFactor, (int)2 * sf));

	int phase = materialTuple->game_phase();
	EvalValue v = (mg * conversionFactor / MAX_FACTOR * phase + eg * evalFactor / MAX_FACTOR * int(MIDDLEGAME_PHASE - phase)) / int(MIDDLEGAME_PHASE);
	st->evalPositional = v;
	st->evalFactor = evalFactor;
	v += materialTuple->value * evalFactor / MAX_FACTOR;

#if 1
	if (Threads.pieceContempt)
	{
		// number of pieces
		int contempt_count = 2 * pos.count(Threads.contemptColor, PAWN)
			+ 2 * pos.count(Threads.contemptColor, KNIGHT) + 3 * pos.count(Threads.contemptColor, BISHOP)
			+ 4 * pos.count(Threads.contemptColor, ROOK) + 8 * pos.count(Threads.contemptColor, QUEEN);

		// stuck pawns
		//int blocked_pawns = popcount(pos.pieces(BLACK, PAWN) & (pos.pieces(WHITE, PAWN) << 8)) - 2;
		//if (blocked_pawns > 0)
		//	contempt_count -= 5 * blocked_pawns * phase / int(MIDDLEGAME_PHASE);

		int contemptScore = 4 * Threads.pieceContempt * contempt_count * evalFactor / MAX_FACTOR;
		if (Threads.contemptColor == WHITE)
			v += EvalValue(contemptScore);
		else
			v -= EvalValue(contemptScore);
	}
#endif

	if (pos.side_to_move() == BLACK)
		v = -v;

	// up to here the score is in 1/1600 pawn (EvalValue)
	// final result in 1/200 pawn (Value)
	//Value result = ValueFromEval(v) + Value((unsigned int)VALUE_TEMPO * (unsigned int)evalFactor / MAX_FACTOR);
	Value result = ValueFromEval(v) + VALUE_TEMPO;

	// 50 move rule
	if (pos.fifty_moves_counter() > Threads.fiftyMovesDistance)
		result = result * (5 * (2 * Threads.fiftyMovesDistance - pos.fifty_moves_counter()) + 6) / 256;

	// test for stalemate if only K + pawns
	if (!pos.non_pawn_material(pos.side_to_move()))
	{
		if (pos.side_to_move() == WHITE)
		{
			if ((pos.attack_from<KING>(pos.king(WHITE)) & ~pos.pieces(WHITE) & ~ai.attack[BLACK][ALL_PIECES]) == 0
				&& ((pos.pieces(WHITE, PAWN) << 8) & ~pos.pieces()) == 0
				&& (((pos.pieces(WHITE, PAWN) & ~FileABB) << 7) & pos.pieces(BLACK)) == 0
				&& (((pos.pieces(WHITE, PAWN) & ~FileHBB) << 9) & pos.pieces(BLACK)) == 0)
			{
				result = DRAW_VALUE;
				st->eval_is_exact = true;
			}
		}
		else
			if ((pos.attack_from<KING>(pos.king(BLACK)) & ~pos.pieces(BLACK) & ~ai.attack[WHITE][ALL_PIECES]) == 0
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
	// Mobility multiplier
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
	const int MobiMultEdge[64] = {
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
		MobiMult_N[n] = 270 * MobiMultConst[n] + (0 * MobiMultRankQuad[n] + 6 * MobiMultFileQuad[n]) / 2
			- 2 * MobiMultCenter[n] + 0 * MobiMultRank[n] + 5 * MobiMultEdge[n];
		MobiMult_B1[n] = 256;
		//MobiMult_B1[n] = 271 * MobiMultConst[n] + (-5 * MobiMultRankQuad[n] + 0 * MobiMultFileQuad[n]) / 2
		//	+ 0 * MobiMultCenter[n] + 0 * MobiMultRank[n] + 0 * MobiMultEdge[n];
		MobiMult_B2[n] = 249 * MobiMultConst[n] + (-8 * MobiMultRankQuad[n] - 3 * MobiMultFileQuad[n]) / 2
			+ 0 * MobiMultCenter[n] - 3 * MobiMultRank[n] - 4 * MobiMultEdge[n];
		MobiMult_R[n] = 255 * MobiMultConst[n] + (1 * MobiMultRankQuad[n] - 5 * MobiMultFileQuad[n]) / 2
			- 6 * MobiMultCenter[n] - 1 * MobiMultRank[n] - 2 * MobiMultEdge[n];
		MobiMult_Q[n] = 272 * MobiMultConst[n] + (-2 * MobiMultRankQuad[n] + 4 * MobiMultFileQuad[n]) / 2
			+ 1 * MobiMultCenter[n] - 2 * MobiMultRank[n] - 8 * MobiMultEdge[n];

		//int MobiMult = TUNE_1 * MobiMultConst[n] + (TUNE_2 * MobiMultRankQuad[n] + TUNE_3 * MobiMultFileQuad[n]) / 2
		//	+ TUNE_4 * MobiMultCenter[n] + TUNE_5 * MobiMultRank[n] + TUNE_6 * MobiMultEdge[n];
		//MobiMult_B1[n] = std::max(std::min(MobiMult, 512), 64);
	}
	for (int n = 0; n < 256; n++)
	{
		double v38 = sqrt(0.125 * n + 1.5) - sqrt(1.5);
		mobi_N[n] = make_score(std::lround(v38 * 207.32 - 417.0), std::lround(v38 * 252.68 - 509.0));
		mobi_R[n] = make_score(std::lround(v38 * 125.90 - 190.0) * 7 / 8, std::lround(v38 * 218.96 - 331.0) * 7 / 8);
		mobi_B2[n] = make_score(std::lround(v38 * 221.48 - 374.0), std::lround(v38 * 203.99 - 344.0));
		mobi_B1[n] = make_score(std::lround(v38 * 92.43 - 171.0), std::lround(v38 * 104.75 - 194.0));
		v38 = sqrt(0.25 * n + 1.5) - sqrt(1.5);
		mobi_Q[n] = make_score(std::lround(v38 * 203.42 - 616.0), std::lround(v38 * 165.33 - 555.0));
	}

	for (int n = 0; n < 8; n++)
	{
		Score_Distance_N_K[n] = make_score(11, 7) * (3 - n);
		Score_Distance_B_K[n] = make_score(1, 6) * (3 - n);
	}

	for (int n = 0; n < 9; n++)
	{
		Score_PawnOnColorBishop[n] = make_score(-17, -27) * (n - 2);
		Score_PawnOtherColorBishop[n] = make_score(29, 11) * (n - 2);
		Score_PawnFileWidth[n] = make_score(0, 2 * (n > 5 ? 9 * n - 36 : n * n - 16));
		Score_Threats[n] = make_score(138, 123) * (n > 1 ? n + 2 : n);
	}

	for (int n = 0; n <= 5; n++)
	{
		Score_PassedPawn_not_QvQ[n] = make_score(49 * (n - 1) * n, 34 * (n * n + 1));
		Score_PassedPawn_QvQ[n]      = make_score(46 * (n - 1) * n, 43 * (n * n + 1));

		Score_PassedPawn_free_passage[n]     = make_score(1, 3) * (n - 1) * n + score_muldiv(make_score(10, 60) * (n - 1) * n, 272, 256);
		Score_PassedPawn_advance_supported[n] = make_score(1, 3) * (n - 1) * n + score_muldiv(make_score(10, 36) * (n - 1) * n, 304, 256);
		Score_PassedPawn_advance_blocked[n]     = make_score(1, 3) * (n - 1) * n;

		for (int distance = 0; distance < 8; distance++)
		{
			int support = 30 * PassedPawnProximity[distance];
			double free_path = (sqrt(distance + 1.0) - 1.0) * (n - 1) * n;
			Score_PassedPawn_Our_K[n][distance] = make_score(0, (support - std::lround(free_path * 40)) * 32 / 35);
			Score_PassedPawn_Their_K[n][distance] = make_score(0, (std::lround(free_path * 76) - support) * 32 / 35);
		}
	}

#if !defined(USE_LICENSE) && !defined(IMPORT_TABLES)
#if 1
	int vprev = 0;
	for (int n = 0; n < 128; n++)
	{
		int v = 13 * KingDanger[n] / 2;
		SafetyTable[8 * n] = make_score(v, 0);
		if (n > 0)
			for (int i = 1; i < 8; i++)
				SafetyTable[8 * n - 8 + i] = make_score((i * v + (8 - i) * vprev) / 8, 0);
		vprev = v;
	}
#else
	const double MaxSlope = 25 * 6.5 / 8;
	const double InflectionPoint = 29 * 8;
	const int MaxValue = std::lround(1400 * 6.5);
	int prev_value = 0;
	for (int n = 0; n < 1024; n++)
	{
		int value;
		if (n > InflectionPoint)
			value = prev_value + std::lround(MaxSlope);
		else {
			const double d2 = 3.1416 / 4 / InflectionPoint;
			value = std::lround(d2 * MaxSlope * pow(sin(d2 * n) / d2, 2));
		}
		SafetyTable[n] = make_score(std::min(MaxValue, value), 0);
		prev_value = value;
	}
#endif
#endif

#if 0
	for (File file = FILE_A; file <= FILE_H; ++file)
	{
		bb_center_pivot_W[file] = bb4(SQ_D4, SQ_D5, SQ_E4, SQ_E5)
			| (file >= FILE_E ? bb3(SQ_F4, SQ_E3, SQ_F3) : bb3(SQ_C4, SQ_D3, SQ_C3));
		bb_center_pivot_B[file] = bb4(SQ_D4, SQ_D5, SQ_E4, SQ_E5)
			| (file >= FILE_E ? bb3(SQ_F5, SQ_E6, SQ_F6) : bb3(SQ_C5, SQ_D6, SQ_C6));
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
