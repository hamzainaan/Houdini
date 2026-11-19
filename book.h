/*
*/

#ifndef BOOK_H_INCLUDED
#define BOOK_H_INCLUDED

#include <fstream>
#include <string>

#include "position.h"

namespace
{
	struct PolyglotTuple;
}

class PolyglotBook : private std::ifstream
{
public:
	PolyglotBook();
	~PolyglotBook();
	Move probe(Position& pos, const std::string& bookPath, bool besteVariant);
	void nieuwe_partij();

private:
	std::string boekBestand;
	Bitboard vorigeStukken;
	int gemisteProbes;

	bool open(const char* bookPath);
	size_t vind_tupel(Key64 sleutel);
	Move zet_van_polyglot(const Position& pos, Move pg_zet);
	bool geeft_zetherhaling(Position & pos, Move pg_zet);
	bool lees_tupel(PolyglotTuple* tupel);
};

#endif // #ifndef BOOK_H_INCLUDED
