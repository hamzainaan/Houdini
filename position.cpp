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
	Key64 rokade[CASTLING_RIGHT_N];
	Key64 aanZet;
	Key64 hash_50move[32];
}

Key64 Position::exclusion_key(Move zet) const
{ 
	return Zobrist::psq[W_KING][from_square(zet)] ^ Zobrist::psq[B_KING][to_square(zet)];
	//return zet;
}

Key64 Position::draw50_key() const
{
	return Zobrist::hash_50move[st->remise50Zetten >> 2];
}


namespace
{
	const string PieceToChar(" KPNBRQ  kpnbrq");
} // namespace


void Position::compute_check_pins() const
{
	compute_pins<WHITE>();
	compute_pins<BLACK>();
	Square veldK = koning(~aanZet);
	st->schaakVelden[PAWN]  = attack_from<PAWN>(veldK, ~aanZet);
	st->schaakVelden[KNIGHT] = attack_from<KNIGHT>(veldK);
	st->schaakVelden[BISHOP] = attack_from<BISHOP>(veldK);
	st->schaakVelden[ROOK] = attack_from<ROOK>(veldK);
	st->schaakVelden[QUEEN]  = st->schaakVelden[BISHOP] | st->schaakVelden[ROOK];
	st->schaakVelden[KING] = 0;
}

template <Color kleur>
void Position::compute_pins() const
{
	Bitboard penningGevers, result = 0;
	Square veldK = koning(kleur);

	penningGevers = (LegeAanval[ROOK][veldK] & pieces(~kleur, QUEEN, ROOK))
		          | (LegeAanval[BISHOP][veldK] & pieces(~kleur, QUEEN, BISHOP));

	while (penningGevers)
	{
		Square veld = pop_lsb(&penningGevers);
		Bitboard b = bb_between(veldK, veld) & pieces();

		if (b && !more_than_one(b))
		{
			result |= b;
			st->penning_door[lsb(b)] = veld;
		}
	}
	st->xRay[kleur] = result;
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

	for (Color kleur = WHITE; kleur <= BLACK; ++kleur)
		for (PieceType stuk = KING; stuk <= QUEEN; ++stuk)
			for (Square veld = SQ_A1; veld <= SQ_H8; ++veld)
				Zobrist::psq[make_piece(kleur, stuk)][veld] = rng.rand<Key64>();

	for (File f = FILE_A; f <= FILE_H; ++f)
		Zobrist::enpassant[f] = rng.rand<Key64>();

	for (int rokade = NO_CASTLING; rokade <= ALL_CASTLING; ++rokade)
	{
		Zobrist::rokade[rokade] = 0;
		Bitboard b = rokade;
		while (b)
		{
			Key64 k = Zobrist::rokade[1ULL << pop_lsb(&b)];
			Zobrist::rokade[rokade] ^= k ? k : rng.rand<Key64>();
		}
	}

	Zobrist::aanZet = rng.rand<Key64>();
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
	std::fill_n(&stukLijst[0][0], sizeof(stukLijst) / sizeof(Square), SQ_NONE);
	st = th->ti->stellingInfo + 5;
	std::memset(st, 0, sizeof(PositionInfo));
	chess960 = isChess960;

	ss >> std::noskipws;

	// stukken
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
	stukBB[ALL_PIECES] = kleurBB[WHITE] | kleurBB[BLACK];

	// kleur
	ss >> token;
	aanZet = (token == 'w' ? WHITE : BLACK);
	ss >> token;

	// rokade
	while ((ss >> token) && !isspace(token))
	{
		Square rsq;
		Color kleur = islower(token) ? BLACK : WHITE;
		Piece toren = make_piece(kleur, ROOK);

		token = char(toupper(token));

		if (token == 'K')
			for (rsq = relative_square(kleur, SQ_H1); piece_on_square(rsq) != toren; --rsq) {}

		else if (token == 'Q')
			for (rsq = relative_square(kleur, SQ_A1); piece_on_square(rsq) != toren; ++rsq) {}

		else if (token >= 'A' && token <= 'H')
			rsq = make_square(File(token - 'A'), relative_rank(kleur, RANK_1));

		else
			continue;

		set_castling_right(kleur, rsq);
	}

	// en-passant
	if (((ss >> col) && (col >= 'a' && col <= 'h'))
		&& ((ss >> row) && (row == '3' || row == '6')))
	{
		st->enpassantVeld = make_square(File(col - 'a'), Rank(row - '1'));

		if (!(attackers_to(st->enpassantVeld) & pieces(aanZet, PAWN)))
			st->enpassantVeld = SQ_NONE;
	}
	else
		st->enpassantVeld = SQ_NONE;

	// zetaantal
	ss >> std::skipws >> st->remise50Zetten >> partijPly;

	partijPly = std::max(2 * (partijPly - 1), 0) + (aanZet == BLACK);

	mijnThread = th;
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
		mijnThread = th;
		threadInfo = th->ti;
		numaInfo = th->ni;
		st = th->ti->stellingInfo + 5;

		PositionInfo* origSt = pos->threadInfo->stellingInfo + 5;
		while (origSt < copyState - 4)
		{
			st->sleutel = origSt->sleutel;
			st++;
			origSt++;
		}
		// copy last 5 StellingInfo records
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


void Position::set_castling_right(Color kleur, Square vanT)
{
	Square vanK = koning(kleur);
	CastlingRight rokade = CastlingRight(WHITE_SHORT << ((vanK >= vanT) + 2 * kleur));

	Square naarK = relative_square(kleur, vanK < vanT ? SQ_G1 : SQ_C1);
	Square naarT = relative_square(kleur, vanK < vanT ? SQ_F1 : SQ_D1);

	st->rokadeMogelijkheden |= rokade;
	rokadeMask[vanK] |= rokade;
	rokadeMask[vanT] |= rokade;
	rokadeTorenVeld[naarK] = vanT;

	Bitboard pad = 0;
	for (Square veld = std::min(vanT, naarT); veld <= std::max(vanT, naarT); ++veld)
		pad |= veld;

	for (Square veld = std::min(vanK, naarK); veld <= std::max(vanK, naarK); ++veld)
		pad |= veld;

	rokadePad[rokade] = pad & ~(bbVeld[vanK] | bbVeld[vanT]);

	if (vanK != relative_square(kleur, SQ_E1))
		chess960 = true;
	if (vanK < vanT && vanT != relative_square(kleur, SQ_H1))
		chess960 = true;
	if (vanK >= vanT && vanT != relative_square(kleur, SQ_A1))
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
	st->loperKleurSleutel = key;
}


void Position::set_position_info(PositionInfo* si) const
{
	si->sleutel = si->materiaalSleutel = 0;
	si->nietPionMateriaal[WHITE] = si->nietPionMateriaal[BLACK] = MATL_0;
	si->psq = SCORE_ZERO;
	si->fase = 0;

	si->schaakGevers = attackers_to(koning(aanZet)) & pieces(~aanZet);

	for (Bitboard b = pieces(); b; )
	{
		Square veld = pop_lsb(&b);
		Piece stuk = piece_on_square(veld);
		si->sleutel ^= Zobrist::psq[stuk][veld];
		si->psq += PST::psq[stuk][veld];
		si->fase += StukFase[piece_type(stuk)];
	}

	if (si->enpassantVeld != SQ_NONE)
		si->sleutel ^= Zobrist::enpassant[lijn(si->enpassantVeld)];

	if (aanZet == BLACK)
		si->sleutel ^= Zobrist::aanZet;

	si->sleutel ^= Zobrist::rokade[si->rokadeMogelijkheden];

	si->pionSleutel = 0x1234567890ABCDEF;
	for (Bitboard b = pieces(PAWN); b; )
	{
		Square veld = pop_lsb(&b);
		si->pionSleutel ^= Zobrist::psq[piece_on_square(veld)][veld];
	}

	for (Color kleur = WHITE; kleur <= BLACK; ++kleur)
		for (PieceType stuk = KING; stuk <= QUEEN; ++stuk)
			for (int cnt = 0; cnt < stukAantal[make_piece(kleur, stuk)]; ++cnt)
				si->materiaalSleutel ^= Zobrist::psq[make_piece(kleur, stuk)][cnt];

	compute_bishop_color_key();

	for (Color kleur = WHITE; kleur <= BLACK; ++kleur)
		for (PieceType stuk = KNIGHT; stuk <= QUEEN; ++stuk)
			si->nietPionMateriaal[kleur] += MateriaalWaarden[stuk] * (int)stukAantal[make_piece(kleur, stuk)];
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

	ss << (aanZet == WHITE ? " w " : " b ");

	if (castling_possible(WHITE_SHORT))
		ss << (chess960 ? char('A' + lijn(castling_rook_square(SQ_G1))) : 'K');

	if (castling_possible(WHITE_LONG))
		ss << (chess960 ? char('A' + lijn(castling_rook_square(SQ_C1))) : 'Q');

	if (castling_possible(BLACK_SHORT))
		ss << (chess960 ? char('a' + lijn(castling_rook_square(SQ_G8))) : 'k');

	if (castling_possible(BLACK_LONG))
		ss << (chess960 ? char('a' + lijn(castling_rook_square(SQ_C8))) : 'q');

	if (!castling_rights(WHITE) && !castling_rights(BLACK))
		ss << '-';

	ss << (enpassant_square() == SQ_NONE ? " - " : " " + UCI::veld(enpassant_square()) + " ")
		<< st->remise50Zetten << " " << 1 + (partijPly - (aanZet == BLACK)) / 2;

	return ss.str();
}


GamePhase Position::game_phase() const
{
	return GamePhase(std::max(0, std::min((int)MIDDLEGAME_PHASE, (int)st->fase - 6)));
}


Bitboard Position::attackers_to(Square veld, Bitboard bezet) const
{
	return (attack_from<PAWN>(veld, BLACK) & pieces(WHITE, PAWN))
		| (attack_from<PAWN>(veld, WHITE)    & pieces(BLACK, PAWN))
		| (attack_from<KNIGHT>(veld)        & pieces(KNIGHT))
		| (attack_bb_rook(veld, bezet)   & pieces(ROOK, QUEEN))
		| (attack_bb_bishop(veld, bezet)   & pieces(BISHOP, QUEEN))
		| (attack_from<KING>(veld)       & pieces(KING));
}


bool Position::legal_move(Move zet) const
{
	assert(is_ok(zet));

	Color ik = aanZet;
	Square van = from_square(zet);

	assert(piece_color(moved_piece(zet)) == ik);
	assert(piece_on_square(koning(ik)) == make_piece(ik, KING));

	if (move_type(zet) == ENPASSANT)
	{
		Square veldK = koning(ik);
		Square naar = to_square(zet);
		Square slagveld = naar - pawn_forward(ik);
		Bitboard bezet = (pieces() ^ van ^ slagveld) | naar;

		assert(naar == enpassant_square());
		assert(moved_piece(zet) == make_piece(ik, PAWN));
		assert(piece_on_square(slagveld) == make_piece(~ik, PAWN));
		assert(piece_on_square(naar) == NO_PIECE);

		return !(attack_bb_rook(veldK, bezet) & pieces(~ik, QUEEN, ROOK))
			&& !(attack_bb_bishop(veldK, bezet) & pieces(~ik, QUEEN, BISHOP));
	}

	if (piece_type(piece_on_square(van)) == KING)
		return move_type(zet) == CASTLING || !(attackers_to(to_square(zet)) & pieces(~ik));

	return !(st->xRay[aanZet] & van) || on_a_line(van, to_square(zet), koning(ik));
}


bool Position::valid_move(const Move zet) const
{
	Color ik = aanZet;
	Square van = from_square(zet);

	if (!(pieces(ik) & van))
		return false;

	Square naar = to_square(zet);
	PieceType stuk = piece_type(moved_piece(zet));

	// rokade 0,8%  enpassant 0,03%  promotie 0,6%
	if (zet >= Move(CASTLING))
	{
		if (zet >= Move(PROMOTION_N))
		{
			if (stuk != PAWN)
				return false;
		}
		else if (zet < Move(ENPASSANT)) // rokade
			return LegalMoveListContainsCastling(*this, zet);
		else  // en-passant
			return LegalMoveListContainsMove(*this, zet);
	}
	else
	{
		//if (stuk != stuk_van_zet(zet))
		//	return false;
	}

	if (pieces(ik) & naar)
		return false;

	if (stuk == PAWN)
	{
		if ((zet >= Move(PROMOTION_N)) ^ (rij(naar) == relative_rank(ik, RANK_8)))
			return false;

		if (!(attack_from<PAWN>(van, ik) & pieces(~ik) & naar)
			&& !((van + pawn_forward(ik) == naar) && empty_square(naar))
			&& !((van + 2 * pawn_forward(ik) == naar)
				&& (rij(van) == relative_rank(ik, RANK_2))
				&& empty_square(naar)
				&& empty_square(naar - pawn_forward(ik))))
			return false;
	}
	else if (!(attack_from(stuk, van) & naar))
	//else if (bb_tussen(van, naar) & stukken())
		return false;

	if (check_givers())
	{
		if (stuk != KING)
		{
			if (more_than_one(check_givers()))
				return false;

			if (!((bb_between(lsb(check_givers()), koning(ik)) | check_givers()) & naar))
				return false;
		}
		else if (attackers_to(naar, pieces() ^ van) & pieces(~ik))
			return false;
	}

	return true;
}


bool Position::gives_check(Move zet) const
{
	assert(is_ok(zet));
	assert(piece_color(moved_piece(zet)) == aanZet);

	Square van = from_square(zet);
	Square naar = to_square(zet);
	Square veldK = koning(~aanZet);

	if (st->schaakVelden[piece_type(piece_on_square(van))] & naar)
		return true;

	if ((st->xRay[~aanZet] & van) && !on_a_line(van, naar, veldK))
		return true;

	if (zet < Move(CASTLING))
		return false;

	if (zet >= Move(PROMOTION_N))
		return attack_bb(promotion_piece(zet), naar, pieces() ^ van) & veldK;

	if (zet < Move(ENPASSANT))  // rokade
	{
		Square vanT = castling_rook_square(naar);
		Square naarT = relative_square(aanZet, vanT > van ? SQ_F1 : SQ_D1);

		return (LegeAanval[ROOK][naarT] & veldK)
			&& (attack_bb_rook(naarT, (pieces() ^ van ^ vanT) | naarT | naar) & veldK);
	}

	// en-passant
	{
		Square ep_veld = make_square(lijn(naar), rij(van));
		Bitboard b = (pieces() ^ van ^ ep_veld) | naar;

		return  (attack_bb_rook(veldK, b) & pieces(aanZet, QUEEN, ROOK))
			| (attack_bb_bishop(veldK, b) & pieces(aanZet, QUEEN, BISHOP));
	}
}


void Position::do_move(Move zet)
{
	bool geeftSchaak = zet < Move(CASTLING) && !discovered_check_possible()
		? st->schaakVelden[piece_type(piece_on_square(from_square(zet)))] & to_square(zet)
		: gives_check(zet);

	do_move(zet, geeftSchaak);
}

void Position::do_move(Move zet, bool geeftSchaak)
{
	assert(is_ok(zet));

	++knopen;
	trace_do_move(zet);
	Key64 sleutel64 = st->sleutel ^ Zobrist::aanZet;

	std::memcpy(st + 1, st, offsetof(PositionInfo, sleutel));
	st++;

	st->remise50Zetten = (st - 1)->remise50Zetten + 1;
	st->afstandTotNullzet = (st - 1)->afstandTotNullzet + 1;

	Color ik = aanZet;
	Color jij = ~ik;
	Square van = from_square(zet);
	Square naar = to_square(zet);
	Piece stuk = piece_on_square(van);
	Piece slagstuk;

	assert(piece_color(stuk) == ik);

	if (move_type(zet) == CASTLING)
	{
		assert(piece_type(stuk) == KING);

		Square vanT, naarT;
		do_castling<true>(ik, van, naar, vanT, naarT);

		slagstuk = NO_PIECE;
		Piece mijnToren = make_piece(ik, ROOK);
		st->psq += PST::psq[mijnToren][naarT] - PST::psq[mijnToren][vanT];
		sleutel64 ^= Zobrist::psq[mijnToren][vanT] ^ Zobrist::psq[mijnToren][naarT];
	}
	else
		slagstuk = move_type(zet) == ENPASSANT ? make_piece(jij, PAWN) : piece_on_square(naar);

	if (slagstuk)
	{
		assert(piece_type(slagstuk) != KING);
		assert(piece_color(slagstuk) == jij);
		Square slagveld = naar;

		if (piece_type(slagstuk) == PAWN)
		{
			if (move_type(zet) == ENPASSANT)
			{
				slagveld -= pawn_forward(ik);

				assert(piece_type(stuk) == PAWN);
				assert(naar == st->enpassantVeld);
				assert(relative_rank(ik, naar) == RANK_6);
				assert(piece_on_square(naar) == NO_PIECE);
				assert(piece_on_square(slagveld) == make_piece(jij, PAWN));

				bord[slagveld] = NO_PIECE;
			}

			st->pionSleutel ^= Zobrist::psq[slagstuk][slagveld];
		}
		else
			st->nietPionMateriaal[jij] -= MateriaalWaarden[slagstuk];

		st->fase -= StukFase[slagstuk];

		remove_piece(jij, slagstuk, slagveld);

		sleutel64 ^= Zobrist::psq[slagstuk][slagveld];
		st->materiaalSleutel ^= Zobrist::psq[slagstuk][stukAantal[slagstuk]];
		prefetch(threadInfo->materiaalTabel[st->materiaalSleutel ^ st->loperKleurSleutel]);

		if (piece_type(slagstuk) == BISHOP)
			compute_bishop_color_key();

		st->psq -= PST::psq[slagstuk][slagveld];

		st->remise50Zetten = 0;
	}

	sleutel64 ^= Zobrist::psq[stuk][van] ^ Zobrist::psq[stuk][naar];

	if (st->enpassantVeld != SQ_NONE)
	{
		sleutel64 ^= Zobrist::enpassant[lijn(st->enpassantVeld)];
		st->enpassantVeld = SQ_NONE;
	}

	if (st->rokadeMogelijkheden && (rokadeMask[van] | rokadeMask[naar]))
	{
		int8_t rokade = rokadeMask[van] | rokadeMask[naar];
		sleutel64 ^= Zobrist::rokade[st->rokadeMogelijkheden & rokade];
		st->rokadeMogelijkheden &= ~rokade;
	}

	if (move_type(zet) != CASTLING)
		move_piece(ik, stuk, van, naar);

	if (piece_type(stuk) == PAWN)
	{
		if ((int(naar) ^ int(van)) == 16
			&& (attack_from<PAWN>(naar - pawn_forward(ik), ik) & pieces(jij, PAWN)))
		{
			st->enpassantVeld = (van + naar) / 2;
			sleutel64 ^= Zobrist::enpassant[lijn(st->enpassantVeld)];
		}

		else if (zet >= Move(PROMOTION_N))
		{
			Piece promotie = make_piece(ik, promotion_piece(zet));

			assert(relative_rank(ik, naar) == RANK_8);
			assert(piece_type(promotie) >= KNIGHT && piece_type(promotie) <= QUEEN);

			remove_piece(ik, stuk, naar);
			put_piece(ik, promotie, naar);

			sleutel64 ^= Zobrist::psq[stuk][naar] ^ Zobrist::psq[promotie][naar];
			st->pionSleutel ^= Zobrist::psq[stuk][naar];
			st->materiaalSleutel ^= Zobrist::psq[promotie][stukAantal[promotie] - 1]
				^ Zobrist::psq[stuk][stukAantal[stuk]];

			if (piece_type(promotie) == BISHOP)
				compute_bishop_color_key();

			st->psq += PST::psq[promotie][naar] - PST::psq[stuk][naar];

			st->nietPionMateriaal[ik] += MateriaalWaarden[promotie];
			st->fase += StukFase[promotie];
		}

		st->pionSleutel ^= Zobrist::psq[stuk][van] ^ Zobrist::psq[stuk][naar];
		prefetch2(threadInfo->pionnenTabel[st->pionSleutel]);

		st->remise50Zetten = 0;
	}

	HoofdHash.prefetch_tuple(sleutel64);

	stukBB[ALL_PIECES] = kleurBB[WHITE] | kleurBB[BLACK];

	st->psq += PST::psq[stuk][naar] - PST::psq[stuk][van];

	st->geslagenStuk = slagstuk;
	st->bewogenStuk = piece_on_square(naar);
	st->vorigeZet = zet;
	st->counterZetWaarden = &numaInfo->counterZetStatistieken[stuk][naar];
	st->evalPositioneel = NO_EVAL;

	st->sleutel = sleutel64;

	aanZet = ~aanZet;
	st->schaakGevers = geeftSchaak ? attackers_to(koning(jij)) & pieces(ik) : 0;
	st->zetHerhaling = is_draw();
	compute_check_pins();

	assert(position_is_ok());
}


void Position::undo_move(Move zet)
{
	assert(is_ok(zet));
	trace_cancel_move();

	aanZet = ~aanZet;

	Color ik = aanZet;
	Square van = from_square(zet);
	Square naar = to_square(zet);
	Piece stuk = piece_on_square(naar);

	assert(empty_square(van) || move_type(zet) == CASTLING);
	assert(piece_type(st->geslagenStuk) != KING);
	assert(stuk == st->bewogenStuk);

	if (zet < Move(CASTLING))
	{
		move_piece(ik, stuk, naar, van);
		if (st->geslagenStuk)
			put_piece(~ik, st->geslagenStuk, naar);
	}
	else
	{
		if (zet >= Move(PROMOTION_N))
		{
			assert(relative_rank(ik, naar) == RANK_8);
			assert(piece_type(stuk) == promotion_piece(zet));
			assert(piece_type(stuk) >= KNIGHT && piece_type(stuk) <= QUEEN);

			remove_piece(ik, stuk, naar);
			stuk = make_piece(ik, PAWN);
			put_piece(ik, stuk, naar);
		}

		if (move_type(zet) == CASTLING)
		{
			Square vanT, naarT;
			do_castling<false>(ik, van, naar, vanT, naarT);
		}
		else
		{
			move_piece(ik, stuk, naar, van);

			if (st->geslagenStuk)
			{
				Square slagveld = naar;

				if (move_type(zet) == ENPASSANT)
				{
					slagveld -= pawn_forward(ik);

					assert(piece_type(stuk) == PAWN);
					assert(naar == (st - 1)->enpassantVeld);
					assert(relative_rank(ik, naar) == RANK_6);
					assert(piece_on_square(slagveld) == NO_PIECE);
					assert(st->geslagenStuk == make_piece(~ik, PAWN));
				}

				put_piece(~ik, st->geslagenStuk, slagveld);
			}
		}
	}
	stukBB[ALL_PIECES] = kleurBB[WHITE] | kleurBB[BLACK];

	st--;

	assert(position_is_ok());
}


template<bool Do>
void Position::do_castling(Color ik, Square van, Square naar, Square& vanT, Square& naarT)
{
	vanT = castling_rook_square(naar);
	naarT = relative_square(ik, vanT > van ? SQ_F1 : SQ_D1);

	if (!chess960)
	{
		move_piece(ik, make_piece(ik, KING), Do ? van : naar, Do ? naar : van);
		move_piece(ik, make_piece(ik, ROOK), Do ? vanT : naarT, Do ? naarT : vanT);
	}
	else
	{
		remove_piece(ik, make_piece(ik, KING), Do ? van : naar);
		remove_piece(ik, make_piece(ik, ROOK), Do ? vanT : naarT);
		bord[Do ? van : naar] = bord[Do ? vanT : naarT] = NO_PIECE;
		put_piece(ik, make_piece(ik, KING), Do ? naar : van);
		put_piece(ik, make_piece(ik, ROOK), Do ? naarT : vanT);
	}
}


void Position::do_null_move()
{
	assert(!check_givers());

	++knopen;
	trace_do_move(0);

	Key64 sleutel64 = st->sleutel ^ Zobrist::aanZet;
	if (st->enpassantVeld != SQ_NONE)
		sleutel64 ^= Zobrist::enpassant[lijn(st->enpassantVeld)];

	HoofdHash.prefetch_tuple(sleutel64);

	std::memcpy(st + 1, st, offsetof(PositionInfo, sleutel));
	st++;

	st->sleutel = sleutel64;
	st->remise50Zetten = (st - 1)->remise50Zetten + 1;
	st->afstandTotNullzet = 0;

	st->enpassantVeld = SQ_NONE;
	st->schaakGevers = 0;
	st->geslagenStuk = NO_PIECE; // niet echt nodig, overal waar geslagenStuk gebruikt wordt 
								  // is er een bescherming tegen voorafgaande null move
	st->vorigeZet = NULL_MOVE;
	st->counterZetWaarden = nullptr;
	st->evalPositioneel = (st - 1)->evalPositioneel;
	st->evalFactor = (st - 1)->evalFactor;

	aanZet = ~aanZet;
	st->zetHerhaling = is_draw();
	compute_check_pins();

	assert(position_is_ok());
}

void Position::undo_null_move()
{
	assert(!check_givers());
	trace_cancel_move();

	st--;
	aanZet = ~aanZet;
}


Key64 Position::key_after_move(Move zet) const
{
	Square van = from_square(zet);
	Square naar = to_square(zet);
	Piece stuk = piece_on_square(van);
	Piece slagstuk = piece_on_square(naar);
	Key64 k = st->sleutel ^ Zobrist::aanZet;

	if (slagstuk)
		k ^= Zobrist::psq[slagstuk][naar];

	return k ^ Zobrist::psq[stuk][naar] ^ Zobrist::psq[stuk][van];
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

// see_sign geeft >= 0 (geen exacte waarde) of < 0 (wel een exacte waarde!)
/*
SeeWaarde Stelling::see(Zet zet) const
{
	SeeWaarde waarde, best, slechtst;
	const SeeWaarde* see_waarde = see_waarden();

	assert(is_ok(zet));

	Veld van = van_veld(zet);
	Veld naar = naar_veld(zet);
	waarde = see_waarde[stuk_op_veld(naar)];
	if (see_waarde[stuk_op_veld(van)] <= waarde)
		return SEE_0;

	if (zet_type(zet) == ROKADE || zet_type(zet) == ENPASSANT)
		return SEE_0;

	best = waarde;
	waarde -= see_waarde[stuk_op_veld(van)];
	slechtst = waarde;

	const Kleur ik = stuk_kleur(stuk_op_veld(van));
	Bord bezet = stukken() ^ van;
	Bord aanvallers = aanvallers_naar(naar, bezet) & bezet;

	do {
		Bord mijnAanvallers = aanvallers & stukken(~ik);
		if (!mijnAanvallers)
			return best;
		//if (usePins) 
		//{
		// Bord pinned = stmAttackers & st->xRay[~stm];
		// while (pinned) {
		//  Square veld = pop_lsb(&pinned);
		//  if (occupied & st->pinner[veld]) {
		//	  stmAttackers ^= veld;
		//	  if (!stmAttackers)
		//		  return best;
		//  }
		// }
		//}

		Bord bb;
		StukType jouwstuk;
		if ((bb = mijnAanvallers & stukken(PION)))
			jouwstuk = PION;
		else if ((bb = mijnAanvallers & stukken(PAARD)))
			jouwstuk = PAARD;
		else if ((bb = mijnAanvallers & stukken(LOPER)))
			jouwstuk = LOPER;
		else if ((bb = mijnAanvallers & stukken(TOREN)))
			jouwstuk = TOREN;
		else if ((bb = mijnAanvallers & stukken(DAME)))
			jouwstuk = DAME;
		else
			return aanvallers & stukken(ik) ? best : slechtst;

		waarde += see_waarde[jouwstuk];
		best = std::min(best, waarde);
		if (slechtst >= best)
			return slechtst;

		bezet ^= (bb & -bb);
		if (!(jouwstuk & 1)) // PION, LOPER, DAME
			aanvallers |= aanval_bb_loper(naar, bezet) & (stukken(LOPER) | stukken(DAME));
		if (jouwstuk >= TOREN)
			aanvallers |= aanval_bb_toren(naar, bezet) & (stukken(TOREN) | stukken(DAME));
		aanvallers &= bezet;

		mijnAanvallers = aanvallers & stukken(ik);
		if (!mijnAanvallers)
			return slechtst;
		//if (usePins)
		//{
		// Bord pinned = stmAttackers & st->xRay[stm];
		// while (pinned) {
		//  Square veld = pop_lsb(&pinned);
		//  if (occupied & st->pinner[veld]) {
		//	  stmAttackers ^= veld;
		//	  if (!stmAttackers)
		//		  return worst;
		//  }
		// }
		//}

		if ((bb = mijnAanvallers & stukken(PION)))
			jouwstuk = PION;
		else if ((bb = mijnAanvallers & stukken(PAARD)))
			jouwstuk = PAARD;
		else if ((bb = mijnAanvallers & stukken(LOPER)))
			jouwstuk = LOPER;
		else if ((bb = mijnAanvallers & stukken(TOREN)))
			jouwstuk = TOREN;
		else if ((bb = mijnAanvallers & stukken(DAME)))
			jouwstuk = DAME;
		else
			return aanvallers & stukken(~ik) ? slechtst : best;

		waarde -= see_waarde[jouwstuk];
		slechtst = std::max(slechtst, waarde);
		if (slechtst >= best)
			return best;
		if (slechtst >= 0)
			return SEE_0;

		bezet ^= (bb & -bb);
		if (!(jouwstuk & 1)) // PION, LOPER, DAME
			aanvallers |= aanval_bb_loper(naar, bezet) & (stukken(LOPER) | stukken(DAME));
		if (jouwstuk >= TOREN)
			aanvallers |= aanval_bb_toren(naar, bezet) & (stukken(TOREN) | stukken(DAME));
		aanvallers &= bezet;
	} while (true);
}
*/

// Test of see(zet) >= value
//template <bool UsePins>
bool Position::see_test(Move zet, SeeValue limiet) const
{
	if (move_type(zet) == CASTLING)
		return 0 >= limiet;

	const SeeValue* see_waarde = see_values();
	Square van = from_square(zet);
	Square naar = to_square(zet);
	Bitboard bezet = pieces();
	const Color ik = piece_color(piece_on_square(van));

	int waarde = see_waarde[piece_on_square(naar)] - limiet;
	if (move_type(zet) == ENPASSANT)
	{
		bezet ^= naar - pawn_forward(ik);
		waarde += see_waarde[PAWN];
	}
	if (waarde < 0)
		return false;

	waarde -= see_waarde[piece_on_square(van)];
	if (waarde >= 0)
		return true;

	bezet ^= van;
	Bitboard aanvallers = attackers_to(naar, bezet) & bezet;

	do {
		Bitboard mijnAanvallers = aanvallers & pieces(~ik);
		if (!mijnAanvallers)
			return true;
		//if (UsePins)
		{
			Bitboard gepend = mijnAanvallers & st->xRay[~ik];
			while (gepend) {
				Square veld = pop_lsb(&gepend);
				if (bezet & st->penning_door[veld])
				{
					mijnAanvallers ^= veld;
					if (!mijnAanvallers)
						return true;
				}
			}
		}

		Bitboard bb;
		PieceType slagstuk;
		if ((bb = mijnAanvallers & pieces(PAWN)))
			slagstuk = PAWN;
		else if ((bb = mijnAanvallers & pieces(KNIGHT)))
			slagstuk = KNIGHT;
		else if ((bb = mijnAanvallers & pieces(BISHOP)))
			slagstuk = BISHOP;
		else if ((bb = mijnAanvallers & pieces(ROOK)))
			slagstuk = ROOK;
		else if ((bb = mijnAanvallers & pieces(QUEEN)))
			slagstuk = QUEEN;
		else
			return aanvallers & pieces(ik);

		waarde += see_waarde[slagstuk];
		if (waarde < 0) 
			return false;
		bezet ^= (bb & -bb);
		if (!(slagstuk & 1)) // PION, LOPER, DAME
			aanvallers |= attack_bb_bishop(naar, bezet) & (pieces(BISHOP) | pieces(QUEEN));
		if (slagstuk >= ROOK)
			aanvallers |= attack_bb_rook(naar, bezet) & (pieces(ROOK) | pieces(QUEEN));
		aanvallers &= bezet;

		mijnAanvallers = aanvallers & pieces(ik);
		if (!mijnAanvallers)
			return false;
		//if (UsePins)
		{
			Bitboard gepend = mijnAanvallers & st->xRay[ik];
			while (gepend) {
				Square veld = pop_lsb(&gepend);
				if (bezet & st->penning_door[veld])
				{
					mijnAanvallers ^= veld;
					if (!mijnAanvallers)
						return false;
				}
			}
		}
		
		if ((bb = mijnAanvallers & pieces(PAWN)))
			slagstuk = PAWN;
		else if ((bb = mijnAanvallers & pieces(KNIGHT)))
			slagstuk = KNIGHT;
		else if ((bb = mijnAanvallers & pieces(BISHOP)))
			slagstuk = BISHOP;
		else if ((bb = mijnAanvallers & pieces(ROOK)))
			slagstuk = ROOK;
		else if ((bb = mijnAanvallers & pieces(QUEEN)))
			slagstuk = QUEEN;
		else
			return !(aanvallers & pieces(~ik));

		waarde -= see_waarde[slagstuk];
		if (waarde >= 0)
			return true;
		bezet ^= (bb & -bb);
		if (!(slagstuk & 1)) // PION, LOPER, DAME
			aanvallers |= attack_bb_bishop(naar, bezet) & (pieces(BISHOP) | pieces(QUEEN));
		if (slagstuk >= ROOK)
			aanvallers |= attack_bb_rook(naar, bezet) & (pieces(ROOK) | pieces(QUEEN));
		aanvallers &= bezet;
	} while (true);
}


bool Position::is_draw() const
{
	if (st->remise50Zetten >= 2 * Threads.vijftigZettenAfstand)
	{
		if (st->remise50Zetten == 100)
			return !st->schaakGevers || AtLeastOneLegalMove(*this);
		return true;
	}

	int n = std::min(st->remise50Zetten, st->afstandTotNullzet) - 4;
	if (n < 0)
		return false;

	PositionInfo* stp = st - 4;
	do
	{
		if (stp->sleutel == st->sleutel)
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
			if ((aanZet != WHITE && aanZet != BLACK)
				|| piece_on_square(koning(WHITE)) != W_KING
				|| piece_on_square(koning(BLACK)) != B_KING
				|| (enpassant_square() != SQ_NONE
					&& relative_rank(aanZet, enpassant_square()) != RANK_6))
				return false;

		if (step == King)
			if (std::count(bord, bord + SQUARE_N, W_KING) != 1
				|| std::count(bord, bord + SQUARE_N, B_KING) != 1
				|| attackers_to(koning(~aanZet)) & pieces(aanZet))
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
			for (Color kleur = WHITE; kleur <= BLACK; ++kleur)
				for (PieceType stuk = KING; stuk <= QUEEN; ++stuk)
				{
					if (stukAantal[make_piece(kleur, stuk)] != popcount(pieces(kleur, stuk)))
						return false;

					for (int i = 0; i < stukAantal[make_piece(kleur, stuk)]; ++i)
						if (bord[stukLijst[make_piece(kleur, stuk)][i]] != make_piece(kleur, stuk)
							|| stukIndex[stukLijst[make_piece(kleur, stuk)][i]] != i)
							return false;
				}

		if (step == Castling)
			for (Color kleur = WHITE; kleur <= BLACK; ++kleur)
				for (int n = 0; n < 2; n++)
				{
					CastlingRight rokade = CastlingRight(WHITE_SHORT << (n + 2 * kleur));

					if (!castling_possible(rokade))
						continue;

					if (piece_on_square(rokadeTorenVeld[rokade]) != make_piece(kleur, ROOK)
						|| rokadeMask[rokadeTorenVeld[rokade]] != rokade
						|| (rokadeMask[koning(kleur)] & (rokade)) != rokade)
						return false;
				}
	}

	return true;
}


std::string Position::validate_position() const
{
	if (aantal(WHITE, KING) != 1)
		return "Exactly one white king required";
	if (aantal(BLACK, KING) != 1)
		return "Exactly one black king required";

	if (popcount(pieces(WHITE)) > 16)
		return "Too many white pieces";
	if (popcount(pieces(BLACK)) > 16)
		return "Too many black pieces";

	if (aantal(WHITE, PAWN) > 8)
		return "Too many white pawns";
	if (aantal(BLACK, PAWN) > 8)
		return "Too many black pawns";

	if (aantal(WHITE, QUEEN) + aantal(WHITE, PAWN) > 9)
		return "Too many white queens";
	if (aantal(BLACK, QUEEN) + aantal(BLACK, PAWN) > 9)
		return "Too many black queens";

	if (aantal(WHITE, ROOK) + aantal(WHITE, PAWN) > 10)
		return "Too many white rooks";
	if (aantal(BLACK, ROOK) + aantal(BLACK, PAWN) > 10)
		return "Too many black rooks";

	if (aantal(WHITE, BISHOP) + aantal(WHITE, PAWN) > 10)
		return "Too many white bishops";
	if (aantal(BLACK, BISHOP) + aantal(BLACK, PAWN) > 10)
		return "Too many black bishops";

	if (aantal(WHITE, KNIGHT) + aantal(WHITE, PAWN) > 10)
		return "Too many white knights";
	if (aantal(BLACK, KNIGHT) + aantal(BLACK, PAWN) > 10)
		return "Too many black knights";

	if (pieces(PAWN) & (Rank1BB | Rank8BB))
		return "Pawn at rank 1 or 8";

	return "";
}

Square Position::compute_threat() const
{
	if (!st->counterZetWaarden)
		return SQ_NONE;

	Square naar = to_square(st->vorigeZet);
	switch (st->bewogenStuk)
	{
	case W_PAWN:
		if (Bitboard b = PionAanval[WHITE][naar] & pieces_except(BLACK, PAWN))
			return lsb(b);
		break;
	case B_PAWN:
		if (Bitboard b = PionAanval[BLACK][naar] & pieces_except(WHITE, PAWN))
			return msb(b);
		break;
	case W_KNIGHT:
		if (Bitboard b = LegeAanval[KNIGHT][naar] & pieces(BLACK, ROOK, QUEEN))
			return lsb(b);
		break;
	case B_KNIGHT:
		if (Bitboard b = LegeAanval[KNIGHT][naar] & pieces(WHITE, ROOK, QUEEN))
			return msb(b);
		break;
	case W_BISHOP:
		// bij L en T eventueel testen dat de dreiging al niet eerder bestond
		// (vorigeZet in dezelfde richting als dreiging)
		if (Bitboard b = attack_bb_bishop(naar, pieces()) & pieces(BLACK, ROOK, QUEEN))
			return lsb(b);
		break;
	case B_BISHOP:
		if (Bitboard b = attack_bb_bishop(naar, pieces()) & pieces(WHITE, ROOK, QUEEN))
			return msb(b);
		break;
	case W_ROOK:
		if (Bitboard b = attack_bb_rook(naar, pieces()) & pieces(BLACK, QUEEN))
			return lsb(b);
		break;
	case B_ROOK:
		if (Bitboard b = attack_bb_rook(naar, pieces()) & pieces(WHITE, QUEEN))
			return msb(b);
		break;
	default:
		break;
	}

	return SQ_NONE;
}

