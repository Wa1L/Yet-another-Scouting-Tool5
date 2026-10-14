// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cache.h"
#include "app/callbacks.h"
#include "app/config.h"
#include "app/injury-names.h"
#include "app/maths.h"
#include "app/mocks.h"
#include "app/my-club.h"
#include "app/player.h"
#include "app/player-table.h"
#include "app/search-handler.h"
#include "app/helpers/formatter.h"
#include "core/logger.h"
#include "platform/platform.h"

#include <stdlib.h>

#include "ui.h"


extern ProcessContext processContext;
extern GameContext gameContext;

static GThread *threads[THREAD_COUNT];
static gint cacheInProgress = false;
static uint8_t completedPlayerThreads = 0;

/**
 * Bumped on every run and every clear, and captured by each worker when it starts. A publish whose
 * generation no longer matches belongs to a cache run that has since been abandoned, so its buffer
 * is discarded instead of being swapped in.
 *
 * Only ever touched on the main thread, and only while no workers are running.
 */
static uint32_t cacheGeneration = 0;

/**
 * The club search runs on a detached thread that cannot be joined, so it can be halfway through
 * gameContext.clubs when the main thread decides to free it. Every swap and free of that buffer is
 * taken under this lock, and the search holds it for the length of its scan.
 */
static GMutex clubsLock;

// Filled by the player workers and swapped into gameContext once the last of them finishes.
static Player *stagingPlayers = NULL;
static uint64_t stagingPlayerCount = 0;

typedef struct {
	uint32_t generation;
	uint8_t index;
} Worker;

typedef struct {
	Nation *nations;
	uint64_t count;
	uint32_t generation;
} NationsPublish;

typedef struct {
	Club *clubs;
	uint64_t count;
	uint32_t generation;
} ClubsPublish;

static gboolean onPlayerThreadComplete(gpointer userData);
static void cachePlayers(uint8_t workerIndex);
static void cacheClubs(uint32_t generation);
static void cacheNations(uint32_t generation);

void cache_lockClubs(void) {
	g_mutex_lock(&clubsLock);
}

void cache_unlockClubs(void) {
	g_mutex_unlock(&clubsLock);
}

/**
 * The result table's rows point into the buffer we just replaced, so they are rebuilt here rather
 * than left to dereference freed memory on the next redraw. Open windows own copies of their
 * players and are refreshed separately, once the replacement buffer exists.
 */
static void invalidatePlayerTable(void) {
	// The table is built after the first cache_clear(), so there may be nothing to invalidate yet.
	if (gameContext.searchResults == NULL) {
		return;
	}

	if (gameContext.playerCount > 0 && gameContext.filterOptions.filterMask != 0) {
		searchHandler_doSearch(false);
	} else {
		playerTable_clear();
	}
}

static gpointer threadFunction(gpointer arg) {
	Worker *worker = arg;

	if (worker->index == 0) {
		cacheNations(worker->generation);
		g_free(worker);
	} else if (worker->index == 1) {
		cacheClubs(worker->generation);
		g_free(worker);
	} else {
		cachePlayers(worker->index - NON_PLAYERS_THREAD_COUNT);
		g_idle_add(onPlayerThreadComplete, worker);
	}

	return NULL;
}

// Swaps a finished buffer into gameContext, replacing whatever it held before.
static gboolean publishNations(gpointer userData) {
	NationsPublish *publish = userData;

	if (publish->generation == cacheGeneration) {
		free(gameContext.nations);
		gameContext.nations = publish->nations;
		gameContext.nationCount = publish->count;
		ui_rerenderPlayerWindows();
	} else {
		LOG_DEBUG("Discarding nations from an abandoned cache run");
		free(publish->nations);
	}

	g_free(publish);

	return G_SOURCE_REMOVE;
}

static gboolean publishClubs(gpointer userData) {
	ClubsPublish *publish = userData;

	if (publish->generation == cacheGeneration) {
		cache_lockClubs();
		free(gameContext.clubs);
		gameContext.clubs = publish->clubs;
		gameContext.clubCount = publish->count;
		cache_unlockClubs();
		ui_rerenderPlayerWindows();
	} else {
		LOG_DEBUG("Discarding clubs from an abandoned cache run");
		free(publish->clubs);
	}

	g_free(publish);

	return G_SOURCE_REMOVE;
}

/**
 * Abandons any in-flight cache run and drops the players, but leaves the clubs and nations in
 * place: they are lookup tables the UI reads constantly, and the re-cache that follows takes
 * seconds to land. Freeing them here left the club search scanning an empty list — and so showing
 * no popover — for most of the time the game was unpaused, since every in-game clock change forces
 * a re-cache. publishClubs/publishNations free whatever they replace, so nothing is leaked.
 *
 * Use cache_reset() when the buffers must actually go, i.e. when the process is gone.
 */
void cache_clear(void) {
	g_atomic_int_set(&cacheInProgress, false);

	for (uint8_t i = 0; i < THREAD_COUNT; i++) {
		if (threads[i] != NULL) {
			g_thread_join(threads[i]);
			threads[i] = NULL;
		}
	}

	// Invalidates any publish already queued by a worker that finished before the join.
	++cacheGeneration;
	completedPlayerThreads = 0;

	free(stagingPlayers);
	stagingPlayers = NULL;
	stagingPlayerCount = 0;

	const bool hadPlayers = gameContext.players != NULL;

	if (gameContext.players != NULL) {
		free(gameContext.players);
		gameContext.players = NULL;
		gameContext.playerCount = 0;
		myClub_reset();
	}

	// Called every tick while disconnected, so only pay for the rebuild when something was dropped.
	// Open windows are left showing the players they already hold until a new cache arrives.
	if (hadPlayers) {
		invalidatePlayerTable();
	}
}

// Drops everything, including the club and nation lookup tables that cache_clear() deliberately
// keeps. The join inside cache_clear() covers the cache workers; the detached club search does not
// join, so the clubs swap is taken under its lock.
void cache_reset(void) {
	cache_clear();

	if (gameContext.clubs != NULL) {
		cache_lockClubs();
		free(gameContext.clubs);
		gameContext.clubs = NULL;
		gameContext.clubCount = 0;
		cache_unlockClubs();
	}
	if (gameContext.nations != NULL) {
		free(gameContext.nations);
		gameContext.nations = NULL;
		gameContext.nationCount = 0;
	}
}


static void cacheNations(const uint32_t generation) {
#ifdef DEBUG
	const int64_t timeStart = platform_getMicroseconds();
#endif

#ifndef MOCKS_MODE
	uint8_t bytes[8];
	readFromMemory(processContext.handle, processContext.moduleBaseAddress + NATION_LIST_PTR_BASE, 8, bytes);
	readFromMemory(processContext.handle, hexBytesToInt(bytes, 8) + NATION_LIST_PTR_BASE_OFFSET, 8, bytes);

	uint8_t nationStartBuffer[8];
	uint8_t nationEndBuffer[8];
	readFromMemory(processContext.handle, hexBytesToInt(bytes, 8) + NATION_LIST_START, 8, nationStartBuffer);
	readFromMemory(processContext.handle, hexBytesToInt(bytes, 8) + NATION_LIST_END, 8, nationEndBuffer);
	const uint64_t nationStart = hexBytesToInt(nationStartBuffer, 8);
	const uint64_t nationEnd = hexBytesToInt(nationEndBuffer, 8);
	const uint64_t nationCount = (nationEnd - nationStart) / NATION_LIST_STRIDE;
	Nation *nations = calloc(nationCount, sizeof(Nation));
	if (nations == NULL) {
		LOG_ERROR("Failed to allocate memory for %" PRIu64 " nations", nationCount);
		return;
	}

	for (uint64_t i = 0; i < nationCount; i++) {
		if (!g_atomic_int_get(&cacheInProgress)) {
			LOG_DEBUG("Ending nations cache early");
			free(nations);
			return;
		}

		uint8_t nationBuffer[8];
		readFromMemory(processContext.handle, nationStart + i * NATION_LIST_STRIDE, 8, nationBuffer);
		readFromMemory(processContext.handle, hexBytesToInt(nationBuffer, 8) + NATION_OFFSET_NAME, 8, bytes);
		const uint64_t nameAddress = hexBytesToInt(bytes, 8);
		if (!nameAddress) {
			LOG_WARN("Nation %" PRIu64 " has no name pointer", i);
			free(nations);
			return;
		}
		readFromMemory(
			processContext.handle,
			nameAddress + STRING_OFFSET_VALUE,
			MAX_NATION_STRING_LENGTH,
			(uint8_t*)nations[i].name
		);
		readFromMemory(processContext.handle, hexBytesToInt(nationBuffer, 8) + NATION_OFFSET_NAME_CODE, 8, bytes);
		readFromMemory(
			processContext.handle,
			hexBytesToInt(bytes, 8) + STRING_OFFSET_VALUE,
			4,
			(uint8_t*)nations[i].code
		);
	}
#else
	const uint64_t nationCount = 251;
	Nation *nations = calloc(nationCount, sizeof(Nation));
	if (nations == NULL) {
		LOG_ERROR("Failed to allocate memory for %" PRIu64 " nations", nationCount);
		return;
	}
	nations[189] = PLAYER_BY_ID_NATION_1;
	nations[170] = PLAYER_BY_ID_NATION_2;
#endif

	NationsPublish *publish = g_new(NationsPublish, 1);
	publish->nations = nations;
	publish->count = nationCount;
	publish->generation = generation;
	g_idle_add(publishNations, publish);

	LOG_DEBUG(
		"Cached %" PRIu64 " nations in %" PRId64 " microseconds",
		nationCount,
		platform_getMicroseconds() - timeStart
	);
}

static void cacheClubs(const uint32_t generation) {
#ifdef DEBUG
	const int64_t timeStart = platform_getMicroseconds();
#endif

#ifndef MOCKS_MODE
	uint8_t bytes[8];
	readFromMemory(processContext.handle, processContext.moduleBaseAddress + CLUB_LIST_PTR_BASE, 4, bytes);
	readFromMemory(processContext.handle, hexBytesToInt(bytes, 4) + CLUB_LIST_PTR_BASE_OFFSET, 4, bytes);

	uint8_t clubStartBuffer[8];
	uint8_t clubEndBuffer[8];
	readFromMemory(processContext.handle, hexBytesToInt(bytes, 8) + CLUB_LIST_START, 8, clubStartBuffer);
	readFromMemory(processContext.handle, hexBytesToInt(bytes, 8) + CLUB_LIST_END, 8, clubEndBuffer);
	const uint64_t clubStart = hexBytesToInt(clubStartBuffer, 8);
	const uint64_t clubEnd = hexBytesToInt(clubEndBuffer, 8);
	const uint64_t clubCount = (clubEnd - clubStart) / CLUB_LIST_STRIDE;
	// calloc so clubs we fail to read stay blank instead of holding another club's name; the slot
	// is kept either way because a player's clubIndex is a row ID, i.e. a position in this list.
	Club *clubs = calloc(clubCount, sizeof(Club));
	if (clubs == NULL) {
		LOG_ERROR("Failed to allocate memory for %" PRIu64 " clubs", (unsigned long long)clubCount);
		return;
	}

	uint64_t missed = 0;
	for (uint64_t i = 0; i < clubCount; i++) {
		if (!g_atomic_int_get(&cacheInProgress)) {
			LOG_DEBUG("Ending clubs cache early");
			free(clubs);
			return;
		}

		uint8_t clubBuffer[8];
		readFromMemory(processContext.handle, clubStart + i * CLUB_LIST_STRIDE, 8, clubBuffer);
		const uint64_t clubAddress = hexBytesToInt(clubBuffer, 8);
		clubs[i].address = clubAddress;
		readFromMemory(processContext.handle, clubAddress + CLUB_OFFSET_NAME, 8, bytes);
		uint64_t namePointer = (uint32_t)hexBytesToInt(bytes, 8);
		if (!namePointer || !readFromMemory(
			processContext.handle,
			hexBytesToInt(bytes, 8) + STRING_OFFSET_VALUE,
			CLUB_LONG_NAME_LENGTH,
			(uint8_t*)clubs[i].name
		)) {
			clubs[i].name[0] = '\0';
			missed++;
			continue;
		}

		readFromMemory(processContext.handle, clubAddress + CLUB_OFFSET_NAME_SHORT, 8, bytes);
		namePointer = (uint32_t)hexBytesToInt(bytes, 8);
		readFromMemory(
			processContext.handle,
			hexBytesToInt(bytes, 8) + STRING_OFFSET_VALUE,
			CLUB_SHORT_NAME_LENGTH,
			(uint8_t*)clubs[i].shortName
		);
	}

	const uint64_t cachedClubCount = clubCount;
	if (missed > 0) {
		LOG_WARN("Could not read a name for %" PRIu64 " of %" PRIu64 " clubs", missed, clubCount);
	}
#else
	const uint32_t clubCount = 36289;
	Club *clubs = calloc(clubCount, sizeof(Club));
	if (clubs == NULL) {
		LOG_ERROR("Failed to allocate memory for %" PRIu32 " clubs", clubCount);
		return;
	}
	clubs[1125] = PLAYER_BY_ID_CLUB;
	const uint64_t cachedClubCount = clubCount;
#endif

	ClubsPublish *publish = g_new(ClubsPublish, 1);
	publish->clubs = clubs;
	publish->count = cachedClubCount;
	publish->generation = generation;
	g_idle_add(publishClubs, publish);

	LOG_DEBUG(
		"Cached %" PRIu64 " clubs in %" PRId64 " microseconds",
		cachedClubCount,
		platform_getMicroseconds() - timeStart
	);
}

static void cachePlayers(const uint8_t workerIndex) {
#ifdef DEBUG
	const int64_t timeStart = platform_getMicroseconds();
	uint64_t cached = 0;
#endif

#ifndef MOCKS_MODE
	const uint64_t start = stagingPlayerCount * workerIndex / PLAYERS_THREAD_COUNT;
	const uint64_t end = stagingPlayerCount * (workerIndex + 1) / PLAYERS_THREAD_COUNT;

	uint8_t bytes[8];
	readFromMemory(processContext.handle, processContext.moduleBaseAddress + PLAYER_LIST_PTR_BASE, 8, bytes);
	const uint64_t playerStart = hexBytesToInt(bytes, 8);
	for (uint64_t i = start; i < end; i++) {
		if (!g_atomic_int_get(&cacheInProgress)) {
			LOG_DEBUG("Ending players cache early");
			return;
		}

		readFromMemory(processContext.handle, playerStart + i * PLAYER_LIST_STRIDE, 8, bytes);
		const uint64_t playerAddress = hexBytesToInt(bytes, 8);
		const uint64_t personAddress = getPersonAddressFromPlayerAddress(processContext.handle, playerAddress);
		const Player player = getPlayer(processContext.handle, true, personAddress, playerAddress);
		if (!player.uid) {
			continue;
		}
		stagingPlayers[i] = player;
#ifdef DEBUG
		++cached;
#endif
	}
#else
	Player playerVini = PLAYER_VINI;
	playerVini.injury.nameIndex = injuryNames_intern(MOCK_INJURY_NAME, sizeof(MOCK_INJURY_NAME) - 1);
	const Player playerJeff = PLAYER_JEFF;
	const Player playerGk = PLAYER_GK;
	const uint64_t start = stagingPlayerCount * workerIndex / PLAYERS_THREAD_COUNT;
	const uint64_t end = stagingPlayerCount * (workerIndex + 1) / PLAYERS_THREAD_COUNT;
	for (uint64_t i = start; i < end; i++) {
#ifdef DEBUG
	++cached;
#endif
	if (i % 3 == 0) {
		memcpy(&stagingPlayers[i], &playerGk, sizeof(Player));
	} else {
		memcpy(&stagingPlayers[i], i & 1 ? &playerVini : &playerJeff, sizeof(Player));
	}
	}
#endif

	LOG_DEBUG("Cached %" PRIu64 " players in %" PRId64 " microseconds", cached, platform_getMicroseconds() - timeStart);
}

void cache_run(void) {
	if (g_atomic_int_get(&cacheInProgress)) {
		return;
	}

	// Prepare player array for multithreaded writing
#ifndef MOCKS_MODE
	uint8_t bytes[8];
	readFromMemory(processContext.handle, processContext.moduleBaseAddress + PLAYER_LIST_PTR_BASE, 8, bytes);
	const uint64_t playerStart = hexBytesToInt(bytes, 8);
	readFromMemory(processContext.handle, processContext.moduleBaseAddress + PLAYER_LIST_PTR_BASE + 0x08, 8, bytes);
	const uint64_t playerEnd = hexBytesToInt(bytes, 8);
	const uint64_t playerCount = (playerEnd - playerStart) / 8;
#else
	const uint64_t playerCount = 900;
#endif

	// calloc so that slots the workers skip stay zeroed (uid == 0) and are
	// recognisable to compactPlayers().
	free(stagingPlayers);
	stagingPlayers = calloc(playerCount, sizeof(Player));
	if (stagingPlayers == NULL) {
		LOG_ERROR("Failed to allocate memory for %" PRIu64 " players", playerCount);
		stagingPlayerCount = 0;
		return;
	}
	stagingPlayerCount = playerCount;

	// Workers read the generation and the staging buffer, both of which are only ever written here
	// and in cache_clear(), which joins every worker first.
	++cacheGeneration;
	completedPlayerThreads = 0;
	g_atomic_int_set(&cacheInProgress, true);
	for (uint8_t i = 0; i < THREAD_COUNT; i++) {
		char buffer[12] = {0};
		snprintf(buffer, sizeof(buffer), "worker-%d", i);
		Worker *worker = g_new(Worker, 1);
		worker->generation = cacheGeneration;
		worker->index = i;
		threads[i] = g_thread_new(buffer, threadFunction, worker);
	}
}

static gboolean onPlayerThreadComplete(gpointer userData) {
	Worker *worker = userData;
	const uint32_t generation = worker->generation;
	g_free(worker);

	if (generation != cacheGeneration) {
		LOG_DEBUG("Ignoring player worker from an abandoned cache run");
		return G_SOURCE_REMOVE;
	}

	// Back in main thread; the players only become visible once every worker has finished writing.
	if (++completedPlayerThreads < PLAYERS_THREAD_COUNT) {
		return G_SOURCE_REMOVE;
	}

	g_atomic_int_set(&cacheInProgress, false);

	free(gameContext.players);
	gameContext.players = stagingPlayers;
	gameContext.playerCount = stagingPlayerCount;
	stagingPlayers = NULL;
	stagingPlayerCount = 0;
	myClub_rebuild();

	char buffer[FORMATTER_NUMBER_SIZE];
	snprintf(buffer, sizeof(buffer), "%llu", gameContext.playerCount);
	formatter_printNumber(buffer);
	char bufferStatus[32];
	snprintf(bufferStatus, sizeof(bufferStatus), "%s players cached", buffer);
	ui_setCurrentStatus(bufferStatus);

	invalidatePlayerTable();
	ui_rebindPlayerWindows();

	return G_SOURCE_REMOVE;
}
