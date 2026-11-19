/*
*/

#include <iostream>

#include "houdini.h"
#include "bitboard.h"
#include "move_hash.h"

TranspositionTable MainHash;

void TranspositionTable::change_size(size_t mbSize) 
{
	bool large_pages;
	size_t newClusterCount = size_t(1) << msb((mbSize * 1024 * 1024) / sizeof(Cluster));

	if (newClusterCount == clusterCount)
		return;

	if (table)
		free_large_page_mem(table);
	clusterCount = 0;

	table = (Cluster*)alloc_large_page_mem(newClusterCount * sizeof(Cluster), &large_pages, false, -1);

	if (!table)
	{
		std::cerr << "ERROR: Unable to allocate the requested hash size " << mbSize << " MB" << std::endl;
		// try again with 1 MB
		newClusterCount = 32768;
		table = (Cluster*)alloc_large_page_mem(newClusterCount * sizeof(Cluster), &large_pages, true, -1);
	}
	sync_cout << "info string " << newClusterCount * sizeof(Cluster) / (1024 * 1024) << " MB " 
		<< (large_pages ? "Large Page " : "") << "Hash" << sync_endl;

	clusterCount = newClusterCount;
	clusterMask = (clusterCount - 1) * sizeof(Cluster);
}


void TranspositionTable::clear() 
{
	std::memset(table, 0, clusterCount * sizeof(Cluster));
}

bool TranspositionTable::is_entry_in_use(const Key64 key, Depth d) const
{
	TTTuple* tt = find_existing(key);
	int dd = d / PLY;
	return tt && (tt->flags8 & IN_USE) && (dd >= tt->depth8);
}

void TranspositionTable::mark_entry_in_use(const Key64 key, Depth depth, uint8_t value)
{
	uint16_t key16 = key >> 48;
	TTTuple* tt = find_replace(key);
	if (tt->key16 != key16)
		tt->save(key, NO_VALUE, NO_BOUND, depth, NO_MOVE,
			NO_VALUE, MainHash.generation());
	tt->flags8 = (tt->flags8 & UsageMask) | value;
}


TTTuple* TranspositionTable::find_existing(const Key64 key) const
{
	TTTuple* const ttt = tuple(key);
	uint16_t key16 = key >> 48;
	//key16 = std::max(key16, (uint16_t)1);

	for (int i = 0; i < ClusterSize; ++i)
		if (ttt[i].key16 == key16)
		{
			if ((ttt[i].flags8 & GenerationMask) != generation8)
				ttt[i].flags8 = uint8_t(generation8 + (ttt[i].flags8 & FlagsMask));

			return &ttt[i];
		}

	return nullptr;
}


TTTuple * TranspositionTable::find_replace(const Key64 key) const
{
	TTTuple* const ttt = tuple(key);
	uint16_t key16 = key >> 48;

	for (int i = 0; i < ClusterSize; ++i)
		if (ttt[i].key16 == 0 || ttt[i].key16 == key16)
			return &ttt[i];

	TTTuple* replacement = ttt;
	for (int i = 1; i < ClusterSize; ++i)
		if (replacement->depth8 - ((generation8 - (replacement->flags8 & GenerationMask)) & GenerationMask)
				> ttt[i].depth8 - ((generation8 - (ttt[i].flags8 & GenerationMask)) & GenerationMask))
			replacement = &ttt[i];

	return replacement;
}


#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#undef NOMINMAX
string GetUserFolder()
{
	char szPath[MAX_PATH];
	SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, szPath);
	return string(szPath);
}
#else
string GetUserFolder()
{
	return "/user/dat/";
}
#endif
static char hash_file_ID[17]  = "HOUDINI_HASH_500";
//static char learn_file_ID[17] = "HOUDINI_LEARN500";

bool TranspositionTable::save_to_file(string uci_hash_file) 
{
	string file_name;
	if (uci_hash_file == "" || uci_hash_file == "<empty>")
		file_name = GetUserFolder() + "\\hash.dat";
	else
		file_name = uci_hash_file;

	FILE *hashfile = fopen(uci_hash_file.c_str(), "wb");
	if (!hashfile)
		return false;

	sync_cout << "info string Saving Hash to File " << file_name << "..." << sync_endl;

	bool result = fwrite(hash_file_ID, 1, 16, hashfile) == 16;

	if (result)
	{
		uint64_t save_values[4];
		save_values[0] = clusterCount;
		save_values[1] = generation8;
		save_values[2] = 0;
		save_values[3] = 0;
		result = fwrite(&save_values[0], sizeof(save_values[0]), 4, hashfile) == 4;
	}

	uint64_t hash_size = clusterCount;
	Cluster *pp = table;
	while (result && hash_size)
	{
		uint64_t block_size = std::min(hash_size, uint64_t(32 * 1024 * 1024));
		result = fwrite(pp, sizeof(Cluster), (size_t)block_size, hashfile) == block_size;
		hash_size -= block_size;
		pp += block_size;
	}

	fclose(hashfile);
	return result;
}

bool TranspositionTable::load_from_file(string uci_hash_file) 
{
	char ID[17];
	uint64_t hash_size, block_size;
	uint64_t save_values[4];

	string file_name;
	if (uci_hash_file == "" || uci_hash_file == "<empty>")
		file_name = GetUserFolder() + "\\hash.dat";
	else
		file_name = uci_hash_file;

	FILE *hashfile = fopen(uci_hash_file.c_str(), "rb");
	if (!hashfile)
		return false;

	sync_cout << "info string Loading Hash from File " << file_name << "..." << sync_endl;

	bool Result = fread(ID, 1, 16, hashfile) == 16;
	if (Result)
		Result = memcmp(ID, hash_file_ID, 16) == 0;

	if (Result)
		Result = fread(&save_values[0], sizeof(save_values[0]), 4, hashfile) == 4;

	if (Result)
	{
		hash_size = save_values[0];

		if (Result && hash_size != clusterCount)
		{
			size_t mb = (size_t)((hash_size * sizeof(Cluster)) >> 20);
			change_size(mb);
			Result = hash_size == clusterCount;
		}

		if (Result)
			generation8 = (uint8_t)save_values[1];

		Cluster *pp = table;
		while (Result && hash_size)
		{
			block_size = std::min(hash_size, uint64_t(32 * 1024 * 1024));
			Result = fread(pp, sizeof(Cluster), (size_t)block_size, hashfile) == block_size;
			hash_size -= block_size;
			pp += block_size;
		}
	}

	fclose(hashfile);
	return Result;
}


int TranspositionTable::compute_hashfull() const
{
	const int i_max = 999 / ClusterSize + 1;
	int cnt = 0;
	for (int i = 0; i < i_max; i++)
	{
		const TTTuple* ttt = &table[i].entry[0];
		for (int j = 0; j < ClusterSize; j++)
			if (ttt[j].key16 && (ttt[j].flags8 & GenerationMask) == generation8)
				cnt++;
	}
	return cnt * 1000 / (i_max * ClusterSize);
}
