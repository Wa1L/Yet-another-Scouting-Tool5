// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/player.h"
#include "app/types.h"


/**
 * A shortlisted player, along with the data we saw for them when they were added (or when their
 * baseline was last reset), which is what their progress is measured against.
 */
typedef struct {
	char name[PLAYER_DISPLAY_NAME_LENGTH];
	uint8_t attributes[ATTRIBUTE_COUNT];
	Date since; // In-game date the baseline was taken
	uint32_t uid;
	uint32_t value;
	uint8_t ca;
	uint8_t pa;
} ShortlistEntry;

void shortlist_init(void);
void shortlist_shutdown(void);
uint64_t shortlist_count(void);
const ShortlistEntry *shortlist_getEntry(uint64_t index);
const ShortlistEntry *shortlist_find(uint32_t uid);
void shortlist_add(const Player *player);
void shortlist_remove(uint32_t uid);
void shortlist_toggle(const Player *player);
void shortlist_resetBaseline(const Player *player);
float shortlist_getBaselineRating(const ShortlistEntry *entry, const Player *player);
