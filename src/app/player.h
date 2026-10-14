// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/types.h"


Player getPlayer(void *handle, bool skipIsValidCheck, uint64_t personAddress, uint64_t playerAddress);
uint64_t getPersonAddressFromPlayerAddress(void *handle, uint64_t playerAddress);
uint64_t getCurrentPersonUniqueId(const ProcessContext *processContext);
Player getPlayerById(const ProcessContext *processContext, uint64_t uniqueId);
PositionWeights *getWeightsForPosition(PositionGrouped position);
void getSortedPositionRatings(Player *player);
uint8_t player_getAge(Date dateOfBirth);
FootPreference player_getFoot(const Player *player);
const char *player_getFootName(FootPreference foot);

// Long enough for "Forename Surname" at their maximum lengths
#define PLAYER_DISPLAY_NAME_LENGTH (PERSON_FORENAME_LENGTH + PERSON_SURNAME_LENGTH + 2)
void player_getDisplayName(const Player *player, char *outName, size_t size);
