// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>


void text_fold(const char *text, char *outText, size_t size);
bool text_containsFolded(const char *text, const char *foldedNeedle);
