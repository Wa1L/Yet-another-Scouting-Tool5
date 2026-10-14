// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "search.h"
#include "app/maths.h"
#include "app/mocks.h"
#include "app/player.h"
#include "app/helpers/date.h"
#include "app/helpers/text.h"
#include "core/logger.h"
#include "helpers/vector-shared-pointer.h"
#include "helpers/vector.h"
#include "platform/platform.h"


extern GameContext gameContext;

// Mock players all match, so these are only needed against real data
#ifndef PLAYER_BY_ID
static bool matchesName(const Player *player, const char *foldedName) {
	if (player->commonName[0] != '\0' && text_containsFolded(player->commonName, foldedName)) {
		return true;
	}

	char fullName[PLAYER_DISPLAY_NAME_LENGTH];
	snprintf(fullName, sizeof(fullName), "%s %s", player->forename, player->surname);
	return text_containsFolded(fullName, foldedName);
}

static bool matchesFoot(const Player *player, const FootFilter filter) {
	const FootPreference foot = player_getFoot(player);
	switch (filter) {
		case FOOT_FILTER_LEFT: return foot == FOOT_LEFT || foot == FOOT_LEFT_ONLY;
		case FOOT_FILTER_RIGHT: return foot == FOOT_RIGHT || foot == FOOT_RIGHT_ONLY;
		case FOOT_FILTER_EITHER: return foot == FOOT_EITHER;
		default: return true;
	}
}

static bool matchesTransferStatus(const Player *player, const TransferStatusFilter filter) {
	const bool isListed = player->transferStatus & (TRANSFER_STATUS_LISTED | TRANSFER_STATUS_LISTED_BY_REQUEST);
	const bool isLoanListed = player->transferStatus & TRANSFER_STATUS_LOAN_LISTED;
	switch (filter) {
		case TRANSFER_FILTER_LISTED: return isListed;
		case TRANSFER_FILTER_LOAN_LISTED: return isLoanListed;
		case TRANSFER_FILTER_EITHER: return isListed || isLoanListed;
		default: return true;
	}
}

static bool matchesAttributes(const Player *player, const AttributeFilter filters[ATTRIBUTE_FILTER_COUNT]) {
	for (uint8_t i = 0; i < ATTRIBUTE_FILTER_COUNT; ++i) {
		const AttributeFilter *filter = &filters[i];
		if (filter->attribute >= ATTRIBUTE_COUNT) {
			continue;
		}

		const uint8_t value = convertTo20Scale(player->attributes[filter->attribute]);
		if ((filter->min && value < filter->min) || (filter->max && value > filter->max)) {
			return false;
		}
	}

	return true;
}
#endif

// TODO: multithread this
void search_findPlayers(void) {
#ifdef DEBUG
	const int64_t timeStart = platform_getMicroseconds();
#endif

	uint32_t *playerIds = NULL;
	const FilterOptions options = gameContext.filterOptions;

	for (uint32_t i = 0; i < gameContext.playerCount; ++i) {
#ifndef PLAYER_BY_ID
		const Player *player = &gameContext.players[i];

		if (
			(options.filterMask & FILTER_HAS_MIN_CA && player->ca < options.minCA) ||
			(options.filterMask & FILTER_HAS_MAX_CA && player->ca > options.maxCA) ||
			(options.filterMask & FILTER_HAS_MIN_PA && player->pa < options.minPA) ||
			(options.filterMask & FILTER_HAS_MAX_PA && player->pa > options.maxPA)
		) {
			continue;
		}

		if (
			(options.filterMask & FILTER_HAS_MIN_AGE && player->age < options.minAge) ||
			(options.filterMask & FILTER_HAS_MAX_AGE && player->age > options.maxAge)
		) {
			continue;
		}

		if (
			(options.filterMask & FILTER_HAS_MIN_VALUE && player->guideValue < options.minValue) ||
			(options.filterMask & FILTER_HAS_MAX_VALUE && player->guideValue > options.maxValue)
		) {
			continue;
		}

		if (options.filterMask & FILTER_HAS_CLUB && player->clubIndex != options.clubIndex) {
			continue;
		}

		// Unused nationality slots hold 0xFF, which can never equal a valid index
		if (
			options.filterMask & FILTER_HAS_NATIONALITY &&
			player->nationality[0] != options.nationalityIndex &&
			player->nationality[1] != options.nationalityIndex &&
			player->nationality[2] != options.nationalityIndex &&
			player->nationality[3] != options.nationalityIndex
		) {
			continue;
		}

		if (options.positions > 0) {
			bool hasPosition = false;
			if (
				(options.positions & POSITION_MASK_GK && player->positions[POSITION_CODE_GK] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_DL && player->positions[POSITION_CODE_DL] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_DC && player->positions[POSITION_CODE_DC] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_DR && player->positions[POSITION_CODE_DR] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_WBL && player->positions[POSITION_CODE_WBL] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_DM && player->positions[POSITION_CODE_DM] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_WBR && player->positions[POSITION_CODE_WBR] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_ML && player->positions[POSITION_CODE_ML] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_MC && player->positions[POSITION_CODE_MC] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_MR && player->positions[POSITION_CODE_MR] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_AML && player->positions[POSITION_CODE_AML] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_AMC && player->positions[POSITION_CODE_AMC] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_AMR && player->positions[POSITION_CODE_AMR] >=
					MINIMUM_POSITIONAL_PROFICIENCY) ||
				(options.positions & POSITION_MASK_ST && player->positions[POSITION_CODE_ST] >=
					MINIMUM_POSITIONAL_PROFICIENCY)
			) {
				hasPosition = true;
			}

			if (!hasPosition) {
				continue;
			}
		}

		if (
			(options.filterMask & FILTER_HAS_MIN_RATING && player->ratings[0].value < options.minRating) ||
			(options.filterMask & FILTER_HAS_MAX_RATING && player->ratings[0].value > options.maxRating)
		) {
			continue;
		}

		if (
			(options.filterMask & FILTER_HAS_MIN_HOME_REPUTATION && player->homeReputation < options.minHomeReputation) ||
			(options.filterMask & FILTER_HAS_MAX_HOME_REPUTATION && player->homeReputation > options.maxHomeReputation) ||
			(options.filterMask & FILTER_HAS_MIN_CURRENT_REPUTATION &&
				player->currentReputation < options.minCurrentReputation) ||
			(options.filterMask & FILTER_HAS_MAX_CURRENT_REPUTATION &&
				player->currentReputation > options.maxCurrentReputation) ||
			(options.filterMask & FILTER_HAS_MIN_WORLD_REPUTATION && player->worldReputation < options.minWorldReputation) ||
			(options.filterMask & FILTER_HAS_MAX_WORLD_REPUTATION && player->worldReputation > options.maxWorldReputation)
		) {
			continue;
		}

		if (options.filterMask & FILTER_HAS_UNATTACHED && player->clubIndex != -1) {
			continue;
		}

		if (options.filterMask & FILTER_HAS_MAX_WAGE && player->weeklyWage > options.maxWage) {
			continue;
		}

		// A player without a contract has nothing to expire, so they never match
		if (
			options.filterMask & FILTER_HAS_CONTRACT_EXPIRY && (
				player->clubIndex == -1 ||
				player->contractEnd.year == 0 ||
				date_monthsUntil(player->contractEnd) > (int32_t)options.contractMonths
			)
		) {
			continue;
		}

		if (
			options.filterMask & FILTER_HAS_TRANSFER_STATUS &&
			!matchesTransferStatus(player, (TransferStatusFilter)options.transferStatus)
		) {
			continue;
		}

		if (
			(options.filterMask & FILTER_HAS_FAST_LEARNER && !player->canDevelopQuickly) ||
			(options.filterMask & FILTER_HAS_HOT_PROSPECT && !player->isHotProspect)
		) {
			continue;
		}

		if (options.filterMask & FILTER_HAS_FOOT && !matchesFoot(player, (FootFilter)options.foot)) {
			continue;
		}

		if (options.filterMask & FILTER_HAS_ATTRIBUTE && !matchesAttributes(player, options.attributes)) {
			continue;
		}

		// Last, as it is by far the most expensive check
		if (options.filterMask & FILTER_HAS_NAME && !matchesName(player, options.name)) {
			continue;
		}
#endif

		vector_push(playerIds, i);
	}

	sharedPointer_unref(gameContext.searchResults);
	gameContext.searchResults = sharedPointer_new(playerIds);

	LOG_DEBUG(
		"Found %zu players in %" PRId64 " microseconds",
		vector_length(playerIds),
		platform_getMicroseconds() - timeStart
	);
}
