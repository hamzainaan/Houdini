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
	Move probe(Position& pos, const std::string& bookPath, bool bestVariation);
	void new_game();

private:
	std::string bookFile;
	Bitboard previousPieces;
	int missedProbes;

	bool open(const char* bookPath);
	size_t find_tuple(Key64 key);
	Move move_from_polyglot(const Position& pos, Move pg_move);
	bool gives_repetition(Position & pos, Move pg_move);
	bool read_tuple(PolyglotTuple* tuple);
};

#endif // #ifndef BOOK_H_INCLUDED
