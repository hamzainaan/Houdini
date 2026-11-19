/*
*/

#ifndef EVAL_MATERIAL_H
#define EVAL_MATERIAL_H

#include "eval_endgames.h"
#include "util_tools.h"
#include "position.h"
#include "houdini.h"

namespace Material 
{
	struct Tuple
	{
		GamePhase partij_fase() const { return GamePhase(partijFase); }

		bool heeft_waarde_functie() const { return waardeFunctieIndex >= 0; }
		Value waarde_uit_functie(const Position& pos) const;

		ScaleFactor schaalfactor_uit_functie(const Position& pos, Color kleur) const;

		Key64 sleutel64;
		int waardeFunctieIndex;
		int schaalFunctieIndex[COLOR_N];
		EvalValue waarde;
		ScaleFactor conversie;
		uint8_t factor[COLOR_N], partijFase;
		bool conversie_is_geschat;
		//char padding[8]; // Align to 32 bytes
	};
	static_assert(sizeof(Tuple) == 32, "Material Entry size incorrect");

	typedef HashTable<Tuple, MATERIAL_HASH_SIZE> Table;

	Tuple* probe(const Position& pos);
}

#endif // #ifndef EVAL_MATERIAL_H
