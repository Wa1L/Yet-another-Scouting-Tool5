// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "my-club.h"

#include <stdlib.h>
#include <string.h>


extern GameContext gameContext;

/**
 * How far above the user's club's reputation a player, or the club they are at, can be while we
 * still expect them to be open to a move. Reputations run from 0 to 10,000.
 */
#define INTEREST_REPUTATION_MARGIN 500

/**
 * A club's reputation, indexed by club row ID. We only read reputation off a team, through each
 * player's contract, so a club's is taken as the best of its teams' that we saw; for any club with
 * players that is its first team.
 */
static uint16_t *clubReputations = NULL;
static uint64_t clubReputationCount = 0;

void myClub_reset(void) {
	free(clubReputations);
	clubReputations = NULL;
	clubReputationCount = 0;
}

/** Rebuilds the club reputations from the player cache; call whenever it is replaced. */
void myClub_rebuild(void) {
	myClub_reset();

	int64_t highestIndex = -1;
	for (uint64_t i = 0; i < gameContext.playerCount; ++i) {
		if (gameContext.players[i].clubIndex > highestIndex) {
			highestIndex = gameContext.players[i].clubIndex;
		}
	}
	if (highestIndex < 0) {
		return;
	}

	clubReputations = calloc((size_t)highestIndex + 1, sizeof(uint16_t));
	if (clubReputations == NULL) {
		return;
	}
	clubReputationCount = (uint64_t)highestIndex + 1;

	for (uint64_t i = 0; i < gameContext.playerCount; ++i) {
		const Player *player = &gameContext.players[i];
		if (player->uid != 0 && player->clubIndex >= 0 && player->teamReputation > clubReputations[player->clubIndex]) {
			clubReputations[player->clubIndex] = player->teamReputation;
		}
	}
}

uint16_t myClub_getClubReputation(const int64_t clubIndex) {
	return clubIndex >= 0 && (uint64_t)clubIndex < clubReputationCount ? clubReputations[clubIndex] : 0;
}

/**
 * The row ID of the club named in the `my-club` option, or -1 when it is unset or not in the loaded
 * save. Club row IDs are positions in the cached club list, so this is looked up rather than stored.
 */
int64_t myClub_getIndex(void) {
	const char *name = gameContext.options.myClub;
	if (name[0] == '\0') {
		return -1;
	}

	for (uint64_t i = 0; i < gameContext.clubCount; ++i) {
		if (!strncmp(gameContext.clubs[i].name, name, CLUB_LONG_NAME_LENGTH)) {
			return (int64_t)i;
		}
	}

	return -1;
}

/**
 * An estimate, not the game's own verdict, which we do not read: FM players broadly want to move to
 * a club of at least the standing of their own. So a player counts as likely to join when neither
 * they nor their current club are much more reputable than the user's club. Free agents only need
 * to pass the first test, and the user's own players never count.
 */
bool myClub_isLikelyToJoin(const Player *player, const int64_t myClubIndex) {
	if (myClubIndex < 0 || player->clubIndex == myClubIndex) {
		return false;
	}

	const uint32_t ceiling = (uint32_t)myClub_getClubReputation(myClubIndex) + INTEREST_REPUTATION_MARGIN;
	if (player->currentReputation > ceiling) {
		return false;
	}

	return player->clubIndex == -1 || myClub_getClubReputation(player->clubIndex) <= ceiling;
}
