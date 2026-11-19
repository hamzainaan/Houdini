/*
*/

#ifndef EVALUATION_H
#define EVALUATION_H

#include "houdini.h"

class Position;

namespace Evaluation 
{
	void initialisatie();
	void init_tune();

	Value evaluatie(const Position& pos, Value alfa, Value beta);
	Value evaluatie_na_nullzet(Value eval);
	bool minstens_twee_mobiele_stukken(const Position& pos);
	void export_tabel();
	void import_tabel();
	extern Score VeiligheidTabel[1024];
}

#endif // #ifndef EVALUATION_H
