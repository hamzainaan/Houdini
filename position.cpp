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

Key64 Position::uitzondering_sleutel(Move zet) const
{ 
	return Zobrist::psq[W_KING][van_veld(zet)] ^ Zobrist::psq[B_KING][naar_veld(zet)];
	//return zet;
}

Key64 Position::remise50_sleutel() const
{
	return Zobrist::hash_50move[st->remise50Zetten >> 2];
}


namespace
{
	const string PieceToChar(" KPNBRQ  kpnbrq");
} // namespace


void Position::bereken_schaak_penningen() const
{
	bereken_penningen<WHITE>();
	bereken_penningen<BLACK>();
	Square veldK = koning(~aanZet);
	st->schaakVelden[PAWN]  = aanval_van<PAWN>(veldK, ~aanZet);
	st->schaakVelden[KNIGHT] = aanval_van<KNIGHT>(veldK);
	st->schaakVelden[BISHOP] = aanval_van<BISHOP>(veldK);
	st->schaakVelden[ROOK] = aanval_van<ROOK>(veldK);
	st->schaakVelden[QUEEN]  = st->schaakVelden[BISHOP] | st->schaakVelden[ROOK];
	st->schaakVelden[KING] = 0;
}

template <Color kleur>
void Position::bereken_penningen() const
{
	Bitboard penningGevers, result = 0;
	Square veldK = koning(kleur);

	penningGevers = (LegeAanval[ROOK][veldK] & stukken(~kleur, QUEEN, ROOK))
		          | (LegeAanval[BISHOP][veldK] & stukken(~kleur, QUEEN, BISHOP));

	while (penningGevers)
	{
		Square veld = pop_lsb(&penningGevers);
		Bitboard b = bb_tussen(veldK, veld) & stukken();

		if (b && !meer_dan_een(b))
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
			os << " | " << PieceToChar[pos.stuk_op_veld(maak_veld(f, r))];

		os << " |\n +---+---+---+---+---+---+---+---+\n";
	}

	os << "\nFen: " << pos.fen() << "\n";

	return os;
}


void Position::initialisatie()
{
	PRNG rng(19680708);

	for (Color kleur = WHITE; kleur <= BLACK; ++kleur)
		for (PieceType stuk = KING; stuk <= QUEEN; ++stuk)
			for (Square veld = SQ_A1; veld <= SQ_H8; ++veld)
				Zobrist::psq[maak_stuk(kleur, stuk)][veld] = rng.rand<Key64>();

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
			zet_stuk(stuk_kleur(Piece(idx)), Piece(idx), sq);
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
		Piece toren = maak_stuk(kleur, ROOK);

		token = char(toupper(token));

		if (token == 'K')
			for (rsq = relatief_veld(kleur, SQ_H1); stuk_op_veld(rsq) != toren; --rsq) {}

		else if (token == 'Q')
			for (rsq = relatief_veld(kleur, SQ_A1); stuk_op_veld(rsq) != toren; ++rsq) {}

		else if (token >= 'A' && token <= 'H')
			rsq = maak_veld(File(token - 'A'), relatieve_rij(kleur, RANK_1));

		else
			continue;

		set_rokade_mogelijkheid(kleur, rsq);
	}

	// en-passant
	if (((ss >> col) && (col >= 'a' && col <= 'h'))
		&& ((ss >> row) && (row == '3' || row == '6')))
	{
		st->enpassantVeld = maak_veld(File(col - 'a'), Rank(row - '1'));

		if (!(aanvallers_naar(st->enpassantVeld) & stukken(aanZet, PAWN)))
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
	set_stelling_info(st);
	bereken_schaak_penningen();

	assert(stelling_is_ok());

	return *this;
}


void Position::kopieer_stelling(const Position* pos, Thread* th, PositionInfo* copyState)
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
	assert(stelling_is_ok());
}


void Position::set_rokade_mogelijkheid(Color kleur, Square vanT)
{
	Square vanK = koning(kleur);
	CastlingRight rokade = CastlingRight(WHITE_SHORT << ((vanK >= vanT) + 2 * kleur));

	Square naarK = relatief_veld(kleur, vanK < vanT ? SQ_G1 : SQ_C1);
	Square naarT = relatief_veld(kleur, vanK < vanT ? SQ_F1 : SQ_D1);

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

	if (vanK != relatief_veld(kleur, SQ_E1))
		chess960 = true;
	if (vanK < vanT && vanT != relatief_veld(kleur, SQ_H1))
		chess960 = true;
	if (vanK >= vanT && vanT != relatief_veld(kleur, SQ_A1))
		chess960 = true;
}


void Position::bereken_loper_kleur_sleutel() const
{
	Key64 key = 0;
	if (stukken(WHITE, BISHOP) & DarkSquares)
		key ^= 0xF3094B57AC4789A2;
	if (stukken(WHITE, BISHOP) & ~DarkSquares)
		key ^= 0x89A2F3094B57AC47;
	if (stukken(BLACK, BISHOP) & DarkSquares)
		key ^= 0xAC4789A2F3094B57;
	if (stukken(BLACK, BISHOP) & ~DarkSquares)
		key ^= 0x4B57AC4789A2F309;
	st->loperKleurSleutel = key;
}


void Position::set_stelling_info(PositionInfo* si) const
{
	si->sleutel = si->materiaalSleutel = 0;
	si->nietPionMateriaal[WHITE] = si->nietPionMateriaal[BLACK] = MATL_0;
	si->psq = SCORE_ZERO;
	si->fase = 0;

	si->schaakGevers = aanvallers_naar(koning(aanZet)) & stukken(~aanZet);

	for (Bitboard b = stukken(); b; )
	{
		Square veld = pop_lsb(&b);
		Piece stuk = stuk_op_veld(veld);
		si->sleutel ^= Zobrist::psq[stuk][veld];
		si->psq += PST::psq[stuk][veld];
		si->fase += StukFase[stuk_type(stuk)];
	}

	if (si->enpassantVeld != SQ_NONE)
		si->sleutel ^= Zobrist::enpassant[lijn(si->enpassantVeld)];

	if (aanZet == BLACK)
		si->sleutel ^= Zobrist::aanZet;

	si->sleutel ^= Zobrist::rokade[si->rokadeMogelijkheden];

	si->pionSleutel = 0x1234567890ABCDEF;
	for (Bitboard b = stukken(PAWN); b; )
	{
		Square veld = pop_lsb(&b);
		si->pionSleutel ^= Zobrist::psq[stuk_op_veld(veld)][veld];
	}

	for (Color kleur = WHITE; kleur <= BLACK; ++kleur)
		for (PieceType stuk = KING; stuk <= QUEEN; ++stuk)
			for (int cnt = 0; cnt < stukAantal[maak_stuk(kleur, stuk)]; ++cnt)
				si->materiaalSleutel ^= Zobrist::psq[maak_stuk(kleur, stuk)][cnt];

	bereken_loper_kleur_sleutel();

	for (Color kleur = WHITE; kleur <= BLACK; ++kleur)
		for (PieceType stuk = KNIGHT; stuk <= QUEEN; ++stuk)
			si->nietPionMateriaal[kleur] += MateriaalWaarden[stuk] * (int)stukAantal[maak_stuk(kleur, stuk)];
}


const string Position::fen() const
{
	int emptyCnt;
	std::ostringstream ss;

	for (Rank r = RANK_8; ; --r)
	{
		for (File f = FILE_A; f <= FILE_H; ++f)
		{
			for (emptyCnt = 0; f <= FILE_H && leeg_veld(maak_veld(f, r)); ++f)
				++emptyCnt;

			if (emptyCnt)
				ss << emptyCnt;

			if (f <= FILE_H)
				ss << PieceToChar[stuk_op_veld(maak_veld(f, r))];
		}

		if (r == RANK_1)
			break;

		ss << '/';
	}

	ss << (aanZet == WHITE ? " w " : " b ");

	if (rokade_mogelijk(WHITE_SHORT))
		ss << (chess960 ? char('A' + lijn(rokade_toren_veld(SQ_G1))) : 'K');

	if (rokade_mogelijk(WHITE_LONG))
		ss << (chess960 ? char('A' + lijn(rokade_toren_veld(SQ_C1))) : 'Q');

	if (rokade_mogelijk(BLACK_SHORT))
		ss << (chess960 ? char('a' + lijn(rokade_toren_veld(SQ_G8))) : 'k');

	if (rokade_mogelijk(BLACK_LONG))
		ss << (chess960 ? char('a' + lijn(rokade_toren_veld(SQ_C8))) : 'q');

	if (!rokade_mogelijkheden(WHITE) && !rokade_mogelijkheden(BLACK))
		ss << '-';

	ss << (enpassant_veld() == SQ_NONE ? " - " : " " + UCI::veld(enpassant_veld()) + " ")
		<< st->remise50Zetten << " " << 1 + (partijPly - (aanZet == BLACK)) / 2;

	return ss.str();
}


GamePhase Position::partij_fase() const
{
	return GamePhase(std::max(0, std::min((int)MIDDLEGAME_PHASE, (int)st->fase - 6)));
}


Bitboard Position::aanvallers_naar(Square veld, Bitboard bezet) const
{
	return (aanval_van<PAWN>(veld, BLACK) & stukken(WHITE, PAWN))
		| (aanval_van<PAWN>(veld, WHITE)    & stukken(BLACK, PAWN))
		| (aanval_van<KNIGHT>(veld)        & stukken(KNIGHT))
		| (aanval_bb_toren(veld, bezet)   & stukken(ROOK, QUEEN))
		| (aanval_bb_loper(veld, bezet)   & stukken(BISHOP, QUEEN))
		| (aanval_van<KING>(veld)       & stukken(KING));
}


bool Position::legale_zet(Move zet) const
{
	assert(is_ok(zet));

	Color ik = aanZet;
	Square van = van_veld(zet);

	assert(stuk_kleur(bewogen_stuk(zet)) == ik);
	assert(stuk_op_veld(koning(ik)) == maak_stuk(ik, KING));

	if (zet_type(zet) == ENPASSANT)
	{
		Square veldK = koning(ik);
		Square naar = naar_veld(zet);
		Square slagveld = naar - pion_vooruit(ik);
		Bitboard bezet = (stukken() ^ van ^ slagveld) | naar;

		assert(naar == enpassant_veld());
		assert(bewogen_stuk(zet) == maak_stuk(ik, PAWN));
		assert(stuk_op_veld(slagveld) == maak_stuk(~ik, PAWN));
		assert(stuk_op_veld(naar) == NO_PIECE);

		return !(aanval_bb_toren(veldK, bezet) & stukken(~ik, QUEEN, ROOK))
			&& !(aanval_bb_loper(veldK, bezet) & stukken(~ik, QUEEN, BISHOP));
	}

	if (stuk_type(stuk_op_veld(van)) == KING)
		return zet_type(zet) == CASTLING || !(aanvallers_naar(naar_veld(zet)) & stukken(~ik));

	return !(st->xRay[aanZet] & van) || op_een_lijn(van, naar_veld(zet), koning(ik));
}


bool Position::geldige_zet(const Move zet) const
{
	Color ik = aanZet;
	Square van = van_veld(zet);

	if (!(stukken(ik) & van))
		return false;

	Square naar = naar_veld(zet);
	PieceType stuk = stuk_type(bewogen_stuk(zet));

	// rokade 0,8%  enpassant 0,03%  promotie 0,6%
	if (zet >= Move(CASTLING))
	{
		if (zet >= Move(PROMOTION_N))
		{
			if (stuk != PAWN)
				return false;
		}
		else if (zet < Move(ENPASSANT)) // rokade
			return LegaleZettenLijstBevatRokade(*this, zet);
		else  // en-passant
			return LegaleZettenLijstBevatZet(*this, zet);
	}
	else
	{
		//if (stuk != stuk_van_zet(zet))
		//	return false;
	}

	if (stukken(ik) & naar)
		return false;

	if (stuk == PAWN)
	{
		if ((zet >= Move(PROMOTION_N)) ^ (rij(naar) == relatieve_rij(ik, RANK_8)))
			return false;

		if (!(aanval_van<PAWN>(van, ik) & stukken(~ik) & naar)
			&& !((van + pion_vooruit(ik) == naar) && leeg_veld(naar))
			&& !((van + 2 * pion_vooruit(ik) == naar)
				&& (rij(van) == relatieve_rij(ik, RANK_2))
				&& leeg_veld(naar)
				&& leeg_veld(naar - pion_vooruit(ik))))
			return false;
	}
	else if (!(aanval_van(stuk, van) & naar))
	//else if (bb_tussen(van, naar) & stukken())
		return false;

	if (schaak_gevers())
	{
		if (stuk != KING)
		{
			if (meer_dan_een(schaak_gevers()))
				return false;

			if (!((bb_tussen(lsb(schaak_gevers()), koning(ik)) | schaak_gevers()) & naar))
				return false;
		}
		else if (aanvallers_naar(naar, stukken() ^ van) & stukken(~ik))
			return false;
	}

	return true;
}


bool Position::geeft_schaak(Move zet) const
{
	assert(is_ok(zet));
	assert(stuk_kleur(bewogen_stuk(zet)) == aanZet);

	Square van = van_veld(zet);
	Square naar = naar_veld(zet);
	Square veldK = koning(~aanZet);

	if (st->schaakVelden[stuk_type(stuk_op_veld(van))] & naar)
		return true;

	if ((st->xRay[~aanZet] & van) && !op_een_lijn(van, naar, veldK))
		return true;

	if (zet < Move(CASTLING))
		return false;

	if (zet >= Move(PROMOTION_N))
		return aanval_bb(promotie_stuk(zet), naar, stukken() ^ van) & veldK;

	if (zet < Move(ENPASSANT))  // rokade
	{
		Square vanT = rokade_toren_veld(naar);
		Square naarT = relatief_veld(aanZet, vanT > van ? SQ_F1 : SQ_D1);

		return (LegeAanval[ROOK][naarT] & veldK)
			&& (aanval_bb_toren(naarT, (stukken() ^ van ^ vanT) | naarT | naar) & veldK);
	}

	// en-passant
	{
		Square ep_veld = maak_veld(lijn(naar), rij(van));
		Bitboard b = (stukken() ^ van ^ ep_veld) | naar;

		return  (aanval_bb_toren(veldK, b) & stukken(aanZet, QUEEN, ROOK))
			| (aanval_bb_loper(veldK, b) & stukken(aanZet, QUEEN, BISHOP));
	}
}


void Position::speel_zet(Move zet)
{
	bool geeftSchaak = zet < Move(CASTLING) && !aftrek_schaak_mogelijk()
		? st->schaakVelden[stuk_type(stuk_op_veld(van_veld(zet)))] & naar_veld(zet)
		: geeft_schaak(zet);

	speel_zet(zet, geeftSchaak);
}

void Position::speel_zet(Move zet, bool geeftSchaak)
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
	Square van = van_veld(zet);
	Square naar = naar_veld(zet);
	Piece stuk = stuk_op_veld(van);
	Piece slagstuk;

	assert(stuk_kleur(stuk) == ik);

	if (zet_type(zet) == CASTLING)
	{
		assert(stuk_type(stuk) == KING);

		Square vanT, naarT;
		speel_rokade<true>(ik, van, naar, vanT, naarT);

		slagstuk = NO_PIECE;
		Piece mijnToren = maak_stuk(ik, ROOK);
		st->psq += PST::psq[mijnToren][naarT] - PST::psq[mijnToren][vanT];
		sleutel64 ^= Zobrist::psq[mijnToren][vanT] ^ Zobrist::psq[mijnToren][naarT];
	}
	else
		slagstuk = zet_type(zet) == ENPASSANT ? maak_stuk(jij, PAWN) : stuk_op_veld(naar);

	if (slagstuk)
	{
		assert(stuk_type(slagstuk) != KING);
		assert(stuk_kleur(slagstuk) == jij);
		Square slagveld = naar;

		if (stuk_type(slagstuk) == PAWN)
		{
			if (zet_type(zet) == ENPASSANT)
			{
				slagveld -= pion_vooruit(ik);

				assert(stuk_type(stuk) == PAWN);
				assert(naar == st->enpassantVeld);
				assert(relatieve_rij(ik, naar) == RANK_6);
				assert(stuk_op_veld(naar) == NO_PIECE);
				assert(stuk_op_veld(slagveld) == maak_stuk(jij, PAWN));

				bord[slagveld] = NO_PIECE;
			}

			st->pionSleutel ^= Zobrist::psq[slagstuk][slagveld];
		}
		else
			st->nietPionMateriaal[jij] -= MateriaalWaarden[slagstuk];

		st->fase -= StukFase[slagstuk];

		verwijder_stuk(jij, slagstuk, slagveld);

		sleutel64 ^= Zobrist::psq[slagstuk][slagveld];
		st->materiaalSleutel ^= Zobrist::psq[slagstuk][stukAantal[slagstuk]];
		prefetch(threadInfo->materiaalTabel[st->materiaalSleutel ^ st->loperKleurSleutel]);

		if (stuk_type(slagstuk) == BISHOP)
			bereken_loper_kleur_sleutel();

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

	if (zet_type(zet) != CASTLING)
		verplaats_stuk(ik, stuk, van, naar);

	if (stuk_type(stuk) == PAWN)
	{
		if ((int(naar) ^ int(van)) == 16
			&& (aanval_van<PAWN>(naar - pion_vooruit(ik), ik) & stukken(jij, PAWN)))
		{
			st->enpassantVeld = (van + naar) / 2;
			sleutel64 ^= Zobrist::enpassant[lijn(st->enpassantVeld)];
		}

		else if (zet >= Move(PROMOTION_N))
		{
			Piece promotie = maak_stuk(ik, promotie_stuk(zet));

			assert(relatieve_rij(ik, naar) == RANK_8);
			assert(stuk_type(promotie) >= KNIGHT && stuk_type(promotie) <= QUEEN);

			verwijder_stuk(ik, stuk, naar);
			zet_stuk(ik, promotie, naar);

			sleutel64 ^= Zobrist::psq[stuk][naar] ^ Zobrist::psq[promotie][naar];
			st->pionSleutel ^= Zobrist::psq[stuk][naar];
			st->materiaalSleutel ^= Zobrist::psq[promotie][stukAantal[promotie] - 1]
				^ Zobrist::psq[stuk][stukAantal[stuk]];

			if (stuk_type(promotie) == BISHOP)
				bereken_loper_kleur_sleutel();

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
	st->bewogenStuk = stuk_op_veld(naar);
	st->vorigeZet = zet;
	st->counterZetWaarden = &numaInfo->counterZetStatistieken[stuk][naar];
	st->evalPositioneel = NO_EVAL;

	st->sleutel = sleutel64;

	aanZet = ~aanZet;
	st->schaakGevers = geeftSchaak ? aanvallers_naar(koning(jij)) & stukken(ik) : 0;
	st->zetHerhaling = is_remise();
	bereken_schaak_penningen();

	assert(stelling_is_ok());
}


void Position::neem_zet_terug(Move zet)
{
	assert(is_ok(zet));
	trace_cancel_move();

	aanZet = ~aanZet;

	Color ik = aanZet;
	Square van = van_veld(zet);
	Square naar = naar_veld(zet);
	Piece stuk = stuk_op_veld(naar);

	assert(leeg_veld(van) || zet_type(zet) == CASTLING);
	assert(stuk_type(st->geslagenStuk) != KING);
	assert(stuk == st->bewogenStuk);

	if (zet < Move(CASTLING))
	{
		verplaats_stuk(ik, stuk, naar, van);
		if (st->geslagenStuk)
			zet_stuk(~ik, st->geslagenStuk, naar);
	}
	else
	{
		if (zet >= Move(PROMOTION_N))
		{
			assert(relatieve_rij(ik, naar) == RANK_8);
			assert(stuk_type(stuk) == promotie_stuk(zet));
			assert(stuk_type(stuk) >= KNIGHT && stuk_type(stuk) <= QUEEN);

			verwijder_stuk(ik, stuk, naar);
			stuk = maak_stuk(ik, PAWN);
			zet_stuk(ik, stuk, naar);
		}

		if (zet_type(zet) == CASTLING)
		{
			Square vanT, naarT;
			speel_rokade<false>(ik, van, naar, vanT, naarT);
		}
		else
		{
			verplaats_stuk(ik, stuk, naar, van);

			if (st->geslagenStuk)
			{
				Square slagveld = naar;

				if (zet_type(zet) == ENPASSANT)
				{
					slagveld -= pion_vooruit(ik);

					assert(stuk_type(stuk) == PAWN);
					assert(naar == (st - 1)->enpassantVeld);
					assert(relatieve_rij(ik, naar) == RANK_6);
					assert(stuk_op_veld(slagveld) == NO_PIECE);
					assert(st->geslagenStuk == maak_stuk(~ik, PAWN));
				}

				zet_stuk(~ik, st->geslagenStuk, slagveld);
			}
		}
	}
	stukBB[ALL_PIECES] = kleurBB[WHITE] | kleurBB[BLACK];

	st--;

	assert(stelling_is_ok());
}


template<bool Do>
void Position::speel_rokade(Color ik, Square van, Square naar, Square& vanT, Square& naarT)
{
	vanT = rokade_toren_veld(naar);
	naarT = relatief_veld(ik, vanT > van ? SQ_F1 : SQ_D1);

	if (!chess960)
	{
		verplaats_stuk(ik, maak_stuk(ik, KING), Do ? van : naar, Do ? naar : van);
		verplaats_stuk(ik, maak_stuk(ik, ROOK), Do ? vanT : naarT, Do ? naarT : vanT);
	}
	else
	{
		verwijder_stuk(ik, maak_stuk(ik, KING), Do ? van : naar);
		verwijder_stuk(ik, maak_stuk(ik, ROOK), Do ? vanT : naarT);
		bord[Do ? van : naar] = bord[Do ? vanT : naarT] = NO_PIECE;
		zet_stuk(ik, maak_stuk(ik, KING), Do ? naar : van);
		zet_stuk(ik, maak_stuk(ik, ROOK), Do ? naarT : vanT);
	}
}


void Position::speel_null_zet()
{
	assert(!schaak_gevers());

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
	st->zetHerhaling = is_remise();
	bereken_schaak_penningen();

	assert(stelling_is_ok());
}

void Position::neem_null_terug()
{
	assert(!schaak_gevers());
	trace_cancel_move();

	st--;
	aanZet = ~aanZet;
}


Key64 Position::sleutel_na_zet(Move zet) const
{
	Square van = van_veld(zet);
	Square naar = naar_veld(zet);
	Piece stuk = stuk_op_veld(van);
	Piece slagstuk = stuk_op_veld(naar);
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

const SeeValue* Position::see_waarden() const
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
	if (zet_type(zet) == CASTLING)
		return 0 >= limiet;

	const SeeValue* see_waarde = see_waarden();
	Square van = van_veld(zet);
	Square naar = naar_veld(zet);
	Bitboard bezet = stukken();
	const Color ik = stuk_kleur(stuk_op_veld(van));

	int waarde = see_waarde[stuk_op_veld(naar)] - limiet;
	if (zet_type(zet) == ENPASSANT)
	{
		bezet ^= naar - pion_vooruit(ik);
		waarde += see_waarde[PAWN];
	}
	if (waarde < 0)
		return false;

	waarde -= see_waarde[stuk_op_veld(van)];
	if (waarde >= 0)
		return true;

	bezet ^= van;
	Bitboard aanvallers = aanvallers_naar(naar, bezet) & bezet;

	do {
		Bitboard mijnAanvallers = aanvallers & stukken(~ik);
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
		if ((bb = mijnAanvallers & stukken(PAWN)))
			slagstuk = PAWN;
		else if ((bb = mijnAanvallers & stukken(KNIGHT)))
			slagstuk = KNIGHT;
		else if ((bb = mijnAanvallers & stukken(BISHOP)))
			slagstuk = BISHOP;
		else if ((bb = mijnAanvallers & stukken(ROOK)))
			slagstuk = ROOK;
		else if ((bb = mijnAanvallers & stukken(QUEEN)))
			slagstuk = QUEEN;
		else
			return aanvallers & stukken(ik);

		waarde += see_waarde[slagstuk];
		if (waarde < 0) 
			return false;
		bezet ^= (bb & -bb);
		if (!(slagstuk & 1)) // PION, LOPER, DAME
			aanvallers |= aanval_bb_loper(naar, bezet) & (stukken(BISHOP) | stukken(QUEEN));
		if (slagstuk >= ROOK)
			aanvallers |= aanval_bb_toren(naar, bezet) & (stukken(ROOK) | stukken(QUEEN));
		aanvallers &= bezet;

		mijnAanvallers = aanvallers & stukken(ik);
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
		
		if ((bb = mijnAanvallers & stukken(PAWN)))
			slagstuk = PAWN;
		else if ((bb = mijnAanvallers & stukken(KNIGHT)))
			slagstuk = KNIGHT;
		else if ((bb = mijnAanvallers & stukken(BISHOP)))
			slagstuk = BISHOP;
		else if ((bb = mijnAanvallers & stukken(ROOK)))
			slagstuk = ROOK;
		else if ((bb = mijnAanvallers & stukken(QUEEN)))
			slagstuk = QUEEN;
		else
			return !(aanvallers & stukken(~ik));

		waarde -= see_waarde[slagstuk];
		if (waarde >= 0)
			return true;
		bezet ^= (bb & -bb);
		if (!(slagstuk & 1)) // PION, LOPER, DAME
			aanvallers |= aanval_bb_loper(naar, bezet) & (stukken(BISHOP) | stukken(QUEEN));
		if (slagstuk >= ROOK)
			aanvallers |= aanval_bb_toren(naar, bezet) & (stukken(ROOK) | stukken(QUEEN));
		aanvallers &= bezet;
	} while (true);
}


bool Position::is_remise() const
{
	if (st->remise50Zetten >= 2 * Threads.vijftigZettenAfstand)
	{
		if (st->remise50Zetten == 100)
			return !st->schaakGevers || MinstensEenLegaleZet(*this);
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


void Position::draaiBord()
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

	set(f, is_chess960(), mijn_thread());

	assert(stelling_is_ok());
}


bool Position::stelling_is_ok(int* failedStep) const
{
	const bool Fast = true;

	enum { Default, King, Bitboards, State, Lists, Castling };

	for (int step = Default; step <= (Fast ? Default : Castling); step++)
	{
		if (failedStep)
			*failedStep = step;

		if (step == Default)
			if ((aanZet != WHITE && aanZet != BLACK)
				|| stuk_op_veld(koning(WHITE)) != W_KING
				|| stuk_op_veld(koning(BLACK)) != B_KING
				|| (enpassant_veld() != SQ_NONE
					&& relatieve_rij(aanZet, enpassant_veld()) != RANK_6))
				return false;

		if (step == King)
			if (std::count(bord, bord + SQUARE_N, W_KING) != 1
				|| std::count(bord, bord + SQUARE_N, B_KING) != 1
				|| aanvallers_naar(koning(~aanZet)) & stukken(aanZet))
				return false;

		if (step == Bitboards)
		{
			if ((stukken(WHITE) & stukken(BLACK))
				|| (stukken(WHITE) | stukken(BLACK)) != stukken())
				return false;

			for (PieceType p1 = KING; p1 <= QUEEN; ++p1)
				for (PieceType p2 = KING; p2 <= QUEEN; ++p2)
					if (p1 != p2 && (stukken(p1) & stukken(p2)))
						return false;
		}

		if (step == State)
		{
			PositionInfo si = *st;
			set_stelling_info(&si);
			if (std::memcmp(&si, st, sizeof(PositionInfo)))
				return false;
		}

		if (step == Lists)
			for (Color kleur = WHITE; kleur <= BLACK; ++kleur)
				for (PieceType stuk = KING; stuk <= QUEEN; ++stuk)
				{
					if (stukAantal[maak_stuk(kleur, stuk)] != popcount(stukken(kleur, stuk)))
						return false;

					for (int i = 0; i < stukAantal[maak_stuk(kleur, stuk)]; ++i)
						if (bord[stukLijst[maak_stuk(kleur, stuk)][i]] != maak_stuk(kleur, stuk)
							|| stukIndex[stukLijst[maak_stuk(kleur, stuk)][i]] != i)
							return false;
				}

		if (step == Castling)
			for (Color kleur = WHITE; kleur <= BLACK; ++kleur)
				for (int n = 0; n < 2; n++)
				{
					CastlingRight rokade = CastlingRight(WHITE_SHORT << (n + 2 * kleur));

					if (!rokade_mogelijk(rokade))
						continue;

					if (stuk_op_veld(rokadeTorenVeld[rokade]) != maak_stuk(kleur, ROOK)
						|| rokadeMask[rokadeTorenVeld[rokade]] != rokade
						|| (rokadeMask[koning(kleur)] & (rokade)) != rokade)
						return false;
				}
	}

	return true;
}


std::string Position::valideer_stelling() const
{
	if (aantal(WHITE, KING) != 1)
		return "Exactly one white king required";
	if (aantal(BLACK, KING) != 1)
		return "Exactly one black king required";

	if (popcount(stukken(WHITE)) > 16)
		return "Too many white pieces";
	if (popcount(stukken(BLACK)) > 16)
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

	if (stukken(PAWN) & (Rank1BB | Rank8BB))
		return "Pawn at rank 1 or 8";

	return "";
}

Square Position::bereken_dreiging() const
{
	if (!st->counterZetWaarden)
		return SQ_NONE;

	Square naar = naar_veld(st->vorigeZet);
	switch (st->bewogenStuk)
	{
	case W_PAWN:
		if (Bitboard b = PionAanval[WHITE][naar] & stukken_uitgezonderd(BLACK, PAWN))
			return lsb(b);
		break;
	case B_PAWN:
		if (Bitboard b = PionAanval[BLACK][naar] & stukken_uitgezonderd(WHITE, PAWN))
			return msb(b);
		break;
	case W_KNIGHT:
		if (Bitboard b = LegeAanval[KNIGHT][naar] & stukken(BLACK, ROOK, QUEEN))
			return lsb(b);
		break;
	case B_KNIGHT:
		if (Bitboard b = LegeAanval[KNIGHT][naar] & stukken(WHITE, ROOK, QUEEN))
			return msb(b);
		break;
	case W_BISHOP:
		// bij L en T eventueel testen dat de dreiging al niet eerder bestond
		// (vorigeZet in dezelfde richting als dreiging)
		if (Bitboard b = aanval_bb_loper(naar, stukken()) & stukken(BLACK, ROOK, QUEEN))
			return lsb(b);
		break;
	case B_BISHOP:
		if (Bitboard b = aanval_bb_loper(naar, stukken()) & stukken(WHITE, ROOK, QUEEN))
			return msb(b);
		break;
	case W_ROOK:
		if (Bitboard b = aanval_bb_toren(naar, stukken()) & stukken(BLACK, QUEEN))
			return lsb(b);
		break;
	case B_ROOK:
		if (Bitboard b = aanval_bb_toren(naar, stukken()) & stukken(WHITE, QUEEN))
			return msb(b);
		break;
	default:
		break;
	}

	return SQ_NONE;
}

