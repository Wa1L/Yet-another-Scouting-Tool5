// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "app/types.h"


void nationalitySearch_init(void);
bool nationalitySearch_select(uint8_t nationIndex);
void callbacks_onNationalityChange(GtkEditable *editable, const SearchDatalist *datalist);
void callbacks_onNationalitySelected(const GtkListBox *box, GtkListBoxRow *row, const SearchDatalist *datalist);
