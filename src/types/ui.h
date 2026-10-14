// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define ATTRIBUTE_FILTER_COUNT 3

typedef struct {
	GtkEntryBuffer *minAge;
	GtkEntryBuffer *maxAge;
	GtkEntryBuffer *minCA;
	GtkEntryBuffer *maxCA;
	GtkEntryBuffer *minPA;
	GtkEntryBuffer *maxPA;
	GtkEntryBuffer *minRating;
	GtkEntryBuffer *maxRating;
	GtkEntryBuffer *minValue;
	GtkEntryBuffer *maxValue;
	GtkEntryBuffer *minHomeReputation;
	GtkEntryBuffer *maxHomeReputation;
	GtkEntryBuffer *minCurrentReputation;
	GtkEntryBuffer *maxCurrentReputation;
	GtkEntryBuffer *minWorldReputation;
	GtkEntryBuffer *maxWorldReputation;
	GtkEntryBuffer *maxWage;
	GtkEntryBuffer *contractMonths;
	GtkEntryBuffer *minAttribute[ATTRIBUTE_FILTER_COUNT];
	GtkEntryBuffer *maxAttribute[ATTRIBUTE_FILTER_COUNT];
} FilterBuffer;

/** The filter widgets that are not plain text entries. */
typedef struct {
	GtkSearchEntry *name;
	GtkDropDown *foot;
	GtkDropDown *transferStatus;
	GtkDropDown *attribute[ATTRIBUTE_FILTER_COUNT];
	GtkCheckButton *unattached;
	GtkCheckButton *likelyToJoin;
	GtkCheckButton *fastLearner;
	GtkCheckButton *hotProspect;
} FilterControls;
