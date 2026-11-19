/*
*/

#ifndef POSITION_H
#define POSITION_H

#include "houdini.h"
#include "bitboard.h"

class Position;
class Thread;
struct ThreadInfo;
struct NumaInfo;
template<int MaxPlus, int MaxMin> struct PieceSquareStats;
typedef PieceSquareStats<3*8192, 3*8192> CounterMoveValues;
struct MoveEx;

namespace PST 
{
	extern Score psq[PIECE_N][SQUARE_N];
	void initialize();
	void export_table();
	void import_table();
}

struct AttackInfo 
{
	Bitboard aanval[COLOR_N][PIECE_TYPE_N];
	Bitboard dubbelAanval[COLOR_N];
	Bitboard gepend[COLOR_N];
	Bitboard mobiliteitMask[COLOR_N];
	int KAanvalScore[COLOR_N];
	bool sterkeDreiging[COLOR_N];
	Bitboard KZone[COLOR_N];
	//Bord KZoneExtra[KLEUR_N];
	//int mobiliteit[KLEUR_N];
};


const int POSTPONED_COUNT = 7;

struct PositionInfo 
{
	// tot sleutel wordt gekopieerd bij speel_zet
	Key64 pionSleutel;
	Key64 materiaalSleutel, loperKleurSleutel;
	MaterialValue nietPionMateriaal[COLOR_N];
	Score psq;
	Value stellingWaarde;
	uint8_t rokadeMogelijkheden, fase, sterkeDreiging;
	Square enpassantVeld;
	uint8_t dummy[4];

	Key64 sleutel;
	int remise50Zetten, afstandTotNullzet, ply, zetNummer;
	Move vorigeZet;
	Piece geslagenStuk, bewogenStuk, dummyx;
	bool eval_is_exact;
	CounterMoveValues* counterZetWaarden;
	Bitboard schaakGevers;
	Bitboard xRay[COLOR_N];
	Bitboard schaakVelden[PIECE_TYPE_N];
	Move* pv;
	Move killers[2];
	Move uitgeslotenZet;
	SortValue statsWaarde;
	EvalValue evalPositioneel;
	uint8_t evalFactor, lmr_reductie;
	bool doeGeenVroegePruning, zetHerhaling;

	// zet_keuze
	MoveEx *mp_huidigeZet, *mp_eindeLijst, *mp_eindeSlechteSlag;
	PickMoveStage mp_etappe;
	Move mp_ttZet, mp_counterZet;
	Depth mp_diepte;
	Square mp_slagVeld;
	bool mp_alleen_rustige_schaakzetten, dummyy[2];
	SeeValue mp_threshold;
	uint8_t mp_uitgesteld_aantal, mp_uitgesteld_huidig;
	MoveCompact mp_uitgesteld[POSTPONED_COUNT];

	Square penning_door[SQUARE_N];
#if !defined(IS_64BIT)
	char padding[16];
#endif
};

static_assert(sizeof(PositionInfo) == 256 + 64 + 16, "StellingInfo size incorrect");
static_assert(offsetof(struct PositionInfo, sleutel) == 48, "offset wrong");


class Position 
{
public:
	static void initialize();
	static void init_hash_move50(int FiftyMoveDistance);

	Position() = default;
	Position(const Position&) = delete;
	Position& operator=(const Position&) = delete;

	Position& set(const std::string& fenStr, bool isChess960, Thread* th);
	const std::string fen() const;

	Bitboard pieces() const;
	Bitboard pieces(PieceType stuk) const;
	Bitboard pieces(PieceType stuk1, PieceType stuk2) const;
	Bitboard pieces(Color kleur) const;
	Bitboard pieces(Color kleur, PieceType stuk) const;
	Bitboard pieces(Color kleur, PieceType stuk1, PieceType stuk2) const;
	Bitboard pieces(Color kleur, PieceType stuk1, PieceType stuk2, PieceType stuk3) const;
	Bitboard pieces_except(Color kleur, PieceType stuk) const;
	Piece piece_on_square(Square veld) const;
	Square enpassant_square() const;
	bool empty_square(Square veld) const;
	int aantal(Color kleur, PieceType stuk) const;
	int aantal(Piece stuk) const;
	const Square* piece_list(Color kleur, PieceType stuk) const;
	Square piece_square(Color kleur, PieceType stuk) const;
	Square koning(Color kleur) const;
	int all_pieces_count() const;

	int castling_rights(Color kleur) const;
	int castling_possible(CastlingRight rokade) const;
	bool castling_impeded(CastlingRight rokade) const;
	Square castling_rook_square(Square koningVeld) const;

	Bitboard check_givers() const;
	Bitboard discovered_check_possible() const;
	Bitboard pinned_pieces() const;
	void compute_check_pins() const;
	template<Color kleur> void compute_pins() const;

	Bitboard attackers_to(Square veld) const;
	Bitboard attackers_to(Square veld, Bitboard bezet) const;
	Bitboard attack_from(PieceType stukT, Square veld) const;
	template<PieceType> Bitboard attack_from(Square veld) const;
	template<PieceType> Bitboard attack_from(Square veld, Color kleur) const;

	bool legal_move(Move zet) const;
	bool valid_move(const Move zet) const;
	bool is_capture(Move zet) const;
	bool slag_of_promotie(Move zet) const;
	bool gives_check(Move zet) const;
	bool advanced_pawn(Move zet) const;
	bool passed_pawn_advance(Move zet, Rank r) const;
	Piece moved_piece(Move zet) const;
	bool material_or_castling_changed() const;
	Square compute_threat() const;

	bool is_passed_pawn(Color kleur, Square veld) const;
	bool opposite_bishops() const;

	void do_move(Move zet, bool geeftSchaak);
	void do_move(Move zet);
	void undo_move(Move zet);
	void do_null_move();
	void undo_null_move();

	const SeeValue* see_values() const;
	//SeeWaarde see(Zet zet) const;
	bool see_test(Move zet, SeeValue v) const;

	Key64 sleutel() const;
	Key64 key_after_move(Move zet) const;
	Key64 exclusion_key(Move zet) const;
	Key64 material_key() const;
	Key64 bishopcolor_key() const;
	Key64 pawn_key() const;
	Key64 draw50_key() const;

	Color side_to_move() const;
	GamePhase game_phase() const;
	int game_ply() const;
	void increase_game_ply();
	void increase_tb_hits();
	bool is_chess960() const;
	Thread* my_thread() const;
	ThreadInfo* ti() const;
	NumaInfo* ni() const;
	uint64_t visited_nodes() const;
	uint64_t tb_hits() const;
	int fifty_moves_counter() const;
	Score psq_score() const;
	MaterialValue non_pawn_material(Color kleur) const;
	PositionInfo* info() const { return st; }
	void copy_position(const Position* pos, Thread* th, PositionInfo* copyState);

	bool position_is_ok(int* failedStep = nullptr) const;
	void flipBoard();
	std::string validate_position() const;

private:
	void set_castling_right(Color kleur, Square vanT);
	void set_position_info(PositionInfo* si) const;
	void compute_bishop_color_key() const;

	void put_piece(Color kleur, Piece stuk, Square veld);
	void remove_piece(Color kleur, Piece stuk, Square veld);
	void move_piece(Color kleur, Piece stuk, Square van, Square naar);
	template<bool Do>
	void do_castling(Color ik, Square van, Square naar, Square& vanT, Square& naarT);
	bool is_draw() const;

	PositionInfo* st;
	Color aanZet;
	Thread* mijnThread;
	ThreadInfo* threadInfo;
	NumaInfo* numaInfo;
	Piece bord[SQUARE_N];
	Bitboard stukBB[PIECE_N];
	Bitboard kleurBB[COLOR_N];
	uint8_t stukAantal[PIECE_N];
	Square stukLijst[PIECE_N][16];
	uint8_t stukIndex[SQUARE_N];
	uint8_t rokadeMask[SQUARE_N];
	Square rokadeTorenVeld[SQUARE_N];
	Bitboard rokadePad[CASTLING_RIGHT_N];
	uint64_t knopen, tbHits;
	int partijPly;
	bool chess960;
	char filler[32];
#if !defined(IS_64BIT)
	char padding[16];
#endif
};

static_assert(sizeof(Position) == 14 * 64, "Stelling foute grootte");

extern std::ostream& operator<<(std::ostream& os, const Position& pos);

inline Color Position::side_to_move() const 
{
	return aanZet;
}

inline bool Position::empty_square(Square veld) const
{
	return bord[veld] == NO_PIECE;
}

inline Piece Position::piece_on_square(Square veld) const
{
	return bord[veld];
}

inline Piece Position::moved_piece(Move zet) const
{
	return bord[from_square(zet)];
}

inline Bitboard Position::pieces() const
{
	return stukBB[ALL_PIECES];
}

inline Bitboard Position::pieces(PieceType stuk) const
{
	return stukBB[make_piece(WHITE, stuk)] | stukBB[make_piece(BLACK, stuk)];
}

inline Bitboard Position::pieces(PieceType stuk1, PieceType stuk2) const
{
	return pieces(stuk1) | pieces(stuk2);
}

inline Bitboard Position::pieces(Color kleur) const
{
	return kleurBB[kleur];
}

inline Bitboard Position::pieces(Color kleur, PieceType stuk) const
{
	return stukBB[make_piece(kleur, stuk)];
}

inline Bitboard Position::pieces(Color kleur, PieceType stuk1, PieceType stuk2) const
{
	return pieces(kleur, stuk1) | pieces(kleur, stuk2);
}

inline Bitboard Position::pieces(Color kleur, PieceType stuk1, PieceType stuk2, PieceType stuk3) const
{
	return pieces(kleur, stuk1) | pieces(kleur, stuk2) | pieces(kleur, stuk3);
}

inline Bitboard Position::pieces_except(Color kleur, PieceType stuk) const
{
	return kleurBB[kleur] ^ stukBB[make_piece(kleur, stuk)];
}

inline int Position::aantal(Color kleur, PieceType stuk) const
{
	return stukAantal[make_piece(kleur, stuk)];
}

inline int Position::aantal(Piece stuk) const
{
	return stukAantal[stuk];
}

inline int Position::all_pieces_count() const
{
	return popcount(pieces());
}

inline const Square* Position::piece_list(Color kleur, PieceType stuk) const
{
	return stukLijst[make_piece(kleur, stuk)];
}

inline Square Position::piece_square(Color kleur, PieceType stuk) const
{
	assert(stukAantal[make_piece(kleur, stuk)] == 1);
	return stukLijst[make_piece(kleur, stuk)][0];
}

inline Square Position::koning(Color kleur) const
{
	return stukLijst[make_piece(kleur, KING)][0];
}

inline Square Position::enpassant_square() const
{
	return st->enpassantVeld;
}

inline int Position::castling_possible(CastlingRight rokade) const
{
	return st->rokadeMogelijkheden & rokade;
}

inline int Position::castling_rights(Color kleur) const
{
	return st->rokadeMogelijkheden & ((WHITE_SHORT | WHITE_LONG) << (2 * kleur));
}

inline bool Position::castling_impeded(CastlingRight rokade) const
{
	return pieces() & rokadePad[rokade];
}

inline Square Position::castling_rook_square(Square koningVeld) const
{
	return rokadeTorenVeld[koningVeld];
}

template<PieceType StukT>
inline Bitboard Position::attack_from(Square veld) const
{
	return StukT == BISHOP ? attack_bb_bishop(veld, pieces())
		 : StukT == ROOK ? attack_bb_rook(veld, pieces())
		 : StukT == QUEEN ? attack_from<ROOK>(veld) | attack_from<BISHOP>(veld)
		 : LegeAanval[StukT][veld];
}

template<>
inline Bitboard Position::attack_from<PAWN>(Square veld, Color kleur) const
{
	return PionAanval[kleur][veld];
}

inline Bitboard Position::attack_from(PieceType stukT, Square veld) const
{
	return attack_bb(stukT, veld, pieces());
}

inline Bitboard Position::attackers_to(Square veld) const
{
	return attackers_to(veld, pieces());
}

inline Bitboard Position::check_givers() const
{
	return st->schaakGevers;
}

inline Bitboard Position::discovered_check_possible() const
{
	return st->xRay[~aanZet] & pieces(aanZet);
}

inline Bitboard Position::pinned_pieces() const
{
	return st->xRay[aanZet] & pieces(aanZet);
}

inline bool Position::is_passed_pawn(Color kleur, Square veld) const
{
	return !(pieces(~kleur, PAWN) & passed_pawn_mask(kleur, veld));
}

inline bool Position::advanced_pawn(Move zet) const
{
	return piece_type(moved_piece(zet)) == PAWN
		&& relative_rank(aanZet, to_square(zet)) >= RANK_6;
}

inline bool Position::passed_pawn_advance(Move zet, Rank r) const
{
	Piece stuk = moved_piece(zet);
	return piece_type(stuk) == PAWN
		&& relative_rank(aanZet, to_square(zet)) >= r
		&& is_passed_pawn(piece_color(stuk), to_square(zet));
}

inline Key64 Position::sleutel() const
{
	return st->sleutel;
}

inline Key64 Position::pawn_key() const
{
	return st->pionSleutel;
}

inline Key64 Position::material_key() const
{
	return st->materiaalSleutel;
}

inline Key64 Position::bishopcolor_key() const
{
	return st->loperKleurSleutel;
}

inline Score Position::psq_score() const
{
	return st->psq;
}

inline MaterialValue Position::non_pawn_material(Color kleur) const
{
	return st->nietPionMateriaal[kleur];
}

inline int Position::game_ply() const
{
	return partijPly;
}

inline void Position::increase_game_ply()
{
	++partijPly;
}

inline void Position::increase_tb_hits()
{
	++tbHits;
}

inline int Position::fifty_moves_counter() const
{
	return st->remise50Zetten;
}

inline uint64_t Position::visited_nodes() const
{
	return knopen;
}

inline uint64_t Position::tb_hits() const
{
	return tbHits;
}

inline bool Position::opposite_bishops() const
{
	return stukAantal[W_BISHOP] == 1
		&& stukAantal[B_BISHOP] == 1
		&& different_color(piece_square(WHITE, BISHOP), piece_square(BLACK, BISHOP));
}

inline bool Position::is_chess960() const
{
	return chess960;
}

inline bool Position::slag_of_promotie(Move zet) const
{
	assert(is_ok(zet));
	return zet < Move(CASTLING) ? !empty_square(to_square(zet)) : zet >= Move(ENPASSANT);
}

inline bool Position::is_capture(Move zet) const
{

	assert(is_ok(zet));
	return (!empty_square(to_square(zet)) && move_type(zet) != CASTLING) || move_type(zet) == ENPASSANT;
	//return leeg_veld(naar_veld(zet)) ? zet_type(zet) == ENPASSANT : zet_type(zet) != ROKADE;
}

inline bool Position::material_or_castling_changed() const
{
	return st->materiaalSleutel != (st - 1)->materiaalSleutel
		|| st->rokadeMogelijkheden != (st - 1)->rokadeMogelijkheden;
}

inline Thread* Position::my_thread() const
{
	return mijnThread;
}

inline ThreadInfo* Position::ti() const
{
	return threadInfo;
}

inline NumaInfo* Position::ni() const
{
	return numaInfo;
}

inline void Position::put_piece(Color kleur, Piece stuk, Square veld)
{
	bord[veld] = stuk;
	stukBB[stuk] |= veld;
	kleurBB[kleur] |= veld;
	stukIndex[veld] = stukAantal[stuk]++;
	stukLijst[stuk][stukIndex[veld]] = veld;
}

inline void Position::remove_piece(Color kleur, Piece stuk, Square veld)
{
	stukBB[stuk] ^= veld;
	kleurBB[kleur] ^= veld;
	Square laatsteVeld = stukLijst[stuk][--stukAantal[stuk]];
	stukIndex[laatsteVeld] = stukIndex[veld];
	stukLijst[stuk][stukIndex[laatsteVeld]] = laatsteVeld;
	stukLijst[stuk][stukAantal[stuk]] = SQ_NONE;
}

inline void Position::move_piece(Color kleur, Piece stuk, Square van, Square naar)
{
	Bitboard van_naar_bb = bbVeld[van] ^ bbVeld[naar];
	stukBB[stuk] ^= van_naar_bb;
	kleurBB[kleur] ^= van_naar_bb;
	bord[van] = NO_PIECE;
	bord[naar] = stuk;
	stukIndex[naar] = stukIndex[van];
	stukLijst[stuk][stukIndex[naar]] = naar;
}

#endif // #ifndef POSITION_H
