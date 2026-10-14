// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "attributes.h"

#include <stdlib.h>
#include <string.h>


// The names used in options.yaml, indexed by ATTR_*
static const char *attributeKeys[ATTRIBUTE_COUNT] = {
	"Crossing",
	"Dribbling",
	"Finishing",
	"Heading",
	"LongShots",
	"Marking",
	"OffTheBall",
	"Passing",
	"PenaltyTaking",
	"Tackling",
	"Vision",
	"Handling",
	"AerialReach",
	"CommandOfArea",
	"Communication",
	"Kicking",
	"Throwing",
	"Anticipation",
	"Decisions",
	"OneOnOnes",
	"Positioning",
	"Reflexes",
	"FirstTouch",
	"Technique",
	"LeftFoot",
	"RightFoot",
	"Flair",
	"CornerTaking",
	"Teamwork",
	"WorkRate",
	"LongThrows",
	"Eccentricity",
	"RushingOut",
	"Punching",
	"Acceleration",
	"FreeKickTaking",
	"Strength",
	"Stamina",
	"Pace",
	"JumpingReach",
	"Leadership",
	"Dirtiness",
	"Balance",
	"Bravery",
	"Consistency",
	"Aggression",
	"Agility",
	"ImportantMatches",
	"InjuryProneness",
	"Versatility",
	"NaturalFitness",
	"Determination",
	"Composure",
	"Concentration",
	"Adaptability",
	"Ambition",
	"Loyalty",
	"Pressure",
	"Professionalism",
	"Sportsmanship",
	"Temperament",
	"Controversy",
};

// The names shown in the UI, indexed by ATTR_*
static const char *attributeNames[ATTRIBUTE_COUNT] = {
	"Crossing",
	"Dribbling",
	"Finishing",
	"Heading",
	"Long shots",
	"Marking",
	"Off the ball",
	"Passing",
	"Penalty taking",
	"Tackling",
	"Vision",
	"Handling",
	"Aerial reach",
	"Command of area",
	"Communication",
	"Kicking",
	"Throwing",
	"Anticipation",
	"Decisions",
	"One on ones",
	"Positioning",
	"Reflexes",
	"First touch",
	"Technique",
	"Left foot",
	"Right foot",
	"Flair",
	"Corners",
	"Teamwork",
	"Work rate",
	"Long throws",
	"Eccentricity",
	"Rushing out",
	"Punching",
	"Acceleration",
	"Free kicks",
	"Strength",
	"Stamina",
	"Pace",
	"Jumping reach",
	"Leadership",
	"Dirtiness",
	"Balance",
	"Bravery",
	"Consistency",
	"Aggression",
	"Agility",
	"Important matches",
	"Injury proneness",
	"Versatility",
	"Natural fitness",
	"Determination",
	"Composure",
	"Concentration",
	"Adaptability",
	"Ambition",
	"Loyalty",
	"Pressure",
	"Professionalism",
	"Sportsmanship",
	"Temperament",
	"Controversy",
};

const char *attributes_getKey(const uint8_t attribute) {
	return attribute < ATTRIBUTE_COUNT ? attributeKeys[attribute] : "";
}

const char *attributes_getName(const uint8_t attribute) {
	return attribute < ATTRIBUTE_COUNT ? attributeNames[attribute] : "";
}

// Keys are matched case-insensitively, as they always have been in options.yaml
bool attributes_fromKey(const char *key, uint8_t *outAttribute) {
	for (uint8_t i = 0; i < ATTRIBUTE_COUNT; ++i) {
		if (!strcasecmp(key, attributeKeys[i])) {
			*outAttribute = i;
			return true;
		}
	}

	return false;
}

static int compareByName(const void *a, const void *b) {
	return strcmp(attributeNames[*(const uint8_t*)a], attributeNames[*(const uint8_t*)b]);
}

void attributes_getSortedByName(uint8_t outAttributes[ATTRIBUTE_COUNT]) {
	for (uint8_t i = 0; i < ATTRIBUTE_COUNT; ++i) {
		outAttributes[i] = i;
	}
	qsort(outAttributes, ATTRIBUTE_COUNT, sizeof(*outAttributes), compareByName);
}
