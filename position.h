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
	void initialisatie();
	void export_tabel();
	void import_tabel();
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
	static void initialisatie();
	static void init_hash_move50(int FiftyMoveDistance);

	Position() = default;
	Position(const Position&) = delete;
	Position& operator=(const Position&) = delete;

	Position& set(const std::string& fenStr, bool isChess960, Thread* th);
	const std::string fen() const;

	Bitboard stukken() const;
	Bitboard stukken(PieceType stuk) const;
	Bitboard stukken(PieceType stuk1, PieceType stuk2) const;
	Bitboard stukken(Color kleur) const;
	Bitboard stukken(Color kleur, PieceType stuk) const;
	Bitboard stukken(Color kleur, PieceType stuk1, PieceType stuk2) const;
	Bitboard stukken(Color kleur, PieceType stuk1, PieceType stuk2, PieceType stuk3) const;
	Bitboard stukken_uitgezonderd(Color kleur, PieceType stuk) const;
	Piece stuk_op_veld(Square veld) const;
	Square enpassant_veld() const;
	bool leeg_veld(Square veld) const;
	int aantal(Color kleur, PieceType stuk) const;
	int aantal(Piece stuk) const;
	const Square* stuk_lijst(Color kleur, PieceType stuk) const;
	Square stuk_veld(Color kleur, PieceType stuk) const;
	Square koning(Color kleur) const;
	int alle_stukken_aantal() const;

	int rokade_mogelijkheden(Color kleur) const;
	int rokade_mogelijk(CastlingRight rokade) const;
	bool rokade_verhinderd(CastlingRight rokade) const;
	Square rokade_toren_veld(Square koningVeld) const;

	Bitboard schaak_gevers() const;
	Bitboard aftrek_schaak_mogelijk() const;
	Bitboard gepende_stukken() const;
	void bereken_schaak_penningen() const;
	template<Color kleur> void bereken_penningen() const;

	Bitboard aanvallers_naar(Square veld) const;
	Bitboard aanvallers_naar(Square veld, Bitboard bezet) const;
	Bitboard aanval_van(PieceType stukT, Square veld) const;
	template<PieceType> Bitboard aanval_van(Square veld) const;
	template<PieceType> Bitboard aanval_van(Square veld, Color kleur) const;

	bool legale_zet(Move zet) const;
	bool geldige_zet(const Move zet) const;
	bool is_slagzet(Move zet) const;
	bool slag_of_promotie(Move zet) const;
	bool geeft_schaak(Move zet) const;
	bool vooruitgeschoven_pion(Move zet) const;
	bool vrijpion_opmars(Move zet, Rank r) const;
	Piece bewogen_stuk(Move zet) const;
	bool materiaal_of_rokade_gewijzigd() const;
	Square bereken_dreiging() const;

	bool is_vrijpion(Color kleur, Square veld) const;
	bool ongelijke_lopers() const;

	void speel_zet(Move zet, bool geeftSchaak);
	void speel_zet(Move zet);
	void neem_zet_terug(Move zet);
	void speel_null_zet();
	void neem_null_terug();

	const SeeValue* see_waarden() const;
	//SeeWaarde see(Zet zet) const;
	bool see_test(Move zet, SeeValue v) const;

	Key64 sleutel() const;
	Key64 sleutel_na_zet(Move zet) const;
	Key64 uitzondering_sleutel(Move zet) const;
	Key64 materiaal_sleutel() const;
	Key64 loperkleur_sleutel() const;
	Key64 pion_sleutel() const;
	Key64 remise50_sleutel() const;

	Color aan_zet() const;
	GamePhase partij_fase() const;
	int partij_ply() const;
	void verhoog_partij_ply();
	void verhoog_tb_hits();
	bool is_chess960() const;
	Thread* mijn_thread() const;
	ThreadInfo* ti() const;
	NumaInfo* ni() const;
	uint64_t bezochte_knopen() const;
	uint64_t tb_hits() const;
	int vijftig_zetten_teller() const;
	Score psq_score() const;
	MaterialValue niet_pion_materiaal(Color kleur) const;
	PositionInfo* info() const { return st; }
	void kopieer_stelling(const Position* pos, Thread* th, PositionInfo* copyState);

	bool stelling_is_ok(int* failedStep = nullptr) const;
	void draaiBord();
	std::string valideer_stelling() const;

private:
	void set_rokade_mogelijkheid(Color kleur, Square vanT);
	void set_stelling_info(PositionInfo* si) const;
	void bereken_loper_kleur_sleutel() const;

	void zet_stuk(Color kleur, Piece stuk, Square veld);
	void verwijder_stuk(Color kleur, Piece stuk, Square veld);
	void verplaats_stuk(Color kleur, Piece stuk, Square van, Square naar);
	template<bool Do>
	void speel_rokade(Color ik, Square van, Square naar, Square& vanT, Square& naarT);
	bool is_remise() const;

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

inline Color Position::aan_zet() const 
{
	return aanZet;
}

inline bool Position::leeg_veld(Square veld) const
{
	return bord[veld] == NO_PIECE;
}

inline Piece Position::stuk_op_veld(Square veld) const
{
	return bord[veld];
}

inline Piece Position::bewogen_stuk(Move zet) const
{
	return bord[van_veld(zet)];
}

inline Bitboard Position::stukken() const
{
	return stukBB[ALL_PIECES];
}

inline Bitboard Position::stukken(PieceType stuk) const
{
	return stukBB[maak_stuk(WHITE, stuk)] | stukBB[maak_stuk(BLACK, stuk)];
}

inline Bitboard Position::stukken(PieceType stuk1, PieceType stuk2) const
{
	return stukken(stuk1) | stukken(stuk2);
}

inline Bitboard Position::stukken(Color kleur) const
{
	return kleurBB[kleur];
}

inline Bitboard Position::stukken(Color kleur, PieceType stuk) const
{
	return stukBB[maak_stuk(kleur, stuk)];
}

inline Bitboard Position::stukken(Color kleur, PieceType stuk1, PieceType stuk2) const
{
	return stukken(kleur, stuk1) | stukken(kleur, stuk2);
}

inline Bitboard Position::stukken(Color kleur, PieceType stuk1, PieceType stuk2, PieceType stuk3) const
{
	return stukken(kleur, stuk1) | stukken(kleur, stuk2) | stukken(kleur, stuk3);
}

inline Bitboard Position::stukken_uitgezonderd(Color kleur, PieceType stuk) const
{
	return kleurBB[kleur] ^ stukBB[maak_stuk(kleur, stuk)];
}

inline int Position::aantal(Color kleur, PieceType stuk) const
{
	return stukAantal[maak_stuk(kleur, stuk)];
}

inline int Position::aantal(Piece stuk) const
{
	return stukAantal[stuk];
}

inline int Position::alle_stukken_aantal() const
{
	return popcount(stukken());
}

inline const Square* Position::stuk_lijst(Color kleur, PieceType stuk) const
{
	return stukLijst[maak_stuk(kleur, stuk)];
}

inline Square Position::stuk_veld(Color kleur, PieceType stuk) const
{
	assert(stukAantal[maak_stuk(kleur, stuk)] == 1);
	return stukLijst[maak_stuk(kleur, stuk)][0];
}

inline Square Position::koning(Color kleur) const
{
	return stukLijst[maak_stuk(kleur, KING)][0];
}

inline Square Position::enpassant_veld() const
{
	return st->enpassantVeld;
}

inline int Position::rokade_mogelijk(CastlingRight rokade) const
{
	return st->rokadeMogelijkheden & rokade;
}

inline int Position::rokade_mogelijkheden(Color kleur) const
{
	return st->rokadeMogelijkheden & ((WHITE_SHORT | WHITE_LONG) << (2 * kleur));
}

inline bool Position::rokade_verhinderd(CastlingRight rokade) const
{
	return stukken() & rokadePad[rokade];
}

inline Square Position::rokade_toren_veld(Square koningVeld) const
{
	return rokadeTorenVeld[koningVeld];
}

template<PieceType StukT>
inline Bitboard Position::aanval_van(Square veld) const
{
	return StukT == BISHOP ? aanval_bb_loper(veld, stukken())
		 : StukT == ROOK ? aanval_bb_toren(veld, stukken())
		 : StukT == QUEEN ? aanval_van<ROOK>(veld) | aanval_van<BISHOP>(veld)
		 : LegeAanval[StukT][veld];
}

template<>
inline Bitboard Position::aanval_van<PAWN>(Square veld, Color kleur) const
{
	return PionAanval[kleur][veld];
}

inline Bitboard Position::aanval_van(PieceType stukT, Square veld) const
{
	return aanval_bb(stukT, veld, stukken());
}

inline Bitboard Position::aanvallers_naar(Square veld) const
{
	return aanvallers_naar(veld, stukken());
}

inline Bitboard Position::schaak_gevers() const
{
	return st->schaakGevers;
}

inline Bitboard Position::aftrek_schaak_mogelijk() const
{
	return st->xRay[~aanZet] & stukken(aanZet);
}

inline Bitboard Position::gepende_stukken() const
{
	return st->xRay[aanZet] & stukken(aanZet);
}

inline bool Position::is_vrijpion(Color kleur, Square veld) const
{
	return !(stukken(~kleur, PAWN) & vrijpion_mask(kleur, veld));
}

inline bool Position::vooruitgeschoven_pion(Move zet) const
{
	return stuk_type(bewogen_stuk(zet)) == PAWN
		&& relatieve_rij(aanZet, naar_veld(zet)) >= RANK_6;
}

inline bool Position::vrijpion_opmars(Move zet, Rank r) const
{
	Piece stuk = bewogen_stuk(zet);
	return stuk_type(stuk) == PAWN
		&& relatieve_rij(aanZet, naar_veld(zet)) >= r
		&& is_vrijpion(stuk_kleur(stuk), naar_veld(zet));
}

inline Key64 Position::sleutel() const
{
	return st->sleutel;
}

inline Key64 Position::pion_sleutel() const
{
	return st->pionSleutel;
}

inline Key64 Position::materiaal_sleutel() const
{
	return st->materiaalSleutel;
}

inline Key64 Position::loperkleur_sleutel() const
{
	return st->loperKleurSleutel;
}

inline Score Position::psq_score() const
{
	return st->psq;
}

inline MaterialValue Position::niet_pion_materiaal(Color kleur) const
{
	return st->nietPionMateriaal[kleur];
}

inline int Position::partij_ply() const
{
	return partijPly;
}

inline void Position::verhoog_partij_ply()
{
	++partijPly;
}

inline void Position::verhoog_tb_hits()
{
	++tbHits;
}

inline int Position::vijftig_zetten_teller() const
{
	return st->remise50Zetten;
}

inline uint64_t Position::bezochte_knopen() const
{
	return knopen;
}

inline uint64_t Position::tb_hits() const
{
	return tbHits;
}

inline bool Position::ongelijke_lopers() const
{
	return stukAantal[W_BISHOP] == 1
		&& stukAantal[B_BISHOP] == 1
		&& verschillende_kleur(stuk_veld(WHITE, BISHOP), stuk_veld(BLACK, BISHOP));
}

inline bool Position::is_chess960() const
{
	return chess960;
}

inline bool Position::slag_of_promotie(Move zet) const
{
	assert(is_ok(zet));
	return zet < Move(CASTLING) ? !leeg_veld(naar_veld(zet)) : zet >= Move(ENPASSANT);
}

inline bool Position::is_slagzet(Move zet) const
{

	assert(is_ok(zet));
	return (!leeg_veld(naar_veld(zet)) && zet_type(zet) != CASTLING) || zet_type(zet) == ENPASSANT;
	//return leeg_veld(naar_veld(zet)) ? zet_type(zet) == ENPASSANT : zet_type(zet) != ROKADE;
}

inline bool Position::materiaal_of_rokade_gewijzigd() const
{
	return st->materiaalSleutel != (st - 1)->materiaalSleutel
		|| st->rokadeMogelijkheden != (st - 1)->rokadeMogelijkheden;
}

inline Thread* Position::mijn_thread() const
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

inline void Position::zet_stuk(Color kleur, Piece stuk, Square veld)
{
	bord[veld] = stuk;
	stukBB[stuk] |= veld;
	kleurBB[kleur] |= veld;
	stukIndex[veld] = stukAantal[stuk]++;
	stukLijst[stuk][stukIndex[veld]] = veld;
}

inline void Position::verwijder_stuk(Color kleur, Piece stuk, Square veld)
{
	stukBB[stuk] ^= veld;
	kleurBB[kleur] ^= veld;
	Square laatsteVeld = stukLijst[stuk][--stukAantal[stuk]];
	stukIndex[laatsteVeld] = stukIndex[veld];
	stukLijst[stuk][stukIndex[laatsteVeld]] = laatsteVeld;
	stukLijst[stuk][stukAantal[stuk]] = SQ_NONE;
}

inline void Position::verplaats_stuk(Color kleur, Piece stuk, Square van, Square naar)
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
