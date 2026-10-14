// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/types.h"


void searchHandler_doSearch(bool refreshFilterCache);
void searchHandler_clearFilters(void);
void searchHandler_cacheFilters(void);
bool searchHandler_parseMoney(const char *text, uint32_t *outValue);
uint8_t searchHandler_getDropDownAttribute(GtkDropDown *dropDown);
