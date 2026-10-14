// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nationality-search.h"
#include "app/entities.h"
#include "app/search-handler.h"
#include "app/ui.h"
#include "app/callbacks/filters.h"
#include "core/logger.h"
#include "platform/platform.h"


extern GameContext gameContext;

void nationalitySearch_init(void) {
	GtkBuilder *builder = gameContext.builder;
	SearchDatalist *datalist = g_new0(SearchDatalist, 1);

	datalist->entry = GTK_SEARCH_ENTRY(gtk_builder_get_object(builder, "entry:nationality"));
	datalist->popover = GTK_POPOVER(gtk_builder_get_object(builder, "popover:nationality-search"));
	datalist->listBox = GTK_LIST_BOX(gtk_builder_get_object(builder, "listbox:nationality-search"));

	gtk_widget_set_parent(GTK_WIDGET(datalist->popover), GTK_WIDGET(datalist->entry));
	gtk_popover_set_pointing_to(datalist->popover, &(GdkRectangle){.x = 102, .y = 27, .width = 1, .height = 1});

	gameContext.nationalityDatalist = datalist;

	g_signal_connect(datalist->entry, "changed", G_CALLBACK(callbacks_onNationalityChange), datalist);
	g_signal_connect(datalist->listBox, "row-activated", G_CALLBACK(callbacks_onNationalitySelected), datalist);
}

void callbacks_onNationalityChange(GtkEditable *editable, const SearchDatalist *datalist) {
	gameContext.filterOptions.filterMask &= ~(uint32_t)FILTER_HAS_NATIONALITY;

	const char *searchValue = gtk_editable_get_text(editable);
	if (*searchValue == '\0') {
		gtk_popover_popdown(datalist->popover);
		return;
	}

#ifdef DEBUG
	const int64_t timeStart = platform_getMicroseconds();
#endif

	uint64_t indices[NATIONALITY_SEARCH_LIMIT];
	uint64_t count = 0;
	for (uint64_t i = 0; i < gameContext.nationCount && count < NATIONALITY_SEARCH_LIMIT; ++i) {
		const Nation *nation = &gameContext.nations[i];
		if (nation->name[0] == '\0') {
			continue;
		}

		if (
			g_str_match_string(searchValue, nation->name, TRUE)
			|| g_str_match_string(searchValue, nation->code, TRUE)
		) {
			indices[count] = i;
			++count;
		}
	}

	for (uint64_t i = 1; i < count; ++i) {
		const uint64_t nationIndex = indices[i];
		const char *key = gameContext.nations[nationIndex].name;
		int64_t j = (int64_t)i - 1;

		while (j >= 0 && strncmp(gameContext.nations[indices[j]].name, key, MAX_NATION_STRING_LENGTH) > 0) {
			indices[j + 1] = indices[j];
			--j;
		}
		indices[j + 1] = nationIndex;
	}

	LOG_DEBUG("Searched %" PRIu64 " nations in %" PRId64 " microseconds", count, platform_getMicroseconds() - timeStart);

	// Clear the list
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(GTK_WIDGET(datalist->listBox))) != NULL) {
		gtk_list_box_remove(datalist->listBox, child);
	}

	char label[MAX_NATION_STRING_LENGTH + 8];
	for (uint64_t i = 0; i < count; ++i) {
		const uint64_t nationIndex = indices[i];
		const Nation *nation = &gameContext.nations[nationIndex];
		snprintf(label, sizeof(label), "%s (%s)", nation->name, nation->code);

		GtkWidget *row = gtk_label_new(label);
		g_object_set_data(G_OBJECT(row), "index", GINT_TO_POINTER(nationIndex));
		gtk_widget_set_halign(row, GTK_ALIGN_START);
		gtk_list_box_append(datalist->listBox, row);
	}

	if (count > 0) {
		gtk_popover_popup(datalist->popover);
	} else {
		gtk_popover_popdown(datalist->popover);
	}
}

void callbacks_onNationalitySelected(
	const GtkListBox *box,
	GtkListBoxRow *row,
	const SearchDatalist *datalist
) {
	(void)box;

	if (row == NULL) {
		return;
	}

	GtkWidget *child = gtk_list_box_row_get_child(row);
	const uint8_t nationIndex = (uint8_t)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "index"));
	if (!nationalitySearch_select(nationIndex)) {
		gtk_popover_popdown(datalist->popover);
	}
}

/** Filters the search by a nationality, as if it had been picked from the nation search's list. */
bool nationalitySearch_select(const uint8_t nationIndex) {
	const SearchDatalist *datalist = gameContext.nationalityDatalist;
	// The nations may have been republished since the list was built.
	const Nation *nation = entities_getNation(nationIndex);
	if (nation == NULL || datalist == NULL) {
		return false;
	}

	// Block the change handler while we update the text
	g_signal_handlers_block_by_func(
		datalist->entry,
		(gpointer)(uintptr_t)callbacks_onNationalityChange,
		(gpointer)datalist
	);

	gtk_editable_set_text(GTK_EDITABLE(datalist->entry), nation->name);

	// Unblock the handler
	g_signal_handlers_unblock_by_func(
		datalist->entry,
		(gpointer)(uintptr_t)callbacks_onNationalityChange,
		(gpointer)datalist
	);

	gtk_popover_popdown(datalist->popover);

	gameContext.filterOptions.filterMask |= FILTER_HAS_NATIONALITY;
	gameContext.filterOptions.nationalityIndex = nationIndex;

	// NOTE: this is the same as the on-enter handler
	searchHandler_doSearch(true);
	callbacks_updateFilterTags();

	return true;
}
