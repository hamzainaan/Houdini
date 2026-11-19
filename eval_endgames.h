/*
*/

#ifndef EVAL_ENDGAMES_H
#define EVAL_ENDGAMES_H

#include <map>

#include "position.h"
#include "houdini.h"

typedef Value (*endgame_value_fx)(const Position& pos);
typedef ScaleFactor (*endgame_scalefactor_fx)(const Position& pos);

class Endgames 
{
	void add_value(const char* pieces, endgame_value_fx, endgame_value_fx);
	void add_scalefactor(const char* pieces, endgame_scalefactor_fx, endgame_scalefactor_fx);

	typedef std::map<Key64, int> FunctionIndexMap;

	int waarde_aantal, factor_aantal;
	FunctionIndexMap mapWaarde;
	FunctionIndexMap mapSchaalFactor;
public:
	Endgames();
	void initialize();

	int probe_value(Key64 key);
	int probe_scalefactor(Key64 key, Color& sterkeZijde);

	endgame_value_fx waarde_functies[16];
	endgame_scalefactor_fx factor_functies[32];
};

template <Color sterkeZijde> Value endgameValue_KXK(const Position& pos);
template <Color sterkeZijde> ScaleFactor endgameScaleFactor_KBPsK(const Position& pos);
template <Color sterkeZijde> ScaleFactor endgameScaleFactor_KQKRPs(const Position& pos);
template <Color sterkeZijde> ScaleFactor endgameScaleFactor_KPsK(const Position& pos);
template <Color sterkeZijde> ScaleFactor endgameScaleFactor_KPKP(const Position& pos);


#endif // #ifndef EVAL_ENDGAMES_H
