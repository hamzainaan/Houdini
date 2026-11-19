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
	Bitboard attack[COLOR_N][PIECE_TYPE_N];
	Bitboard doubleAttack[COLOR_N];
	Bitboard pinned[COLOR_N];
	Bitboard mobilityMask[COLOR_N];
	int KAttackScore[COLOR_N];
	bool strongThreat[COLOR_N];
	Bitboard KZone[COLOR_N];
	//Bitboard KZoneExtra[COLOR_N];
	//int mobility[COLOR_N];
};


const int POSTPONED_COUNT = 7;

struct PositionInfo 
{
	// up to key is copied in do_move
	Key64 pawnKey;
	Key64 materialKey, bishopColorKey;
	MaterialValue nonPawnMaterial[COLOR_N];
	Score psq;
	Value positionValue;
	uint8_t castlingRights, phase, strongThreat;
	Square enpassantSquare;
	uint8_t dummy[4];

	Key64 key;
	int draw50Moves, distanceToNullMove, ply, moveNumber;
	Move previousMove;
	Piece capturedPiece, movedPiece, dummyx;
	bool eval_is_exact;
	CounterMoveValues* counterMoveValues;
	Bitboard checkGivers;
	Bitboard xRay[COLOR_N];
	Bitboard checkSquares[PIECE_TYPE_N];
	Move* pv;
	Move killers[2];
	Move excludedMove;
	SortValue statsValue;
	EvalValue evalPositional;
	uint8_t evalFactor, lmr_reduction;
	bool doNoEarlyPruning, moveRepetition;

	// move_selection
	MoveEx *mp_currentMove, *mp_endList, *mp_endBadCapture;
	PickMoveStage mp_stage;
	Move mp_ttMove, mp_counterMove;
	Depth mp_depth;
	Square mp_captureSquare;
	bool mp_only_quiet_checks, dummyy[2];
	SeeValue mp_threshold;
	uint8_t mp_postponed_count, mp_postponed_current;
	MoveCompact mp_postponed[POSTPONED_COUNT];

	Square pinned_by[SQUARE_N];
#if !defined(IS_64BIT)
	char padding[16];
#endif
};

static_assert(sizeof(PositionInfo) == 256 + 64 + 16, "StellingInfo size incorrect");
static_assert(offsetof(struct PositionInfo, key) == 48, "offset wrong");


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
	Bitboard pieces(PieceType piece) const;
	Bitboard pieces(PieceType piece1, PieceType piece2) const;
	Bitboard pieces(Color color) const;
	Bitboard pieces(Color color, PieceType piece) const;
	Bitboard pieces(Color color, PieceType piece1, PieceType piece2) const;
	Bitboard pieces(Color color, PieceType piece1, PieceType piece2, PieceType piece3) const;
	Bitboard pieces_except(Color color, PieceType piece) const;
	Piece piece_on_square(Square square) const;
	Square enpassant_square() const;
	bool empty_square(Square square) const;
	int count(Color color, PieceType piece) const;
	int count(Piece piece) const;
	const Square* piece_list(Color color, PieceType piece) const;
	Square piece_square(Color color, PieceType piece) const;
	Square king(Color color) const;
	int all_pieces_count() const;

	int castling_rights(Color color) const;
	int castling_possible(CastlingRight castling) const;
	bool castling_impeded(CastlingRight castling) const;
	Square castling_rook_square(Square kingSquare) const;

	Bitboard check_givers() const;
	Bitboard discovered_check_possible() const;
	Bitboard pinned_pieces() const;
	void compute_check_pins() const;
	template<Color color> void compute_pins() const;

	Bitboard attackers_to(Square square) const;
	Bitboard attackers_to(Square square, Bitboard occupied) const;
	Bitboard attack_from(PieceType pieceT, Square square) const;
	template<PieceType> Bitboard attack_from(Square square) const;
	template<PieceType> Bitboard attack_from(Square square, Color color) const;

	bool legal_move(Move move) const;
	bool valid_move(const Move move) const;
	bool is_capture(Move move) const;
	bool capture_or_promotion(Move move) const;
	bool gives_check(Move move) const;
	bool advanced_pawn(Move move) const;
	bool passed_pawn_advance(Move move, Rank r) const;
	Piece moved_piece(Move move) const;
	bool material_or_castling_changed() const;
	Square compute_threat() const;

	bool is_passed_pawn(Color color, Square square) const;
	bool opposite_bishops() const;

	void do_move(Move move, bool givesCheck);
	void do_move(Move move);
	void undo_move(Move move);
	void do_null_move();
	void undo_null_move();

	const SeeValue* see_values() const;
	//SeeValue see(Move move) const;
	bool see_test(Move move, SeeValue v) const;

	Key64 key() const;
	Key64 key_after_move(Move move) const;
	Key64 exclusion_key(Move move) const;
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
	MaterialValue non_pawn_material(Color color) const;
	PositionInfo* info() const { return st; }
	void copy_position(const Position* pos, Thread* th, PositionInfo* copyState);

	bool position_is_ok(int* failedStep = nullptr) const;
	void flipBoard();
	std::string validate_position() const;

private:
	void set_castling_right(Color color, Square fromR);
	void set_position_info(PositionInfo* si) const;
	void compute_bishop_color_key() const;

	void put_piece(Color color, Piece piece, Square square);
	void remove_piece(Color color, Piece piece, Square square);
	void move_piece(Color color, Piece piece, Square from, Square to);
	template<bool Do>
	void do_castling(Color us, Square from, Square to, Square& fromR, Square& toR);
	bool is_draw() const;

	PositionInfo* st;
	Color sideToMove;
	Thread* myThread;
	ThreadInfo* threadInfo;
	NumaInfo* numaInfo;
	Piece board[SQUARE_N];
	Bitboard pieceBB[PIECE_N];
	Bitboard colorBB[COLOR_N];
	uint8_t pieceCount[PIECE_N];
	Square pieceList[PIECE_N][16];
	uint8_t pieceIndex[SQUARE_N];
	uint8_t castlingMask[SQUARE_N];
	Square castlingRookSquare[SQUARE_N];
	Bitboard castlingPath[CASTLING_RIGHT_N];
	uint64_t nodes, tbHits;
	int gamePly;
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
	return sideToMove;
}

inline bool Position::empty_square(Square square) const
{
	return board[square] == NO_PIECE;
}

inline Piece Position::piece_on_square(Square square) const
{
	return board[square];
}

inline Piece Position::moved_piece(Move move) const
{
	return board[from_square(move)];
}

inline Bitboard Position::pieces() const
{
	return pieceBB[ALL_PIECES];
}

inline Bitboard Position::pieces(PieceType piece) const
{
	return pieceBB[make_piece(WHITE, piece)] | pieceBB[make_piece(BLACK, piece)];
}

inline Bitboard Position::pieces(PieceType piece1, PieceType piece2) const
{
	return pieces(piece1) | pieces(piece2);
}

inline Bitboard Position::pieces(Color color) const
{
	return colorBB[color];
}

inline Bitboard Position::pieces(Color color, PieceType piece) const
{
	return pieceBB[make_piece(color, piece)];
}

inline Bitboard Position::pieces(Color color, PieceType piece1, PieceType piece2) const
{
	return pieces(color, piece1) | pieces(color, piece2);
}

inline Bitboard Position::pieces(Color color, PieceType piece1, PieceType piece2, PieceType piece3) const
{
	return pieces(color, piece1) | pieces(color, piece2) | pieces(color, piece3);
}

inline Bitboard Position::pieces_except(Color color, PieceType piece) const
{
	return colorBB[color] ^ pieceBB[make_piece(color, piece)];
}

inline int Position::count(Color color, PieceType piece) const
{
	return pieceCount[make_piece(color, piece)];
}

inline int Position::count(Piece piece) const
{
	return pieceCount[piece];
}

inline int Position::all_pieces_count() const
{
	return popcount(pieces());
}

inline const Square* Position::piece_list(Color color, PieceType piece) const
{
	return pieceList[make_piece(color, piece)];
}

inline Square Position::piece_square(Color color, PieceType piece) const
{
	assert(pieceCount[make_piece(color, piece)] == 1);
	return pieceList[make_piece(color, piece)][0];
}

inline Square Position::king(Color color) const
{
	return pieceList[make_piece(color, KING)][0];
}

inline Square Position::enpassant_square() const
{
	return st->enpassantSquare;
}

inline int Position::castling_possible(CastlingRight castling) const
{
	return st->castlingRights & castling;
}

inline int Position::castling_rights(Color color) const
{
	return st->castlingRights & ((WHITE_SHORT | WHITE_LONG) << (2 * color));
}

inline bool Position::castling_impeded(CastlingRight castling) const
{
	return pieces() & castlingPath[castling];
}

inline Square Position::castling_rook_square(Square kingSquare) const
{
	return castlingRookSquare[kingSquare];
}

template<PieceType PieceT>
inline Bitboard Position::attack_from(Square square) const
{
	return PieceT == BISHOP ? attack_bb_bishop(square, pieces())
		 : PieceT == ROOK ? attack_bb_rook(square, pieces())
		 : PieceT == QUEEN ? attack_from<ROOK>(square) | attack_from<BISHOP>(square)
		 : EmptyAttack[PieceT][square];
}

template<>
inline Bitboard Position::attack_from<PAWN>(Square square, Color color) const
{
	return PawnAttack[color][square];
}

inline Bitboard Position::attack_from(PieceType pieceT, Square square) const
{
	return attack_bb(pieceT, square, pieces());
}

inline Bitboard Position::attackers_to(Square square) const
{
	return attackers_to(square, pieces());
}

inline Bitboard Position::check_givers() const
{
	return st->checkGivers;
}

inline Bitboard Position::discovered_check_possible() const
{
	return st->xRay[~sideToMove] & pieces(sideToMove);
}

inline Bitboard Position::pinned_pieces() const
{
	return st->xRay[sideToMove] & pieces(sideToMove);
}

inline bool Position::is_passed_pawn(Color color, Square square) const
{
	return !(pieces(~color, PAWN) & passed_pawn_mask(color, square));
}

inline bool Position::advanced_pawn(Move move) const
{
	return piece_type(moved_piece(move)) == PAWN
		&& relative_rank(sideToMove, to_square(move)) >= RANK_6;
}

inline bool Position::passed_pawn_advance(Move move, Rank r) const
{
	Piece piece = moved_piece(move);
	return piece_type(piece) == PAWN
		&& relative_rank(sideToMove, to_square(move)) >= r
		&& is_passed_pawn(piece_color(piece), to_square(move));
}

inline Key64 Position::key() const
{
	return st->key;
}

inline Key64 Position::pawn_key() const
{
	return st->pawnKey;
}

inline Key64 Position::material_key() const
{
	return st->materialKey;
}

inline Key64 Position::bishopcolor_key() const
{
	return st->bishopColorKey;
}

inline Score Position::psq_score() const
{
	return st->psq;
}

inline MaterialValue Position::non_pawn_material(Color color) const
{
	return st->nonPawnMaterial[color];
}

inline int Position::game_ply() const
{
	return gamePly;
}

inline void Position::increase_game_ply()
{
	++gamePly;
}

inline void Position::increase_tb_hits()
{
	++tbHits;
}

inline int Position::fifty_moves_counter() const
{
	return st->draw50Moves;
}

inline uint64_t Position::visited_nodes() const
{
	return nodes;
}

inline uint64_t Position::tb_hits() const
{
	return tbHits;
}

inline bool Position::opposite_bishops() const
{
	return pieceCount[W_BISHOP] == 1
		&& pieceCount[B_BISHOP] == 1
		&& different_color(piece_square(WHITE, BISHOP), piece_square(BLACK, BISHOP));
}

inline bool Position::is_chess960() const
{
	return chess960;
}

inline bool Position::capture_or_promotion(Move move) const
{
	assert(is_ok(move));
	return move < Move(CASTLING) ? !empty_square(to_square(move)) : move >= Move(ENPASSANT);
}

inline bool Position::is_capture(Move move) const
{

	assert(is_ok(move));
	return (!empty_square(to_square(move)) && move_type(move) != CASTLING) || move_type(move) == ENPASSANT;
	//return empty_square(to_square(move)) ? move_type(move) == ENPASSANT : move_type(move) != CASTLING;
}

inline bool Position::material_or_castling_changed() const
{
	return st->materialKey != (st - 1)->materialKey
		|| st->castlingRights != (st - 1)->castlingRights;
}

inline Thread* Position::my_thread() const
{
	return myThread;
}

inline ThreadInfo* Position::ti() const
{
	return threadInfo;
}

inline NumaInfo* Position::ni() const
{
	return numaInfo;
}

inline void Position::put_piece(Color color, Piece piece, Square square)
{
	board[square] = piece;
	pieceBB[piece] |= square;
	colorBB[color] |= square;
	pieceIndex[square] = pieceCount[piece]++;
	pieceList[piece][pieceIndex[square]] = square;
}

inline void Position::remove_piece(Color color, Piece piece, Square square)
{
	pieceBB[piece] ^= square;
	colorBB[color] ^= square;
	Square lastSquare = pieceList[piece][--pieceCount[piece]];
	pieceIndex[lastSquare] = pieceIndex[square];
	pieceList[piece][pieceIndex[lastSquare]] = lastSquare;
	pieceList[piece][pieceCount[piece]] = SQ_NONE;
}

inline void Position::move_piece(Color color, Piece piece, Square from, Square to)
{
	Bitboard from_to_bb = bbSquare[from] ^ bbSquare[to];
	pieceBB[piece] ^= from_to_bb;
	colorBB[color] ^= from_to_bb;
	board[from] = NO_PIECE;
	board[to] = piece;
	pieceIndex[to] = pieceIndex[from];
	pieceList[piece][pieceIndex[to]] = to;
}

#endif // #ifndef POSITION_H
