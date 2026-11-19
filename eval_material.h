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
		GamePhase game_phase() const { return GamePhase(gamePhase); }

		bool has_value_function() const { return valueFunctionIndex >= 0; }
		Value value_from_function(const Position& pos) const;

		ScaleFactor scalefactor_from_function(const Position& pos, Color color) const;

		Key64 key64;
		int valueFunctionIndex;
		int scaleFunctionIndex[COLOR_N];
		EvalValue value;
		ScaleFactor conversionFactor;
		uint8_t factor[COLOR_N], gamePhase;
		bool conversion_is_estimated;
		//char padding[8]; // Align to 32 bytes
	};
	static_assert(sizeof(Tuple) == 32, "Material Entry size incorrect");

	typedef HashTable<Tuple, MATERIAL_HASH_SIZE> Table;

	Tuple* probe(const Position& pos);
}

#endif // #ifndef EVAL_MATERIAL_H
