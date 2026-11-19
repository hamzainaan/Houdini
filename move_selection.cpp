/*
*/

#include <iostream>

#include "houdini.h"
#include "move_selection.h"
#include "search_smp.h"
#include "uci.h"

namespace
{
	inline void insertion_sort(MoveEx* begin, MoveEx* end)
	{
		MoveEx tmp, *p, *q;

		for (p = begin + 1; p < end; ++p)
		{
			tmp = *p;
			for (q = p; q != begin && *(q - 1) < tmp; --q)
				*q = *(q - 1);
			*q = tmp;
		}
	}

	inline MoveEx* partition_moves(MoveEx* begin, MoveEx* end, SortValue v)
	{
		MoveEx tmp;

		while (true)
		{
			while (true)
			{

				if (begin == end)
					return begin;
				else if (begin->value > v)
					begin++;
				else
					break;
			}
			end--;
			while (true)
			{
				if (begin == end)
					return begin;
				else if (!(end->value > v))
					end--;
				else
					break;
			}
			tmp = *begin;
			*begin = *end;
			*end = tmp;
			begin++;
		}
	}

	void partial_insertion_sort(MoveEx* begin, MoveEx* end, SortValue v)
	{
		MoveEx *sortedUntil = begin + 1;
		for (MoveEx *p = begin + 1; p < end; ++p)
		{
			if (p->value >= v)
			{
				MoveEx tmp = *p, *q;
				*p = *sortedUntil;
				for (q = sortedUntil; q != begin && *(q - 1) < tmp; --q)
					*q = *(q - 1);
				*q = tmp;
				++sortedUntil;
			}
		}
	}

	void quick_sort(MoveEx* left, MoveEx* right) 
	{
		MoveEx* i = left;
		MoveEx* j = right;
		SortValue pivot = left->value;

		while (true)
		{
			while (i->value > pivot)
				i++;
			while (j->value < pivot)
				j--;
			if (i >= j)
				break;

			MoveEx tmp = *i;
			*i = *j;
			*j = tmp;

			i++;
			j--;
			if (i >= j)
				break;
		};

		if (i > left + 9)
			quick_sort(left, i - 1);
		if (right > i + 9)
			quick_sort(i + 1, right);
	}


	inline Move find_best_move(MoveEx* begin, MoveEx* end)
	{
		MoveEx* best = begin;
		for (MoveEx* z = begin + 1; z < end; z++)
			if (z->value > best->value)
				best = z;
		Move move = best->move;
		*best = *begin;
		return move;
	}

	const int PieceOrder[PIECE_N] =
	{
		0, 6, 1, 2, 3, 4, 5, 0,
		0, 6, 1, 2, 3, 4, 5, 0
	};

	const int CaptureSortValues[PIECE_N] =
	{
		0, 0, 198, 817, 836, 1270, 2521, 0,
		0, 0, 198, 817, 836, 1270, 2521, 0
	};

	unsigned short crc16(const unsigned char* data_p, unsigned char length)
	{
		unsigned char x;
		unsigned short crc = 0xFFFF;

		while (length--) {
			x = crc >> 8 ^ *data_p++;
			x ^= x >> 4;
			crc = (crc << 8) ^ ((unsigned short)(x << 12)) ^ ((unsigned short)(x << 5)) ^ ((unsigned short)x);
		}
		return crc;
	}

	int hash_bitboard(const Bitboard bb)
	{
		unsigned short crc = crc16((const unsigned char *)(&bb), 8);
		return int(crc);
	}
} // namespace


namespace MoveSelection
{
	void init_search(const Position& pos, Move ttMove, Depth depth, bool only_quiet_checks)
	{
		assert(depth >= PLY);
		PositionInfo* st = pos.info();
		st->mp_depth = depth;
		st->mp_only_quiet_checks = only_quiet_checks;
		st->mp_ttMove = ttMove && pos.valid_move(ttMove) ? ttMove : NO_MOVE;

		if (pos.check_givers())
			st->mp_stage = st->mp_ttMove ? EVADE_CHECK : EVASIONS_GEN;
		else
		{
			st->mp_stage = st->mp_ttMove ? NORMAL_SEARCH : GOOD_CAPTURES_GEN;
			if (st->counterMoveValues)
			{
				st->mp_counterMove = Move(pos.ti()->counterMoves.get(st->movedPiece, to_square(st->previousMove)));
				if (!st->mp_ttMove && (st - 1)->counterMoveValues
					&& (!st->mp_counterMove || !pos.valid_move(st->mp_counterMove) || pos.capture_or_promotion(st->mp_counterMove)))
				{
					st->mp_counterMove = pos.ti()->counterfollowupMoves.get((st - 1)->movedPiece, to_square((st - 1)->previousMove),
						st->movedPiece, to_square(st->previousMove));
				}
			}
			else
				st->mp_counterMove = NO_MOVE;
		}
	}

	void init_qsearch(const Position& pos, Move ttMove, Depth depth, Square square)
	{
		assert(depth <= DEPTH_0);
		PositionInfo* st = pos.info();

		if (pos.check_givers())
			st->mp_stage = EVADE_CHECK;

		else if (depth == DEPTH_0)
			st->mp_stage = QSEARCH_WITH_CHECKS;

		//else if (diepte == -PLY && delta < Waarde(50))
		//	st->mp_etappe = QSEARCH_MET_DAMESCHAAK;

		else if (depth >= -4 * PLY)
			st->mp_stage = QSEARCH_WITHOUT_CHECKS;

		else
		{
			st->mp_stage = RECAPTURE_GEN;
			st->mp_captureSquare = square;
			return;
		}

		st->mp_ttMove = ttMove && pos.valid_move(ttMove) ? ttMove : NO_MOVE;
		if (!st->mp_ttMove)
			++st->mp_stage;
	}

	void init_probcut(const Position& pos, Move ttMove, SeeValue bound)
	{
		assert(!pos.check_givers());
		PositionInfo* st = pos.info();
		st->mp_threshold = bound + SeeValue(1);

		st->mp_ttMove = ttMove
			&& pos.valid_move(ttMove)
			&& pos.capture_or_promotion(ttMove)
			&& pos.see_test(ttMove, st->mp_threshold) ? ttMove : NO_MOVE;

		st->mp_stage = st->mp_ttMove ? PROBCUT : PROBCUT_GEN;
	}

	template<>
	void score<GEN_CAPTURES_OR_PROMOTIONS>(const Position& pos)
	{
		PositionInfo* st = pos.info();
		for (MoveEx *z = st->mp_currentMove; z < st->mp_endList; z++)
			z->value = SortValue(CaptureSortValues[pos.piece_on_square(to_square(z->move))]
				- 200 * relative_rank(pos.side_to_move(), to_square(z->move)));
	}

	template<>
	void score<GEN_QUIET_MOVES>(const Position& pos)
	{
		const MoveValueStats& history = pos.ti()->history;

		PositionInfo* st = pos.info();
		const CounterMoveValues* cm = st->counterMoveValues ? st->counterMoveValues : &pos.ni()->counterMoveStatistics[NO_PIECE][SQ_A1];
		const CounterMoveValues* fm = (st - 1)->counterMoveValues ? (st - 1)->counterMoveValues : &pos.ni()->counterMoveStatistics[NO_PIECE][SQ_A1];
		const CounterMoveValues* f2 = (st - 3)->counterMoveValues ? (st - 3)->counterMoveValues : &pos.ni()->counterMoveStatistics[NO_PIECE][SQ_A1];

		Square threat = st->mp_depth < 6 * PLY ? pos.compute_threat() : SQ_NONE;

		for (MoveEx *z = st->mp_currentMove; z < st->mp_endList; z++)
		{
			int offset = MoveValueStats::compute_offset(pos.moved_piece(z->move), to_square(z->move));
			z->value = SortValue(history.valueAtOffset(offset))
				+ SortValue(cm->valueAtOffset(offset))
				+ SortValue(fm->valueAtOffset(offset))
				+ SortValue(f2->valueAtOffset(offset));
			z->value += SortValue(8 * pos.ti()->maxGainTable.get(pos.moved_piece(z->move), z->move));

			if (from_square(z->move) == threat)
				z->value += SortValue(9000 - 1000 * (st->mp_depth / PLY));
		}
	}

	template<>
	void score<GEN_EVASIONS>(const Position& pos)
	{
		PositionInfo* st = pos.info();
		const MoveValueStats& history = pos.ti()->evasionHistory;

		for (MoveEx *z = st->mp_currentMove; z < st->mp_endList; z++)
		{
			if (pos.is_capture(z->move))
				z->value = SortValue(CaptureSortValues[pos.piece_on_square(to_square(z->move))]
					- PieceOrder[pos.moved_piece(z->move)]) + SORT_MAX;
			//else if (z->zet == st->mp_counterZet)
			//	z->waarde = SorteerWaarde(99999);
			else
			{
				int offset = MoveValueStats::compute_offset(pos.moved_piece(z->move), to_square(z->move));
				z->value = SortValue(history.valueAtOffset(offset));
			}
		}
	}


	Move next_move(const Position& pos)
	{
		PositionInfo* st = pos.info();

		switch (st->mp_stage)
		{
		case NORMAL_SEARCH: case EVADE_CHECK:
		case QSEARCH_WITH_CHECKS: case QSEARCH_WITHOUT_CHECKS: 
		//case QSEARCH_MET_DAMESCHAAK:
		case PROBCUT:
			st->mp_endList = (st - 1)->mp_endList;
			++st->mp_stage;
			return st->mp_ttMove;

		case GOOD_CAPTURES_GEN:
			st->mp_currentMove = (st - 1)->mp_endList;
			st->mp_endBadCapture = st->mp_currentMove;
			st->mp_postponed_count = 0;
			st->mp_endList = generateMoves<GEN_CAPTURES_OR_PROMOTIONS>(pos, st->mp_currentMove);
			score<GEN_CAPTURES_OR_PROMOTIONS>(pos);
			st->mp_stage = GOOD_CAPTURES;

		case GOOD_CAPTURES:
			while (st->mp_currentMove < st->mp_endList)
			{
				Move move = find_best_move(st->mp_currentMove++, st->mp_endList);
				if (move != st->mp_ttMove)
				{
					if (pos.see_test(move, SEE_0))
						return move;

					*st->mp_endBadCapture++ = move;
				}
			}

			// first killer
			st->mp_stage = KILLERS;
			{
				Move move = st->killers[0];
				if (move && move != st->mp_ttMove && pos.valid_move(move) && !pos.capture_or_promotion(move))
					return move;
			}

		case KILLERS:
			// second killer
			st->mp_stage = KILLERS1;
			{
				Move move = st->killers[1];
				if (move && move != st->mp_ttMove && pos.valid_move(move) && !pos.capture_or_promotion(move))
					return move;
			}

		case KILLERS1:
			// counter move
			st->mp_stage = BxN_CAPTURES_GEN;
			{
				Move move = st->mp_counterMove;
				if (move && move != st->mp_ttMove && move != st->killers[0]
					&& move != st->killers[1]
					&& pos.valid_move(move) && !pos.capture_or_promotion(move))
					return move;
			}

		case BxN_CAPTURES_GEN:
			st->mp_currentMove = (st - 1)->mp_endList;
			st->mp_stage = BxN_CAPTURES;

		case BxN_CAPTURES:
			while (st->mp_currentMove < st->mp_endBadCapture)
			{
				Move move = *st->mp_currentMove++;
				if (piece_type(pos.piece_on_square(to_square(move))) == KNIGHT && piece_type(pos.piece_on_square(from_square(move))) == BISHOP)
				{
					*(st->mp_currentMove - 1) = NO_MOVE;
					return move;
				}
			}

		//case RUSTIGE_ZETTEN_GEN:
			st->mp_currentMove = st->mp_endBadCapture;
			if (st->mp_only_quiet_checks && st->moveNumber >= 1)
			{
				MoveEx* z = st->mp_currentMove;
				z = generateMoves<GEN_QUIET_CHECKS>(pos, z);
				z = generateMoves<GEN_PAWN_ADVANCES>(pos, z);
				st->mp_endList = z;
				score<GEN_QUIET_MOVES>(pos);
				insertion_sort(st->mp_currentMove, st->mp_endList);
			}
			else
			{
				st->mp_endList = generateMoves<GEN_QUIET_MOVES>(pos, st->mp_currentMove);
				score<GEN_QUIET_MOVES>(pos);

				MoveEx* sort_until = st->mp_endList;
				// op lage diepte: sorteer enkel maar de positieve zetten; dus eerst een partitie maken
				if (st->mp_depth < 6 * PLY)
					sort_until = partition_moves(st->mp_currentMove, st->mp_endList, SortValue(6000 - 6000 * (st->mp_depth / PLY)));
				insertion_sort(st->mp_currentMove, sort_until);
				//if (st->mp_diepte < 8 * PLY)
				//	partial_insertion_sort(st->mp_huidigeZet, st->mp_eindeLijst, SorteerWaarde(2000 - 3000 * (st->mp_diepte / PLY)));
				//else
				//	insertion_sort(st->mp_huidigeZet, sorteer_tot);
				//partial_insertion_sort(st->mp_huidigeZet, st->mp_eindeLijst, SorteerWaarde(4000 - 4000 * (st->mp_diepte / PLY)));
			}
			st->mp_stage = QUIET_MOVES;

		case QUIET_MOVES:
			while (st->mp_currentMove < st->mp_endList)
			{
				Move move = *st->mp_currentMove++;
				if (move != st->mp_ttMove
					&& move != st->killers[0]
					&& move != st->killers[1]
					&& move != st->mp_counterMove)
				{
					return move;
				}
			}

			st->mp_currentMove = (st - 1)->mp_endList;
			st->mp_endList = st->mp_endBadCapture;
			st->mp_stage = BAD_CAPTURES;

		case BAD_CAPTURES:
			while (st->mp_currentMove < st->mp_endList)
			{
				Move move = *st->mp_currentMove++;
				if (move)
					return move;
			}
			if (!st->mp_postponed_count)
				return NO_MOVE;

			st->mp_stage = POSTPONED_MOVES;
			st->mp_postponed_current = 0;

		case POSTPONED_MOVES:
			if (st->mp_postponed_current != st->mp_postponed_count)
				return Move(st->mp_postponed[st->mp_postponed_current++]);
			return NO_MOVE;

		case EVASIONS_GEN:
			st->mp_currentMove = (st - 1)->mp_endList;
			st->mp_endList = generateMoves<GEN_EVASIONS>(pos, st->mp_currentMove);
			score<GEN_EVASIONS>(pos);
			st->mp_stage = EVASIONS_LOOP;

		case EVASIONS_LOOP:
			while (st->mp_currentMove < st->mp_endList)
			{
				Move move = find_best_move(st->mp_currentMove++, st->mp_endList);
				if (move != st->mp_ttMove)
					return move;
			}
			return NO_MOVE;

		case QSEARCH_1: case QSEARCH_2: 
		//case QSEARCH_3:
			st->mp_currentMove = (st - 1)->mp_endList;
			st->mp_endList = generateMoves<GEN_CAPTURES_OR_PROMOTIONS>(pos, st->mp_currentMove);
			score<GEN_CAPTURES_OR_PROMOTIONS>(pos);
			++st->mp_stage;

		case QSEARCH_1_CAPTURES: case QSEARCH_2_CAPTURES: 
		//case QSEARCH_3_SLAGZETTEN:
			while (st->mp_currentMove < st->mp_endList)
			{
				Move move = find_best_move(st->mp_currentMove++, st->mp_endList);
				if (move != st->mp_ttMove)
					return move;
			}

			if (st->mp_stage == QSEARCH_2_CAPTURES)
				return NO_MOVE;

			st->mp_currentMove = (st - 1)->mp_endList;
			//if (st->mp_etappe == QSEARCH_3_SLAGZETTEN)
			//	st->mp_eindeLijst = genereerZetten<GEN_DAME_SCHAAK>(pos, st->mp_huidigeZet);
			//else
				st->mp_endList = generateMoves<GEN_QUIET_CHECKS>(pos, st->mp_currentMove);
			//score<GEN_RUSTIGE_ZETTEN>(pos);
			++st->mp_stage;

		case QSEARCH_CHECKS: 
		//case QSEARCH_DAMESCHAAKZETTEN:
			while (st->mp_currentMove < st->mp_endList)
			{
				Move move = *st->mp_currentMove++;
				//Zet zet = vind_beste_zet(st->mp_huidigeZet++, st->mp_eindeLijst);
				if (move != st->mp_ttMove)
					return move;
			}
			return NO_MOVE;

		case PROBCUT_GEN:
			st->mp_currentMove = (st - 1)->mp_endList;
			st->mp_endList = generateMoves<GEN_CAPTURES_OR_PROMOTIONS>(pos, st->mp_currentMove);
			score<GEN_CAPTURES_OR_PROMOTIONS>(pos);
			st->mp_stage = PROBCUT_CAPTURES;

		case PROBCUT_CAPTURES:
			while (st->mp_currentMove < st->mp_endList)
			{
				Move move = find_best_move(st->mp_currentMove++, st->mp_endList);
				if (move != st->mp_ttMove && pos.see_test(move, st->mp_threshold))
					return move;
			}
			return NO_MOVE;

		case RECAPTURE_GEN:
			st->mp_currentMove = (st - 1)->mp_endList;
			st->mp_endList = generateCapturesOnSquare(pos, st->mp_currentMove, st->mp_captureSquare);
			score<GEN_CAPTURES_OR_PROMOTIONS>(pos);
			st->mp_stage = RECAPTURE_MOVES;

		case RECAPTURE_MOVES:
			while (st->mp_currentMove < st->mp_endList)
			{
				Move move = find_best_move(st->mp_currentMove++, st->mp_endList);
				if (to_square(move) == st->mp_captureSquare)
					return move;
			}
			return NO_MOVE;

		default:
			assert(false);
			return NO_MOVE;
		}
	}
}


int SpecialKillerStats::index_our_pieces(const Position &pos, Color color)
{
	return hash_bitboard(pos.pieces(color));
}

int SpecialKillerStats::index_their_pieces(const Position &pos, Color color, Square to)
{
	return hash_bitboard(pos.pieces(color, piece_type(pos.piece_on_square(to))));
}

