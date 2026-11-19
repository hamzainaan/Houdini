/*
*/

#ifndef EVALUATION_H
#define EVALUATION_H

#include "houdini.h"

class Position;

namespace Evaluation 
{
	void initialize();
	void init_tune();

	Value evaluation(const Position& pos, Value alpha, Value beta);
	Value evaluation_after_nullmove(Value eval);
	bool at_least_two_mobile_pieces(const Position& pos);
	void export_table();
	void import_table();
	extern Score SafetyTable[1024];
}

#endif // #ifndef EVALUATION_H
