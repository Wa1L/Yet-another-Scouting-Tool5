// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/types.h"

#include <gtk/gtk.h>


#define SEARCH_TYPE_PLAYER_ROW (search_player_row_get_type())
G_DECLARE_FINAL_TYPE(SearchPlayerRow, search_player_row, SEARCH, PLAYER_ROW, GObject)

struct _SearchPlayerRow {
	GObject parent_instance;
	Player *player;
};

void playerTable_init(void);
void playerTable_clear(void);
void playerTable_populate(void);
void playerTable_applyColumns(void);
bool playerTable_columnFromKey(const char *key, uint16_t *outColumnId);
const char *playerTable_columnKey(uint16_t columnId);
bool playerTable_openSelected(void);
bool playerTable_hasFocus(void);
