/*
*/

#ifndef SEARCH_SMP_H
#define SEARCH_SMP_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "houdini.h"

//#ifdef USE_NUMA
//#   undef _WIN32_WINNT
//#   define _WIN32_WINNT 0x0601
//#endif

#ifndef NOMINMAX
#   define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#undef NOMINMAX

#include "eval_material.h"
#include "move_selection.h"
#include "eval_pawnstruct.h"
#include "position.h"
#include "search.h"
#include "util_win32.h"

#if defined(PREMIUM) || defined(PRO_LICENSE)
const int MAX_THREADS = 128;
#else
const int MAX_THREADS = 8;
#endif
const int MAX_NUMA_NODES = 16;

void *alloc_large_page_mem(size_t size, bool *large_page, bool exit_on_error, int numa_node);
void free_large_page_mem(void *p);
void test_large_pages();


struct NumaInfo
{
	CounterMoveHistory counterMoveStatistics;
};

struct ThreadInfo
{
	Position rootPosition;
	PositionInfo positionInfo[1024];
	MoveEx moveList[8192];
	MoveValueStats history;
	MoveValueStats evasionHistory;
	MaxGainStats maxGainTable;
	CounterMoveStats counterMoves;
	CounterFollowUpMoveStats counterfollowupMoves;
	MoveValueStats captureHistory;
	Material::Table materialTable;
	Pawns::Table pawnsTable;
};

class Thread
{
	std::thread nativeThread;
	Mutex mutex;
	ConditionVariable sleepCondition;
	bool exit, searchActive;
	int threadIndex;

public:
	Thread();
	virtual ~Thread();
	virtual void start_search();
	void idle_loop();
	void wake_up(bool activate_search);
	void wait_for_search_finished();
	void wait(std::atomic_bool& b);

	ThreadInfo* ti;
	NumaInfo* ni;
	Position* rootPosition;

	RootMoves rootMoves;
	Depth completedDepth, previousTacticalDepth;
	bool tacticalMode, tacticalModeUsed;
	int activePV;
};


struct MainThread : public Thread
{
	virtual void start_search();

	bool quickMoveAllowed, quickMovePlayed, quickMoveEvaluationBusy, quickMoveEvaluationAborted, failedLow;
	int bestMoveChanges;
	Value previousRootScore;
	int interruptCounter;
	//bool quickMoveTested;
	Depth previousRootDepth;
};


struct ThreadPool
{
	void initialize();
	void exit();

	int threadCount;
	TimePoint programStart;
	int totalAnalysisTime;
	Thread* threads[MAX_THREADS];
	MainThread* main() { return static_cast<MainThread*>(threads[0]); }
	void start_search_task(Position&, const TimeLimit&);
	void change_thread_count();
	uint64_t visited_nodes();
	uint64_t tb_hits();
	void clear_counter_move_history();

	int activeThreadCount; // can be different, with PlayStrength < 100 only 1 thread is used
	int tamperedWithExe;
	Color contemptColor;
	int pieceContempt;
	Value rootContemptValue;
	Endgames endgames;
	Position* rootPosition;
	RootMoves rootMoves;
	PositionInfo* rootPositionInfo;
	bool analysisMode;
	int tacticalModeIndex;
	int fiftyMovesDistance;
	int multiPV, multiPV_cp, multiPVmax;
	int playStrength;
	uint64_t playStrengthMaxNodes;
	bool dummyNullMoveThreat, dummyProbCut;
	bool hideRedundantOutput;

#ifdef USE_NUMA
private:
	GROUP_AFFINITY NUMA_affinities[MAX_NUMA_NODES];
	int NUMA_offset = 0;
	void initialize_Numa();
	void reinitialize_Numa();
	int get_numa_node_affinities();
public:
	unsigned long NUMA_highest_node;
	NumaInfo* NUMA_data[MAX_NUMA_NODES];
	int numa_node_for_thread[MAX_THREADS];
	void set_thread_affinity(int thread_id);
	void change_numa(bool on);
	void change_numa_offset(int offset);
#endif
};

extern ThreadPool Threads;

#endif // #ifndef SEARCH_SMP_H
