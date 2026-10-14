// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/constants.h"

#include <stdbool.h>
#include <stdint.h>


const char *attributes_getKey(uint8_t attribute);
const char *attributes_getName(uint8_t attribute);
bool attributes_fromKey(const char *key, uint8_t *outAttribute);
void attributes_getSortedByName(uint8_t outAttributes[ATTRIBUTE_COUNT]);
