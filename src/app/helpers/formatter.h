// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/maths.h"

#include <stdbool.h>
#include <stdint.h>


// printNumber grows the string in place and printCurrency prefixes it, so callers of either must
// size for the result, not the input: 20 digits of uint64 (or 19 plus a sign), six separators,
// the two bytes of UTF-8 £, and the terminator.
#define FORMATTER_NUMBER_SIZE 32

// Each formatter below writes markup of a fixed maximum shape, so the required capacity is a
// property of the function rather than of the caller. Declare output buffers with these sizes:
// the writes are bounded internally, but passing anything smaller is still a stack overflow.
#define FORMATTER_RATING_SIZE 44
#define FORMATTER_RATING_PLAIN_SIZE 16
#define FORMATTER_PERCENTAGE_SIZE 40
#define FORMATTER_MONEY_SIZE 24
#define FORMATTER_DELTA_SIZE 72

void formatter_init(void);
void formatter_printNumber(char *outString);
void formatter_printCurrency(char *outString);
void formatter_formatRating(float rating, char *outString);
void formatter_formatRatingPlain(float rating, char *outString);
void formatter_formatPercentage(float percentage, bool inverted, char *outString);
RGB formatter_qualityColour(uint8_t value);
void formatter_formatMoneyShort(int64_t value, bool showPlus, char *outString);
void formatter_formatDelta(double delta, bool higherIsBetter, const char *text, char *outString);
