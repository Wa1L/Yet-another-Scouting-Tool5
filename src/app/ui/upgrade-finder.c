// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/entities.h"
#include "app/player.h"
#include "app/ui.h"
#include "app/helpers/formatter.h"
#include "app/helpers/ratings.h"
#include "app/helpers/vector.h"
#include "app/ui/player-list.h"
#include "types/position-code-names.h"


#define UPGRADES_PER_POSITION 5

/**
 * One entry per distinct position in the formation. Where a formation repeats a position (two DCs,
 * say), the weaker incumbent is the one to beat, since that is the slot an upgrade would replace.
 */
typedef struct {
	char incumbent[PLAYER_DISPLAY_NAME_LENGTH];
	float incumbentRating; // 0 when nobody in the search results could fill the slot
	PositionCode position;
} UpgradeSlot;

typedef struct {
	UpgradeSlot slots[FORMATION_POSITION_COUNT];
	PlayerSnapshot *candidates;
	uint8_t *candidateSlots; // Parallel to candidates->players
	uint8_t slotCount;
} UpgradeFinderState;

static void freeState(void *data) {
	UpgradeFinderState *state = data;
	playerSnapshot_free(state->candidates);
	g_free(state->candidateSlots);
	g_free(state);
}

/** The player's rating in `position`, or 0 if they are not proficient enough to play there. */
static float getSlotRating(const Player *player, const PositionCode position) {
	if (player->positions[position] < MINIMUM_POSITIONAL_PROFICIENCY) {
		return 0.f;
	}
	return getRatingForPosition(player, position);
}

static void insertCandidate(uint32_t *indices, float *ratings, uint8_t *count, const uint32_t index, const float rating) {
	if (*count == UPGRADES_PER_POSITION && rating <= ratings[UPGRADES_PER_POSITION - 1]) {
		return;
	}

	uint8_t position = *count < UPGRADES_PER_POSITION ? (*count)++ : UPGRADES_PER_POSITION - 1;
	while (position > 0 && ratings[position - 1] < rating) {
		ratings[position] = ratings[position - 1];
		indices[position] = indices[position - 1];
		--position;
	}
	ratings[position] = rating;
	indices[position] = index;
}

/**
 * For each position in the formation, finds the best players anywhere in the database who would
 * improve on the current pick by at least `minImprovement`, within the budget and age limit. Players
 * already in the search results are skipped: they were available to the Best XI already.
 */
void ui_createUpgradeFinderWindow(
	const PositionCode positions[FORMATION_POSITION_COUNT],
	const BestElevenRow rows[FORMATION_POSITION_COUNT],
	const PlayerSnapshot *squad,
	const float minImprovement,
	const uint32_t maxValue,
	const uint8_t maxAge
) {
	if (gameContext.players == NULL) {
		return;
	}

	UpgradeFinderState *state = g_new0(UpgradeFinderState, 1);
	for (uint8_t i = 0; i < FORMATION_POSITION_COUNT; ++i) {
		const float rating = rows[i].player != NULL ? rows[i].rating : 0.f;

		uint8_t slot = 0;
		while (slot < state->slotCount && state->slots[slot].position != positions[i]) {
			++slot;
		}
		if (slot == state->slotCount) {
			state->slots[state->slotCount++] = (UpgradeSlot){.position = positions[i], .incumbentRating = rating};
		} else if (rating >= state->slots[slot].incumbentRating) {
			continue;
		}

		state->slots[slot].incumbentRating = rating;
		if (rows[i].player != NULL) {
			player_getDisplayName(rows[i].player, state->slots[slot].incumbent, sizeof(state->slots[slot].incumbent));
		} else {
			state->slots[slot].incumbent[0] = '\0';
		}
	}

	GHashTable *squadUids = g_hash_table_new(g_direct_hash, g_direct_equal);
	for (uint32_t i = 0; squad != NULL && i < squad->count; ++i) {
		g_hash_table_add(squadUids, GUINT_TO_POINTER(squad->players[i].uid));
	}

	uint32_t topIndices[FORMATION_POSITION_COUNT][UPGRADES_PER_POSITION];
	float topRatings[FORMATION_POSITION_COUNT][UPGRADES_PER_POSITION];
	uint8_t topCounts[FORMATION_POSITION_COUNT] = {0};
	for (uint32_t i = 0; i < gameContext.playerCount; ++i) {
		const Player *player = &gameContext.players[i];
		if (
			player->uid == 0 ||
			player->age > maxAge ||
			(maxValue > 0 && player->guideValue > maxValue) ||
			g_hash_table_contains(squadUids, GUINT_TO_POINTER(player->uid))
		) {
			continue;
		}

		for (uint8_t slot = 0; slot < state->slotCount; ++slot) {
			const float rating = getSlotRating(player, state->slots[slot].position);
			if (rating > 0.f && rating - state->slots[slot].incumbentRating >= minImprovement) {
				insertCandidate(topIndices[slot], topRatings[slot], &topCounts[slot], i, rating);
			}
		}
	}
	g_hash_table_destroy(squadUids);

	uint32_t indices[FORMATION_POSITION_COUNT * UPGRADES_PER_POSITION];
	state->candidateSlots = g_new(uint8_t, FORMATION_POSITION_COUNT * UPGRADES_PER_POSITION);
	uint32_t count = 0;
	for (uint8_t slot = 0; slot < state->slotCount; ++slot) {
		for (uint8_t j = 0; j < topCounts[slot]; ++j) {
			indices[count] = topIndices[slot][j];
			state->candidateSlots[count] = slot;
			++count;
		}
	}
	state->candidates = playerSnapshot_create(indices, count);

	const WindowContext context = playerList_openWindow(WINDOW_UPGRADE_FINDER, "Upgrade finder", state, freeState);
	char money[FORMATTER_MONEY_SIZE];
	formatter_formatMoneyShort(maxValue, false, money);
	char subtitle[256];
	snprintf(
		subtitle,
		sizeof(subtitle),
		"The best players outside your search results who would improve each position by at least %.1f, "
		"aged %u or under%s%s.",
		minImprovement,
		maxAge,
		maxValue > 0 ? " and valued at no more than " : "",
		maxValue > 0 ? money : ""
	);
	playerList_setSubtitle(context, subtitle);

	ui_renderUpgradeFinderWindow(context);
	ui_presentWindow(context);
}

void ui_renderUpgradeFinderWindow(const WindowContext context) {
	const UpgradeFinderState *state = context.data;

	static const char *const headings[] = {"Player", "Age", "Club", "Rating", "Gain", "Value", "Wage p/w"};
	PlayerGrid grid;
	playerGrid_begin(&grid, context, headings, G_N_ELEMENTS(headings));

	for (uint8_t slot = 0; slot < state->slotCount; ++slot) {
		const UpgradeSlot *upgradeSlot = &state->slots[slot];

		char rating[FORMATTER_RATING_PLAIN_SIZE];
		formatter_formatRatingPlain(upgradeSlot->incumbentRating, rating);
		gchar *heading = upgradeSlot->incumbent[0] != '\0'
			? g_markup_printf_escaped(
				"<b>%s</b>  ·  currently %s (%s)",
				positionCodeNames[upgradeSlot->position],
				upgradeSlot->incumbent,
				rating
			)
			: g_markup_printf_escaped("<b>%s</b>  ·  nobody in your results can play here", positionCodeNames[upgradeSlot->position]);
		playerGrid_appendGroup(&grid, heading);
		g_free(heading);

		bool hasCandidates = false;
		for (uint32_t i = 0; state->candidates != NULL && i < state->candidates->count; ++i) {
			if (state->candidateSlots[i] != slot) {
				continue;
			}
			hasCandidates = true;

			const Player *player = &state->candidates->players[i];
			const float slotRating = getSlotRating(player, upgradeSlot->position);

			char age[8];
			char gain[16];
			char wage[FORMATTER_MONEY_SIZE];
			snprintf(age, sizeof(age), "%u", player->age);
			snprintf(gain, sizeof(gain), "+%.1f", slotRating - upgradeSlot->incumbentRating);
			char gainMarkup[FORMATTER_DELTA_SIZE];
			formatter_formatDelta(1.0, true, gain, gainMarkup);
			formatter_formatMoneyShort(player->weeklyWage, false, wage);

			GtkWidget *cells[] = {
				playerGrid_nameCell(player),
				playerGrid_textCell(age, 0.5f),
				playerGrid_clubCell(player),
				playerGrid_ratingCell(slotRating),
				playerGrid_textCell(gainMarkup, 1.f),
				playerGrid_valueCell(player->guideValue),
				playerGrid_textCell(player->clubIndex == -1 ? "" : wage, 1.f),
			};
			playerGrid_appendRow(&grid, cells, G_N_ELEMENTS(cells));
		}

		if (!hasCandidates) {
			playerGrid_appendMessage(&grid, "No upgrades within these limits.");
		}
	}
}

void ui_rebindUpgradeFinderWindow(const WindowContext context, const PlayerLookup *lookup) {
	const UpgradeFinderState *state = context.data;
	playerSnapshot_refresh(state->candidates, lookup);
	ui_renderUpgradeFinderWindow(context);
}
