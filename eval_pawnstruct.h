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
		Score pionnen_score() const { return score; }
		Bitboard pion_aanval(Color kleur) const { return pionAanval[kleur]; }
		Bitboard vrijpionnen(Color kleur) const { return vrijPionnen[kleur]; }
		Bitboard veilig_voor_pion(Color kleur) const { return veiligVoorPion[kleur]; }
		int pion_bereik(Color kleur) const { return pionBereik[kleur]; }

		int half_open_lijn(Color kleur, File f) const
		{
			return halfOpenLijnen[kleur] & (1 << f);
		}

		int semiopen_side(Color kleur, File f, bool leftSide) const
		{
			return halfOpenLijnen[kleur] & (leftSide ? (1 << f) - 1 : ~((1 << (f + 1)) - 1));
		}

		int pionnen_op_kleur(Color kleur, Square veld) const
		{
			return pionnenOpKleur[kleur][!!(DonkereVelden & veld)];
		}

		int pionnen_niet_op_kleur(Color kleur, Square veld) const
		{
			return pionnenOpKleur[kleur][!(DonkereVelden & veld)];
		}

		template<Color IK>
		Score koning_veiligheid(const Position& pos)
		{
			if (koningVeld[IK] != pos.koning(IK) || rokadeMogelijkheden[IK] != pos.rokade_mogelijkheden(IK))
				koningVeiligheid[IK] = bereken_koning_veiligheid<IK>(pos);
			return koningVeiligheid[IK];
		}

		template<Color IK>
		Score bereken_koning_veiligheid(const Position& pos);

		Key64 sleutel64;
		Bitboard vrijPionnen[KLEUR_N];
		Bitboard pionAanval[KLEUR_N];
		Bitboard veiligVoorPion[KLEUR_N];
		Score score;
		Score koningVeiligheid[KLEUR_N];
		uint8_t koningVeld[KLEUR_N];
		uint8_t rokadeMogelijkheden[KLEUR_N];
		uint8_t halfOpenLijnen[KLEUR_N];
		uint8_t pionBereik[KLEUR_N];
		int asymmetrie;
		uint8_t pionnenOpKleur[KLEUR_N][KLEUR_N]; // [kleur][kleur veld]
		int gemiddeldeLijn, nPionnen;
		bool conversie_moeilijk;
		int safety[KLEUR_N];
		int lijn_breedte;
		//int open_lijnen;
		char padding[20]; // Align to 128 bytes
	};
	static_assert(offsetof(struct Tuple, halfOpenLijnen) == 72, "offset wrong");
	static_assert(sizeof(Tuple) == 128, "Pawn Entry size incorrect");

	typedef HashTable<Tuple, PION_HASH_GROOTTE> Table;

	void initialisatie();
	Tuple* probe(const Position& pos);
} // namespace

#endif // #ifndef EVAL_PAWNSTRUCT_H
