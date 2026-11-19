/*
*/

#include "houdini.h"
#include "move_generation.h"
#include "position.h"
#include "util_tools.h"

namespace 
{
	template<CastlingRight castling, bool OnlyCheckMoves, bool Chess960>
	MoveEx* write_castling(const Position& pos, MoveEx* moves)
	{
		const Color Us = castling <= WHITE_LONG ? WHITE : BLACK;
		const Color Them = (Us == WHITE ? BLACK : WHITE);
		const bool short_castling = (castling == WHITE_SHORT || castling == BLACK_SHORT);

		if (pos.castling_impeded(castling) || !pos.castling_possible(castling))
			return moves;

		const Square fromK = Chess960 ? pos.king(Us) : relative_square(Us, SQ_E1);
		const Square toK = relative_square(Us, short_castling ? SQ_G1 : SQ_C1);
		const Square direction = toK > fromK ? LEFT : RIGHT;

		assert(!pos.check_givers());

		if (Chess960)
		{
			for (Square square = toK; square != fromK; square += direction)
				if (pos.attackers_to(square) & pos.pieces(Them))
					return moves;

			Square fromR = pos.castling_rook_square(toK);
			if (attack_bb_rook(toK, pos.pieces() ^ fromR) & pos.pieces(Them, ROOK, QUEEN))
				return moves;
		}
		else
		{
			if (pos.attackers_to(toK) & pos.pieces(Them))
				return moves;
			if (pos.attackers_to(toK + direction) & pos.pieces(Them))
				return moves;
		}

		Move move = make_move(CASTLING, fromK, toK);

		if (OnlyCheckMoves && !pos.gives_check(move))
			return moves;

		*moves++ = move;
		return moves;
	}


	template<Color Us, MoveGeneration Type, Square Delta>
	MoveEx* write_promotions(const Position& pos, MoveEx* moves, Square to)
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);

		if (Type == GEN_CAPTURES_OR_PROMOTIONS || Type == GEN_EVASIONS || Type == GEN_ALL_MOVES)
			*moves++ = make_move(PROMOTION_Q, to - Delta, to);

		if (Type == GEN_QUIET_MOVES || Type == GEN_EVASIONS || Type == GEN_ALL_MOVES)
		{
			*moves++ = make_move(PROMOTION_R, to - Delta, to);
			*moves++ = make_move(PROMOTION_B, to - Delta, to);
			*moves++ = make_move(PROMOTION_N, to - Delta, to);
		}

		if (Type == GEN_QUIET_CHECKS && (EmptyAttack[KNIGHT][to] & pos.king(Them)))
			*moves++ = make_move(PROMOTION_N, to - Delta, to);

		return moves;
	}


	template<Color Us, MoveGeneration Type>
	MoveEx* moves_for_pawn(const Position& pos, MoveEx* moves, Bitboard target)
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);
		const Bitboard EighthRank = (Us == WHITE ? Rank8BB : Rank1BB);
		const Bitboard SeventhRank = (Us == WHITE ? Rank7BB : Rank2BB);
		const Bitboard ThirdRank = (Us == WHITE ? Rank3BB : Rank6BB);
		const Square Forward = (Us == WHITE ? UP : DOWN);
		const Square CaptureRight = (Us == WHITE ? UP_RIGHT : DOWN_LEFT);
		const Square CaptureLeft = (Us == WHITE ? UP_LEFT : DOWN_RIGHT);

		Bitboard emptySquares;
		Bitboard pawns7thRank = pos.pieces(Us, PAWN) &  SeventhRank;
		Bitboard pawnsNot7thRank = pos.pieces(Us, PAWN) & ~SeventhRank;

		Bitboard theirPieces = (Type == GEN_EVASIONS ? pos.pieces(Them) & target :
			Type == GEN_CAPTURES_OR_PROMOTIONS ? target : pos.pieces(Them));

		if (Type != GEN_CAPTURES_OR_PROMOTIONS)
		{
			emptySquares = (Type == GEN_QUIET_MOVES || Type == GEN_QUIET_CHECKS ? target : ~pos.pieces());

			Bitboard bbSinglePush = shift_up<Us>(pawnsNot7thRank) & emptySquares;
			Bitboard bbDoublePush = shift_up<Us>(bbSinglePush & ThirdRank) & emptySquares;

			if (Type == GEN_EVASIONS)
			{
				bbSinglePush &= target;
				bbDoublePush &= target;
			}

			if (Type == GEN_QUIET_CHECKS)
			{
				bbSinglePush &= pos.attack_from<PAWN>(pos.king(Them), Them);
				bbDoublePush &= pos.attack_from<PAWN>(pos.king(Them), Them);

				Bitboard discoveredCheck = pos.info()->xRay[~pos.side_to_move()];
				if (pawnsNot7thRank & discoveredCheck)
				{
					Bitboard discoveredForward = shift_up<Us>(pawnsNot7thRank & discoveredCheck) & emptySquares & ~bb_file(pos.king(Them));
					Bitboard discoveredDouble = shift_up<Us>(discoveredForward & ThirdRank) & emptySquares;

					bbSinglePush |= discoveredForward;
					bbDoublePush |= discoveredDouble;
				}
			}

			while (bbSinglePush)
			{
				Square to = pop_lsb(&bbSinglePush);
				*moves++ = make_move(PAWN, to - Forward, to);
			}

			while (bbDoublePush)
			{
				Square to = pop_lsb(&bbDoublePush);
				*moves++ = make_move(PAWN, to - Forward - Forward, to);
			}
		}

		if (pawns7thRank && (Type != GEN_EVASIONS || (target & EighthRank)))
		{
			if (Type == GEN_CAPTURES_OR_PROMOTIONS)
				emptySquares = ~pos.pieces();

			if (Type == GEN_EVASIONS)
				emptySquares &= target;

			Bitboard promotionRight = shift_bb<CaptureRight>(pawns7thRank) & theirPieces;
			Bitboard promotionLeft = shift_bb<CaptureLeft>(pawns7thRank) & theirPieces;
			Bitboard promotionForward = shift_up<Us>(pawns7thRank) & emptySquares;

			while (promotionRight)
				moves = write_promotions<Us, Type, CaptureRight>(pos, moves, pop_lsb(&promotionRight));

			while (promotionLeft)
				moves = write_promotions<Us, Type, CaptureLeft>(pos, moves, pop_lsb(&promotionLeft));

			while (promotionForward)
				moves = write_promotions<Us, Type, Forward>(pos, moves, pop_lsb(&promotionForward));
		}

		if (Type == GEN_CAPTURES_OR_PROMOTIONS || Type == GEN_EVASIONS || Type == GEN_ALL_MOVES)
		{
			Bitboard captureRight = shift_bb<CaptureRight>(pawnsNot7thRank) & theirPieces;
			Bitboard captureLeft = shift_bb<CaptureLeft>(pawnsNot7thRank) & theirPieces;

			while (captureRight)
			{
				Square to = pop_lsb(&captureRight);
				*moves++ = make_move(PAWN, to - CaptureRight, to);
			}

			while (captureLeft)
			{
				Square to = pop_lsb(&captureLeft);
				*moves++ = make_move(PAWN, to - CaptureLeft, to);
			}

			if (pos.enpassant_square() != SQ_NONE)
			{
				assert(rank_of(pos.enpassant_square()) == relative_rank(Us, RANK_6));

				if (Type == GEN_EVASIONS && !(target & (pos.enpassant_square() - Forward)))
					return moves;

				captureRight = pawnsNot7thRank & pos.attack_from<PAWN>(pos.enpassant_square(), Them);

				assert(captureRight);

				while (captureRight)
					*moves++ = make_move(ENPASSANT, pop_lsb(&captureRight), pos.enpassant_square());
			}
		}

		return moves;
	}


	template<Color Us, PieceType piece, bool OnlyCheckMoves>
	MoveEx* moves_for_piece(const Position& pos, MoveEx* moves, Bitboard target)
	{
		assert(piece != KING && piece != PAWN);

		const Square* pl = pos.piece_list(Us, piece);

		for (Square from = *pl; from != SQ_NONE; from = *++pl)
		{
			if (OnlyCheckMoves)
			{
				if ((piece == BISHOP || piece == ROOK || piece == QUEEN)
					&& !(EmptyAttack[piece][from] & target & pos.info()->checkSquares[piece]))
					continue;

				// aftrekschaaks worden in genereerZetten<GEN_RUSTIGE_SCHAAKS> geschreven
				if (pos.info()->xRay[~pos.side_to_move()] & from)
					continue;
			}

			Bitboard squares = pos.attack_from<piece>(from) & target;

			if (OnlyCheckMoves)
				squares &= pos.info()->checkSquares[piece];

			while (squares)
				*moves++ = make_move(piece, from, pop_lsb(&squares));
		}

		return moves;
	}


	template<Color Us, MoveGeneration Type>
	MoveEx* moves_for_all_pieces(const Position& pos, MoveEx* moves, Bitboard target)
	{
		const bool OnlyCheckMoves = Type == GEN_QUIET_CHECKS;

		if (Type != GEN_CASTLING)
		{
			moves = moves_for_pawn<Us, Type>(pos, moves, target);
			moves = moves_for_piece<Us, KNIGHT, OnlyCheckMoves>(pos, moves, target);
			moves = moves_for_piece<Us, BISHOP, OnlyCheckMoves>(pos, moves, target);
			moves = moves_for_piece<Us, ROOK, OnlyCheckMoves>(pos, moves, target);
			moves = moves_for_piece<Us, QUEEN, OnlyCheckMoves>(pos, moves, target);

			if (Type != GEN_QUIET_CHECKS && Type != GEN_EVASIONS)
			{
				Square squareK = pos.king(Us);
				Bitboard squares = pos.attack_from<KING>(squareK) & target;
				while (squares)
					*moves++ = make_move(KING, squareK, pop_lsb(&squares));
			}
		}

		if (Type != GEN_CAPTURES_OR_PROMOTIONS && Type != GEN_EVASIONS && pos.castling_rights(Us))
		{
			if (pos.is_chess960())
			{
				moves = write_castling<Us == WHITE ? WHITE_SHORT : BLACK_SHORT, OnlyCheckMoves, true>(pos, moves);
				moves = write_castling<Us == WHITE ? WHITE_LONG : BLACK_LONG, OnlyCheckMoves, true>(pos, moves);
			}
			else
			{
				moves = write_castling<Us == WHITE ? WHITE_SHORT : BLACK_SHORT, OnlyCheckMoves, false>(pos, moves);
				moves = write_castling<Us == WHITE ? WHITE_LONG : BLACK_LONG, OnlyCheckMoves, false>(pos, moves);
			}
		}

		return moves;
	}


	template<Color Us>
	MoveEx* generate_pawn_advances(const Position& pos, MoveEx* moves)
	{
		const Bitboard RANK67 = Us == WHITE ? Rank6BB | Rank7BB : Rank3BB | Rank2BB;

		Bitboard squares = shift_up<Us>(pos.pieces(Us, PAWN)) & RANK67 & ~pos.pieces();
		while (squares)
		{
			Square to = pop_lsb(&squares);
			*moves++ = make_move(PAWN, to - pawn_forward(Us), to);
		}

		return moves;
	}

	
} // namespace


template<MoveGeneration Type>
MoveEx* generateMoves(const Position& pos, MoveEx* moves) 
{
	assert(Type == GEN_CAPTURES_OR_PROMOTIONS || Type == GEN_QUIET_MOVES || Type == GEN_ALL_MOVES || Type == GEN_CASTLING);
	assert(!pos.check_givers());

	Color us = pos.side_to_move();

	Bitboard target = Type == GEN_CAPTURES_OR_PROMOTIONS ? pos.pieces(~us)
		: Type == GEN_QUIET_MOVES ? ~pos.pieces()
		: Type == GEN_ALL_MOVES ? ~pos.pieces(us) : 0;

	return us == WHITE ? moves_for_all_pieces<WHITE, Type>(pos, moves, target)
		: moves_for_all_pieces<BLACK, Type>(pos, moves, target);
}

template MoveEx* generateMoves<GEN_CAPTURES_OR_PROMOTIONS>(const Position&, MoveEx*);
template MoveEx* generateMoves<GEN_QUIET_MOVES>(const Position&, MoveEx*);
template MoveEx* generateMoves<GEN_ALL_MOVES>(const Position&, MoveEx*);


template<>
MoveEx* generateMoves<GEN_QUIET_CHECKS>(const Position& pos, MoveEx* moves) 
{
	assert(!pos.check_givers());

	Color us = pos.side_to_move();
	Bitboard discoveredCheck = pos.discovered_check_possible();

	while (discoveredCheck)
	{
		Square from = pop_lsb(&discoveredCheck);
		PieceType piece = piece_type(pos.piece_on_square(from));

		if (piece == PAWN)
			continue;

		Bitboard squares = pos.attack_from(piece, from) & ~pos.pieces();

		if (piece == KING)
			squares &= ~EmptyAttack[QUEEN][pos.king(~us)];

		while (squares)
			*moves++ = make_move(piece, from, pop_lsb(&squares));
	}

	return us == WHITE ? moves_for_all_pieces<WHITE, GEN_QUIET_CHECKS>(pos, moves, ~pos.pieces())
		: moves_for_all_pieces<BLACK, GEN_QUIET_CHECKS>(pos, moves, ~pos.pieces());
}


template<>
MoveEx* generateMoves<GEN_PAWN_ADVANCES>(const Position& pos, MoveEx* moves)
{
	assert(!pos.check_givers());

	Color us = pos.side_to_move();
	return us == WHITE ? generate_pawn_advances<WHITE>(pos, moves)
		: generate_pawn_advances<BLACK>(pos, moves);
}


template<>
MoveEx* generateMoves<GEN_QUEEN_CHECKS>(const Position& pos, MoveEx* moves)
{
	return pos.side_to_move() == WHITE 
		? moves_for_piece<WHITE, QUEEN, true>(pos, moves, ~pos.pieces())
		: moves_for_piece<BLACK, QUEEN, true>(pos, moves, ~pos.pieces());
}

template<>
MoveEx* generateMoves<GEN_EVASIONS>(const Position& pos, MoveEx* moves) 
{
	assert(pos.check_givers());

	Color us = pos.side_to_move();
	Square squareK = pos.king(us);
	Bitboard attackedSquares = 0;
	Bitboard distantAttackers = pos.check_givers() & ~pos.pieces(KNIGHT, PAWN);

	while (distantAttackers)
	{
		Square checksquare = pop_lsb(&distantAttackers);
		attackedSquares |= bbConnection[checksquare][squareK] ^ checksquare;
	}

	Bitboard squares = pos.attack_from<KING>(squareK) & ~pos.pieces(us) & ~attackedSquares;
	while (squares)
		*moves++ = make_move(KING, squareK, pop_lsb(&squares));

	if (more_than_one(pos.check_givers()))
		return moves;

	Square checkSquare = lsb(pos.check_givers());
	Bitboard target = bb_between(checkSquare, squareK) | checkSquare;

	return us == WHITE ? moves_for_all_pieces<WHITE, GEN_EVASIONS>(pos, moves, target)
		: moves_for_all_pieces<BLACK, GEN_EVASIONS>(pos, moves, target);
}


MoveEx* generateLegalMoves(const Position& pos, MoveEx* moves) 
{
	Bitboard pinned = pos.pinned_pieces();
	Square squareK = pos.king(pos.side_to_move());
	MoveEx* pMove = moves;

	moves = pos.check_givers() ? generateMoves<GEN_EVASIONS>(pos, moves)
		: generateMoves<GEN_ALL_MOVES>(pos, moves);
	while (pMove != moves)
		if ((pinned || from_square(*pMove) == squareK || move_type(*pMove) == ENPASSANT)
			&& !pos.legal_move(*pMove))
			*pMove = (--moves)->move;
		else
			++pMove;

	return moves;
}


MoveEx* generateCapturesOnSquare(const Position& pos, MoveEx* moves, Square square) 
{
	Bitboard target = bbSquare[square];

	return pos.side_to_move() == WHITE
		? moves_for_all_pieces<WHITE, GEN_CAPTURES_OR_PROMOTIONS>(pos, moves, target)
		: moves_for_all_pieces<BLACK, GEN_CAPTURES_OR_PROMOTIONS>(pos, moves, target);
}


bool LegalMoveListContainsMove(const Position & pos, Move move)
{
	MoveEx moves[MAX_MOVES];
	MoveEx* last = pos.check_givers() ? generateMoves<GEN_EVASIONS>(pos, moves)
		: generateMoves<GEN_ALL_MOVES>(pos, moves);

	MoveEx* pMove = moves;
	while (pMove != last)
	{
		if (pMove->move == move)
			return pos.legal_move(move);
		pMove++;
	}
	return false;
}


bool AtLeastOneLegalMove(const Position & pos)
{
	MoveEx moves[MAX_MOVES];
	MoveEx* last = pos.check_givers() ? generateMoves<GEN_EVASIONS>(pos, moves)
		: generateMoves<GEN_ALL_MOVES>(pos, moves);

	MoveEx* pMove = moves;
	while (pMove != last)
	{
		if (pos.legal_move(pMove->move))
			return true;
		pMove++;
	}
	return false;
}


bool LegalMoveListContainsCastling(const Position & pos, Move move)
{
	MoveEx moves[MAX_MOVES];
	if (pos.check_givers())
		return false;

	MoveEx* last = generateMoves<GEN_CASTLING>(pos, moves);

	MoveEx* pMove = moves;
	while (pMove != last)
	{
		if (pMove->move == move)
			return true;
		pMove++;
	}
	return false;
}
