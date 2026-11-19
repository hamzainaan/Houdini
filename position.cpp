/*
*/

#include <iomanip>
#include <sstream>
#include <iostream>

#include "houdini.h"
#include "bitboard.h"
#include "position.h"
#include "util_tools.h"
#include "search_smp.h"
#include "move_generation.h"
#include "move_hash.h"
#include "uci.h"

using std::string;

namespace Zobrist
{
	Key64 psq[PIECE_N][SQUARE_N];
	Key64 enpassant[FILE_N];
	Key64 castling[CASTLING_RIGHT_N];
	Key64 sideToMove;
	Key64 hash_50move[32];
}

Key64 Position::exclusion_key(Move move) const
{ 
	return Zobrist::psq[W_KING][from_square(move)] ^ Zobrist::psq[B_KING][to_square(move)];
	//return move;
}

Key64 Position::draw50_key() const
{
	return Zobrist::hash_50move[st->draw50Moves >> 2];
}


namespace
{
	const string PieceToChar(" KPNBRQ  kpnbrq");
} // namespace


void Position::compute_check_pins() const
{
	compute_pins<WHITE>();
	compute_pins<BLACK>();
	Square squareK = king(~sideToMove);
	st->checkSquares[PAWN]  = attack_from<PAWN>(squareK, ~sideToMove);
	st->checkSquares[KNIGHT] = attack_from<KNIGHT>(squareK);
	st->checkSquares[BISHOP] = attack_from<BISHOP>(squareK);
	st->checkSquares[ROOK] = attack_from<ROOK>(squareK);
	st->checkSquares[QUEEN]  = st->checkSquares[BISHOP] | st->checkSquares[ROOK];
	st->checkSquares[KING] = 0;
}

template <Color color>
void Position::compute_pins() const
{
	Bitboard pinners, result = 0;
	Square squareK = king(color);

	pinners = (EmptyAttack[ROOK][squareK] & pieces(~color, QUEEN, ROOK))
		          | (EmptyAttack[BISHOP][squareK] & pieces(~color, QUEEN, BISHOP));

	while (pinners)
	{
		Square square = pop_lsb(&pinners);
		Bitboard b = bb_between(squareK, square) & pieces();

		if (b && !more_than_one(b))
		{
			result |= b;
			st->pinned_by[lsb(b)] = square;
		}
	}
	st->xRay[color] = result;
}


std::ostream& operator<<(std::ostream& os, const Position& pos)
{
	os << "\n +---+---+---+---+---+---+---+---+\n";

	for (Rank r = RANK_8; r >= RANK_1; --r)
	{
		for (File f = FILE_A; f <= FILE_H; ++f)
			os << " | " << PieceToChar[pos.piece_on_square(make_square(f, r))];

		os << " |\n +---+---+---+---+---+---+---+---+\n";
	}

	os << "\nFen: " << pos.fen() << "\n";

	return os;
}


void Position::initialize()
{
	PRNG rng(19680708);

	for (Color color = WHITE; color <= BLACK; ++color)
		for (PieceType piece = KING; piece <= QUEEN; ++piece)
			for (Square square = SQ_A1; square <= SQ_H8; ++square)
				Zobrist::psq[make_piece(color, piece)][square] = rng.rand<Key64>();

	for (File f = FILE_A; f <= FILE_H; ++f)
		Zobrist::enpassant[f] = rng.rand<Key64>();

	for (int castling = NO_CASTLING; castling <= ALL_CASTLING; ++castling)
	{
		Zobrist::castling[castling] = 0;
		Bitboard b = castling;
		while (b)
		{
			Key64 k = Zobrist::castling[1ULL << pop_lsb(&b)];
			Zobrist::castling[castling] ^= k ? k : rng.rand<Key64>();
		}
	}

	Zobrist::sideToMove = rng.rand<Key64>();
	init_hash_move50(50);
}


void Position::init_hash_move50(int FiftyMoveDistance)
{
	for (int i = 0; i < 32; i++)
		if (4 * i + 50 < 2 * FiftyMoveDistance)
			Zobrist::hash_50move[i] = 0;
		else
			Zobrist::hash_50move[i] = 0x0001000100010001 << i;
}


Position& Position::set(const string& fenStr, bool isChess960, Thread* th)
{
	assert(th != nullptr);

	unsigned char col, row, token;
	size_t idx;
	Square sq = SQ_A8;
	std::istringstream ss(fenStr);

	std::memset(this, 0, sizeof(Position));
	std::fill_n(&pieceList[0][0], sizeof(pieceList) / sizeof(Square), SQ_NONE);
	st = th->ti->positionInfo + 5;
	std::memset(st, 0, sizeof(PositionInfo));
	chess960 = isChess960;

	ss >> std::noskipws;

	// pieces
	while ((ss >> token) && !isspace(token))
	{
		if (isdigit(token))
			sq += Square(token - '0'); // Advance the given number of files

		else if (token == '/')
			sq -= Square(16);

		else if ((idx = PieceToChar.find(token)) != string::npos)
		{
			put_piece(piece_color(Piece(idx)), Piece(idx), sq);
			++sq;
		}
	}
	pieceBB[ALL_PIECES] = colorBB[WHITE] | colorBB[BLACK];

	// color
	ss >> token;
	sideToMove = (token == 'w' ? WHITE : BLACK);
	ss >> token;

	// castling
	while ((ss >> token) && !isspace(token))
	{
		Square rsq;
		Color color = islower(token) ? BLACK : WHITE;
		Piece rook = make_piece(color, ROOK);

		token = char(toupper(token));

		if (token == 'K')
			for (rsq = relative_square(color, SQ_H1); piece_on_square(rsq) != rook; --rsq) {}

		else if (token == 'Q')
			for (rsq = relative_square(color, SQ_A1); piece_on_square(rsq) != rook; ++rsq) {}

		else if (token >= 'A' && token <= 'H')
			rsq = make_square(File(token - 'A'), relative_rank(color, RANK_1));

		else
			continue;

		set_castling_right(color, rsq);
	}

	// en-passant
	if (((ss >> col) && (col >= 'a' && col <= 'h'))
		&& ((ss >> row) && (row == '3' || row == '6')))
	{
		st->enpassantSquare = make_square(File(col - 'a'), Rank(row - '1'));

		if (!(attackers_to(st->enpassantSquare) & pieces(sideToMove, PAWN)))
			st->enpassantSquare = SQ_NONE;
	}
	else
		st->enpassantSquare = SQ_NONE;

	// move count
	ss >> std::skipws >> st->draw50Moves >> gamePly;

	gamePly = std::max(2 * (gamePly - 1), 0) + (sideToMove == BLACK);

	myThread = th;
	threadInfo = th->ti;
	numaInfo = th->ni;
	set_position_info(st);
	compute_check_pins();

	assert(position_is_ok());

	return *this;
}


void Position::copy_position(const Position* pos, Thread* th, PositionInfo* copyState)
{
	std::memcpy(this, pos, sizeof(Position));
	if (th)
	{
		myThread = th;
		threadInfo = th->ti;
		numaInfo = th->ni;
		st = th->ti->positionInfo + 5;

		PositionInfo* origSt = pos->threadInfo->positionInfo + 5;
		while (origSt < copyState - 4)
		{
			st->key = origSt->key;
			st++;
			origSt++;
		}
		// copy last 5 PositionInfo records
		while (origSt <= copyState)
		{
			*st = *origSt;
			st++;
			origSt++;
		}
		st--;
	}
	assert(position_is_ok());
}


void Position::set_castling_right(Color color, Square fromR)
{
	Square fromK = king(color);
	CastlingRight castling = CastlingRight(WHITE_SHORT << ((fromK >= fromR) + 2 * color));

	Square toK = relative_square(color, fromK < fromR ? SQ_G1 : SQ_C1);
	Square toR = relative_square(color, fromK < fromR ? SQ_F1 : SQ_D1);

	st->castlingRights |= castling;
	castlingMask[fromK] |= castling;
	castlingMask[fromR] |= castling;
	castlingRookSquare[toK] = fromR;

	Bitboard path = 0;
	for (Square square = std::min(fromR, toR); square <= std::max(fromR, toR); ++square)
		path |= square;

	for (Square square = std::min(fromK, toK); square <= std::max(fromK, toK); ++square)
		path |= square;

	castlingPath[castling] = path & ~(bbSquare[fromK] | bbSquare[fromR]);

	if (fromK != relative_square(color, SQ_E1))
		chess960 = true;
	if (fromK < fromR && fromR != relative_square(color, SQ_H1))
		chess960 = true;
	if (fromK >= fromR && fromR != relative_square(color, SQ_A1))
		chess960 = true;
}


void Position::compute_bishop_color_key() const
{
	Key64 key = 0;
	if (pieces(WHITE, BISHOP) & DarkSquares)
		key ^= 0xF3094B57AC4789A2;
	if (pieces(WHITE, BISHOP) & ~DarkSquares)
		key ^= 0x89A2F3094B57AC47;
	if (pieces(BLACK, BISHOP) & DarkSquares)
		key ^= 0xAC4789A2F3094B57;
	if (pieces(BLACK, BISHOP) & ~DarkSquares)
		key ^= 0x4B57AC4789A2F309;
	st->bishopColorKey = key;
}


void Position::set_position_info(PositionInfo* si) const
{
	si->key = si->materialKey = 0;
	si->nonPawnMaterial[WHITE] = si->nonPawnMaterial[BLACK] = MATL_0;
	si->psq = SCORE_ZERO;
	si->phase = 0;

	si->checkGivers = attackers_to(king(sideToMove)) & pieces(~sideToMove);

	for (Bitboard b = pieces(); b; )
	{
		Square square = pop_lsb(&b);
		Piece piece = piece_on_square(square);
		si->key ^= Zobrist::psq[piece][square];
		si->psq += PST::psq[piece][square];
		si->phase += PiecePhase[piece_type(piece)];
	}

	if (si->enpassantSquare != SQ_NONE)
		si->key ^= Zobrist::enpassant[file_of(si->enpassantSquare)];

	if (sideToMove == BLACK)
		si->key ^= Zobrist::sideToMove;

	si->key ^= Zobrist::castling[si->castlingRights];

	si->pawnKey = 0x1234567890ABCDEF;
	for (Bitboard b = pieces(PAWN); b; )
	{
		Square square = pop_lsb(&b);
		si->pawnKey ^= Zobrist::psq[piece_on_square(square)][square];
	}

	for (Color color = WHITE; color <= BLACK; ++color)
		for (PieceType piece = KING; piece <= QUEEN; ++piece)
			for (int cnt = 0; cnt < pieceCount[make_piece(color, piece)]; ++cnt)
				si->materialKey ^= Zobrist::psq[make_piece(color, piece)][cnt];

	compute_bishop_color_key();

	for (Color color = WHITE; color <= BLACK; ++color)
		for (PieceType piece = KNIGHT; piece <= QUEEN; ++piece)
			si->nonPawnMaterial[color] += MaterialValues[piece] * (int)pieceCount[make_piece(color, piece)];
}


const string Position::fen() const
{
	int emptyCnt;
	std::ostringstream ss;

	for (Rank r = RANK_8; ; --r)
	{
		for (File f = FILE_A; f <= FILE_H; ++f)
		{
			for (emptyCnt = 0; f <= FILE_H && empty_square(make_square(f, r)); ++f)
				++emptyCnt;

			if (emptyCnt)
				ss << emptyCnt;

			if (f <= FILE_H)
				ss << PieceToChar[piece_on_square(make_square(f, r))];
		}

		if (r == RANK_1)
			break;

		ss << '/';
	}

	ss << (sideToMove == WHITE ? " w " : " b ");

	if (castling_possible(WHITE_SHORT))
		ss << (chess960 ? char('A' + file_of(castling_rook_square(SQ_G1))) : 'K');

	if (castling_possible(WHITE_LONG))
		ss << (chess960 ? char('A' + file_of(castling_rook_square(SQ_C1))) : 'Q');

	if (castling_possible(BLACK_SHORT))
		ss << (chess960 ? char('a' + file_of(castling_rook_square(SQ_G8))) : 'k');

	if (castling_possible(BLACK_LONG))
		ss << (chess960 ? char('a' + file_of(castling_rook_square(SQ_C8))) : 'q');

	if (!castling_rights(WHITE) && !castling_rights(BLACK))
		ss << '-';

	ss << (enpassant_square() == SQ_NONE ? " - " : " " + UCI::square(enpassant_square()) + " ")
		<< st->draw50Moves << " " << 1 + (gamePly - (sideToMove == BLACK)) / 2;

	return ss.str();
}


GamePhase Position::game_phase() const
{
	return GamePhase(std::max(0, std::min((int)MIDDLEGAME_PHASE, (int)st->phase - 6)));
}


Bitboard Position::attackers_to(Square square, Bitboard occupied) const
{
	return (attack_from<PAWN>(square, BLACK) & pieces(WHITE, PAWN))
		| (attack_from<PAWN>(square, WHITE)    & pieces(BLACK, PAWN))
		| (attack_from<KNIGHT>(square)        & pieces(KNIGHT))
		| (attack_bb_rook(square, occupied)   & pieces(ROOK, QUEEN))
		| (attack_bb_bishop(square, occupied)   & pieces(BISHOP, QUEEN))
		| (attack_from<KING>(square)       & pieces(KING));
}


bool Position::legal_move(Move move) const
{
	assert(is_ok(move));

	Color us = sideToMove;
	Square from = from_square(move);

	assert(piece_color(moved_piece(move)) == us);
	assert(piece_on_square(king(us)) == make_piece(us, KING));

	if (move_type(move) == ENPASSANT)
	{
		Square squareK = king(us);
		Square to = to_square(move);
		Square captureSq = to - pawn_forward(us);
		Bitboard occupied = (pieces() ^ from ^ captureSq) | to;

		assert(to == enpassant_square());
		assert(moved_piece(move) == make_piece(us, PAWN));
		assert(piece_on_square(captureSq) == make_piece(~us, PAWN));
		assert(piece_on_square(to) == NO_PIECE);

		return !(attack_bb_rook(squareK, occupied) & pieces(~us, QUEEN, ROOK))
			&& !(attack_bb_bishop(squareK, occupied) & pieces(~us, QUEEN, BISHOP));
	}

	if (piece_type(piece_on_square(from)) == KING)
		return move_type(move) == CASTLING || !(attackers_to(to_square(move)) & pieces(~us));

	return !(st->xRay[sideToMove] & from) || on_a_line(from, to_square(move), king(us));
}


bool Position::valid_move(const Move move) const
{
	Color us = sideToMove;
	Square from = from_square(move);

	if (!(pieces(us) & from))
		return false;

	Square to = to_square(move);
	PieceType piece = piece_type(moved_piece(move));

	// castling 0,8%  enpassant 0,03%  promotion 0,6%
	if (move >= Move(CASTLING))
	{
		if (move >= Move(PROMOTION_N))
		{
			if (piece != PAWN)
				return false;
		}
		else if (move < Move(ENPASSANT)) // castling
			return LegalMoveListContainsCastling(*this, move);
		else  // en-passant
			return LegalMoveListContainsMove(*this, move);
	}
	else
	{
		//if (piece != piece_of_move(move))
		//	return false;
	}

	if (pieces(us) & to)
		return false;

	if (piece == PAWN)
	{
		if ((move >= Move(PROMOTION_N)) ^ (rank_of(to) == relative_rank(us, RANK_8)))
			return false;

		if (!(attack_from<PAWN>(from, us) & pieces(~us) & to)
			&& !((from + pawn_forward(us) == to) && empty_square(to))
			&& !((from + 2 * pawn_forward(us) == to)
				&& (rank_of(from) == relative_rank(us, RANK_2))
				&& empty_square(to)
				&& empty_square(to - pawn_forward(us))))
			return false;
	}
	else if (!(attack_from(piece, from) & to))
	//else if (bb_between(from, to) & pieces())
		return false;

	if (check_givers())
	{
		if (piece != KING)
		{
			if (more_than_one(check_givers()))
				return false;

			if (!((bb_between(lsb(check_givers()), king(us)) | check_givers()) & to))
				return false;
		}
		else if (attackers_to(to, pieces() ^ from) & pieces(~us))
			return false;
	}

	return true;
}


bool Position::gives_check(Move move) const
{
	assert(is_ok(move));
	assert(piece_color(moved_piece(move)) == sideToMove);

	Square from = from_square(move);
	Square to = to_square(move);
	Square squareK = king(~sideToMove);

	if (st->checkSquares[piece_type(piece_on_square(from))] & to)
		return true;

	if ((st->xRay[~sideToMove] & from) && !on_a_line(from, to, squareK))
		return true;

	if (move < Move(CASTLING))
		return false;

	if (move >= Move(PROMOTION_N))
		return attack_bb(promotion_piece(move), to, pieces() ^ from) & squareK;

	if (move < Move(ENPASSANT))  // castling
	{
		Square fromR = castling_rook_square(to);
		Square toR = relative_square(sideToMove, fromR > from ? SQ_F1 : SQ_D1);

		return (EmptyAttack[ROOK][toR] & squareK)
			&& (attack_bb_rook(toR, (pieces() ^ from ^ fromR) | toR | to) & squareK);
	}

	// en-passant
	{
		Square ep_square = make_square(file_of(to), rank_of(from));
		Bitboard b = (pieces() ^ from ^ ep_square) | to;

		return  (attack_bb_rook(squareK, b) & pieces(sideToMove, QUEEN, ROOK))
			| (attack_bb_bishop(squareK, b) & pieces(sideToMove, QUEEN, BISHOP));
	}
}


void Position::do_move(Move move)
{
	bool givesCheck = move < Move(CASTLING) && !discovered_check_possible()
		? st->checkSquares[piece_type(piece_on_square(from_square(move)))] & to_square(move)
		: gives_check(move);

	do_move(move, givesCheck);
}

void Position::do_move(Move move, bool givesCheck)
{
	assert(is_ok(move));

	++nodes;
	trace_do_move(move);
	Key64 key64 = st->key ^ Zobrist::sideToMove;

	std::memcpy(st + 1, st, offsetof(PositionInfo, key));
	st++;

	st->draw50Moves = (st - 1)->draw50Moves + 1;
	st->distanceToNullMove = (st - 1)->distanceToNullMove + 1;

	Color us = sideToMove;
	Color them = ~us;
	Square from = from_square(move);
	Square to = to_square(move);
	Piece piece = piece_on_square(from);
	Piece capturePiece;

	assert(piece_color(piece) == us);

	if (move_type(move) == CASTLING)
	{
		assert(piece_type(piece) == KING);

		Square fromR, toR;
		do_castling<true>(us, from, to, fromR, toR);

		capturePiece = NO_PIECE;
		Piece ourRook = make_piece(us, ROOK);
		st->psq += PST::psq[ourRook][toR] - PST::psq[ourRook][fromR];
		key64 ^= Zobrist::psq[ourRook][fromR] ^ Zobrist::psq[ourRook][toR];
	}
	else
		capturePiece = move_type(move) == ENPASSANT ? make_piece(them, PAWN) : piece_on_square(to);

	if (capturePiece)
	{
		assert(piece_type(capturePiece) != KING);
		assert(piece_color(capturePiece) == them);
		Square captureSq = to;

		if (piece_type(capturePiece) == PAWN)
		{
			if (move_type(move) == ENPASSANT)
			{
				captureSq -= pawn_forward(us);

				assert(piece_type(piece) == PAWN);
				assert(to == st->enpassantSquare);
				assert(relative_rank(us, to) == RANK_6);
				assert(piece_on_square(to) == NO_PIECE);
				assert(piece_on_square(captureSq) == make_piece(them, PAWN));

				board[captureSq] = NO_PIECE;
			}

			st->pawnKey ^= Zobrist::psq[capturePiece][captureSq];
		}
		else
			st->nonPawnMaterial[them] -= MaterialValues[capturePiece];

		st->phase -= PiecePhase[capturePiece];

		remove_piece(them, capturePiece, captureSq);

		key64 ^= Zobrist::psq[capturePiece][captureSq];
		st->materialKey ^= Zobrist::psq[capturePiece][pieceCount[capturePiece]];
		prefetch(threadInfo->materialTable[st->materialKey ^ st->bishopColorKey]);

		if (piece_type(capturePiece) == BISHOP)
			compute_bishop_color_key();

		st->psq -= PST::psq[capturePiece][captureSq];

		st->draw50Moves = 0;
	}

	key64 ^= Zobrist::psq[piece][from] ^ Zobrist::psq[piece][to];

	if (st->enpassantSquare != SQ_NONE)
	{
		key64 ^= Zobrist::enpassant[file_of(st->enpassantSquare)];
		st->enpassantSquare = SQ_NONE;
	}

	if (st->castlingRights && (castlingMask[from] | castlingMask[to]))
	{
		int8_t castling = castlingMask[from] | castlingMask[to];
		key64 ^= Zobrist::castling[st->castlingRights & castling];
		st->castlingRights &= ~castling;
	}

	if (move_type(move) != CASTLING)
		move_piece(us, piece, from, to);

	if (piece_type(piece) == PAWN)
	{
		if ((int(to) ^ int(from)) == 16
			&& (attack_from<PAWN>(to - pawn_forward(us), us) & pieces(them, PAWN)))
		{
			st->enpassantSquare = (from + to) / 2;
			key64 ^= Zobrist::enpassant[file_of(st->enpassantSquare)];
		}

		else if (move >= Move(PROMOTION_N))
		{
			Piece promotion = make_piece(us, promotion_piece(move));

			assert(relative_rank(us, to) == RANK_8);
			assert(piece_type(promotion) >= KNIGHT && piece_type(promotion) <= QUEEN);

			remove_piece(us, piece, to);
			put_piece(us, promotion, to);

			key64 ^= Zobrist::psq[piece][to] ^ Zobrist::psq[promotion][to];
			st->pawnKey ^= Zobrist::psq[piece][to];
			st->materialKey ^= Zobrist::psq[promotion][pieceCount[promotion] - 1]
				^ Zobrist::psq[piece][pieceCount[piece]];

			if (piece_type(promotion) == BISHOP)
				compute_bishop_color_key();

			st->psq += PST::psq[promotion][to] - PST::psq[piece][to];

			st->nonPawnMaterial[us] += MaterialValues[promotion];
			st->phase += PiecePhase[promotion];
		}

		st->pawnKey ^= Zobrist::psq[piece][from] ^ Zobrist::psq[piece][to];
		prefetch2(threadInfo->pawnsTable[st->pawnKey]);

		st->draw50Moves = 0;
	}

	MainHash.prefetch_tuple(key64);

	pieceBB[ALL_PIECES] = colorBB[WHITE] | colorBB[BLACK];

	st->psq += PST::psq[piece][to] - PST::psq[piece][from];

	st->capturedPiece = capturePiece;
	st->movedPiece = piece_on_square(to);
	st->previousMove = move;
	st->counterMoveValues = &numaInfo->counterMoveStatistics[piece][to];
	st->evalPositional = NO_EVAL;

	st->key = key64;

	sideToMove = ~sideToMove;
	st->checkGivers = givesCheck ? attackers_to(king(them)) & pieces(us) : 0;
	st->moveRepetition = is_draw();
	compute_check_pins();

	assert(position_is_ok());
}


void Position::undo_move(Move move)
{
	assert(is_ok(move));
	trace_cancel_move();

	sideToMove = ~sideToMove;

	Color us = sideToMove;
	Square from = from_square(move);
	Square to = to_square(move);
	Piece piece = piece_on_square(to);

	assert(empty_square(from) || move_type(move) == CASTLING);
	assert(piece_type(st->capturedPiece) != KING);
	assert(piece == st->movedPiece);

	if (move < Move(CASTLING))
	{
		move_piece(us, piece, to, from);
		if (st->capturedPiece)
			put_piece(~us, st->capturedPiece, to);
	}
	else
	{
		if (move >= Move(PROMOTION_N))
		{
			assert(relative_rank(us, to) == RANK_8);
			assert(piece_type(piece) == promotion_piece(move));
			assert(piece_type(piece) >= KNIGHT && piece_type(piece) <= QUEEN);

			remove_piece(us, piece, to);
			piece = make_piece(us, PAWN);
			put_piece(us, piece, to);
		}

		if (move_type(move) == CASTLING)
		{
			Square fromR, toR;
			do_castling<false>(us, from, to, fromR, toR);
		}
		else
		{
			move_piece(us, piece, to, from);

			if (st->capturedPiece)
			{
				Square captureSq = to;

				if (move_type(move) == ENPASSANT)
				{
					captureSq -= pawn_forward(us);

					assert(piece_type(piece) == PAWN);
					assert(to == (st - 1)->enpassantSquare);
					assert(relative_rank(us, to) == RANK_6);
					assert(piece_on_square(captureSq) == NO_PIECE);
					assert(st->capturedPiece == make_piece(~us, PAWN));
				}

				put_piece(~us, st->capturedPiece, captureSq);
			}
		}
	}
	pieceBB[ALL_PIECES] = colorBB[WHITE] | colorBB[BLACK];

	st--;

	assert(position_is_ok());
}


template<bool Do>
void Position::do_castling(Color us, Square from, Square to, Square& fromR, Square& toR)
{
	fromR = castling_rook_square(to);
	toR = relative_square(us, fromR > from ? SQ_F1 : SQ_D1);

	if (!chess960)
	{
		move_piece(us, make_piece(us, KING), Do ? from : to, Do ? to : from);
		move_piece(us, make_piece(us, ROOK), Do ? fromR : toR, Do ? toR : fromR);
	}
	else
	{
		remove_piece(us, make_piece(us, KING), Do ? from : to);
		remove_piece(us, make_piece(us, ROOK), Do ? fromR : toR);
		board[Do ? from : to] = board[Do ? fromR : toR] = NO_PIECE;
		put_piece(us, make_piece(us, KING), Do ? to : from);
		put_piece(us, make_piece(us, ROOK), Do ? toR : fromR);
	}
}


void Position::do_null_move()
{
	assert(!check_givers());

	++nodes;
	trace_do_move(0);

	Key64 key64 = st->key ^ Zobrist::sideToMove;
	if (st->enpassantSquare != SQ_NONE)
		key64 ^= Zobrist::enpassant[file_of(st->enpassantSquare)];

	MainHash.prefetch_tuple(key64);

	std::memcpy(st + 1, st, offsetof(PositionInfo, key));
	st++;

	st->key = key64;
	st->draw50Moves = (st - 1)->draw50Moves + 1;
	st->distanceToNullMove = 0;

	st->enpassantSquare = SQ_NONE;
	st->checkGivers = 0;
	st->capturedPiece = NO_PIECE; // not really needed, everywhere where capturedPiece is used 
								  // is there a protection against a preceding null move
	st->previousMove = NULL_MOVE;
	st->counterMoveValues = nullptr;
	st->evalPositional = (st - 1)->evalPositional;
	st->evalFactor = (st - 1)->evalFactor;

	sideToMove = ~sideToMove;
	st->moveRepetition = is_draw();
	compute_check_pins();

	assert(position_is_ok());
}

void Position::undo_null_move()
{
	assert(!check_givers());
	trace_cancel_move();

	st--;
	sideToMove = ~sideToMove;
}


Key64 Position::key_after_move(Move move) const
{
	Square from = from_square(move);
	Square to = to_square(move);
	Piece piece = piece_on_square(from);
	Piece capturePiece = piece_on_square(to);
	Key64 k = st->key ^ Zobrist::sideToMove;

	if (capturePiece)
		k ^= Zobrist::psq[capturePiece][to];

	return k ^ Zobrist::psq[piece][to] ^ Zobrist::psq[piece][from];
}


const SeeValue SeeValueSimple[PIECE_N] =
{
	SEE_0, SEE_0, SEE_PAWN, SEE_KNIGHT, SEE_BISHOP, SEE_ROOK, SEE_QUEEN, SEE_0,
	SEE_0, SEE_0, SEE_PAWN, SEE_KNIGHT, SEE_BISHOP, SEE_ROOK, SEE_QUEEN, SEE_0
};

const SeeValue* Position::see_values() const
{
	return SeeValueSimple;
}

// see_sign returns >= 0 (no exact value) or < 0 (an exact value!)
/*
SeeValue Position::see(Move move) const
{
	SeeValue value, best, worst;
	const SeeValue* see_value = see_values();

	assert(is_ok(move));

	Square from = from_square(move);
	Square to = to_square(move);
	value = see_value[piece_on_square(to)];
	if (see_value[piece_on_square(from)] <= value)
		return SEE_0;

	if (move_type(move) == CASTLING || move_type(move) == ENPASSANT)
		return SEE_0;

	best = value;
	value -= see_value[piece_on_square(from)];
	worst = value;

	const Color us = piece_color(piece_on_square(from));
	Bitboard occupied = pieces() ^ from;
	Bitboard attackers = attackers_to(to, occupied) & occupied;

	do {
		Bitboard ourAttackers = attackers & pieces(~us);
		if (!ourAttackers)
			return best;
		//if (usePins) 
		//{
		// Bitboard pinned = stmAttackers & st->xRay[~stm];
		// while (pinned) {
		//  Square square = pop_lsb(&pinned);
		//  if (occupied & st->pinner[square]) {
		//	  stmAttackers ^= square;
		//	  if (!stmAttackers)
		//		  return best;
		//  }
		// }
		//}

		Bitboard bb;
		PieceType theirPiece;
		if ((bb = ourAttackers & pieces(PAWN)))
			theirPiece = PAWN;
		else if ((bb = ourAttackers & pieces(KNIGHT)))
			theirPiece = KNIGHT;
		else if ((bb = ourAttackers & pieces(BISHOP)))
			theirPiece = BISHOP;
		else if ((bb = ourAttackers & pieces(ROOK)))
			theirPiece = ROOK;
		else if ((bb = ourAttackers & pieces(QUEEN)))
			theirPiece = QUEEN;
		else
			return attackers & pieces(us) ? best : worst;

		value += see_value[theirPiece];
		best = std::min(best, value);
		if (worst >= best)
			return worst;

		occupied ^= (bb & -bb);
		if (!(theirPiece & 1)) // PAWN, BISHOP, QUEEN
			attackers |= attack_bb_bishop(to, occupied) & (pieces(BISHOP) | pieces(QUEEN));
		if (theirPiece >= ROOK)
			attackers |= attack_bb_rook(to, occupied) & (pieces(ROOK) | pieces(QUEEN));
		attackers &= occupied;

		ourAttackers = attackers & pieces(us);
		if (!ourAttackers)
			return worst;
		//if (usePins)
		//{
		// Bitboard pinned = stmAttackers & st->xRay[stm];
		// while (pinned) {
		//  Square square = pop_lsb(&pinned);
		//  if (occupied & st->pinner[square]) {
		//	  stmAttackers ^= square;
		//	  if (!stmAttackers)
		//		  return worst;
		//  }
		// }
		//}

		if ((bb = ourAttackers & pieces(PAWN)))
			theirPiece = PAWN;
		else if ((bb = ourAttackers & pieces(KNIGHT)))
			theirPiece = KNIGHT;
		else if ((bb = ourAttackers & pieces(BISHOP)))
			theirPiece = BISHOP;
		else if ((bb = ourAttackers & pieces(ROOK)))
			theirPiece = ROOK;
		else if ((bb = ourAttackers & pieces(QUEEN)))
			theirPiece = QUEEN;
		else
			return attackers & pieces(~us) ? worst : best;

		value -= see_value[theirPiece];
		worst = std::max(worst, value);
		if (worst >= best)
			return best;
		if (worst >= 0)
			return SEE_0;

		occupied ^= (bb & -bb);
		if (!(theirPiece & 1)) // PAWN, BISHOP, QUEEN
			attackers |= attack_bb_bishop(to, occupied) & (pieces(BISHOP) | pieces(QUEEN));
		if (theirPiece >= ROOK)
			attackers |= attack_bb_rook(to, occupied) & (pieces(ROOK) | pieces(QUEEN));
		attackers &= occupied;
	} while (true);
}
*/

// Test if see(move) >= value
//template <bool UsePins>
bool Position::see_test(Move move, SeeValue bound) const
{
	if (move_type(move) == CASTLING)
		return 0 >= bound;

	const SeeValue* see_value = see_values();
	Square from = from_square(move);
	Square to = to_square(move);
	Bitboard occupied = pieces();
	const Color us = piece_color(piece_on_square(from));

	int value = see_value[piece_on_square(to)] - bound;
	if (move_type(move) == ENPASSANT)
	{
		occupied ^= to - pawn_forward(us);
		value += see_value[PAWN];
	}
	if (value < 0)
		return false;

	value -= see_value[piece_on_square(from)];
	if (value >= 0)
		return true;

	occupied ^= from;
	Bitboard attackers = attackers_to(to, occupied) & occupied;

	do {
		Bitboard ourAttackers = attackers & pieces(~us);
		if (!ourAttackers)
			return true;
		//if (UsePins)
		{
			Bitboard pinned = ourAttackers & st->xRay[~us];
			while (pinned) {
				Square square = pop_lsb(&pinned);
				if (occupied & st->pinned_by[square])
				{
					ourAttackers ^= square;
					if (!ourAttackers)
						return true;
				}
			}
		}

		Bitboard bb;
		PieceType capturePiece;
		if ((bb = ourAttackers & pieces(PAWN)))
			capturePiece = PAWN;
		else if ((bb = ourAttackers & pieces(KNIGHT)))
			capturePiece = KNIGHT;
		else if ((bb = ourAttackers & pieces(BISHOP)))
			capturePiece = BISHOP;
		else if ((bb = ourAttackers & pieces(ROOK)))
			capturePiece = ROOK;
		else if ((bb = ourAttackers & pieces(QUEEN)))
			capturePiece = QUEEN;
		else
			return attackers & pieces(us);

		value += see_value[capturePiece];
		if (value < 0) 
			return false;
		occupied ^= (bb & -bb);
		if (!(capturePiece & 1)) // PAWN, BISHOP, QUEEN
			attackers |= attack_bb_bishop(to, occupied) & (pieces(BISHOP) | pieces(QUEEN));
		if (capturePiece >= ROOK)
			attackers |= attack_bb_rook(to, occupied) & (pieces(ROOK) | pieces(QUEEN));
		attackers &= occupied;

		ourAttackers = attackers & pieces(us);
		if (!ourAttackers)
			return false;
		//if (UsePins)
		{
			Bitboard pinned = ourAttackers & st->xRay[us];
			while (pinned) {
				Square square = pop_lsb(&pinned);
				if (occupied & st->pinned_by[square])
				{
					ourAttackers ^= square;
					if (!ourAttackers)
						return false;
				}
			}
		}
		
		if ((bb = ourAttackers & pieces(PAWN)))
			capturePiece = PAWN;
		else if ((bb = ourAttackers & pieces(KNIGHT)))
			capturePiece = KNIGHT;
		else if ((bb = ourAttackers & pieces(BISHOP)))
			capturePiece = BISHOP;
		else if ((bb = ourAttackers & pieces(ROOK)))
			capturePiece = ROOK;
		else if ((bb = ourAttackers & pieces(QUEEN)))
			capturePiece = QUEEN;
		else
			return !(attackers & pieces(~us));

		value -= see_value[capturePiece];
		if (value >= 0)
			return true;
		occupied ^= (bb & -bb);
		if (!(capturePiece & 1)) // PAWN, BISHOP, QUEEN
			attackers |= attack_bb_bishop(to, occupied) & (pieces(BISHOP) | pieces(QUEEN));
		if (capturePiece >= ROOK)
			attackers |= attack_bb_rook(to, occupied) & (pieces(ROOK) | pieces(QUEEN));
		attackers &= occupied;
	} while (true);
}


bool Position::is_draw() const
{
	if (st->draw50Moves >= 2 * Threads.fiftyMovesDistance)
	{
		if (st->draw50Moves == 100)
			return !st->checkGivers || AtLeastOneLegalMove(*this);
		return true;
	}

	int n = std::min(st->draw50Moves, st->distanceToNullMove) - 4;
	if (n < 0)
		return false;

	PositionInfo* stp = st - 4;
	do
	{
		if (stp->key == st->key)
			return true; // Draw at first repetition

		stp -= 2;
		n -= 2;
	} while (n >= 0);

	return false;
}


void Position::flipBoard()
{
	string f, token;
	std::stringstream ss(fen());

	for (Rank r = RANK_8; r >= RANK_1; --r)
	{
		std::getline(ss, token, r > RANK_1 ? '/' : ' ');
		f.insert(0, token + (f.empty() ? " " : "/"));
	}

	ss >> token;
	f += (token == "w" ? "B " : "W ");

	ss >> token;
	f += token + " ";

	std::transform(f.begin(), f.end(), f.begin(),
		[](char c) { return char(islower(c) ? toupper(c) : tolower(c)); });

	ss >> token;
	f += (token == "-" ? token : token.replace(1, 1, token[1] == '3' ? "6" : "3"));

	std::getline(ss, token);
	f += token;

	set(f, is_chess960(), my_thread());

	assert(position_is_ok());
}


bool Position::position_is_ok(int* failedStep) const
{
	const bool Fast = true;

	enum { Default, King, Bitboards, State, Lists, Castling };

	for (int step = Default; step <= (Fast ? Default : Castling); step++)
	{
		if (failedStep)
			*failedStep = step;

		if (step == Default)
			if ((sideToMove != WHITE && sideToMove != BLACK)
				|| piece_on_square(king(WHITE)) != W_KING
				|| piece_on_square(king(BLACK)) != B_KING
				|| (enpassant_square() != SQ_NONE
					&& relative_rank(sideToMove, enpassant_square()) != RANK_6))
				return false;

		if (step == King)
			if (std::count(board, board + SQUARE_N, W_KING) != 1
				|| std::count(board, board + SQUARE_N, B_KING) != 1
				|| attackers_to(king(~sideToMove)) & pieces(sideToMove))
				return false;

		if (step == Bitboards)
		{
			if ((pieces(WHITE) & pieces(BLACK))
				|| (pieces(WHITE) | pieces(BLACK)) != pieces())
				return false;

			for (PieceType p1 = KING; p1 <= QUEEN; ++p1)
				for (PieceType p2 = KING; p2 <= QUEEN; ++p2)
					if (p1 != p2 && (pieces(p1) & pieces(p2)))
						return false;
		}

		if (step == State)
		{
			PositionInfo si = *st;
			set_position_info(&si);
			if (std::memcmp(&si, st, sizeof(PositionInfo)))
				return false;
		}

		if (step == Lists)
			for (Color color = WHITE; color <= BLACK; ++color)
				for (PieceType piece = KING; piece <= QUEEN; ++piece)
				{
					if (pieceCount[make_piece(color, piece)] != popcount(pieces(color, piece)))
						return false;

					for (int i = 0; i < pieceCount[make_piece(color, piece)]; ++i)
						if (board[pieceList[make_piece(color, piece)][i]] != make_piece(color, piece)
							|| pieceIndex[pieceList[make_piece(color, piece)][i]] != i)
							return false;
				}

		if (step == Castling)
			for (Color color = WHITE; color <= BLACK; ++color)
				for (int n = 0; n < 2; n++)
				{
					CastlingRight castling = CastlingRight(WHITE_SHORT << (n + 2 * color));

					if (!castling_possible(castling))
						continue;

					if (piece_on_square(castlingRookSquare[castling]) != make_piece(color, ROOK)
						|| castlingMask[castlingRookSquare[castling]] != castling
						|| (castlingMask[king(color)] & (castling)) != castling)
						return false;
				}
	}

	return true;
}


std::string Position::validate_position() const
{
	if (count(WHITE, KING) != 1)
		return "Exactly one white king required";
	if (count(BLACK, KING) != 1)
		return "Exactly one black king required";

	if (popcount(pieces(WHITE)) > 16)
		return "Too many white pieces";
	if (popcount(pieces(BLACK)) > 16)
		return "Too many black pieces";

	if (count(WHITE, PAWN) > 8)
		return "Too many white pawns";
	if (count(BLACK, PAWN) > 8)
		return "Too many black pawns";

	if (count(WHITE, QUEEN) + count(WHITE, PAWN) > 9)
		return "Too many white queens";
	if (count(BLACK, QUEEN) + count(BLACK, PAWN) > 9)
		return "Too many black queens";

	if (count(WHITE, ROOK) + count(WHITE, PAWN) > 10)
		return "Too many white rooks";
	if (count(BLACK, ROOK) + count(BLACK, PAWN) > 10)
		return "Too many black rooks";

	if (count(WHITE, BISHOP) + count(WHITE, PAWN) > 10)
		return "Too many white bishops";
	if (count(BLACK, BISHOP) + count(BLACK, PAWN) > 10)
		return "Too many black bishops";

	if (count(WHITE, KNIGHT) + count(WHITE, PAWN) > 10)
		return "Too many white knights";
	if (count(BLACK, KNIGHT) + count(BLACK, PAWN) > 10)
		return "Too many black knights";

	if (pieces(PAWN) & (Rank1BB | Rank8BB))
		return "Pawn at rank 1 or 8";

	return "";
}

Square Position::compute_threat() const
{
	if (!st->counterMoveValues)
		return SQ_NONE;

	Square to = to_square(st->previousMove);
	switch (st->movedPiece)
	{
	case W_PAWN:
		if (Bitboard b = PawnAttack[WHITE][to] & pieces_except(BLACK, PAWN))
			return lsb(b);
		break;
	case B_PAWN:
		if (Bitboard b = PawnAttack[BLACK][to] & pieces_except(WHITE, PAWN))
			return msb(b);
		break;
	case W_KNIGHT:
		if (Bitboard b = EmptyAttack[KNIGHT][to] & pieces(BLACK, ROOK, QUEEN))
			return lsb(b);
		break;
	case B_KNIGHT:
		if (Bitboard b = EmptyAttack[KNIGHT][to] & pieces(WHITE, ROOK, QUEEN))
			return msb(b);
		break;
	case W_BISHOP:
		// for B and R possibly test that the threat did not already exist earlier
		// (previousMove in the same direction as threat)
		if (Bitboard b = attack_bb_bishop(to, pieces()) & pieces(BLACK, ROOK, QUEEN))
			return lsb(b);
		break;
	case B_BISHOP:
		if (Bitboard b = attack_bb_bishop(to, pieces()) & pieces(WHITE, ROOK, QUEEN))
			return msb(b);
		break;
	case W_ROOK:
		if (Bitboard b = attack_bb_rook(to, pieces()) & pieces(BLACK, QUEEN))
			return lsb(b);
		break;
	case B_ROOK:
		if (Bitboard b = attack_bb_rook(to, pieces()) & pieces(WHITE, QUEEN))
			return msb(b);
		break;
	default:
		break;
	}

	return SQ_NONE;
}

