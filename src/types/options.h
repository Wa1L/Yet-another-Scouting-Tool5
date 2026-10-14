// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define FORMATION_NAME_LENGTH 32

#define FORMATION_POSITION_COUNT 11

typedef struct {
	char name[FORMATION_NAME_LENGTH];
	PositionCode positions[FORMATION_POSITION_COUNT];
} Formation;

typedef struct {
	float weight;
	uint8_t attribute;
} RatingWeight;

typedef struct {
	RatingWeight *weights;
	PositionGrouped position; // POSITION_GROUPED_COUNT means all roles unless otherwise explicitly defined
	float scale;
} PositionWeights;

typedef struct {
	Formation *formations;
	PositionWeights *weights;
	uint16_t *columns; // Visible result table columns in display order; see player-table.c
	char myClub[CLUB_LONG_NAME_LENGTH]; // The club the user manages, by full name; empty when unset
	bool columnsSet; // false means the file has no `columns` key, so the defaults apply
	bool darkMode;
} Options;
