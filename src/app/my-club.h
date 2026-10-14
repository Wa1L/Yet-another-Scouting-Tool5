// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/types.h"


void myClub_rebuild(void);
void myClub_reset(void);
int64_t myClub_getIndex(void);
uint16_t myClub_getClubReputation(int64_t clubIndex);
bool myClub_isLikelyToJoin(const Player *player, int64_t myClubIndex);
