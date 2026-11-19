/*
*/

#include "houdini.h"
#include "bitboard.h"
#include "eval_material.h"
#include "search_smp.h"
#include "uci.h"


namespace
{
	static const double PawnFactor[5] = { 0.4, 0.55, 0.7, 0.8, 0.9 };
	static const double BalanceFactorOld[5] = { 0.1, 0.1, 0.1, 0.2, 0.5 };
	static const double BalanceFactor[4] = { 0.05, 0.08, 0.15, 0.3 };

	EvalValue imbalanceH(int WPawn, int WN, int WB, int WBL, int WBD, int WR, int WQ,
		int BPawn, int BN, int BB, int BBL, int BBD, int BR, int BQ, ScaleFactor &conversion)
	{
		int HScore = 0;
		int WBPair = (WBL && WBD);
		int BBPair = (BBL && BBD);

		HScore += (WPawn - BPawn) * (  950 -  90 * (WQ + BQ) -  28 * (WR + BR) - 17 * (WB + BB) + 16 * (WN + BN)                       );
		HScore += (WN - BN)       * ( 3510 -  50 * (WQ + BQ) -  35 * (WR + BR) -  7 * (WB + BB) -  8 * (WN + BN) + 22 * (WPawn + BPawn));
		HScore += (WB - BB)       * ( 3775 -  55 * (WQ + BQ) -  27 * (WR + BR) -  9 * (WB + BB) -  6 * (WN + BN) +  6 * (WPawn + BPawn));
		HScore += (WR - BR)       * ( 6295 - 261 * (WQ + BQ) - 217 * (WR + BR) - 34 * (WB + BB) - 40 * (WN + BN) - 10 * (WPawn + BPawn));
		HScore += (WQ - BQ)       * (11715 - 338 * (WQ + BQ) - 169 * (WR + BR) - 37 * (WB + BB) - 25 * (WN + BN) +  6 * (WPawn + BPawn));
		HScore += (WBPair - BBPair)     * (  515 -  41 * (WQ + BQ) -  21 * (WR + BR) -  7 * (WB + BB) -  7 * (WN + BN));

		int WMinorPiece = WB + WN;
		int BMinorPiece = BB + BN;

		if (WMinorPiece > BMinorPiece)
		{
			if (WMinorPiece > BMinorPiece + 2)
				HScore += 200;
			if (WB > BB)
				HScore += 17;
			if (WN > BN)
				HScore -= 17;
		}
		else if (WMinorPiece < BMinorPiece)
		{
			if (BMinorPiece > WMinorPiece + 2)
				HScore -= 200;
			if (WB < BB)
				HScore -= 17;
			if (WN < BN)
				HScore += 17;
		}

		const int MAX_PHASE = 32;
		int phase = (WN + BN) + (WB + BB) + 3 * (WR + BR) + 6 * (WQ + BQ);
		phase = std::min(phase, MAX_PHASE);
		double conversionFactor;

		if (phase == 0)
			conversionFactor = 1.3;	// pionneneindspel
		else
			conversionFactor = 0.95 + 0.21 * phase / MAX_PHASE;

		if (WB == 1 && BB == 1 && ((WBL && BBD) || (WBD && BBL)))
		{
			// ongelijke lopers
			if (phase == 2)
				conversionFactor = 0.6;
			else
				conversionFactor = conversionFactor * 0.95;
		}
		if (WPawn + BPawn)
		{
			if (HScore < 0)
			{
				if (BPawn <= 4)
					conversionFactor = conversionFactor * PawnFactor[BPawn];
			}
			else if (WPawn <= 4)
				conversionFactor = conversionFactor * PawnFactor[WPawn];
		}
		//else if (WD + ZD == 0 && WT == 1 && ZT == 1 && WP + WL + ZP + ZL == 1)
		else if (phase == 7 && WR == 1 && BR == 1)
			conversionFactor = conversionFactor * 0.1;
		else
		{
			// zonder pionnen
			int matDifference = abs(3 * (WN - BN) + 7 * WB / 2 - 7 * BB / 2 + 5 * (WR - BR) + 9 * (WQ - BQ));
			if (matDifference <= 4)
				conversionFactor = conversionFactor * BalanceFactorOld[matDifference];
		}

		conversion = ScaleFactor(std::lround(conversionFactor * MAX_FACTOR));
		return EvalValue(ScoreMultiply(HScore));
	}

	/*
	EvalWaarde imbalanceK(int WPion, int WP, int WL, int WLL, int WLD, int WT, int WD,
		int ZPion, int ZP, int ZL, int ZLL, int ZLD, int ZT, int ZD, SchaalFactor &conversion)
	{
		double conversie;
		int matScore;

		const int MAX_FASE = 32;
		int fase = (WP + ZP) + (WL + ZL) + 3 * (WT + ZT) + 6 * (WD + ZD);
		fase = std::min(fase, MAX_FASE);

		int matIndexW = 6 * WP + 7 * WL + 10 * WT + 18 * WD;
		int matIndexZ = 6 * ZP + 7 * ZL + 10 * ZT + 18 * ZD;

		int matScoreW = 1000 * WPion + 3465 * WP + 3728 * WL + 6209 * WT + 11759 * WD
			+ WPion * (21 * WP - 79 * WT - 129 * WD)
			+ WP * (4 * WL - 45 * WT - 47 * WD)
			+ WL * (-30 * WT - 60 * WD)
			+ WT * (193 - 193 * WT - 524 * WD)
			+ WD * (712 - 712 * WD);

		int matScoreZ = 1000 * ZPion + 3465 * ZP + 3728 * ZL + 6209 * ZT + 11759 * ZD
			+ ZPion * (21 * ZP - 79 * ZT - 129 * ZD)
			+ ZP * (4 * ZL - 45 * ZT - 47 * ZD)
			+ ZL * (-30 * ZT - 60 * ZD)
			+ ZT * (193 - 193 * ZT - 524 * ZD)
			+ ZD * (712 - 712 * ZD);

		if (WLL && WLD)	// W loperpaar
		{
			matScoreW += 488 - 76 * WD - 3 * WP - 8 * WPion - 17 * WT;
			if (!(ZP + ZL))
				matScoreW += 37;
		}

		if (ZLL && ZLD)	// Z Loperpaar
		{
			matScoreZ += 488 - 76 * ZD - 3 * ZP - 8 * ZPion - 17 * ZT;
			if (!(WL + WP))
				matScoreZ += 37;
		}

		matScoreW += ZPion * (9 * WT + 2 * WL + 18 * WP + 24 * WD);
		matScoreZ += WPion * (9 * ZT + 2 * ZL + 18 * ZP + 24 * ZD);

		int WLichtStuk = WL + WP;
		int ZLichtStuk = ZL + ZP;

		if (WLichtStuk > ZLichtStuk)
		{
			if (WLichtStuk > ZLichtStuk + 2)
				matScoreW += 200;
			if (WL > ZL)
				matScoreW += 17;
			if (WP > ZP)
				matScoreW -= 17;
		}
		else if (WLichtStuk < ZLichtStuk)
		{
			if (ZLichtStuk > WLichtStuk + 2)
				matScoreZ += 200;
			if (WL < ZL)
				matScoreZ += 17;
			if (WP < ZP)
				matScoreZ -= 17;
		}

		matScore = matScoreW - matScoreZ;
		conversie = 0.95 + 0.21 * fase / MAX_FASE;

		// pionneneindspel
		if (ZPion + WPion && !matIndexW && !matIndexZ)
			conversie = 1.3;

		// D vs D of T vs T -> * 0.9
		if (matIndexW == 10 && matIndexZ == 10 || matIndexW == 18 && matIndexZ == 18)
			conversie *= 0.9;

		// ongelijke L
		if (WL == 1 && ZL == 1 && ((WLL && ZLD) || (WLD && ZLL)))
		{
			// ongelijke lopers
			if (matIndexW == 7 && matIndexZ == 7)
				conversie = 0.6;
			else
				conversie *= 0.95;
		}

		if (WPion + ZPion)
		{
			if (matScore <= 0)
			{
				if (matScore)
				{
					if (ZPion <= 4)
						conversie *= PionFactor[ZPion];
				}
				else
				{
					int pion = std::max(WPion, ZPion);
					if (pion <= 4)
						conversie *= PionFactor[pion];
				}
			}
			else if (WPion <= 4)
				conversie *= PionFactor[WPion];
		}
		else // geen pionnen
		{
			int matDifference = abs(matIndexW / 2 - matIndexZ / 2);
			if (matDifference <= 3)
				conversie *= BalanceFactor[matDifference];
		}

		//if (abs(matScore) > 600)
		// matScore = matScore * 107 / 100;

		conversion = SchaalFactor(std::lround(conversie * MAX_FACTOR));
		return EvalWaarde(ScoreMultiply(matScore));
	} */

} // namespace

namespace Material
{
	Value Tuple::value_from_function(const Position& pos) const
	{
		return (*Threads.endgames.value_functions[valueFunctionIndex])(pos);
	}

	ScaleFactor Tuple::scalefactor_from_function(const Position & pos, Color color) const
	{
		if (scaleFunctionIndex[color] >= 0)
		{
			ScaleFactor sf = (*Threads.endgames.factor_functions[scaleFunctionIndex[color]])(pos);
			if (sf != NO_FACTOR)
				return sf;
		}
		return ScaleFactor(factor[color]);
	}

	Tuple* probe(const Position& pos)
	{
		// index in materialTable is de sleutel met kleur lopers (want conversie-factoren hangen af van kleur lopers)
		// index in endgames is sleutel zonder kleur lopers (want de routines maken dit onderscheid zelf)
		Key64 key = pos.material_key() ^ pos.bishopcolor_key();
		Tuple* e = pos.ti()->materialTable[key];

		if (e->key64 == key)
			return e;

		std::memset(e, 0, sizeof(Tuple));
		e->key64 = key;
		e->factor[WHITE] = e->factor[BLACK] = (uint8_t)NORMAL_FACTOR;
		e->gamePhase = pos.game_phase();
		e->conversionFactor = MAX_FACTOR;
		e->valueFunctionIndex = e->scaleFunctionIndex[WHITE] = e->scaleFunctionIndex[BLACK] = -1;

		e->value = imbalanceH(
			pos.count(WHITE, PAWN), pos.count(WHITE, KNIGHT), pos.count(WHITE, BISHOP),
			popcount(pos.pieces(WHITE, BISHOP) & ~DarkSquares), popcount(pos.pieces(WHITE, BISHOP) & DarkSquares),
			pos.count(WHITE, ROOK), pos.count(WHITE, QUEEN),
			pos.count(BLACK, PAWN), pos.count(BLACK, KNIGHT), pos.count(BLACK, BISHOP),
			popcount(pos.pieces(BLACK, BISHOP) & ~DarkSquares), popcount(pos.pieces(BLACK, BISHOP) & DarkSquares),
			pos.count(BLACK, ROOK), pos.count(BLACK, QUEEN), e->conversionFactor);

		e->valueFunctionIndex = Threads.endgames.probe_value(pos.material_key());
		if (e->valueFunctionIndex >= 0)
			return e;

		for (Color color = WHITE; color <= BLACK; ++color)
			if (!more_than_one(pos.pieces(~color)) && pos.non_pawn_material(color) >= MATL_ROOK)
			{
				e->valueFunctionIndex = color == WHITE ? 0 : 1;  // endgameValue_KXK
				return e;
			}

		Color strongSide;
		int sf = Threads.endgames.probe_scalefactor(pos.material_key(), strongSide);

		if (sf >= 0)
		{
			e->scaleFunctionIndex[strongSide] = sf;
			return e;
		}

		for (Color color = WHITE; color <= BLACK; ++color)
		{
			if (pos.non_pawn_material(color) == MATL_BISHOP && pos.count(color, BISHOP) == 1 && pos.count(color, PAWN) >= 1)
				e->scaleFunctionIndex[color] = color == WHITE ? 0 : 1;  // endgameScaleFactor_KBPsK

			else if (!pos.count(color, PAWN) && pos.non_pawn_material(color) == MATL_QUEEN && pos.count(color, QUEEN) == 1
				&& pos.count(~color, ROOK) == 1 && pos.count(~color, PAWN) >= 1)
				e->scaleFunctionIndex[color] = color == WHITE ? 2 : 3;  // endgameScaleFactor_KQKRPs
		}

		MaterialValue npm_w = pos.non_pawn_material(WHITE);
		MaterialValue npm_b = pos.non_pawn_material(BLACK);

		if (npm_w + npm_b == MATL_0 && pos.pieces(PAWN)) // pionneneindspel, maar niet KPK
		{
			if (!pos.count(BLACK, PAWN))
			{
				assert(pos.count(WHITE, PAWN) >= 2);
				e->scaleFunctionIndex[WHITE] = 4;  // endgameScaleFactor_KPsK<WIT>
			}
			else if (!pos.count(WHITE, PAWN))
			{
				assert(pos.count(BLACK, PAWN) >= 2);
				e->scaleFunctionIndex[BLACK] = 5;  // endgameScaleFactor_KPsK<ZWART>
			}
			else if (pos.count(WHITE, PAWN) == 1 && pos.count(BLACK, PAWN) == 1)
			{
				e->scaleFunctionIndex[WHITE] = 6;    // endgameScaleFactor_KPKP<WIT>
				e->scaleFunctionIndex[BLACK] = 7;  // endgameScaleFactor_KPKP<ZWART>
			}
		}

		if (!pos.count(WHITE, PAWN) && npm_w - npm_b <= MATL_BISHOP)
			e->factor[WHITE] = uint8_t(npm_w < MATL_ROOK ? DRAW_FACTOR :
				npm_b <= MATL_BISHOP ? ScaleFactor(6) : ScaleFactor(22));

		if (!pos.count(BLACK, PAWN) && npm_b - npm_w <= MATL_BISHOP)
			e->factor[BLACK] = uint8_t(npm_b <  MATL_ROOK ? DRAW_FACTOR :
				npm_w <= MATL_BISHOP ? ScaleFactor(6) : ScaleFactor(22));

		if (pos.count(WHITE, PAWN) == 1 && npm_w - npm_b <= MATL_BISHOP)
			e->factor[WHITE] = (uint8_t)ONE_PAWN_FACTOR;

		if (pos.count(BLACK, PAWN) == 1 && npm_b - npm_w <= MATL_BISHOP)
			e->factor[BLACK] = (uint8_t)ONE_PAWN_FACTOR;

		e->conversion_is_estimated = true;

		return e;
	}
} // namespace
