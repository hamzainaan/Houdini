/*
*/

#ifndef MOVE_HASH_H
#define MOVE_HASH_H

#include "houdini.h"
#include "util_tools.h"
#include "search_smp.h"

using std::string;

enum TTFlags : uint8_t
{
	NO_BOUND,
	THREAT_WHITE   = 1,
	THREAT_BLACK = 2,
	IN_USE = 4,
	LOWER_BOUND = 64,
	UPPER_BOUND = 128,
	EXACT_VALUE = UPPER_BOUND | LOWER_BOUND
};

const uint8_t GenerationMask = 0x38;
const uint8_t FlagsMask   = 0xc7;
const uint8_t ThreatMask  = 0x03;
const uint8_t UsageMask   = 0xfb;

struct TTTuple 
{
	Move move() const { return (Move)move16; }
	Value value() const { return (Value)value16; }
	Value eval() const { return (Value)eval16; }
	Depth depth() const { return Depth(depth8 * int(PLY) + PLY - 1); }
	TTFlags bound() const { return (TTFlags)(flags8 & EXACT_VALUE); }
	TTFlags threat() const { return (TTFlags)(flags8 & ThreatMask); }

	void save(Key64 k, Value v, uint8_t flags, Depth d, Move z, Value evaluation, uint8_t gen)
	{
		int dd = d / PLY;
		uint16_t k16 = k >> 48;
		if (z || k16 != key16)
			move16 = (uint16_t)z;

		if (k16 != key16
			|| dd > depth8 - 4
			|| (flags & EXACT_VALUE) == EXACT_VALUE)
		{
			key16 = k16;
			value16 = (int16_t)v;
			eval16 = (int16_t)evaluation;
			flags8 = (uint8_t)(gen + flags);
			depth8 = (int8_t)dd;
		}
	}

private:
	friend class TranspositionTable;

	// 10 bytes
	uint16_t key16;
	int8_t   depth8;
	uint8_t  flags8;
	int16_t  value16;
	int16_t  eval16;
	uint16_t move16;
};


class TranspositionTable 
{
	static const int CacheLineSize = 64;
	static const int ClusterSize = 3;

	struct Cluster
	{
		TTTuple entry[ClusterSize];
		char padding[2];
	};

	static_assert(CacheLineSize % sizeof(Cluster) == 0, "Cluster size incorrect");

public:
	~TranspositionTable() { free_large_page_mem(table); }
	void new_generation() { generation8 = (generation8 + 8) & GenerationMask; }
	uint8_t generation() const { return generation8; }
	int tuple_replace_value(TTTuple* t) const;
	TTTuple* find_existing(const Key64 key) const;
	TTTuple* find_replace(const Key64 key) const;
	int compute_hashfull() const;
	void change_size(size_t mbSize);
	void clear();
	bool is_entry_in_use(const Key64 key, Depth depth) const;
	void mark_entry_in_use(const Key64 key, Depth depth, uint8_t value);

	inline TTTuple* tuple(const Key64 key) const
	{
		//return &table[(size_t)key & (clusterCount - 1)].entry[0];
		return (TTTuple*)((char *)table + (key & clusterMask));
	}
	inline void prefetch_tuple(const Key64 key) const
	{
		prefetch(tuple(key));
	}
	bool save_to_file(string uci_hash_file);
	bool load_from_file(string uci_hash_file);

private:
	size_t clusterCount;
	size_t clusterMask;
	Cluster* table;
	uint8_t generation8;
};

extern TranspositionTable MainHash;

#endif // #ifndef MOVE_HASH_H
