/*
*/

#include <iostream>

#include "bitboard.h"
#include "evaluation.h"
#include "position.h"
#include "search.h"
#include "search_smp.h"
#include "move_hash.h"
#include "uci.h"

#ifdef USE_LICENSE
extern bool LICENSE_OK;
void read_license_tables();
#endif

int main(int argc, char* argv[])
{
	std::cout << engine_info() << std::endl;
	Threads.programStart = now();

	UCI::initialize(UciOptions);
	PST::initialize();
	Bitboards::initialize();
	Position::initialize();
	Search::initialize();
	Evaluation::initialize();
	Pawns::initialize();

#ifdef EXPORT_TABLES
	Search::export_table();
	Evaluation::export_table();
	PST::export_table();
#endif
#ifdef USE_LICENSE
	read_license_tables();
	if (!LICENSE_OK)
		std::cout << "info string No valid license found - the engine strength will be reduced" << std::endl;
#endif
#ifdef IMPORT_TABLES
	Search::import_table();
	Evaluation::import_table();
	PST::import_table();
#endif

	Threads.initialize();
	MainHash.change_size(UciOptions["Hash"]);
	UCI::mainLoop(argc, argv);

	Threads.exit();
	return 0;
}
