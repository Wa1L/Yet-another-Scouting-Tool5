// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "player.h"
#include "app/constants.h"
#include "app/injury-names.h"
#include "app/maths.h"
#include "app/mocks.h"
#include "app/helpers/game.h"
#include "app/helpers/ratings.h"
#include "app/helpers/vector.h"
#include "core/logger.h"
#include "platform/platform.h"


extern GameContext gameContext;

static bool isPlayerAlsoStaff(void *handle, uint64_t playerAddress);
static bool isPersonAlsoStaff(void *handle, uint64_t personAddress);
static bool isPlayerValid(void *handle, uint64_t personAddress);
static bool isPersonValid(void *handle, uint64_t personAddress);
static uint64_t getPlayerAddressFromPersonAddress(void *handle, uint64_t personAddress);
static inline void getPersonName(void *handle, uint8_t pointer[8], char str[PERSON_COMMON_NAME_LENGTH]);
static inline void getPersonForename(void *handle, uint64_t attributeBase, char str[PERSON_COMMON_NAME_LENGTH]);
static inline void getPersonSurname(void *handle, uint64_t attributeBase, char str[PERSON_COMMON_NAME_LENGTH]);
static inline void getPersonCommonName(void *handle, uint64_t attributeBase, char str[PERSON_COMMON_NAME_LENGTH]);
static void readContract(void *handle, uint64_t personAddress, Player *player);
static uint64_t getPersonAddressFromUid(const ProcessContext *processContext, uint64_t uid);
static float getRatingPerPosition(const Player *player, PositionGrouped position);

// Assumes valid ProcessContext
uint64_t getCurrentPersonUniqueId(const ProcessContext *processContext) {
#ifndef PLAYER_BY_ID
	uint8_t bytes[8];
	void *handle = processContext->handle;
	readFromMemory(handle, processContext->moduleBaseAddress + CURRENT_SCREEN_PERSON_ID_PTR_BASE, 8, bytes);
	readFromMemory(handle, hexBytesToInt(bytes, 8) + CURRENT_SCREEN_PERSON_ID_PTR_BASE_OFFSET_1, 8, bytes);
	readFromMemory(
		handle,
		hexBytesToInt(bytes, 8) + CURRENT_SCREEN_PERSON_ID_PTR_BASE_OFFSET_2 + PERSON_OFFSET_UNIQUE_ID,
		8,
		bytes
	);
	return hexBytesToInt(bytes, 8);
#else
	const Player player = PLAYER_BY_ID;
	return player.uid;
#endif
}

// Assumes valid ProcessContext
Player getPlayerById(const ProcessContext *processContext, const uint64_t uniqueId) {
	const uint64_t personAddress = getPersonAddressFromUid(processContext, uniqueId);
	if (personAddress && isPlayerValid(processContext->handle, personAddress)) {
		return getPlayer(processContext->handle, false, personAddress, 0);
	}

	LOG_ERROR("Could not find player by ID %" PRIu64, uniqueId);

	return (Player){0};
}

static uint64_t getPlayerAddressFromPersonAddress(void *handle, const uint64_t personAddress) {
	const int64_t offset = isPersonAlsoStaff(handle, personAddress)
		? STAFF_OFFSET_FROM_PERSON
		: PLAYER_OFFSET_FROM_PERSON;

	return personAddress + (uint64_t)offset;
}

uint64_t getPersonAddressFromPlayerAddress(void *handle, const uint64_t playerAddress) {
	const int64_t offset = isPlayerAlsoStaff(handle, playerAddress)
		? STAFF_OFFSET_FROM_PERSON
		: PLAYER_OFFSET_FROM_PERSON;

	return playerAddress - (uint64_t)offset;
}

Player getPlayer(
	void *handle,
	const bool skipIsValidCheck,
	const uint64_t personAddress,
	uint64_t playerAddress
) {
#ifndef PLAYER_BY_ID
	if (!skipIsValidCheck && !isPlayerValid(handle, personAddress)) {
		return (Player){0};
	}

	if (playerAddress == 0) {
		playerAddress = getPlayerAddressFromPersonAddress(handle, personAddress);
	}

	uint8_t bytes[8];
	readFromMemory(handle, personAddress + PERSON_OFFSET_UNIQUE_ID, 4, bytes);
	const uint32_t uid = (uint32_t)hexBytesToInt(bytes, 4);
	if (!uid) {
		return (Player){0};
	}

	uint8_t ability[3];
	readFromMemory(handle, playerAddress + PLAYER_OFFSET_ABILITY, 3, ability);

	readFromMemory(handle, personAddress + PERSON_OFFSET_ROW_ID, 4, bytes);
	const uint32_t rowId = (uint32_t)hexBytesToInt(bytes, 4);
	readFromMemory(handle, personAddress + PERSON_OFFSET_RANDOM_ID, 4, bytes);
	const uint32_t rid = (uint32_t)hexBytesToInt(bytes, 4);

	readFromMemory(handle, personAddress + PERSON_OFFSET_DOB, 4, bytes);

	Player player = {
		.personAddress = personAddress,
		.playerAddress = playerAddress,
		.dateOfBirth = {
			.days = (uint16_t)hexBytesToInt(bytes, 2),
			.year = (uint16_t)hexBytesToInt(bytes + 2, 2)
		},
		.ca = ability[ABILITY_CA],
		.pa = ability[ABILITY_PA],
		.rid = rid,
		.uid = uid,
		.rowId = rowId,
		.nationality = {0xFF, 0xFF, 0xFF, 0xFF}
	};
	player.age = player_getAge(player.dateOfBirth);
	getPersonForename(handle, personAddress, player.forename);
	getPersonSurname(handle, personAddress, player.surname);
	getPersonCommonName(handle, personAddress, player.commonName);

	readFromMemory(handle, playerAddress + PLAYER_OFFSET_HAPPINESS, 1, bytes);
	player.morale = (uint8_t)hexBytesToInt(bytes, 1);
	readFromMemory(handle, playerAddress + PLAYER_OFFSET_WEIGHT, 2, bytes);
	player.weight = (uint8_t)hexBytesToInt(bytes, 2);
	readFromMemory(handle, playerAddress + PLAYER_OFFSET_HEIGHT, 2, bytes);
	player.height = (uint8_t)hexBytesToInt(bytes, 2);

	readFromMemory(handle, playerAddress + PLAYER_OFFSET_INJURY_POINTER, 8, bytes);
	const uint64_t injuryAddress = hexBytesToInt(bytes, 8);
	if (injuryAddress) {
		readFromMemory(handle, injuryAddress, 8, bytes);
		const uint64_t injuryStartAddress = hexBytesToInt(bytes, 8);
		if (injuryStartAddress) {
			readFromMemory(handle, injuryStartAddress, 8, bytes);
			const uint64_t injuryInstanceAddress = hexBytesToInt(bytes, 8);
			readFromMemory(handle, injuryInstanceAddress + 0x20, 2, bytes);
			player.injury.date = game_parseDateTime(bytes);
			readFromMemory(handle, injuryInstanceAddress + 0x28, 2, bytes);
			player.injury.duration = (uint16_t)hexBytesToInt(bytes, 2);
			readFromMemory(handle, injuryInstanceAddress + 0x30, 1, bytes);
			player.injury.treatmentMask = (uint8_t)hexBytesToInt(bytes, 1);
			readFromMemory(handle, injuryInstanceAddress + 0x08, 8, bytes);
			readFromMemory(handle, hexBytesToInt(bytes, 8) + 0x18, 8, bytes);
			const uint64_t injuryNameAddress = hexBytesToInt(bytes, 8);
			readFromMemory(handle, injuryNameAddress, 4, bytes);
			const uint8_t length = (uint8_t)hexBytesToInt(bytes, 4);
			char name[256];
			readFromMemory(handle, injuryNameAddress + 0x04, length, (uint8_t*)name);
			player.injury.nameIndex = injuryNames_intern(name, length);
		}
	}

	readFromMemory(
		handle,
		personAddress + PERSON_OFFSET_PERSONALITY,
		PERSONALITY_COUNT,
		player.attributes + TRUE_ATTRIBUTE_COUNT
	);
	readFromMemory(handle, playerAddress + PLAYER_OFFSET_ATTRIBUTES, TRUE_ATTRIBUTE_COUNT, player.attributes);
	readFromMemory(handle, playerAddress + PLAYER_OFFSET_POSITIONS, 15, player.positions);
	readContract(handle, personAddress, &player);
	getSortedPositionRatings(&player);

	// Normalise personality
	for (uint8_t i = TRUE_ATTRIBUTE_COUNT; i < ATTRIBUTE_COUNT; ++i) {
		player.attributes[i] *= 5;
	}

	// Sharpness, fatigue, condition
	readFromMemory(handle, playerAddress + PLAYER_OFFSET_SHARPNESS, 6, bytes);
	player.sharpness = (uint16_t)hexBytesToInt(bytes, 2);
	player.fatigue = (int16_t)hexBytesToInt(bytes + 2, 2);
	player.condition = (uint16_t)hexBytesToInt(bytes + 4, 2);

	// Nationality
	readFromMemory(handle, personAddress + PERSON_OFFSET_NATIONALITY, 8, bytes);
	const uint64_t country = hexBytesToInt(bytes, 8);
	readFromMemory(handle, country + NATION_OFFSET_ROW_ID, 4, bytes);
	player.nationality[0] = (uint8_t)hexBytesToInt(bytes, 4);

	uint8_t nationalityIndex = 1;
	readFromMemory(handle, personAddress + PERSON_OFFSET_RELATIONSHIPS, 8, bytes);
	const uint64_t relationships = hexBytesToInt(bytes, 8);
	readFromMemory(handle, relationships, 8, bytes);
	uint64_t relationshipStart = hexBytesToInt(bytes, 8);
	readFromMemory(handle, relationships + 0x08, 8, bytes);
	const uint64_t relationshipEnd = hexBytesToInt(bytes, 8);
	while (relationshipStart < relationshipEnd && nationalityIndex < 4) {
		readFromMemory(handle, relationshipStart + RELATIONSHIP_OFFSET_TYPE, 2, bytes);
		const uint16_t type = (uint16_t)hexBytesToInt(bytes, 2);
		if (type == 0x0908) {
			readFromMemory(handle, relationshipStart + RELATIONSHIP_OFFSET_TARGET_ADDRESS, 8, bytes);
			const uint64_t nationality = hexBytesToInt(bytes, 8);
			readFromMemory(handle, nationality + NATION_OFFSET_ROW_ID, 8, bytes);
			player.nationality[nationalityIndex] = (uint8_t)hexBytesToInt(bytes, 4);

			nationalityIndex++;
		}
		relationshipStart += 0x10;
	}

	// Reputation
	readFromMemory(handle, playerAddress + PLAYER_OFFSET_HOME_REPUTATION, 6, bytes);
	player.homeReputation = (uint16_t)hexBytesToInt(bytes, 2);
	player.currentReputation = (uint16_t)hexBytesToInt(bytes + 2, 2);
	player.worldReputation = (uint16_t)hexBytesToInt(bytes + 4, 2);

	// Value
	readFromMemory(handle, playerAddress + PLAYER_OFFSET_GUIDE_VALUE, 4, bytes);
	player.guideValue = (uint32_t)hexBytesToInt(bytes, 4);

	// Extra
	player.canDevelopQuickly = player.age <= 23 &&
		player.attributes[ATTR_INJ] < 70 &&
		player.attributes[ATTR_AMB] > 10 &&
		player.attributes[ATTR_PRO] > 10 &&
		player.attributes[ATTR_DET] > 50;
	if (player.age >= 15 && player.age <= 19) {
		player.isHotProspect = player.ca >= 80 + 5 * (player.age - 15);
	} else if (player.age <= 23) {
		player.isHotProspect = player.ca >= 140;
	} else {
		player.isHotProspect = false;
	}
#else
	(void)handle;
	(void)skipIsValidCheck;
	(void)personAddress;
	(void)playerAddress;
	const Player player = PLAYER_BY_ID;
#endif

	return player;
}

static bool isPlayerAlsoStaff(void *handle, const uint64_t playerAddress) {
	uint8_t bytes[8];
	readFromMemory(handle, playerAddress - (uint64_t)PLAYER_OFFSET_FROM_PERSON + 0x0C, 8, bytes);
	const uint64_t ids = hexBytesToInt(bytes, 8);
	return ids >> 32 != (ids & 0xFFFFFFFF);
}

static bool isPersonAlsoStaff(void *handle, const uint64_t personAddress) {
	uint8_t bytes[4];
	readFromMemory(handle, personAddress + (uint64_t)PLAYER_OFFSET_FROM_PERSON + 0x08, 4, bytes);
	return hexBytesToInt(bytes, 4) == 0;
}

static bool isPlayerValid(void *handle, const uint64_t personAddress) {
#ifndef MOCKS_MODE
	const uint64_t playerAddress = personAddress + (uint64_t)PLAYER_OFFSET_FROM_PERSON;
	for (uint8_t i = 0; i < 5; ++i) {
		const uint8_t attribute = readByte(handle, playerAddress + PLAYER_OFFSET_HIDDEN_ATTRIBUTES + i);
		if (!attribute || attribute > 100) {
			return false;
		}
	}

	return isPersonValid(handle, personAddress);
#else
	(void)handle;
	(void)personAddress;
	return true;
#endif
}

static bool isPersonValid(void *handle, const uint64_t personAddress) {
#ifndef MOCKS_MODE
	for (uint8_t i = 0; i < 8; ++i) {
		const uint8_t attribute = readByte(handle, personAddress + PERSON_OFFSET_PERSONALITY + i);
		if (!attribute || attribute > 20) {
			return false;
		}
	}
#else
	(void)handle;
	(void)personAddress;
#endif

	return true;
}

static inline void getPersonName(void *handle, uint8_t pointer[8], char str[PERSON_COMMON_NAME_LENGTH]) {
	uint64_t a = hexBytesToInt(pointer, 8);
	if (!a) {
		str[0] = '\0';
		return;
	}
	readFromMemory(handle, a, 8, pointer);
	a = hexBytesToInt(pointer, 8);
	readFromMemory(handle, a + 4, PERSON_COMMON_NAME_LENGTH, (uint8_t*)str);
}

static inline void getPersonForename(void *handle, const uint64_t attributeBase, char str[PERSON_COMMON_NAME_LENGTH]) {
	uint8_t pointer[8];
	readFromMemory(handle, attributeBase + PERSON_OFFSET_FORENAME, sizeof(pointer), pointer);
	getPersonName(handle, pointer, str);
}

static inline void getPersonSurname(void *handle, const uint64_t attributeBase, char str[PERSON_COMMON_NAME_LENGTH]) {
	uint8_t pointer[8];
	readFromMemory(handle, attributeBase + PERSON_OFFSET_SURNAME, sizeof(pointer), pointer);
	getPersonName(handle, pointer, str);
}

static inline void getPersonCommonName(
	void *handle,
	const uint64_t attributeBase,
	char str[PERSON_COMMON_NAME_LENGTH]
) {
	uint8_t pointer[8];
	readFromMemory(handle, attributeBase + PERSON_OFFSET_COMMON_NAME, sizeof(pointer), pointer);
	getPersonName(handle, pointer, str);
}

uint8_t player_getAge(const Date dateOfBirth) {
	uint8_t age = (uint8_t)(gameContext.currentDate.year - dateOfBirth.year);
	if (gameContext.currentDate.days < dateOfBirth.days) {
		--age;
	}

	return age;
}

static void readContract(void *handle, const uint64_t personAddress, Player *player) {
	uint8_t bytes[8];
	readFromMemory(handle, personAddress + PERSON_OFFSET_CONTRACTS, 8, bytes);
	const uint64_t contractAddress = hexBytesToInt(bytes, 8);
	if (!contractAddress) {
		player->clubIndex = -1;
		return;
	}

	readFromMemory(handle, contractAddress + CONTRACTS_OFFSET_TEAM, 8, bytes);
	const uint64_t teamAddress = hexBytesToInt(bytes, 8);
	readFromMemory(handle, teamAddress + TEAM_OFFSET_CLUB, 8, bytes);
	const uint64_t clubAddress = hexBytesToInt(bytes, 8);
	readFromMemory(handle, clubAddress + CLUB_OFFSET_ROW_ID, 4, bytes);
	player->clubIndex = (int64_t)hexBytesToInt(bytes, 4);

	readFromMemory(handle, contractAddress + CONTRACTS_OFFSET_WEEKLY_WAGE, 4, bytes);
	player->weeklyWage = (uint32_t)hexBytesToInt(bytes, 4);

	// Stored in the same packed format as the in-game date
	readFromMemory(handle, contractAddress + CONTRACTS_OFFSET_END_DATE, 4, bytes);
	const DateTime end = game_parseDateTime(bytes);
	player->contractEnd = (Date){.days = end.days, .year = end.year};

	player->transferStatus = readByte(handle, contractAddress + CONTRACTS_OFFSET_TRANSFER_STATUS);
}

/**
 * Footedness as FM labels it: either foot reaching 15 (on the 1-20 scale) makes a player two-footed,
 * otherwise the stronger foot wins, and a weaker foot of 5 or less makes it "only".
 */
FootPreference player_getFoot(const Player *player) {
	const uint8_t left = convertTo20Scale(player->attributes[ATTR_LEF]);
	const uint8_t right = convertTo20Scale(player->attributes[ATTR_RIG]);

	if ((left >= 15 && right >= 15) || left == right) {
		return FOOT_EITHER;
	}
	if (left > right) {
		return right <= 5 ? FOOT_LEFT_ONLY : FOOT_LEFT;
	}

	return left <= 5 ? FOOT_RIGHT_ONLY : FOOT_RIGHT;
}

const char *player_getFootName(const FootPreference foot) {
	switch (foot) {
		case FOOT_LEFT: return "Left";
		case FOOT_LEFT_ONLY: return "Left only";
		case FOOT_RIGHT: return "Right";
		case FOOT_RIGHT_ONLY: return "Right only";
		default: return "Either";
	}
}

/** Writes "Forename Surname", or the common name when the player has one. */
void player_getDisplayName(const Player *player, char *outName, const size_t size) {
	if (player->commonName[0] == '\0') {
		snprintf(outName, size, "%s %s", player->forename, player->surname);
	} else {
		snprintf(outName, size, "%s", player->commonName);
	}
}

PositionWeights *getWeightsForPosition(const PositionGrouped position) {
	uint64_t generalIndex = 0;
	uint64_t matchingIndex = 0xFFFFFFFFFFFFFFFF;
	for (uint64_t i = 0; i < vector_length(gameContext.options.weights); ++i) {
		if (gameContext.options.weights[i].position == position) {
			matchingIndex = i;
			break;
		}
		if (gameContext.options.weights[i].position == POSITION_GROUPED_COUNT) {
			generalIndex = i;
		}
	}

	if (matchingIndex == 0xFFFFFFFFFFFFFFFF) {
		matchingIndex = generalIndex;
	}

	return &gameContext.options.weights[matchingIndex];
}

static float getRatingPerPosition(const Player *player, const PositionGrouped position) {
	/* Check positional proficiency */
	int indices[5];
	const int count = positionGroupToIndices(position, indices);
	bool canPlay = false;
	for (int i = 0; i < count; ++i) {
		const int idx = indices[i];
		if (player->positions[idx] >= MINIMUM_POSITIONAL_PROFICIENCY) {
			canPlay = true;
			break;
		}
	}
	if (!canPlay) {
		return 0.0f;
	}

	const PositionWeights *weights = getWeightsForPosition(position);
	const float totalScale = weights->scale;
	float rating = 0.0f;
	for (uint64_t i = 0; i < vector_length(weights->weights); ++i) {
		const RatingWeight ratingWeight = weights->weights[i];
		const float value = ratingWeight.weight;
		const uint8_t attribute = ratingWeight.attribute;
		if (value > 0.1f || value < -0.1f) {
			const float scaledValue = (float)player->attributes[attribute] / 100.f;
			rating += value * scaledValue;
		}
	}

	if (totalScale != 0.f) {
		rating /= totalScale;
	}

	return rating;
}

void getSortedPositionRatings(Player *player) {
	PositionGrouped i = 0;
	PositionGrouped j = 0;
	while (i < POSITION_GROUPED_COUNT) {
		const float value = getRatingPerPosition(player, i);
		if (value > 0.f) {
			player->ratings[j].value = value;
			player->ratings[j].position = i;
			++j;
		}
		++i;
	}

	/* simple selection sort descending */
	for (i = 0; i < POSITION_GROUPED_COUNT; ++i) {
		PositionGrouped best = i;
		for (j = i + 1; j < POSITION_GROUPED_COUNT; ++j) {
			if (player->ratings[j].value > player->ratings[best].value) {
				best = j;
			}
		}
		if (best != i) {
			const Rating tmp = player->ratings[i];
			player->ratings[i] = player->ratings[best];
			player->ratings[best] = tmp;
		}
	}
}

static uint64_t getPersonAddressFromUid(const ProcessContext *processContext, const uint64_t uid) {
#ifndef PLAYER_BY_ID
	// Iterate over all players to find one with the matching UID
	uint8_t bytes[8];
	readFromMemory(processContext->handle, processContext->moduleBaseAddress + PLAYER_COUNT_PTR_BASE, 4, bytes);
	const uint32_t playerCount = (uint32_t)hexBytesToInt(bytes, 4);
	if (playerCount < 1 || playerCount > 500000) {
		LOG_ERROR("Unexpected number of players in database: %d", playerCount);
		return 0;
	}

	readFromMemory(
		processContext->handle,
		processContext->moduleBaseAddress + PLAYER_LIST_PTR_BASE,
		8,
		bytes
	);
	const uint64_t allPlayers = hexBytesToInt(bytes, 8);

	for (uint32_t i = 0; i < playerCount; ++i) {
		readFromMemory(processContext->handle, allPlayers + PLAYER_LIST_STRIDE * i, 8, bytes);
		const uint64_t playerAddress = hexBytesToInt(bytes, 8);
		const uint64_t personAddress = playerAddress - (uint64_t)PLAYER_OFFSET_FROM_PERSON;

		readFromMemory(processContext->handle, personAddress + (uint64_t)PERSON_OFFSET_UNIQUE_ID, 8, bytes);
		const uint64_t foundPlayerId = hexBytesToInt(bytes, 8);
		if (foundPlayerId == uid) {
			return personAddress;
		}
	}

	return 0;
#else
	return 0x5E18008;
#endif
}
