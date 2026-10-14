// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filters.h"
#include "app/entities.h"
#include "app/my-club.h"
#include "app/player-table.h"
#include "app/search-handler.h"
#include "app/ui.h"
#include "app/helpers/attributes.h"
#include "app/helpers/formatter.h"
#include "app/helpers/vector.h"
#include "core/logger.h"


extern GameContext gameContext;

// Set while we mutate filter widgets ourselves, so their change signals do not re-run a search
static bool filtersAreResetting = false;

BUILDER_CALLBACK void callbacks_onFiltersClear(void) {
	filtersAreResetting = true;
	searchHandler_clearFilters();
	filtersAreResetting = false;

	ui_clearFilterTags();
	playerTable_clear();
}

BUILDER_CALLBACK void callbacks_onPositionToggled(GtkCheckButton *button, gpointer data) {
	(void)button;
	(void)data;

	// Filling in the dropdowns at startup notifies before the results table exists
	if (filtersAreResetting || gameContext.searchResults == NULL) {
		return;
	}

	searchHandler_cacheFilters();
	if (gameContext.filterOptions.filterMask) {
		searchHandler_doSearch(false);
	} else {
		playerTable_clear();
	}

	callbacks_updateFilterTags();
}

// Notified on "selected", so the dropdowns behave like the toggles above.
BUILDER_CALLBACK void callbacks_onFilterDropDownChanged(GObject *object, GParamSpec *pspec, gpointer data) {
	(void)object;
	(void)pspec;
	callbacks_onPositionToggled(NULL, data);
}

// GtkSearchEntry debounces this signal, so typing a name does not search on every keystroke.
BUILDER_CALLBACK void callbacks_onNameSearchChanged(GtkSearchEntry *entry, gpointer data) {
	(void)entry;
	callbacks_onPositionToggled(NULL, data);
}

BUILDER_CALLBACK gboolean callbacks_onFiltersKeypress(
	GtkEventControllerKey *controller,
	guint keyval,
	guint keycode,
	GdkModifierType state,
	gpointer data
) {
	(void)controller;
	(void)keycode;
	(void)state;
	(void)data;

	if (keyval == GDK_KEY_Return) {
		searchHandler_doSearch(true);
		callbacks_updateFilterTags();

		return TRUE;
	}

	return FALSE;
}

BUILDER_CALLBACK void callbacks_onFilterRun(void) {
	searchHandler_doSearch(true);
	callbacks_updateFilterTags();
}

/**
 * A tag for each filter bound, or a cleared entry when the bound did not parse, mirroring what the
 * original filters below do.
 */
static void createBoundTag(
	const bool isSet,
	const char *text,
	GtkEntryBuffer *buffer
) {
	if (isSet) {
		ui_createFilterTag(text, buffer);
	} else {
		gtk_entry_buffer_set_text(buffer, "", 0);
	}
}

static void createAdditionalFilterTags(const FilterOptions options, const FilterBuffer fb) {
	const uint32_t mask = options.filterMask;
	const FilterControls *controls = &gameContext.filterControls;
	char buffer[96];
	char money[FORMATTER_MONEY_SIZE];

	formatter_formatMoneyShort(options.minValue, false, money);
	snprintf(buffer, sizeof(buffer), "Value ≥ %s", money);
	createBoundTag(mask & FILTER_HAS_MIN_VALUE, buffer, fb.minValue);
	formatter_formatMoneyShort(options.maxValue, false, money);
	snprintf(buffer, sizeof(buffer), "Value ≤ %s", money);
	createBoundTag(mask & FILTER_HAS_MAX_VALUE, buffer, fb.maxValue);

	snprintf(buffer, sizeof(buffer), "Home rep ≥ %d", options.minHomeReputation);
	createBoundTag(mask & FILTER_HAS_MIN_HOME_REPUTATION, buffer, fb.minHomeReputation);
	snprintf(buffer, sizeof(buffer), "Home rep ≤ %d", options.maxHomeReputation);
	createBoundTag(mask & FILTER_HAS_MAX_HOME_REPUTATION, buffer, fb.maxHomeReputation);
	snprintf(buffer, sizeof(buffer), "Current rep ≥ %d", options.minCurrentReputation);
	createBoundTag(mask & FILTER_HAS_MIN_CURRENT_REPUTATION, buffer, fb.minCurrentReputation);
	snprintf(buffer, sizeof(buffer), "Current rep ≤ %d", options.maxCurrentReputation);
	createBoundTag(mask & FILTER_HAS_MAX_CURRENT_REPUTATION, buffer, fb.maxCurrentReputation);
	snprintf(buffer, sizeof(buffer), "World rep ≥ %d", options.minWorldReputation);
	createBoundTag(mask & FILTER_HAS_MIN_WORLD_REPUTATION, buffer, fb.minWorldReputation);
	snprintf(buffer, sizeof(buffer), "World rep ≤ %d", options.maxWorldReputation);
	createBoundTag(mask & FILTER_HAS_MAX_WORLD_REPUTATION, buffer, fb.maxWorldReputation);

	formatter_formatMoneyShort(options.maxWage, false, money);
	snprintf(buffer, sizeof(buffer), "Wage ≤ %s p/w", money);
	createBoundTag(mask & FILTER_HAS_MAX_WAGE, buffer, fb.maxWage);
	snprintf(
		buffer,
		sizeof(buffer),
		options.contractMonths == 1 ? "Expires ≤ %d month" : "Expires ≤ %d months",
		options.contractMonths
	);
	createBoundTag(mask & FILTER_HAS_CONTRACT_EXPIRY, buffer, fb.contractMonths);

	if (mask & FILTER_HAS_TRANSFER_STATUS) {
		static const char *const statuses[] = {"", "Transfer listed", "Loan listed", "Transfer or loan listed"};
		ui_createDropDownFilterTag(statuses[options.transferStatus], controls->transferStatus);
	}
	if (mask & FILTER_HAS_FOOT) {
		static const char *const feet[] = {"", "Foot: Left", "Foot: Right", "Foot: Either"};
		ui_createDropDownFilterTag(feet[options.foot], controls->foot);
	}
	if (mask & FILTER_HAS_UNATTACHED) {
		ui_createCheckFilterTag("Unattached", controls->unattached);
	}
	if (mask & FILTER_HAS_LIKELY_TO_JOIN) {
		ui_createCheckFilterTag(
			myClub_getIndex() >= 0 ? "Likely to join my club" : "Likely to join: set your club in Settings",
			controls->likelyToJoin
		);
	}
	if (mask & FILTER_HAS_FAST_LEARNER) {
		ui_createCheckFilterTag("Fast learner", controls->fastLearner);
	}
	if (mask & FILTER_HAS_HOT_PROSPECT) {
		ui_createCheckFilterTag("Hot prospect", controls->hotProspect);
	}

	for (uint8_t i = 0; i < ATTRIBUTE_FILTER_COUNT; ++i) {
		const AttributeFilter *filter = &options.attributes[i];
		if (filter->attribute >= ATTRIBUTE_COUNT) {
			continue;
		}

		const char *name = attributes_getName(filter->attribute);
		snprintf(buffer, sizeof(buffer), "%s ≥ %d", name, filter->min);
		createBoundTag(filter->min, buffer, fb.minAttribute[i]);
		snprintf(buffer, sizeof(buffer), "%s ≤ %d", name, filter->max);
		createBoundTag(filter->max, buffer, fb.maxAttribute[i]);
	}
}

void callbacks_updateFilterTags(void) {
	ui_clearFilterTags();

	const FilterOptions options = gameContext.filterOptions;
	const FilterBuffer fb = gameContext.filterBuffer;

	char buffer[96] = {0};
	if (options.filterMask & FILTER_HAS_NAME) {
		snprintf(buffer, sizeof(buffer), "Name: %s", gtk_editable_get_text(GTK_EDITABLE(gameContext.filterControls.name)));
		ui_createEditableFilterTag(buffer, GTK_EDITABLE(gameContext.filterControls.name));
	}
	if (options.filterMask & FILTER_HAS_MIN_AGE) {
		snprintf(buffer, sizeof(buffer), "Age ≥ %d", options.minAge);
		ui_createFilterTag(buffer, fb.minAge);
	} else {
		gtk_entry_buffer_set_text(fb.minAge, "", 1);
	}
	if (options.filterMask & FILTER_HAS_MAX_AGE) {
		snprintf(buffer, sizeof(buffer), "Age ≤ %d", options.maxAge);
		ui_createFilterTag(buffer, fb.maxAge);
	} else {
		gtk_entry_buffer_set_text(fb.maxAge, "", 1);
	}
	if (options.filterMask & FILTER_HAS_MIN_CA) {
		snprintf(buffer, sizeof(buffer), "CA ≥ %d", options.minCA);
		ui_createFilterTag(buffer, fb.minCA);
	} else {
		gtk_entry_buffer_set_text(fb.minCA, "", 1);
	}
	if (options.filterMask & FILTER_HAS_MAX_CA) {
		snprintf(buffer, sizeof(buffer), "CA ≤ %d", options.maxCA);
		ui_createFilterTag(buffer, fb.maxCA);
	} else {
		gtk_entry_buffer_set_text(fb.maxCA, "", 1);
	}
	if (options.filterMask & FILTER_HAS_MIN_PA) {
		snprintf(buffer, sizeof(buffer), "PA ≥ %d", options.minPA);
		ui_createFilterTag(buffer, fb.minPA);
	} else {
		gtk_entry_buffer_set_text(fb.minPA, "", 1);
	}
	if (options.filterMask & FILTER_HAS_MAX_PA) {
		snprintf(buffer, sizeof(buffer), "PA ≤ %d", options.maxPA);
		ui_createFilterTag(buffer, fb.maxPA);
	} else {
		gtk_entry_buffer_set_text(fb.maxPA, "", 1);
	}
	if (options.filterMask & FILTER_HAS_MIN_RATING) {
		snprintf(buffer, sizeof(buffer), "Rating ≥ %.2f%%", options.minRating);
		ui_createFilterTag(buffer, fb.minRating);
	} else {
		gtk_entry_buffer_set_text(fb.minRating, "", 1);
	}
	if (options.filterMask & FILTER_HAS_MAX_RATING) {
		snprintf(buffer, sizeof(buffer), "Rating ≤ %.2f%%", options.maxRating);
		ui_createFilterTag(buffer, fb.maxRating);
	} else {
		gtk_entry_buffer_set_text(fb.maxRating, "", 1);
	}
	createAdditionalFilterTags(options, fb);

	if (options.filterMask & FILTER_HAS_CLUB) {
		const Club *club = entities_getClub(options.clubIndex);
		if (club != NULL) {
			ui_createClubFilterTag(club->shortName, GTK_EDITABLE(gameContext.clubDatalist->entry));
		}
	}
	if (options.filterMask & FILTER_HAS_NATIONALITY) {
		const Nation *nation = entities_getNation(options.nationalityIndex);
		if (nation != NULL) {
			ui_createNationalityFilterTag(nation->name, GTK_EDITABLE(gameContext.nationalityDatalist->entry));
		}
	}

	if (options.filterMask & FILTER_HAS_POSITION) {
		// Indices line up with the POSITION_MASK_* bit positions
		static const char *const names[] = {
			"GK",
			"DL",
			"DC",
			"DR",
			"WBL",
			"DM",
			"WBR",
			"ML",
			"MC",
			"MR",
			"AML",
			"AMC",
			"AMR",
			"ST",
		};
		const CheckBox *checks = &gameContext.checkboxes;
		GtkCheckButton *const buttons[] = {
			checks->positionGK,
			checks->positionDL,
			checks->positionDC,
			checks->positionDR,
			checks->positionWBL,
			checks->positionDM,
			checks->positionWBR,
			checks->positionML,
			checks->positionMC,
			checks->positionMR,
			checks->positionAML,
			checks->positionAMC,
			checks->positionAMR,
			checks->positionST,
		};

		for (uint8_t i = 0; i < G_N_ELEMENTS(names); ++i) {
			if (options.positions & (1u << i)) {
				ui_createPositionFilterTag(names[i], buttons[i]);
			}
		}
	}

	if (!options.filterMask && gameContext.searchResults != NULL && vector_length(gameContext.searchResults->data) > 0) {
		ui_createFilterTag("Showing: all", NULL);
	}
}
