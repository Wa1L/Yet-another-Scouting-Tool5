// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/types.h"


const char *date_getOrdinal(uint16_t day);
DayMonthYearTime date_prettify(DateTime dateTime);
int32_t date_daysUntil(Date date);
int32_t date_monthsUntil(Date date);

// "30 Sep 2027" plus terminator, with room to spare
#define DATE_FORMAT_SIZE 24
void date_format(Date date, char *outString, size_t size);
