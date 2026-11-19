/*
*/

#ifndef EVAL_PAWNSTRUCT_H
#define EVAL_PAWNSTRUCT_H

#include "util_tools.h"
#include "position.h"
#include "houdini.h"

namespace Pawns 
{
	struct Tuple
	{
		Score pawns_score() const { return score; }
		Bitboard pawn_attack(Color color) const { return pawnAttack[color]; }
		Bitboard passed_pawns(Color color) const { return passedPawns[color]; }
		Bitboard safe_from_pawn(Color color) const { return safeFromPawn[color]; }
		int pawn_range(Color color) const { return pawnRange[color]; }

		int half_open_file(Color color, File f) const
		{
			return halfOpenFiles[color] & (1 << f);
		}

		int semiopen_side(Color color, File f, bool leftSide) const
		{
			return halfOpenFiles[color] & (leftSide ? (1 << f) - 1 : ~((1 << (f + 1)) - 1));
		}

		int pawns_on_color(Color color, Square square) const
		{
			return pawnsOnColor[color][!!(DarkSquares & square)];
		}

		int pawns_not_on_color(Color color, Square square) const
		{
			return pawnsOnColor[color][!(DarkSquares & square)];
		}

		template<Color Us>
		Score king_safety(const Position& pos)
		{
			if (kingSquare[Us] != pos.king(Us) || castlingRights[Us] != pos.castling_rights(Us))
				kingSafety[Us] = compute_king_safety<Us>(pos);
			return kingSafety[Us];
		}

		template<Color Us>
		Score compute_king_safety(const Position& pos);

		Key64 key64;
		Bitboard passedPawns[COLOR_N];
		Bitboard pawnAttack[COLOR_N];
		Bitboard safeFromPawn[COLOR_N];
		Score score;
		Score kingSafety[COLOR_N];
		uint8_t kingSquare[COLOR_N];
		uint8_t castlingRights[COLOR_N];
		uint8_t halfOpenFiles[COLOR_N];
		uint8_t pawnRange[COLOR_N];
		int asymmetry;
		uint8_t pawnsOnColor[COLOR_N][COLOR_N]; // [kleur][kleur veld]
		int averageFile, nPawns;
		bool conversion_difficult;
		int safety[COLOR_N];
		int file_width;
		//int open_lijnen;
		char padding[20]; // Align to 128 bytes
	};
	static_assert(offsetof(struct Tuple, halfOpenFiles) == 72, "offset wrong");
	static_assert(sizeof(Tuple) == 128, "Pawn Entry size incorrect");

	typedef HashTable<Tuple, PAWN_HASH_SIZE> Table;

	void initialize();
	Tuple* probe(const Position& pos);
} // namespace

#endif // #ifndef EVAL_PAWNSTRUCT_H
