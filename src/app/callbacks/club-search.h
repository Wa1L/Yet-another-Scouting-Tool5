// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "app/types.h"


void clubSearch_init(void);
bool clubSearch_select(int64_t clubIndex);
void callbacks_OnClubNameChange(GtkEditable *editable, SearchDatalist *dataList);
void callbacks_onClubNameSelected(
	const GtkListBox *box,
	GtkListBoxRow *row,
	const SearchDatalist *dataList
);
