// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <inttypes.h>
#include <stdint.h>
#include <gtk/gtk.h>

#include "types/entities.h"
#include "types/logger.h"
#include "types/position.h"
#include "types/options.h"
#include "types/process.h"
#include "types/search.h"
#include "types/shared-pointer.h"
#include "types/ui.h"

#define FILTER_HAS_MIN_RATING (1 << 0)
#define FILTER_HAS_MAX_RATING (1 << 1)
#define FILTER_HAS_MIN_VALUE (1 << 2)
#define FILTER_HAS_MAX_VALUE (1 << 3)
#define FILTER_HAS_CLUB (1 << 4)
#define FILTER_HAS_MIN_CA (1 << 5)
#define FILTER_HAS_MAX_CA (1 << 6)
#define FILTER_HAS_MIN_PA (1 << 7)
#define FILTER_HAS_MAX_PA (1 << 8)
#define FILTER_HAS_MIN_HOME_REPUTATION (1 << 9)
#define FILTER_HAS_MAX_HOME_REPUTATION (1 << 10)
#define FILTER_HAS_MIN_CURRENT_REPUTATION (1 << 11)
#define FILTER_HAS_MAX_CURRENT_REPUTATION (1 << 12)
#define FILTER_HAS_MIN_WORLD_REPUTATION (1 << 13)
#define FILTER_HAS_MAX_WORLD_REPUTATION (1 << 14)
#define FILTER_HAS_MIN_AGE (1 << 15)
#define FILTER_HAS_MAX_AGE (1 << 16)
#define FILTER_HAS_NATIONALITY (1 << 17)
#define FILTER_HAS_POSITION (1 << 18)
#define FILTER_HAS_NAME (1 << 19)
#define FILTER_HAS_MAX_WAGE (1 << 20)
#define FILTER_HAS_CONTRACT_EXPIRY (1 << 21)
#define FILTER_HAS_TRANSFER_STATUS (1 << 22)
#define FILTER_HAS_UNATTACHED (1 << 23)
#define FILTER_HAS_FOOT (1 << 24)
#define FILTER_HAS_FAST_LEARNER (1 << 25)
#define FILTER_HAS_HOT_PROSPECT (1 << 26)
#define FILTER_HAS_ATTRIBUTE (1 << 27)
#define FILTER_HAS_LIKELY_TO_JOIN (1 << 28)

#define POSITION_MASK_GK (1 << 0)
#define POSITION_MASK_DL (1 << 1)
#define POSITION_MASK_DC (1 << 2)
#define POSITION_MASK_DR (1 << 3)
#define POSITION_MASK_WBL (1 << 4)
#define POSITION_MASK_DM (1 << 5)
#define POSITION_MASK_WBR (1 << 6)
#define POSITION_MASK_ML (1 << 7)
#define POSITION_MASK_MC (1 << 8)
#define POSITION_MASK_MR (1 << 9)
#define POSITION_MASK_AML (1 << 10)
#define POSITION_MASK_AMC (1 << 11)
#define POSITION_MASK_AMR (1 << 12)
#define POSITION_MASK_ST (1 << 13)

// Indices line up with the entries of the dropdowns in player-search.ui
typedef enum {
	FOOT_FILTER_ANY,
	FOOT_FILTER_LEFT,
	FOOT_FILTER_RIGHT,
	FOOT_FILTER_EITHER,
} FootFilter;

typedef enum {
	TRANSFER_FILTER_ANY,
	TRANSFER_FILTER_LISTED,
	TRANSFER_FILTER_LOAN_LISTED,
	TRANSFER_FILTER_EITHER,
} TransferStatusFilter;

/** Bounds are on the 1-20 scale; 0 means that side is unbounded. */
typedef struct {
	uint8_t attribute;
	uint8_t min;
	uint8_t max;
} AttributeFilter;

// Long enough for any name we read, after case and accent folding
#define NAME_SEARCH_LENGTH 72

typedef struct {
	char name[NAME_SEARCH_LENGTH]; // Folded with text_fold()
	AttributeFilter attributes[ATTRIBUTE_FILTER_COUNT];
	int64_t clubIndex;
	uint32_t filterMask; // 1 bit to represent the presence of each filter
	float minRating;
	float maxRating;
	uint32_t minValue;
	uint32_t maxValue;
	uint32_t maxWage; // Per week
	uint16_t minHomeReputation;
	uint16_t maxHomeReputation;
	uint16_t minCurrentReputation;
	uint16_t maxCurrentReputation;
	uint16_t minWorldReputation;
	uint16_t maxWorldReputation;
	uint16_t positions; // Masked, one bit per position
	uint16_t contractMonths;
	uint8_t minAge;
	uint8_t maxAge;
	uint8_t minCA;
	uint8_t maxCA;
	uint8_t minPA;
	uint8_t maxPA;
	uint8_t nationalityIndex;
	uint8_t foot; // FootFilter
	uint8_t transferStatus; // TransferStatusFilter
} FilterOptions;

typedef struct {
	GtkCheckButton *positionGK;
	GtkCheckButton *positionDL;
	GtkCheckButton *positionDC;
	GtkCheckButton *positionDR;
	GtkCheckButton *positionWBL;
	GtkCheckButton *positionDM;
	GtkCheckButton *positionWBR;
	GtkCheckButton *positionML;
	GtkCheckButton *positionMC;
	GtkCheckButton *positionMR;
	GtkCheckButton *positionAML;
	GtkCheckButton *positionAMC;
	GtkCheckButton *positionAMR;
	GtkCheckButton *positionST;
} CheckBox;

typedef enum {
	WINDOW_PLAYER_SEARCH,
	WINDOW_PLAYER_INFO,
	WINDOW_BEST_XI,
	WINDOW_SQUAD_DEPTH,
	WINDOW_SHORTLIST,
	WINDOW_SIMILAR_PLAYERS,
	WINDOW_UPGRADE_FINDER,
	WINDOW_SETTINGS,
} WindowType;


typedef struct {
	GtkBuilder *builder;
	GtkWidget *window;
	void *data;
	WindowType type;
} WindowContext;

typedef struct {
	CheckBox checkboxes;
	FilterBuffer filterBuffer;
	FilterControls filterControls;
	FilterOptions filterOptions;
	char gameVersion[GAME_STATUS_STRING_BUFFER_SIZE];
	Options options;
	SearchDatalist *clubDatalist;
	SearchDatalist *nationalityDatalist;
	SharedPointer *searchResults;
	DateTime currentDate;
	GtkBuilder *builder;
	GtkApplication *app;
	Club *clubs;
	Nation *nations;
	Player *players;
	WindowContext *windows;
	uint64_t gameKey;
	uint64_t clubCount;
	uint64_t nationCount;
	uint64_t playerCount;
} GameContext;
