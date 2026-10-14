// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "player-snapshot.h"
#include "core/logger.h"

#include <stdlib.h>
#include <string.h>


extern GameContext gameContext;

static PlayerSnapshot *allocateSnapshot(const uint32_t count) {
	PlayerSnapshot *snapshot = malloc(sizeof(PlayerSnapshot));
	if (snapshot == NULL) {
		LOG_ERROR("Failed to allocate a player snapshot");
		return NULL;
	}

	snapshot->players = count > 0 ? malloc((size_t)count * sizeof(Player)) : NULL;
	if (count > 0 && snapshot->players == NULL) {
		LOG_ERROR("Failed to allocate memory for %u snapshot players", count);
		free(snapshot);
		return NULL;
	}

	snapshot->count = 0;

	return snapshot;
}

/** Copies the players at `playerIds` (indices into the live cache), skipping any that are stale. */
PlayerSnapshot *playerSnapshot_create(const uint32_t *playerIds, const uint64_t count) {
	PlayerSnapshot *snapshot = allocateSnapshot((uint32_t)count);
	if (snapshot == NULL) {
		return NULL;
	}

	for (uint64_t i = 0; i < count; ++i) {
		if (playerIds[i] >= gameContext.playerCount) {
			continue;
		}

		snapshot->players[snapshot->count++] = gameContext.players[playerIds[i]];
	}

	return snapshot;
}

PlayerSnapshot *playerSnapshot_createOne(const Player *player) {
	PlayerSnapshot *snapshot = allocateSnapshot(1);
	if (snapshot == NULL) {
		return NULL;
	}

	snapshot->players[0] = *player;
	snapshot->count = 1;

	return snapshot;
}

// Signature matches GDestroyNotify so a window can own its snapshot directly.
void playerSnapshot_free(void *snapshot) {
	PlayerSnapshot *self = snapshot;
	if (self == NULL) {
		return;
	}

	free(self->players);
	free(self);
}

/**
 * Open addressing with linear probing over a power-of-two table, sized so it stays under half full.
 * Cache slots the workers skipped are left zeroed, and uid 0 is not a real player, so those double
 * as the empty marker.
 */
bool playerLookup_build(PlayerLookup *out) {
	out->slots = NULL;
	out->mask = 0;

	if (gameContext.players == NULL || gameContext.playerCount == 0) {
		return false;
	}

	uint64_t size = 16;
	while (size < gameContext.playerCount * 2) {
		size <<= 1;
	}

	const Player **slots = calloc(size, sizeof(const Player*));
	if (slots == NULL) {
		LOG_ERROR("Failed to allocate a player lookup of %llu slots", (unsigned long long)size);
		return false;
	}

	const uint32_t mask = (uint32_t)(size - 1);
	for (uint64_t i = 0; i < gameContext.playerCount; ++i) {
		const Player *player = &gameContext.players[i];
		if (player->uid == 0) {
			continue;
		}

		uint32_t slot = player->uid * 2654435761u & mask;
		while (slots[slot] != NULL) {
			if (slots[slot]->uid == player->uid) {
				break; // Duplicate uid: the first one read wins.
			}
			slot = slot + 1 & mask;
		}
		if (slots[slot] == NULL) {
			slots[slot] = player;
		}
	}

	out->slots = slots;
	out->mask = mask;

	return true;
}

void playerLookup_destroy(PlayerLookup *lookup) {
	free(lookup->slots);
	lookup->slots = NULL;
	lookup->mask = 0;
}

const Player *playerLookup_find(const PlayerLookup *lookup, const uint32_t uid) {
	if (lookup == NULL || lookup->slots == NULL) {
		return NULL;
	}

	uint32_t slot = uid * 2654435761u & lookup->mask;
	while (lookup->slots[slot] != NULL) {
		if (lookup->slots[slot]->uid == uid) {
			return lookup->slots[slot];
		}
		slot = slot + 1 & lookup->mask;
	}

	return NULL;
}

/** Re-points every copy at the player's current data, leaving anyone the cache has dropped as-is. */
void playerSnapshot_refresh(PlayerSnapshot *snapshot, const PlayerLookup *lookup) {
	if (snapshot == NULL || lookup == NULL || lookup->slots == NULL) {
		return;
	}

	for (uint32_t i = 0; i < snapshot->count; ++i) {
		const Player *current = playerLookup_find(lookup, snapshot->players[i].uid);
		if (current != NULL) {
			snapshot->players[i] = *current;
		}
	}
}
