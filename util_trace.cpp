/*
*/

#include "houdini.h"

#ifdef TRACE_LOG

#include <stdio.h>
#include "uci.h"
#include <windows.h>

static FILE *tracefile;
static int trace_indent, line_count;

#pragma auto_inline(off)

#define TRACE_FREQ 0x0000
#define TRACE_FULL_MIN 0x0000000
#define TRACE_FULL_MAX 0x0100000

bool trace_on()
{
	return (line_count & TRACE_FREQ) == 0 && line_count >= TRACE_FULL_MIN && line_count <= TRACE_FULL_MAX;
}

void close_tracefile()
{
	if (tracefile)
	{
		fclose(tracefile);
		tracefile = 0;
	}
}

void open_tracefile()
{
	static int randomNumber = GetTickCount() ^ int(uint64_t(GetCurrentProcess()));
	char buf[256];
	close_tracefile();
	sprintf(buf, "R:\\Schaak\\trace%d.log", randomNumber);
	tracefile = fopen(buf, "w");
	trace_indent = 0;
	line_count = 1;
}

std::string move_string(Move move)
{
	char sMove[6];

	Square from = from_square(move);
	Square to = to_square(move);

	if (move == NO_MOVE || move == NULL_MOVE)
		return "0000";

	sMove[0] = 'a' + file_of(from);
	sMove[1] = '1' + rank_of(from);
	sMove[2] = 'a' + file_of(to);
	sMove[3] = '1' + rank_of(to);
	if (move_type(move) != PROMOTION)
		return std::string(sMove, 4);

	sMove[4] = "   nbrq"[promotion_piece(move)];
	return std::string(sMove, 5);
}

void trace_do_move(int move)
{
	std::string s;

	if (tracefile)
	{
		line_count++;
		if (line_count == 360)
		{
			s = move_string(Move(move));
		}
		s = move_string(Move(move));
		if (trace_on())
			fprintf(tracefile, "\n%d %*s%s", line_count, trace_indent, "", s.c_str());
		trace_indent += 6;
	}
}

void trace_reset_indent()
{
	trace_indent = 0;
}

void trace_cancel_move()
{
	if (tracefile)
	{
		trace_indent -= 6;
	}
}

void trace_eval(int value)
{
	if (tracefile)
	{
		if (trace_on())
			fprintf(tracefile, " [%d]", value);
	}

}

void trace_msg(const char *MSG, int depth, int alpha, int beta, bool force)
{
	if (tracefile)
	{
		line_count++;
		if (line_count == 546)
		{
			alpha = alpha;
		}
		if (trace_on() || force)
			fprintf(tracefile, "\n%d %*s%s(%d,%d...%d)", line_count, trace_indent, "", MSG, depth, alpha, beta);
	}
}

void trace_tm_msg(int MoveNumber, int elapsed, int optimum, const char *MSG)
{
	if (tracefile)
	{
		line_count++;
		fprintf(tracefile, "\nMove %3d    Elapsed %6d ms    Optimum %6d ms    %s", MoveNumber, elapsed, optimum, MSG);
	}
}

#pragma auto_inline(on)

#endif
