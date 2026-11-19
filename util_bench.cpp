/*
*/

#include <fstream>
#include <iostream>
#include <istream>
#include <vector>

#include "houdini.h"
#include "util_tools.h"
#include "position.h"
#include "search.h"
#include "search_smp.h"
#include "uci.h"

using namespace std;

void benchmark(const Position& current, istream& is)
{
	string word;
	vector<string> positions;
	TimeLimit timeLimit;

	string ttSize = (is >> word) ? word : "16";
	string threads = (is >> word) ? word : "1";
	int limit = (is >> word) ? stoi(word) : DEFAULT_BENCHMARK_DEPTH;
	string fenFile = (is >> word) ? word : "default";
	string limitType = (is >> word) ? word : "depth";
	int filter = (is >> word) ? stoi(word) : 0;

	UciOptions["Hash"] = ttSize;
	UciOptions["Threads"] = threads;
	Search::clear_all();

	if (limitType == "time")
		timeLimit.movetime = limit;

	else if (limitType == "nodes")
		timeLimit.nodes = limit;

	else if (limitType == "mate")
		timeLimit.mate = limit;

	else
		timeLimit.depth = limit;

	if (fenFile == "current")
		positions.push_back(current.fen());

	else
	{
		if (fenFile == "default")
			fenFile = "benchpos.txt";
		string fen;
		ifstream file(fenFile);

		if (!file.is_open())
		{
			cerr << "Unable to open file " << fenFile << endl;
			return;
		}

		while (getline(file, fen))
			if (!fen.empty())
				positions.push_back(fen);

		file.close();
	}

	uint64_t nodes = 0;
	TimePoint elapsed = now();
	Position pos;

	cerr << positions.size() << " bench positions" << endl;
	ofstream bad_file, ok_file;
	if (filter)
	{
		bad_file.open("bad_pos.epd");
		ok_file.open("ok_pos.epd");
	}

	for (size_t i = 0; i < positions.size(); ++i)
	{
		pos.set(positions[i], UciOptions["UCI_Chess960"], Threads.main());

		if (i % 10 == 9)
			cerr << "o";
		else
			cerr << ".";
		cout << endl;

		//if (limitType == "perft")
		//    nodes += Search::perft(pos, limits.depth * PLY);

		//else
		{
			timeLimit.startTime = now();
			Threads.start_search_task(pos, timeLimit);
			Threads.main()->wait_for_search_finished();
			nodes += Threads.visited_nodes();
		}
		if (filter)
		{
			ofstream* outfile = abs(Threads.main()->previousRootScore) > Value(2 * filter) ? &bad_file : &ok_file;
			*outfile << positions[i] << " " << UCI::value(Threads.main()->previousRootScore) << endl;
		}
	}
	if (filter)
	{
		bad_file.close();
		ok_file.close();
	}

	elapsed = std::max(now() - elapsed, TimePoint(1));

	dbg_print();

	cerr << "\n==========================="
		<< "\nTotal time (ms) : " << elapsed
		<< "\nNodes searched  : " << nodes
		<< "\nNodes/second    : " << nodes / elapsed * 1000 << endl;
}
