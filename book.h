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
	void new_game();

private:
	std::string boekBestand;
	Bitboard vorigeStukken;
	int gemisteProbes;

	bool open(const char* bookPath);
	size_t find_tuple(Key64 sleutel);
	Move move_from_polyglot(const Position& pos, Move pg_zet);
	bool gives_repetition(Position & pos, Move pg_zet);
	bool read_tuple(PolyglotTuple* tupel);
};

#endif // #ifndef BOOK_H_INCLUDED
