/*
8/8/8/5k2/4b3/3K4/2B5/N1B5 w - - 0 1
8/3r4/1P6/5k2/3Kb3/1R1B4/8/8 w - - 0 1
8/1R6/6p1/r6k/3P4/2KP4/8/8 w - - 0 1
*/

#include <iostream>

#include "houdini.h"
#include "position.h"
#include "search.h"
#include "syzygy\tbprobe.h"

static bool SyzygyInUse = false;
static std::string CurrentSyzygyPath = "";
Value Syzygy_probe_wdl(Position& pos, Value alfa, Value beta);
Value Syzygy_probe_dtz(Position& pos, Value alfa, Value beta);


void Syzygy_init(const std::string& path)
{
	bool PathSpecified = (path != "") && (path != "<empty>");
	if (PathSpecified)
	{
		if (path != CurrentSyzygyPath)
		{
			syzygy_path_init(path);
			CurrentSyzygyPath = path;
		}
		EGTB::MaxPieces_wdl = EGTB::MaxPieces_dtz = TBmax_men;
		if (EGTB::MaxPieces_wdl)
		{
			SyzygyInUse = true;
			EGTB::EGTB_probe_wdl = &Syzygy_probe_wdl;
			EGTB::EGTB_probe_dtz = &Syzygy_probe_dtz;
			sync_cout << "info string Syzygy " << EGTB::MaxPieces_wdl << " men EGTB available - " << TBnum_piece + TBnum_pawn  << " tablebases found" << sync_endl;
			return;
		}
	}
	if (PathSpecified || SyzygyInUse)
	{
		SyzygyInUse = false;
		EGTB::MaxPieces_wdl = EGTB::MaxPieces_dtz = 0;
		EGTB::EGTB_probe_wdl = nullptr;
		EGTB::EGTB_probe_dtz = nullptr;
		sync_cout << "info string Syzygy EGTB not available" << sync_endl;
	}
}

Value Syzygy_probe_wdl(Position& pos, Value alfa, Value beta)
{
	int success;
	int v;

	v = syzygy_probe_wdl(pos, &success);

	if (success == 0)
		return NO_VALUE;

	pos.increase_tb_hits();

	int drawScore = EGTB::UseRule50 ? 1 : 0;
	if (v > drawScore)
		return Value(LONGEST_MATE_VALUE - pos.info()->ply);
	else if (v < -drawScore)
		return Value(-LONGEST_MATE_VALUE + pos.info()->ply);
	else
		return DRAW_VALUE;
}

Value Syzygy_probe_dtz(Position& pos, Value alfa, Value beta)
{
	int success;
	int v;

	v = syzygy_probe_dtz(pos, &success);

	if (success == 0)
		return NO_VALUE;

	pos.increase_tb_hits();

	int move50 = pos.fifty_moves_counter();

	if (v > 0 && (!EGTB::UseRule50 || move50 + v <= 100))  // gewonnen
		return std::max(Value(LONGEST_MATE_VALUE - (pos.info()->ply + v)), EGTB_WIN_VALUE + VALUE_1);
	else if (v < 0 && (!EGTB::UseRule50 || move50 - v <= 100))  // verloren
		return std::min(Value(-LONGEST_MATE_VALUE + (pos.info()->ply - v)), -EGTB_WIN_VALUE - VALUE_1);
	else // remise, of meer dan 50 zetten nodig om te winnen of te verliezen
		return DRAW_VALUE;
}
