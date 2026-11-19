/*
*/

#include <iostream>

#include "houdini.h"
#include "bitboard.h"
#include "move_hash.h"

TranspositionTable HoofdHash;

void TranspositionTable::change_size(size_t mbSize) 
{
	bool large_pages;
	size_t nieuwClusterAantal = size_t(1) << msb((mbSize * 1024 * 1024) / sizeof(Cluster));

	if (nieuwClusterAantal == clusterAantal)
		return;

	if (tabel)
		free_large_page_mem(tabel);
	clusterAantal = 0;

	tabel = (Cluster*)alloc_large_page_mem(nieuwClusterAantal * sizeof(Cluster), &large_pages, false, -1);

	if (!tabel)
	{
		std::cerr << "ERROR: Unable to allocate the requested hash size " << mbSize << " MB" << std::endl;
		// probeer opnieuw met 1 MB
		nieuwClusterAantal = 32768;
		tabel = (Cluster*)alloc_large_page_mem(nieuwClusterAantal * sizeof(Cluster), &large_pages, true, -1);
	}
	sync_cout << "info string " << nieuwClusterAantal * sizeof(Cluster) / (1024 * 1024) << " MB " 
		<< (large_pages ? "Large Page " : "") << "Hash" << sync_endl;

	clusterAantal = nieuwClusterAantal;
	clusterMask = (clusterAantal - 1) * sizeof(Cluster);
}


void TranspositionTable::clear() 
{
	std::memset(tabel, 0, clusterAantal * sizeof(Cluster));
}

bool TranspositionTable::is_entry_in_use(const Key64 sleutel, Depth d) const
{
	TTTuple* tt = find_existing(sleutel);
	int dd = d / PLY;
	return tt && (tt->vlaggen8 & IN_USE) && (dd >= tt->diepte8);
}

void TranspositionTable::mark_entry_in_use(const Key64 sleutel, Depth diepte, uint8_t waarde)
{
	uint16_t sleutel16 = sleutel >> 48;
	TTTuple* tt = find_replace(sleutel);
	if (tt->sleutel16 != sleutel16)
		tt->save(sleutel, NO_VALUE, NO_BOUND, diepte, NO_MOVE,
			NO_VALUE, HoofdHash.generatie());
	tt->vlaggen8 = (tt->vlaggen8 & UsageMask) | waarde;
}


TTTuple* TranspositionTable::find_existing(const Key64 sleutel) const
{
	TTTuple* const ttt = tuple(sleutel);
	uint16_t sleutel16 = sleutel >> 48;
	//sleutel16 = std::max(sleutel16, (uint16_t)1);

	for (int i = 0; i < ClusterSize; ++i)
		if (ttt[i].sleutel16 == sleutel16)
		{
			if ((ttt[i].vlaggen8 & GenerationMask) != generatie8)
				ttt[i].vlaggen8 = uint8_t(generatie8 + (ttt[i].vlaggen8 & FlagsMask));

			return &ttt[i];
		}

	return nullptr;
}


TTTuple * TranspositionTable::find_replace(const Key64 sleutel) const
{
	TTTuple* const ttt = tuple(sleutel);
	uint16_t sleutel16 = sleutel >> 48;

	for (int i = 0; i < ClusterSize; ++i)
		if (ttt[i].sleutel16 == 0 || ttt[i].sleutel16 == sleutel16)
			return &ttt[i];

	TTTuple* vervanging = ttt;
	for (int i = 1; i < ClusterSize; ++i)
		if (vervanging->diepte8 - ((generatie8 - (vervanging->vlaggen8 & GenerationMask)) & GenerationMask)
				> ttt[i].diepte8 - ((generatie8 - (ttt[i].vlaggen8 & GenerationMask)) & GenerationMask))
			vervanging = &ttt[i];

	return vervanging;
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
		uint64_t save_waarden[4];
		save_waarden[0] = clusterAantal;
		save_waarden[1] = generatie8;
		save_waarden[2] = 0;
		save_waarden[3] = 0;
		result = fwrite(&save_waarden[0], sizeof(save_waarden[0]), 4, hashfile) == 4;
	}

	uint64_t hash_grootte = clusterAantal;
	Cluster *pp = tabel;
	while (result && hash_grootte)
	{
		uint64_t blok_grootte = std::min(hash_grootte, uint64_t(32 * 1024 * 1024));
		result = fwrite(pp, sizeof(Cluster), (size_t)blok_grootte, hashfile) == blok_grootte;
		hash_grootte -= blok_grootte;
		pp += blok_grootte;
	}

	fclose(hashfile);
	return result;
}

bool TranspositionTable::load_from_file(string uci_hash_file) 
{
	char ID[17];
	uint64_t hash_grootte, blok_grootte;
	uint64_t save_waarden[4];

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
		Result = fread(&save_waarden[0], sizeof(save_waarden[0]), 4, hashfile) == 4;

	if (Result)
	{
		hash_grootte = save_waarden[0];

		if (Result && hash_grootte != clusterAantal)
		{
			size_t mb = (size_t)((hash_grootte * sizeof(Cluster)) >> 20);
			change_size(mb);
			Result = hash_grootte == clusterAantal;
		}

		if (Result)
			generatie8 = (uint8_t)save_waarden[1];

		Cluster *pp = tabel;
		while (Result && hash_grootte)
		{
			blok_grootte = std::min(hash_grootte, uint64_t(32 * 1024 * 1024));
			Result = fread(pp, sizeof(Cluster), (size_t)blok_grootte, hashfile) == blok_grootte;
			hash_grootte -= blok_grootte;
			pp += blok_grootte;
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
		const TTTuple* ttt = &tabel[i].entry[0];
		for (int j = 0; j < ClusterSize; j++)
			if (ttt[j].sleutel16 && (ttt[j].vlaggen8 & GenerationMask) == generatie8)
				cnt++;
	}
	return cnt * 1000 / (i_max * ClusterSize);
}
