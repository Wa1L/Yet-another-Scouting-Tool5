// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/constants.h"

#include <stdbool.h>
#include <stdint.h>


// Longest club name: Club de Fútbol Lobos de la Benemérita Universidad Autónoma de Puebla, 71 chars
// Longest club short name: Persatuan Sepakbola Indonesia Karawang, 38 chars
#define CLUB_LONG_NAME_LENGTH 72
#define CLUB_SHORT_NAME_LENGTH 40

typedef struct {
	char name[CLUB_LONG_NAME_LENGTH];
	char shortName[CLUB_SHORT_NAME_LENGTH];
	uint64_t address;
	uint8_t teamType;
} Club;

// "Saint Vincent and the Grenadines" is the longest at 32 bytes
#define MAX_NATION_STRING_LENGTH 32

typedef struct {
	char name[MAX_NATION_STRING_LENGTH];
	char code[4];
} Nation;

// TODO: note I only tested players, not all people
// Longest person forename: Kenji Syed Rusydi Al-Asyraf, 27 chars
// Longest person surname: Conceição Benevenuto Malaquias, 32 chars
// Longest person common name: Yamanalage Lakshitha Jayathunga, 31 chars
#define PERSON_FORENAME_LENGTH 32
#define PERSON_SURNAME_LENGTH 32
#define PERSON_COMMON_NAME_LENGTH 32

typedef enum PositionGrouped {
	POSITION_GROUPED_GK = 0,
	POSITION_GROUPED_FB,
	POSITION_GROUPED_CB,
	POSITION_GROUPED_WB,
	POSITION_GROUPED_DM,
	POSITION_GROUPED_MC,
	POSITION_GROUPED_W,
	POSITION_GROUPED_AM,
	POSITION_GROUPED_ST,
	POSITION_GROUPED_COUNT,
} PositionGrouped;

typedef struct {
	float value;
	PositionGrouped position;
} Rating;

typedef struct {
	uint16_t days;
	uint16_t year;
	uint8_t time;
} DateTime;

#define MONTH_NAME_LENGTH 12

typedef struct {
	char month[MONTH_NAME_LENGTH];
	char timeString[6];
	uint16_t year;
	uint16_t day;
	uint8_t time;
} DayMonthYearTime;

typedef struct {
	uint16_t days;
	uint16_t year;
} Date;

/**
 * Injury names are read straight out of the game and repeat across thousands of players, so they
 * are interned once and referenced by index. Index 0 is the empty string, i.e. "no name".
 */
typedef struct {
	DateTime date;
	uint16_t duration;
	uint16_t nameIndex;
	uint8_t treatmentMask;
} Injury;

// Bit flags read from a contract's transfer status byte
#define TRANSFER_STATUS_LISTED 0x01
#define TRANSFER_STATUS_LOAN_LISTED 0x02
#define TRANSFER_STATUS_LISTED_BY_REQUEST 0x08
#define TRANSFER_STATUS_NOT_FOR_SALE 0x10

typedef enum {
	FOOT_EITHER,
	FOOT_LEFT,
	FOOT_LEFT_ONLY,
	FOOT_RIGHT,
	FOOT_RIGHT_ONLY,
} FootPreference;

typedef struct {
	uint8_t attributes[ATTRIBUTE_COUNT];
	Rating ratings[POSITION_GROUPED_COUNT];
	char forename[PERSON_FORENAME_LENGTH + 1];
	char surname[PERSON_SURNAME_LENGTH + 1];
	char commonName[PERSON_COMMON_NAME_LENGTH + 1];
	uint8_t nationality[4];
	Injury injury;
	int64_t clubIndex; // -1 means unemployed
	uint64_t personAddress;
	uint64_t playerAddress;
	uint32_t rowId;
	uint32_t rid;
	uint32_t uid;
	uint32_t guideValue;
	uint32_t weeklyWage;
	Date dateOfBirth;
	Date contractEnd; // Zeroed when the player has no contract
	uint16_t sharpness;
	int16_t fatigue;
	uint16_t condition;
	uint16_t homeReputation;
	uint16_t currentReputation;
	uint16_t worldReputation;
	uint8_t selectedRatingIndex;
	uint8_t positions[15];
	uint8_t age;
	uint8_t ca;
	uint8_t pa;
	uint8_t height;
	uint8_t weight;
	uint8_t morale;
	uint8_t transferStatus; // TRANSFER_STATUS_* bit flags
	bool canDevelopQuickly;
	bool isHotProspect;
} Player;

#define GAME_STATUS_STRING_BUFFER_SIZE 32

typedef struct {
	const Player *player;
	float rating;
} BestElevenRow;
