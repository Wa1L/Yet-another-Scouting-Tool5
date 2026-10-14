// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "search-handler.h"
#include "app/player-table.h"
#include "app/search.h"
#include "app/helpers/attributes.h"
#include "app/helpers/text.h"

#include <gtk/gtk.h>


extern GameContext gameContext;

static inline bool valueFitsOneByte(int64_t value) {
	return value >= 0 && value <= 0xFF;
}

/** Reads a whole number in [0, max] from `buffer`; false when it is empty or out of range. */
static bool readNumber(GtkEntryBuffer *buffer, const int64_t max, int64_t *outValue) {
	const gchar *text = gtk_entry_buffer_get_text(buffer);
	if (text == NULL || *text == '\0') {
		return false;
	}

	const int64_t value = g_ascii_strtoll(text, NULL, 10);
	if (value < 0 || value > max) {
		return false;
	}

	*outValue = value;
	return true;
}

/**
 * Reads an amount of money, accepting the shorthand people actually type: "450k", "2.5m" and
 * "1,200,000" all work. False when the buffer is empty or does not hold a usable amount.
 */
bool searchHandler_parseMoney(const char *text, uint32_t *outValue) {
	if (text == NULL || *text == '\0') {
		return false;
	}

	char digits[32];
	size_t length = 0;
	for (const char *c = text; *c != '\0' && length + 1 < sizeof(digits); ++c) {
		if (*c != ',' && *c != ' ' && *c != '\xC2' && *c != '\xA3') { // Skip separators and "£"
			digits[length++] = *c;
		}
	}
	digits[length] = '\0';

	char *end = NULL;
	double value = g_ascii_strtod(digits, &end);
	if (end == digits) {
		return false;
	}
	if (*end == 'k' || *end == 'K') {
		value *= 1e3;
	} else if (*end == 'm' || *end == 'M') {
		value *= 1e6;
	} else if (*end == 'b' || *end == 'B') {
		value *= 1e9;
	}

	if (value < 0 || value > (double)UINT32_MAX) {
		return false;
	}

	*outValue = (uint32_t)value;
	return true;
}

static bool readMoney(GtkEntryBuffer *buffer, uint32_t *outValue) {
	return searchHandler_parseMoney(gtk_entry_buffer_get_text(buffer), outValue);
}

/** The attribute picked in an attribute filter's dropdown, or ATTRIBUTE_COUNT for none. */
uint8_t searchHandler_getDropDownAttribute(GtkDropDown *dropDown) {
	const guint selected = gtk_drop_down_get_selected(dropDown);
	if (selected == 0 || selected > ATTRIBUTE_COUNT) {
		return ATTRIBUTE_COUNT;
	}

	// The dropdown lists a placeholder followed by every attribute sorted by name
	uint8_t sorted[ATTRIBUTE_COUNT];
	attributes_getSortedByName(sorted);
	return sorted[selected - 1];
}

static void cacheAdditionalFilters(FilterOptions *options) {
	const FilterBuffer *fb = &gameContext.filterBuffer;
	const FilterControls *controls = &gameContext.filterControls;
	int64_t value;

	const char *name = gtk_editable_get_text(GTK_EDITABLE(controls->name));
	text_fold(name != NULL ? name : "", options->name, sizeof(options->name));
	g_strstrip(options->name);
	if (options->name[0] != '\0') {
		options->filterMask |= FILTER_HAS_NAME;
	}

	if (readMoney(fb->minValue, &options->minValue)) {
		options->filterMask |= FILTER_HAS_MIN_VALUE;
	}
	if (readMoney(fb->maxValue, &options->maxValue)) {
		options->filterMask |= FILTER_HAS_MAX_VALUE;
	}

	struct {
		GtkEntryBuffer *buffer;
		uint16_t *target;
		uint32_t flag;
	} reputations[] = {
		{fb->minHomeReputation, &options->minHomeReputation, FILTER_HAS_MIN_HOME_REPUTATION},
		{fb->maxHomeReputation, &options->maxHomeReputation, FILTER_HAS_MAX_HOME_REPUTATION},
		{fb->minCurrentReputation, &options->minCurrentReputation, FILTER_HAS_MIN_CURRENT_REPUTATION},
		{fb->maxCurrentReputation, &options->maxCurrentReputation, FILTER_HAS_MAX_CURRENT_REPUTATION},
		{fb->minWorldReputation, &options->minWorldReputation, FILTER_HAS_MIN_WORLD_REPUTATION},
		{fb->maxWorldReputation, &options->maxWorldReputation, FILTER_HAS_MAX_WORLD_REPUTATION},
	};
	for (size_t i = 0; i < G_N_ELEMENTS(reputations); ++i) {
		if (readNumber(reputations[i].buffer, UINT16_MAX, &value)) {
			*reputations[i].target = (uint16_t)value;
			options->filterMask |= reputations[i].flag;
		}
	}

	if (readMoney(fb->maxWage, &options->maxWage)) {
		options->filterMask |= FILTER_HAS_MAX_WAGE;
	}
	if (readNumber(fb->contractMonths, 1200, &value)) {
		options->contractMonths = (uint16_t)value;
		options->filterMask |= FILTER_HAS_CONTRACT_EXPIRY;
	}

	options->transferStatus = (uint8_t)gtk_drop_down_get_selected(controls->transferStatus);
	if (options->transferStatus != TRANSFER_FILTER_ANY && options->transferStatus <= TRANSFER_FILTER_EITHER) {
		options->filterMask |= FILTER_HAS_TRANSFER_STATUS;
	}
	options->foot = (uint8_t)gtk_drop_down_get_selected(controls->foot);
	if (options->foot != FOOT_FILTER_ANY && options->foot <= FOOT_FILTER_EITHER) {
		options->filterMask |= FILTER_HAS_FOOT;
	}

	if (gtk_check_button_get_active(controls->unattached)) {
		options->filterMask |= FILTER_HAS_UNATTACHED;
	}
	if (gtk_check_button_get_active(controls->fastLearner)) {
		options->filterMask |= FILTER_HAS_FAST_LEARNER;
	}
	if (gtk_check_button_get_active(controls->hotProspect)) {
		options->filterMask |= FILTER_HAS_HOT_PROSPECT;
	}

	for (uint8_t i = 0; i < ATTRIBUTE_FILTER_COUNT; ++i) {
		AttributeFilter *filter = &options->attributes[i];
		*filter = (AttributeFilter){.attribute = searchHandler_getDropDownAttribute(controls->attribute[i])};
		if (filter->attribute >= ATTRIBUTE_COUNT) {
			continue;
		}

		if (readNumber(fb->minAttribute[i], 20, &value)) {
			filter->min = (uint8_t)value;
		}
		if (readNumber(fb->maxAttribute[i], 20, &value)) {
			filter->max = (uint8_t)value;
		}
		if (filter->min || filter->max) {
			options->filterMask |= FILTER_HAS_ATTRIBUTE;
		}
	}
}

void searchHandler_cacheFilters(void) {
	FilterOptions *options = &gameContext.filterOptions;
	options->filterMask &= FILTER_HAS_CLUB | FILTER_HAS_NATIONALITY;

	const gchar *minAge = gtk_entry_buffer_get_text(gameContext.filterBuffer.minAge);
	const gchar *maxAge = gtk_entry_buffer_get_text(gameContext.filterBuffer.maxAge);
	const gchar *minCA = gtk_entry_buffer_get_text(gameContext.filterBuffer.minCA);
	const gchar *maxCA = gtk_entry_buffer_get_text(gameContext.filterBuffer.maxCA);
	const gchar *minPA = gtk_entry_buffer_get_text(gameContext.filterBuffer.minPA);
	const gchar *maxPA = gtk_entry_buffer_get_text(gameContext.filterBuffer.maxPA);
	const gchar *minRating = gtk_entry_buffer_get_text(gameContext.filterBuffer.minRating);
	const gchar *maxRating = gtk_entry_buffer_get_text(gameContext.filterBuffer.maxRating);

	const CheckBox *checks = &gameContext.checkboxes;
	const bool isPositionGK = gtk_check_button_get_active(checks->positionGK);
	const bool isPositionDL = gtk_check_button_get_active(checks->positionDL);
	const bool isPositionDC = gtk_check_button_get_active(checks->positionDC);
	const bool isPositionDR = gtk_check_button_get_active(checks->positionDR);
	const bool isPositionWBL = gtk_check_button_get_active(checks->positionWBL);
	const bool isPositionDM = gtk_check_button_get_active(checks->positionDM);
	const bool isPositionWBR = gtk_check_button_get_active(checks->positionWBR);
	const bool isPositionML = gtk_check_button_get_active(checks->positionML);
	const bool isPositionMC = gtk_check_button_get_active(checks->positionMC);
	const bool isPositionMR = gtk_check_button_get_active(checks->positionMR);
	const bool isPositionAML = gtk_check_button_get_active(checks->positionAML);
	const bool isPositionAMC = gtk_check_button_get_active(checks->positionAMC);
	const bool isPositionAMR = gtk_check_button_get_active(checks->positionAMR);
	const bool isPositionST = gtk_check_button_get_active(checks->positionST);

	options->positions = (isPositionGK ? POSITION_MASK_GK : 0)
		| (isPositionDL ? POSITION_MASK_DL : 0)
		| (isPositionDC ? POSITION_MASK_DC : 0)
		| (isPositionDR ? POSITION_MASK_DR : 0)
		| (isPositionWBL ? POSITION_MASK_WBL : 0)
		| (isPositionDM ? POSITION_MASK_DM : 0)
		| (isPositionWBR ? POSITION_MASK_WBR : 0)
		| (isPositionML ? POSITION_MASK_ML : 0)
		| (isPositionMC ? POSITION_MASK_MC : 0)
		| (isPositionMR ? POSITION_MASK_MR : 0)
		| (isPositionAML ? POSITION_MASK_AML : 0)
		| (isPositionAMC ? POSITION_MASK_AMC : 0)
		| (isPositionAMR ? POSITION_MASK_AMR : 0)
		| (isPositionST ? POSITION_MASK_ST : 0);

	if (options->positions) {
		options->filterMask |= FILTER_HAS_POSITION;
	}

	int64_t value;

	if (minAge != NULL && *minAge != '\0') {
		value = g_ascii_strtoll(minAge, NULL, 10);
		if (valueFitsOneByte(value)) {
			options->minAge = (uint8_t)value;
			options->filterMask |= FILTER_HAS_MIN_AGE;
		}
	}
	if (maxAge != NULL && *maxAge != '\0') {
		value = g_ascii_strtoll(maxAge, NULL, 10);
		if (valueFitsOneByte(value)) {
			options->maxAge = (uint8_t)value;
			options->filterMask |= FILTER_HAS_MAX_AGE;
		}
	}
	if (minCA != NULL && *minCA != '\0') {
		value = g_ascii_strtoll(minCA, NULL, 10);
		if (valueFitsOneByte(value)) {
			options->minCA = (uint8_t)value;
			options->filterMask |= FILTER_HAS_MIN_CA;
		}
	}
	if (maxCA != NULL && *maxCA != '\0') {
		value = g_ascii_strtoll(maxCA, NULL, 10);
		if (valueFitsOneByte(value)) {
			options->maxCA = (uint8_t)value;
			options->filterMask |= FILTER_HAS_MAX_CA;
		}
	}
	if (minPA != NULL && *minPA != '\0') {
		value = g_ascii_strtoll(minPA, NULL, 10);
		if (valueFitsOneByte(value)) {
			options->minPA = (uint8_t)value;
			options->filterMask |= FILTER_HAS_MIN_PA;
		}
	}
	if (maxPA != NULL && *maxPA != '\0') {
		value = g_ascii_strtoll(maxPA, NULL, 10);
		if (valueFitsOneByte(value)) {
			options->maxPA = (uint8_t)value;
			options->filterMask |= FILTER_HAS_MAX_PA;
		}
	}
	if (minRating != NULL && *minRating != '\0') {
		options->minRating = (float)g_ascii_strtod(minRating, NULL);
		options->filterMask |= FILTER_HAS_MIN_RATING;
	}
	if (maxRating != NULL && *maxRating != '\0') {
		options->maxRating = (float)g_ascii_strtod(maxRating, NULL);
		options->filterMask |= FILTER_HAS_MAX_RATING;
	}

	cacheAdditionalFilters(options);
}

void searchHandler_doSearch(const bool refreshFilterCache) {
	if (refreshFilterCache) {
		searchHandler_cacheFilters();
	}

	search_findPlayers();
	playerTable_populate();
}

void searchHandler_clearFilters(void) {
	gtk_entry_buffer_set_text(gameContext.filterBuffer.minAge, "", 1);
	gtk_entry_buffer_set_text(gameContext.filterBuffer.maxAge, "", 1);
	gtk_entry_buffer_set_text(gameContext.filterBuffer.minCA, "", 1);
	gtk_entry_buffer_set_text(gameContext.filterBuffer.maxCA, "", 1);
	gtk_entry_buffer_set_text(gameContext.filterBuffer.minPA, "", 1);
	gtk_entry_buffer_set_text(gameContext.filterBuffer.maxPA, "", 1);
	gtk_entry_buffer_set_text(gameContext.filterBuffer.minRating, "", 1);
	gtk_entry_buffer_set_text(gameContext.filterBuffer.maxRating, "", 1);

	FilterBuffer *fb = &gameContext.filterBuffer;
	GtkEntryBuffer *const buffers[] = {
		fb->minValue,
		fb->maxValue,
		fb->minHomeReputation,
		fb->maxHomeReputation,
		fb->minCurrentReputation,
		fb->maxCurrentReputation,
		fb->minWorldReputation,
		fb->maxWorldReputation,
		fb->maxWage,
		fb->contractMonths,
	};
	for (size_t i = 0; i < G_N_ELEMENTS(buffers); ++i) {
		gtk_entry_buffer_set_text(buffers[i], "", 0);
	}

	const FilterControls *controls = &gameContext.filterControls;
	for (uint8_t i = 0; i < ATTRIBUTE_FILTER_COUNT; ++i) {
		gtk_entry_buffer_set_text(fb->minAttribute[i], "", 0);
		gtk_entry_buffer_set_text(fb->maxAttribute[i], "", 0);
		gtk_drop_down_set_selected(controls->attribute[i], 0);
	}
	gtk_editable_set_text(GTK_EDITABLE(controls->name), "");
	gtk_drop_down_set_selected(controls->foot, FOOT_FILTER_ANY);
	gtk_drop_down_set_selected(controls->transferStatus, TRANSFER_FILTER_ANY);
	gtk_check_button_set_active(controls->unattached, FALSE);
	gtk_check_button_set_active(controls->fastLearner, FALSE);
	gtk_check_button_set_active(controls->hotProspect, FALSE);

	gtk_editable_set_text(GTK_EDITABLE(gameContext.clubDatalist->entry), "");
	gtk_editable_set_text(GTK_EDITABLE(gameContext.nationalityDatalist->entry), "");

	const CheckBox *checks = &gameContext.checkboxes;
	gtk_check_button_set_active(checks->positionGK, FALSE);
	gtk_check_button_set_active(checks->positionDL, FALSE);
	gtk_check_button_set_active(checks->positionDC, FALSE);
	gtk_check_button_set_active(checks->positionDR, FALSE);
	gtk_check_button_set_active(checks->positionWBL, FALSE);
	gtk_check_button_set_active(checks->positionDM, FALSE);
	gtk_check_button_set_active(checks->positionWBR, FALSE);
	gtk_check_button_set_active(checks->positionML, FALSE);
	gtk_check_button_set_active(checks->positionMC, FALSE);
	gtk_check_button_set_active(checks->positionMR, FALSE);
	gtk_check_button_set_active(checks->positionAML, FALSE);
	gtk_check_button_set_active(checks->positionAMC, FALSE);
	gtk_check_button_set_active(checks->positionAMR, FALSE);
	gtk_check_button_set_active(checks->positionST, FALSE);

	gameContext.filterOptions.positions = 0;
}
