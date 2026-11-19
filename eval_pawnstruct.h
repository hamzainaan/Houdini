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
		Bitboard pion_aanval(Color kleur) const { return pionAanval[kleur]; }
		Bitboard vrijpionnen(Color kleur) const { return vrijPionnen[kleur]; }
		Bitboard safe_from_pawn(Color kleur) const { return veiligVoorPion[kleur]; }
		int pawn_range(Color kleur) const { return pionBereik[kleur]; }

		int half_open_file(Color kleur, File f) const
		{
			return halfOpenLijnen[kleur] & (1 << f);
		}

		int semiopen_side(Color kleur, File f, bool leftSide) const
		{
			return halfOpenLijnen[kleur] & (leftSide ? (1 << f) - 1 : ~((1 << (f + 1)) - 1));
		}

		int pawns_on_color(Color kleur, Square veld) const
		{
			return pionnenOpKleur[kleur][!!(DarkSquares & veld)];
		}

		int pawns_not_on_color(Color kleur, Square veld) const
		{
			return pionnenOpKleur[kleur][!(DarkSquares & veld)];
		}

		template<Color Us>
		Score koning_veiligheid(const Position& pos)
		{
			if (koningVeld[Us] != pos.koning(Us) || rokadeMogelijkheden[Us] != pos.castling_rights(Us))
				koningVeiligheid[Us] = compute_king_safety<Us>(pos);
			return koningVeiligheid[Us];
		}

		template<Color Us>
		Score compute_king_safety(const Position& pos);

		Key64 sleutel64;
		Bitboard vrijPionnen[COLOR_N];
		Bitboard pionAanval[COLOR_N];
		Bitboard veiligVoorPion[COLOR_N];
		Score score;
		Score koningVeiligheid[COLOR_N];
		uint8_t koningVeld[COLOR_N];
		uint8_t rokadeMogelijkheden[COLOR_N];
		uint8_t halfOpenLijnen[COLOR_N];
		uint8_t pionBereik[COLOR_N];
		int asymmetrie;
		uint8_t pionnenOpKleur[COLOR_N][COLOR_N]; // [kleur][kleur veld]
		int gemiddeldeLijn, nPionnen;
		bool conversie_moeilijk;
		int safety[COLOR_N];
		int lijn_breedte;
		//int open_lijnen;
		char padding[20]; // Align to 128 bytes
	};
	static_assert(offsetof(struct Tuple, halfOpenLijnen) == 72, "offset wrong");
	static_assert(sizeof(Tuple) == 128, "Pawn Entry size incorrect");

	typedef HashTable<Tuple, PAWN_HASH_SIZE> Table;

	void initialize();
	Tuple* probe(const Position& pos);
} // namespace

#endif // #ifndef EVAL_PAWNSTRUCT_H
