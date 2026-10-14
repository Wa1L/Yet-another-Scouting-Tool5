// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/types.h"


/**
 * A window's own copy of the players it was opened with. The cache frees and republishes
 * gameContext.players whenever the save moves on, so a window that held pointers into it would
 * dangle; instead it holds this, and the copies are re-matched to the new buffer by uid after every
 * publish. A player who is no longer in the cache keeps the last data we saw for them.
 *
 * Player is a plain value — its injury name is an index into the interned table, which outlives
 * every cache — so copying one is a straight assignment.
 *
 * The array is allocated once and refreshed in place, so pointers into it stay valid for the life
 * of the window.
 */
typedef struct {
	Player *players;
	uint32_t count;
} PlayerSnapshot;

/**
 * uid → player lookup over the current cache. Building it costs one pass over gameContext.players,
 * so it is built once per refresh pass and shared by every open window.
 */
typedef struct {
	const Player **slots;
	uint32_t mask;
} PlayerLookup;

PlayerSnapshot *playerSnapshot_create(const uint32_t *playerIds, uint64_t count);
PlayerSnapshot *playerSnapshot_createOne(const Player *player);
void playerSnapshot_free(void *snapshot);
void playerSnapshot_replace(PlayerSnapshot *snapshot, const uint32_t *playerIds, uint64_t count);

bool playerLookup_build(PlayerLookup *out);
void playerLookup_destroy(PlayerLookup *lookup);
const Player *playerLookup_find(const PlayerLookup *lookup, uint32_t uid);
void playerSnapshot_refresh(PlayerSnapshot *snapshot, const PlayerLookup *lookup);
