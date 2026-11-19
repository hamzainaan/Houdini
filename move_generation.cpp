/*
*/

#include "houdini.h"
#include "move_generation.h"
#include "position.h"
#include "util_tools.h"

namespace 
{
	template<CastlingRight rokade, bool OnlyCheckMoves, bool Chess960>
	MoveEx* write_castling(const Position& pos, MoveEx* zetten)
	{
		const Color Us = rokade <= WHITE_LONG ? WHITE : BLACK;
		const Color Them = (Us == WHITE ? BLACK : WHITE);
		const bool korte_rokade = (rokade == WHITE_SHORT || rokade == BLACK_SHORT);

		if (pos.castling_impeded(rokade) || !pos.castling_possible(rokade))
			return zetten;

		const Square vanK = Chess960 ? pos.koning(Us) : relative_square(Us, SQ_E1);
		const Square naarK = relative_square(Us, korte_rokade ? SQ_G1 : SQ_C1);
		const Square richting = naarK > vanK ? LEFT : RIGHT;

		assert(!pos.check_givers());

		if (Chess960)
		{
			for (Square veld = naarK; veld != vanK; veld += richting)
				if (pos.attackers_to(veld) & pos.pieces(Them))
					return zetten;

			Square vanT = pos.castling_rook_square(naarK);
			if (attack_bb_rook(naarK, pos.pieces() ^ vanT) & pos.pieces(Them, ROOK, QUEEN))
				return zetten;
		}
		else
		{
			if (pos.attackers_to(naarK) & pos.pieces(Them))
				return zetten;
			if (pos.attackers_to(naarK + richting) & pos.pieces(Them))
				return zetten;
		}

		Move zet = make_move(CASTLING, vanK, naarK);

		if (OnlyCheckMoves && !pos.gives_check(zet))
			return zetten;

		*zetten++ = zet;
		return zetten;
	}


	template<Color Us, MoveGeneration Type, Square Delta>
	MoveEx* write_promotions(const Position& pos, MoveEx* zetten, Square naar)
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);

		if (Type == GEN_CAPTURES_OR_PROMOTIONS || Type == GEN_EVASIONS || Type == GEN_ALL_MOVES)
			*zetten++ = make_move(PROMOTION_Q, naar - Delta, naar);

		if (Type == GEN_QUIET_MOVES || Type == GEN_EVASIONS || Type == GEN_ALL_MOVES)
		{
			*zetten++ = make_move(PROMOTION_R, naar - Delta, naar);
			*zetten++ = make_move(PROMOTION_B, naar - Delta, naar);
			*zetten++ = make_move(PROMOTION_N, naar - Delta, naar);
		}

		if (Type == GEN_QUIET_CHECKS && (LegeAanval[KNIGHT][naar] & pos.koning(Them)))
			*zetten++ = make_move(PROMOTION_N, naar - Delta, naar);

		return zetten;
	}


	template<Color Us, MoveGeneration Type>
	MoveEx* moves_for_pawn(const Position& pos, MoveEx* zetten, Bitboard target)
	{
		const Color Them = (Us == WHITE ? BLACK : WHITE);
		const Bitboard AchtsteRij = (Us == WHITE ? Rank8BB : Rank1BB);
		const Bitboard ZevendeRij = (Us == WHITE ? Rank7BB : Rank2BB);
		const Bitboard ThirdRank = (Us == WHITE ? Rank3BB : Rank6BB);
		const Square Forward = (Us == WHITE ? UP : DOWN);
		const Square SlagRechts = (Us == WHITE ? UP_RIGHT : DOWN_LEFT);
		const Square SlagLinks = (Us == WHITE ? UP_LEFT : DOWN_RIGHT);

		Bitboard legeVelden;
		Bitboard pionnen7deRij = pos.pieces(Us, PAWN) &  ZevendeRij;
		Bitboard pionnenNiet7deRij = pos.pieces(Us, PAWN) & ~ZevendeRij;

		Bitboard jouwStukken = (Type == GEN_EVASIONS ? pos.pieces(Them) & target :
			Type == GEN_CAPTURES_OR_PROMOTIONS ? target : pos.pieces(Them));

		if (Type != GEN_CAPTURES_OR_PROMOTIONS)
		{
			legeVelden = (Type == GEN_QUIET_MOVES || Type == GEN_QUIET_CHECKS ? target : ~pos.pieces());

			Bitboard bbEnkeleZet = shift_up<Us>(pionnenNiet7deRij) & legeVelden;
			Bitboard bbDubbeleZet = shift_up<Us>(bbEnkeleZet & ThirdRank) & legeVelden;

			if (Type == GEN_EVASIONS)
			{
				bbEnkeleZet &= target;
				bbDubbeleZet &= target;
			}

			if (Type == GEN_QUIET_CHECKS)
			{
				bbEnkeleZet &= pos.attack_from<PAWN>(pos.koning(Them), Them);
				bbDubbeleZet &= pos.attack_from<PAWN>(pos.koning(Them), Them);

				Bitboard aftrekSchaak = pos.info()->xRay[~pos.side_to_move()];
				if (pionnenNiet7deRij & aftrekSchaak)
				{
					Bitboard aftrekVoorwaarts = shift_up<Us>(pionnenNiet7deRij & aftrekSchaak) & legeVelden & ~bb_file(pos.koning(Them));
					Bitboard aftrekDubbel = shift_up<Us>(aftrekVoorwaarts & ThirdRank) & legeVelden;

					bbEnkeleZet |= aftrekVoorwaarts;
					bbDubbeleZet |= aftrekDubbel;
				}
			}

			while (bbEnkeleZet)
			{
				Square naar = pop_lsb(&bbEnkeleZet);
				*zetten++ = make_move(PAWN, naar - Forward, naar);
			}

			while (bbDubbeleZet)
			{
				Square naar = pop_lsb(&bbDubbeleZet);
				*zetten++ = make_move(PAWN, naar - Forward - Forward, naar);
			}
		}

		if (pionnen7deRij && (Type != GEN_EVASIONS || (target & AchtsteRij)))
		{
			if (Type == GEN_CAPTURES_OR_PROMOTIONS)
				legeVelden = ~pos.pieces();

			if (Type == GEN_EVASIONS)
				legeVelden &= target;

			Bitboard promotieRechts = shift_bb<SlagRechts>(pionnen7deRij) & jouwStukken;
			Bitboard promotieLinks = shift_bb<SlagLinks>(pionnen7deRij) & jouwStukken;
			Bitboard promotieVoorwaarts = shift_up<Us>(pionnen7deRij) & legeVelden;

			while (promotieRechts)
				zetten = write_promotions<Us, Type, SlagRechts>(pos, zetten, pop_lsb(&promotieRechts));

			while (promotieLinks)
				zetten = write_promotions<Us, Type, SlagLinks>(pos, zetten, pop_lsb(&promotieLinks));

			while (promotieVoorwaarts)
				zetten = write_promotions<Us, Type, Forward>(pos, zetten, pop_lsb(&promotieVoorwaarts));
		}

		if (Type == GEN_CAPTURES_OR_PROMOTIONS || Type == GEN_EVASIONS || Type == GEN_ALL_MOVES)
		{
			Bitboard slagRechts = shift_bb<SlagRechts>(pionnenNiet7deRij) & jouwStukken;
			Bitboard slagLinks = shift_bb<SlagLinks>(pionnenNiet7deRij) & jouwStukken;

			while (slagRechts)
			{
				Square naar = pop_lsb(&slagRechts);
				*zetten++ = make_move(PAWN, naar - SlagRechts, naar);
			}

			while (slagLinks)
			{
				Square naar = pop_lsb(&slagLinks);
				*zetten++ = make_move(PAWN, naar - SlagLinks, naar);
			}

			if (pos.enpassant_square() != SQ_NONE)
			{
				assert(rij(pos.enpassant_square()) == relative_rank(Us, RANK_6));

				if (Type == GEN_EVASIONS && !(target & (pos.enpassant_square() - Forward)))
					return zetten;

				slagRechts = pionnenNiet7deRij & pos.attack_from<PAWN>(pos.enpassant_square(), Them);

				assert(slagRechts);

				while (slagRechts)
					*zetten++ = make_move(ENPASSANT, pop_lsb(&slagRechts), pos.enpassant_square());
			}
		}

		return zetten;
	}


	template<Color Us, PieceType stuk, bool OnlyCheckMoves>
	MoveEx* moves_for_piece(const Position& pos, MoveEx* zetten, Bitboard target)
	{
		assert(stuk != KING && stuk != PAWN);

		const Square* pl = pos.piece_list(Us, stuk);

		for (Square van = *pl; van != SQ_NONE; van = *++pl)
		{
			if (OnlyCheckMoves)
			{
				if ((stuk == BISHOP || stuk == ROOK || stuk == QUEEN)
					&& !(LegeAanval[stuk][van] & target & pos.info()->schaakVelden[stuk]))
					continue;

				// aftrekschaaks worden in genereerZetten<GEN_RUSTIGE_SCHAAKS> geschreven
				if (pos.info()->xRay[~pos.side_to_move()] & van)
					continue;
			}

			Bitboard velden = pos.attack_from<stuk>(van) & target;

			if (OnlyCheckMoves)
				velden &= pos.info()->schaakVelden[stuk];

			while (velden)
				*zetten++ = make_move(stuk, van, pop_lsb(&velden));
		}

		return zetten;
	}


	template<Color Us, MoveGeneration Type>
	MoveEx* moves_for_all_pieces(const Position& pos, MoveEx* zetten, Bitboard target)
	{
		const bool OnlyCheckMoves = Type == GEN_QUIET_CHECKS;

		if (Type != GEN_CASTLING)
		{
			zetten = moves_for_pawn<Us, Type>(pos, zetten, target);
			zetten = moves_for_piece<Us, KNIGHT, OnlyCheckMoves>(pos, zetten, target);
			zetten = moves_for_piece<Us, BISHOP, OnlyCheckMoves>(pos, zetten, target);
			zetten = moves_for_piece<Us, ROOK, OnlyCheckMoves>(pos, zetten, target);
			zetten = moves_for_piece<Us, QUEEN, OnlyCheckMoves>(pos, zetten, target);

			if (Type != GEN_QUIET_CHECKS && Type != GEN_EVASIONS)
			{
				Square veldK = pos.koning(Us);
				Bitboard velden = pos.attack_from<KING>(veldK) & target;
				while (velden)
					*zetten++ = make_move(KING, veldK, pop_lsb(&velden));
			}
		}

		if (Type != GEN_CAPTURES_OR_PROMOTIONS && Type != GEN_EVASIONS && pos.castling_rights(Us))
		{
			if (pos.is_chess960())
			{
				zetten = write_castling<Us == WHITE ? WHITE_SHORT : BLACK_SHORT, OnlyCheckMoves, true>(pos, zetten);
				zetten = write_castling<Us == WHITE ? WHITE_LONG : BLACK_LONG, OnlyCheckMoves, true>(pos, zetten);
			}
			else
			{
				zetten = write_castling<Us == WHITE ? WHITE_SHORT : BLACK_SHORT, OnlyCheckMoves, false>(pos, zetten);
				zetten = write_castling<Us == WHITE ? WHITE_LONG : BLACK_LONG, OnlyCheckMoves, false>(pos, zetten);
			}
		}

		return zetten;
	}


	template<Color Us>
	MoveEx* generate_pawn_advances(const Position& pos, MoveEx* zetten)
	{
		const Bitboard RANK67 = Us == WHITE ? Rank6BB | Rank7BB : Rank3BB | Rank2BB;

		Bitboard velden = shift_up<Us>(pos.pieces(Us, PAWN)) & RANK67 & ~pos.pieces();
		while (velden)
		{
			Square naar = pop_lsb(&velden);
			*zetten++ = make_move(PAWN, naar - pawn_forward(Us), naar);
		}

		return zetten;
	}

	
} // namespace


template<MoveGeneration Type>
MoveEx* generateMoves(const Position& pos, MoveEx* zetten) 
{
	assert(Type == GEN_CAPTURES_OR_PROMOTIONS || Type == GEN_QUIET_MOVES || Type == GEN_ALL_MOVES || Type == GEN_CASTLING);
	assert(!pos.check_givers());

	Color ik = pos.side_to_move();

	Bitboard target = Type == GEN_CAPTURES_OR_PROMOTIONS ? pos.pieces(~ik)
		: Type == GEN_QUIET_MOVES ? ~pos.pieces()
		: Type == GEN_ALL_MOVES ? ~pos.pieces(ik) : 0;

	return ik == WHITE ? moves_for_all_pieces<WHITE, Type>(pos, zetten, target)
		: moves_for_all_pieces<BLACK, Type>(pos, zetten, target);
}

template MoveEx* generateMoves<GEN_CAPTURES_OR_PROMOTIONS>(const Position&, MoveEx*);
template MoveEx* generateMoves<GEN_QUIET_MOVES>(const Position&, MoveEx*);
template MoveEx* generateMoves<GEN_ALL_MOVES>(const Position&, MoveEx*);


template<>
MoveEx* generateMoves<GEN_QUIET_CHECKS>(const Position& pos, MoveEx* zetten) 
{
	assert(!pos.check_givers());

	Color ik = pos.side_to_move();
	Bitboard aftrekSchaak = pos.discovered_check_possible();

	while (aftrekSchaak)
	{
		Square van = pop_lsb(&aftrekSchaak);
		PieceType stuk = piece_type(pos.piece_on_square(van));

		if (stuk == PAWN)
			continue;

		Bitboard velden = pos.attack_from(stuk, van) & ~pos.pieces();

		if (stuk == KING)
			velden &= ~LegeAanval[QUEEN][pos.koning(~ik)];

		while (velden)
			*zetten++ = make_move(stuk, van, pop_lsb(&velden));
	}

	return ik == WHITE ? moves_for_all_pieces<WHITE, GEN_QUIET_CHECKS>(pos, zetten, ~pos.pieces())
		: moves_for_all_pieces<BLACK, GEN_QUIET_CHECKS>(pos, zetten, ~pos.pieces());
}


template<>
MoveEx* generateMoves<GEN_PAWN_ADVANCES>(const Position& pos, MoveEx* zetten)
{
	assert(!pos.check_givers());

	Color ik = pos.side_to_move();
	return ik == WHITE ? generate_pawn_advances<WHITE>(pos, zetten)
		: generate_pawn_advances<BLACK>(pos, zetten);
}


template<>
MoveEx* generateMoves<GEN_QUEEN_CHECKS>(const Position& pos, MoveEx* zetten)
{
	return pos.side_to_move() == WHITE 
		? moves_for_piece<WHITE, QUEEN, true>(pos, zetten, ~pos.pieces())
		: moves_for_piece<BLACK, QUEEN, true>(pos, zetten, ~pos.pieces());
}

template<>
MoveEx* generateMoves<GEN_EVASIONS>(const Position& pos, MoveEx* zetten) 
{
	assert(pos.check_givers());

	Color ik = pos.side_to_move();
	Square veldK = pos.koning(ik);
	Bitboard aangevallenVelden = 0;
	Bitboard verreAanvallers = pos.check_givers() & ~pos.pieces(KNIGHT, PAWN);

	while (verreAanvallers)
	{
		Square schaakveld = pop_lsb(&verreAanvallers);
		aangevallenVelden |= bbVerbinding[schaakveld][veldK] ^ schaakveld;
	}

	Bitboard velden = pos.attack_from<KING>(veldK) & ~pos.pieces(ik) & ~aangevallenVelden;
	while (velden)
		*zetten++ = make_move(KING, veldK, pop_lsb(&velden));

	if (more_than_one(pos.check_givers()))
		return zetten;

	Square schaakVeld = lsb(pos.check_givers());
	Bitboard target = bb_between(schaakVeld, veldK) | schaakVeld;

	return ik == WHITE ? moves_for_all_pieces<WHITE, GEN_EVASIONS>(pos, zetten, target)
		: moves_for_all_pieces<BLACK, GEN_EVASIONS>(pos, zetten, target);
}


MoveEx* generateLegalMoves(const Position& pos, MoveEx* zetten) 
{
	Bitboard gepend = pos.pinned_pieces();
	Square veldK = pos.koning(pos.side_to_move());
	MoveEx* pZet = zetten;

	zetten = pos.check_givers() ? generateMoves<GEN_EVASIONS>(pos, zetten)
		: generateMoves<GEN_ALL_MOVES>(pos, zetten);
	while (pZet != zetten)
		if ((gepend || from_square(*pZet) == veldK || move_type(*pZet) == ENPASSANT)
			&& !pos.legal_move(*pZet))
			*pZet = (--zetten)->zet;
		else
			++pZet;

	return zetten;
}


MoveEx* generateCapturesOnSquare(const Position& pos, MoveEx* zetten, Square veld) 
{
	Bitboard target = bbVeld[veld];

	return pos.side_to_move() == WHITE
		? moves_for_all_pieces<WHITE, GEN_CAPTURES_OR_PROMOTIONS>(pos, zetten, target)
		: moves_for_all_pieces<BLACK, GEN_CAPTURES_OR_PROMOTIONS>(pos, zetten, target);
}


bool LegalMoveListContainsMove(const Position & pos, Move zet)
{
	MoveEx zetten[MAX_MOVES];
	MoveEx* einde = pos.check_givers() ? generateMoves<GEN_EVASIONS>(pos, zetten)
		: generateMoves<GEN_ALL_MOVES>(pos, zetten);

	MoveEx* pZet = zetten;
	while (pZet != einde)
	{
		if (pZet->zet == zet)
			return pos.legal_move(zet);
		pZet++;
	}
	return false;
}


bool AtLeastOneLegalMove(const Position & pos)
{
	MoveEx zetten[MAX_MOVES];
	MoveEx* einde = pos.check_givers() ? generateMoves<GEN_EVASIONS>(pos, zetten)
		: generateMoves<GEN_ALL_MOVES>(pos, zetten);

	MoveEx* pZet = zetten;
	while (pZet != einde)
	{
		if (pos.legal_move(pZet->zet))
			return true;
		pZet++;
	}
	return false;
}


bool LegalMoveListContainsCastling(const Position & pos, Move zet)
{
	MoveEx zetten[MAX_MOVES];
	if (pos.check_givers())
		return false;

	MoveEx* einde = generateMoves<GEN_CASTLING>(pos, zetten);

	MoveEx* pZet = zetten;
	while (pZet != einde)
	{
		if (pZet->zet == zet)
			return true;
		pZet++;
	}
	return false;
}
