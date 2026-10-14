// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/config.h"


#ifdef MOCKS_MODE
#define PLAYER_VINI ((Player){ \
	.attributes = { 68, 95, 83, 62, 62, 18, 86, 75, 59, 31, 73, 15, 5, 5, 5, 5, 10, 84, 81, 10, 47, 10, 73, 79, 100, 60, 80, 37, 63, 71, 24, 5, 5, 15, 93, 40, 61, 80, 88, 39, 69, 55, 79, 84, 69, 85, 88, 65, 25, 67, 78, 100, 75, 65, 15, 17, 12, 9, 14, 8, 4, 18 }, \
	.ratings = { { .value = 81.53f, .position = POSITION_GROUPED_W }, { .value = 76.69f, .position = POSITION_GROUPED_ST } }, \
	.commonName = "Vinícius Júnior", \
	.forename = "Vinícius José", \
	.surname = "Paixão de Oliveira Júnior", \
	.nationality = { 189, 170, 255, 255 }, \
	.rowId = 23442, \
	.rid = 19302146, \
	.uid = 19302146, \
	.personAddress = 0xa06cf440, \
	.playerAddress = 0xa06cf1c8, \
	.guideValue = 252086992, \
	.weeklyWage = 350000, \
	.contractEnd = {.days = 181, .year = 2027}, \
	.clubIndex = 1125, \
	.sharpness = 10000, \
	.fatigue = 13, \
	.condition = 8737, \
	.homeReputation = 8550, \
	.currentReputation = 9541, \
	.worldReputation = 10000, \
	.positions = {1, 1, 1, 1, 1, 1, 16, 1, 10, 20, 1, 15, 19, 1, 1 }, \
	.age = 24, \
	.ca = 190, \
	.pa = 190, \
	.morale = 13, \
	.canDevelopQuickly = false, \
	.isHotProspect = false, \
	})
#define PLAYER_JEFF ((Player){ \
	.attributes = { 68, 95, 83, 62, 62, 18, 86, 75, 59, 31, 73, 15, 5, 5, 5, 5, 10, 84, 81, 10, 47, 10, 73, 79, 60, 100, 80, 37, 63, 71, 24, 5, 5, 15, 93, 40, 61, 80, 88, 39, 69, 55, 79, 84, 69, 85, 88, 65, 25, 67, 78, 100, 75, 65, 15, 17, 12, 9, 14, 8, 4, 18 }, \
	.ratings = { { .value = 92.65f, .position = POSITION_GROUPED_W }, { .value = 90.58f, .position = POSITION_GROUPED_AM } }, \
	.forename = "Jeff", \
	.surname = "Jefferson", \
	.nationality = { 189, 255, 255, 255 }, \
	.rowId = 99999, \
	.rid = 999999999, \
	.uid = 999999999, \
	.personAddress = 0xa06df440, \
	.playerAddress = 0xa06df1c8, \
	.guideValue = 300000000, \
	.weeklyWage = 675000, \
	.contractEnd = {.days = 181, .year = 2029}, \
	.transferStatus = TRANSFER_STATUS_LISTED, \
	.clubIndex = 1125, \
	.sharpness = 10000, \
	.fatigue = -7000, \
	.condition = 3000, \
	.homeReputation = 10000, \
	.currentReputation = 10000, \
	.worldReputation = 10000, \
	.positions = {1, 1, 1, 1, 1, 1, 20, 1, 20, 20, 20, 20, 20, 1, 1 }, \
	.age = 17, \
	.ca = 190, \
	.pa = 200, \
	.morale = 10, \
	.canDevelopQuickly = true, \
	.isHotProspect = true, \
	})
#define PLAYER_GK ((Player){ \
	.attributes = { 68, 95, 83, 62, 62, 18, 86, 75, 59, 31, 73, 15, 5, 5, 5, 5, 10, 84, 81, 10, 47, 10, 73, 79, 100, 24, 80, 37, 63, 71, 24, 5, 5, 15, 93, 40, 61, 80, 88, 39, 69, 55, 79, 84, 69, 85, 88, 65, 25, 67, 78, 100, 75, 65, 15, 17, 12, 9, 14, 8, 4, 18 }, \
	.ratings = { { .value = 84.12f, .position = POSITION_GROUPED_GK } }, \
	.forename = "Goal", \
	.surname = "Keeper", \
	.nationality = { 189, 255, 255, 255 }, \
	.rowId = 99998, \
	.rid = 999999998, \
	.uid = 999999998, \
	.personAddress = 0xa06df441, \
	.playerAddress = 0xa06df1c9, \
	.guideValue = 123456789, \
	.weeklyWage = 288000, \
	.contractEnd = {.days = 365, .year = 2026}, \
	.transferStatus = TRANSFER_STATUS_LOAN_LISTED, \
	.clubIndex = 1125, \
	.sharpness = 8000, \
	.fatigue = 0, \
	.condition = 7000, \
	.homeReputation = 6000, \
	.currentReputation = 8000, \
	.worldReputation = 4000, \
	.positions = {20, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, \
	.age = 24, \
	.ca = 168, \
	.pa = 184, \
	.morale = 20, \
	.canDevelopQuickly = false, \
	.isHotProspect = true, \
	.injury = {.duration = 5, .treatmentMask = 1, .date = (DateTime){.days=34, .year=2024, .time=15}}, \
	})
#define PLAYER_BY_ID PLAYER_VINI
#define MOCK_INJURY_NAME "broken hand"

#define PLAYER_BY_ID_CLUB (Club){.name = "Real Madrid Club de Fútbol", .shortName = "Real Madrid", .address = 1, .teamType = 19}
#define PLAYER_BY_ID_NATION_1 (Nation){.code = "BRA", .name = "Brazil"}
#define PLAYER_BY_ID_NATION_2 (Nation){.code = "ESP", .name = "Spain"}
#endif
