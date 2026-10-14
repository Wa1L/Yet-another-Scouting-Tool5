// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/types.h"

#include <gtk/gtk.h>


/** A grid of players with one header row, filled in a row at a time. */
typedef struct {
	GtkGrid *grid;
	int32_t row;
	int32_t columnCount;
} PlayerGrid;

WindowContext playerList_openWindow(WindowType type, const char *title, void *state, GDestroyNotify freeState);
void playerList_setSubtitle(WindowContext context, const char *text);
GtkBox *playerList_getControls(WindowContext context);

void playerGrid_begin(PlayerGrid *grid, WindowContext context, const char *const *headings, int32_t count);
void playerGrid_appendGroup(PlayerGrid *grid, const char *markup);
void playerGrid_appendMessage(PlayerGrid *grid, const char *text);
void playerGrid_appendRow(PlayerGrid *grid, GtkWidget **cells, int32_t count);
GtkWidget *playerGrid_nameCell(const Player *player);
GtkWidget *playerGrid_textCell(const char *markup, float xalign);
GtkWidget *playerGrid_clubCell(const Player *player);
GtkWidget *playerGrid_ratingCell(float rating);
GtkWidget *playerGrid_valueCell(uint32_t value);
GtkWidget *playerGrid_contractCell(const Player *player);
