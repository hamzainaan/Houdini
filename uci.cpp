/*
*/

#include <iostream>
#include <sstream>
#include <iomanip>

#include "houdini.h"
#include "evaluation.h"
#include "move_generation.h"
#include "position.h"
#include "search.h"
#include "search_smp.h"
#include "uci_time.h"
#include "uci.h"
#include "polarssl\aes.h"
#include "zlib\zlib.h"

using namespace std;

extern void benchmark(const Position& pos, istream& is);
#ifdef USE_LICENSE
extern bool LICENSE_OK;
#endif

namespace
{
	const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

	void position(Position& pos, istringstream& is)
	{
		Move move;
		string token, fen;

		is >> token;

		if (token == "startpos")
		{
			fen = StartFEN;
			is >> token; // "moves"
		}
		else if (token == "fen")
			while (is >> token && token != "moves")
				fen += token + " ";
		else
			return;

		pos.set(fen, UciOptions["UCI_Chess960"], Threads.main());
		string validation = pos.validate_position();
		if (validation != "")
		{
			sync_cout << "Invalid position: " << validation << endl << "Reverting to starting position" << sync_endl;
			pos.set(StartFEN, UciOptions["UCI_Chess960"], Threads.main());
		}

		// move list
		while (is >> token && (move = UCI::move_from_string(pos, token)) != NO_MOVE)
		{
			pos.do_move(move);
			pos.increase_game_ply();
		}
	}


	void setoption(istringstream& is)
	{
		string token, name, value;

		is >> token;  // "name"

		while (is >> token && token != "value")
			name += string(" ", name.empty() ? 0 : 1) + token;

		while (is >> token)
			value += string(" ", value.empty() ? 0 : 1) + token;

		if (UciOptions.count(name))
			UciOptions[name] = value;
		else
			sync_cout << "info string Unknown UCI option \"" << name << "\"" << sync_endl;
	}

	void setoption2(istringstream& is)
	{
		string name, value;

		is >> name;
		is >> value;

		if (UciOptions.count(name))
			UciOptions[name] = value;
		else
			sync_cout << "info string Unknown UCI option \"" << name << "\"" << sync_endl;
	}


	void go(Position& pos, istringstream& is)
	{
		TimeLimit timeLimit;
		string word;

		timeLimit.startTime = now();

		while (is >> word)
		{
			if (word == "searchmoves")
				while (is >> word)
					timeLimit.searchMoves.add(UCI::move_from_string(pos, word));

			else if (word == "wtime")     is >> timeLimit.time[WHITE];
			else if (word == "btime")     is >> timeLimit.time[BLACK];
			else if (word == "winc")      is >> timeLimit.inc[WHITE];
			else if (word == "binc")      is >> timeLimit.inc[BLACK];
			else if (word == "movestogo") is >> timeLimit.movestogo;
			else if (word == "depth")     is >> timeLimit.depth;
			else if (word == "nodes")     is >> timeLimit.nodes;
			else if (word == "movetime")  is >> timeLimit.movetime;
			else if (word == "mate")      is >> timeLimit.mate;
			else if (word == "infinite")  timeLimit.infinite = 1;
			else if (word == "ponder")    timeLimit.ponder = 1;
		}

		// go = go infinite
		if (!timeLimit.time[pos.side_to_move()] && !timeLimit.movestogo && !timeLimit.depth && !timeLimit.nodes
			&& !timeLimit.movetime && !timeLimit.mate)
			timeLimit.infinite = 1;

		// for Arena we accept pondering even if the UCI PONDER is not activated
		if (timeLimit.ponder && !UciOptions["Ponder"])
			UciOptions["Ponder"] = string("true");

#ifdef USE_LICENSE
		if (!LICENSE_OK)
			sync_cout << "info string No valid license found - the engine strength will be reduced" << sync_endl;
#endif

		Threads.start_search_task(pos, timeLimit);
	}

} // namespace


std::string trim(const std::string& str, const std::string& whitespace = " \t")
{
	const auto strBegin = str.find_first_not_of(whitespace);
	if (strBegin == std::string::npos)
		return ""; // no content

	const auto strEnd = str.find_last_not_of(whitespace);
	const auto strRange = strEnd - strBegin + 1;

	return str.substr(strBegin, strRange);
}


void show_evaluation(const Position& pos)
{
	Value v = Evaluation::evaluation(pos, NO_VALUE, NO_VALUE);
	v = pos.side_to_move() == WHITE ? v : -v;

	sync_cout << std::showpos << std::fixed << std::setprecision(2)
		<< "Evaluation: " << double(v) / VALUE_PAWN << sync_endl;
}


void UCI::mainLoop(int argc, char* argv[])
{
	Position pos;
	string word, cmd;

	pos.set(StartFEN, false, Threads.main());
	Search::clear_all();

#ifdef TRACE_TM
	open_tracefile();
#endif

	for (int i = 1; i < argc; ++i)
		cmd += std::string(argv[i]) + " ";

	do {
		if (argc == 1 && !getline(cin, cmd))
			cmd = "quit";
		cmd = trim(cmd);
		if (cmd == "")
			continue;

		istringstream is(cmd);

		word.clear();
		is >> skipws >> word;

		if (word == "quit"
			|| word == "stop"
			|| (word == "ponderhit" && Search::Signals.stopOnPonderhit))
		{
			Search::Signals.stopAnalysis = true;
			Threads.main()->wake_up(false);
			//Threads.main()->wait_for_search_finished();
		}
		else if (word == "ponderhit")
		{
			Search::Clock.ponder = 0;
			Search::adjust_time_after_ponderhit();
		}

		else if (word == "uci")
			sync_cout << "id name " << engine_info(true)
			<< UciOptions
			<< "uciok" << sync_endl;

		else if (word == "ucinewgame")
		{
			Search::Signals.stopAnalysis = true;
			Threads.main()->wake_up(false);
			Threads.main()->wait_for_search_finished();
			Search::clear_all();
		}
		else if (word == "isready")
			sync_cout << "readyok" << sync_endl;
		else if (Search::Running)
			sync_cout << "info string Search running, command ignored" << sync_endl;
		else
		{
			if (word == "go") go(pos, is);
			else if (word == "position")  position(pos, is);
			else if (word == "setoption") setoption(is);
			else if (word == "set")       setoption2(is);
			else if (word == "bench")     benchmark(pos, is);
			else if (word == "b")         benchmark(pos, is);
			//else if (word == "d")         sync_cout << pos << sync_endl;
			else if (word == "e")         show_evaluation(pos);
#ifdef USE_LARGE_PAGES
			else if (word == "lp")        test_large_pages();
#endif
			else if (word == "z?")
				cerr << crc32(crc32(0, nullptr, 0), (Bytef *)aes_setkey_enc, 4096) << endl;
			else
				sync_cout << "info string Unknown UCI command \"" << cmd << "\"" << sync_endl;
		}

	} while (word != "quit" && argc == 1);

	Threads.main()->wait_for_search_finished();

#ifdef TRACE_TM
	close_tracefile();
#endif
}


string UCI::value(Value v)
{
	stringstream ss;

	if (abs(v) < LONGEST_MATE_VALUE)
	{
		if (abs(v) < WIN_VALUE)
			v -= Threads.rootContemptValue / 2;

#if 1
		ss << "cp " << v / 2;
#else
		int vv;
		if (abs(v) >= Value(400))
			vv = 16 * v;
		else if (v <= Value(-200))
			vv = 14 * v - Value(800);
		else if (v <= Value(-100))
			vv = 16 * v - Value(400);
		else if (v <= Value(100))
			vv = 20 * v;
		else if (v <= Value(200))
			vv = 16 * v + Value(400);
		else
			vv = 14 * v + Value(800);

		ss << "cp " << vv * 100 / (16 * VALUE_PAWN);
#endif
	}
	else
		ss << "mate " << (v > 0 ? MATE_VALUE - v + 1 : -MATE_VALUE - v) / 2;

	return ss.str();
}


std::string UCI::square(Square square)
{
	return std::string{ char('a' + file_of(square)), char('1' + rank_of(square)) };
}


string UCI::move(Move move, const Position& pos)
{
	char sMove[6];

	Square from = from_square(move);
	Square to = to_square(move);

	if (move == NO_MOVE || move == NULL_MOVE)
		return "0000";

	if (move_type(move) == CASTLING && pos.is_chess960())
		to = pos.castling_rook_square(to);

	//string sMove = UCI::square(from) + UCI::square(to);
	sMove[0] = 'a' + file_of(from);
	sMove[1] = '1' + rank_of(from);
	sMove[2] = 'a' + file_of(to);
	sMove[3] = '1' + rank_of(to);
	if (move < Move(PROMOTION_N))
		return string(sMove, 4);

	sMove[4] = "   nbrq"[promotion_piece(move)];
	return string(sMove, 5);
}


Move UCI::move_from_string(const Position& pos, string& str)
{
	if (pos.is_chess960())
	{
		// Arena uses O-O and O-O-O
		if (str == "O-O")
			str = UCI::move(make_move(CASTLING, pos.king(pos.side_to_move()), relative_square(pos.side_to_move(), SQ_G1)), pos);
		else if (str == "O-O-O")
			str = UCI::move(make_move(CASTLING, pos.king(pos.side_to_move()), relative_square(pos.side_to_move(), SQ_C1)), pos);
	}

	if (str.length() == 5)
		str[4] = char(tolower(str[4]));

	for (const auto& move : LegalMoveList(pos))
		if (str == UCI::move(move, pos))
			return move;

	return NO_MOVE;
}
