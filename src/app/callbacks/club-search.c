// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "club-search.h"
#include "app/cache.h"
#include "app/entities.h"
#include "app/search-handler.h"
#include "app/ui.h"
#include "app/callbacks/filters.h"
#include "core/logger.h"
#include "platform/platform.h"


extern GameContext gameContext;

/**
 * Bumped on every keystroke, and once more when the search entry is torn down. Each worker carries
 * the value it started with, so anything that no longer matches belongs to a superceded keystroke —
 * or to a window that is going away — and is thrown away instead of touching the UI.
 *
 * Only the main thread writes it, but the workers read it, so every access goes through
 * g_atomic_int_*.
 */
static gint searchGeneration = 0;

static void runThreadedSearch(SearchContext *context);

// The entry, popover and list box all belong to the main window, so once that is destroyed the
// pointers held in gameContext.clubDatalist dangle. Invalidating every in-flight search here stops
// a worker that is mid-scan from calling back into freed widgets.
static void onSearchEntryDestroy(GtkWidget *widget, gpointer userData) {
	(void)widget;
	(void)userData;

	g_atomic_int_add(&searchGeneration, 1);
}

void clubSearch_init(void) {
	GtkBuilder *builder = gameContext.builder;
	SearchDatalist *dataList = g_new0(SearchDatalist, 1);

	dataList->entry = GTK_SEARCH_ENTRY(gtk_builder_get_object(builder, "entry:club"));
	dataList->popover = GTK_POPOVER(gtk_builder_get_object(builder, "popover:club-search"));
	dataList->listBox = GTK_LIST_BOX(gtk_builder_get_object(builder, "listbox:club-search"));

	gtk_widget_set_parent(GTK_WIDGET(dataList->popover), GTK_WIDGET(dataList->entry));
	gtk_popover_set_pointing_to(dataList->popover, &(GdkRectangle){.x = 102, .y = 27, .width = 1, .height = 1});

	gameContext.clubDatalist = dataList;

	g_signal_connect(dataList->entry, "changed", G_CALLBACK(callbacks_OnClubNameChange), dataList);
	g_signal_connect(dataList->listBox, "row-activated", G_CALLBACK(callbacks_onClubNameSelected), dataList);
	g_signal_connect(dataList->entry, "destroy", G_CALLBACK(onSearchEntryDestroy), NULL);
}

void callbacks_OnClubNameChange(GtkEditable *editable, SearchDatalist *dataList) {
	gameContext.filterOptions.filterMask &= ~(uint32_t)FILTER_HAS_CLUB;

	const char *searchValue = gtk_editable_get_text(editable);
	if (strlen(searchValue) < 3) {
		gtk_popover_popdown(dataList->popover);
		return;
	}

	SearchContext *context = malloc(sizeof(SearchContext));
	context->dataList = dataList;
	context->count = 0;
	// g_atomic_int_add returns the previous value, so this records the generation the keystroke
	// establishes; every search started before it is now stale.
	context->generation = g_atomic_int_add(&searchGeneration, 1) + 1;
	strncpy(context->searchValue, searchValue, CLUB_SEARCH_NAME_LENGTH - 1);
	context->searchValue[CLUB_SEARCH_NAME_LENGTH - 1] = '\0';

	runThreadedSearch(context);
}

void callbacks_onClubNameSelected(
	const GtkListBox *box,
	GtkListBoxRow *row,
	const SearchDatalist *dataList
) {
	(void)box;

	if (row == NULL) {
		return;
	}

	GtkWidget *child = gtk_list_box_row_get_child(row);
	const int64_t clubIndex = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "index"));
	if (!clubSearch_select(clubIndex)) {
		gtk_popover_popdown(dataList->popover);
	}
}

/** Filters the search by a club, as if it had been picked from the club search's list. */
bool clubSearch_select(const int64_t clubIndex) {
	const SearchDatalist *dataList = gameContext.clubDatalist;
	const Club *club = entities_getClub(clubIndex);
	if (club == NULL || dataList == NULL) {
		return false;
	}

	// Block the change handler while we update the text
	g_signal_handlers_block_by_func(
		dataList->entry,
		(gpointer)(uintptr_t)callbacks_OnClubNameChange,
		(gpointer)dataList
	);

	gtk_editable_set_text(GTK_EDITABLE(dataList->entry), club->shortName);

	// Unblock the handler
	g_signal_handlers_unblock_by_func(
		dataList->entry,
		(gpointer)(uintptr_t)callbacks_OnClubNameChange,
		(gpointer)dataList
	);

	gtk_popover_popdown(dataList->popover);

	gameContext.filterOptions.filterMask |= FILTER_HAS_CLUB;
	gameContext.filterOptions.clubIndex = clubIndex;

	// NOTE: this is the same as the on-enter handler
	searchHandler_doSearch(true);
	callbacks_updateFilterTags();

	return true;
}

// This runs on the main thread; it's safe to modify the UI
static gboolean updateUIWithResults(gpointer userData) {
	SearchContext *context = userData;

	// Authoritative staleness check: this runs on the main thread, so neither a newer keystroke nor
	// the window teardown can slip in between here and the last gtk_* call below.
	if (context->generation != g_atomic_int_get(&searchGeneration)) {
		free(context);
		return G_SOURCE_REMOVE;
	}

	const SearchDatalist *dataList = context->dataList;

	// Clear the list
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(GTK_WIDGET(dataList->listBox))) != NULL) {
		gtk_list_box_remove(dataList->listBox, GTK_WIDGET(child));
	}

	// Add matching options using the names the worker snapshotted under the clubs lock. The buffer
	// they came from may already have been republished, so nothing is re-read from it here.
	for (uint64_t i = 0; i < context->count; i++) {
		const ClubMatch *match = &context->matches[i];

		GtkWidget *label = gtk_label_new(match->shortName);
		g_object_set_data(G_OBJECT(label), "index", GINT_TO_POINTER(match->index));
		gtk_widget_set_halign(label, GTK_ALIGN_START);
		gtk_list_box_append(dataList->listBox, label);
	}

	// Show/hide popover based on matches
	if (context->count > 0) {
		gtk_popover_popup(dataList->popover);
	} else {
		gtk_popover_popdown(dataList->popover);
	}

	free(context);
	return G_SOURCE_REMOVE; // Don't repeat this callback
}

// This runs on the worker thread; CPU-intensive work only
static void runSearch(SearchContext *context) {
#ifdef DEBUG
	const int64_t timeStart = platform_getMicroseconds();
#endif

	const char *searchValue = context->searchValue;

	// Typing queues one worker per keystroke, so bail before taking the lock if a later keystroke
	// has already superseded this search. Purely an optimisation; updateUIWithResults re-checks.
	if (g_atomic_int_get(&searchGeneration) != context->generation) {
		free(context);
		return;
	}

	// The main thread frees and republishes the clubs, and this thread is detached, so the whole
	// scan is taken under the cache's lock rather than racing the swap.
	cache_lockClubs();

	// Collected as bare indices so the sort below shuffles 8 bytes per move rather than a whole
	// ClubMatch; the names are copied out once the order is settled.
	uint64_t indices[CLUB_SEARCH_LIMIT];
	uint64_t count = 0;

	// Add matching options
	for (uint64_t i = 0; i < gameContext.clubCount && count < CLUB_SEARCH_LIMIT; i++) {
		if (
			g_str_match_string(searchValue, gameContext.clubs[i].name, TRUE)
			|| g_str_match_string(searchValue, gameContext.clubs[i].shortName, TRUE)
		) {
			indices[count] = i;
			++count;
		}
	}

	for (uint64_t i = 1; i < count; ++i) {
		const uint64_t clubIndex = indices[i];
		const char *key = gameContext.clubs[clubIndex].shortName;
		int64_t j = (int64_t)i - 1;

		while (j >= 0 && strncmp(gameContext.clubs[indices[j]].shortName, key, CLUB_SHORT_NAME_LENGTH) > 0) {
			indices[j + 1] = indices[j];
			--j;
		}
		indices[j + 1] = clubIndex;
	}

	// Snapshot the names before dropping the lock: the main thread renders from these, so a
	// republish between here and updateUIWithResults can no longer blank out a valid result set.
	context->count = count;
	for (uint64_t i = 0; i < count; ++i) {
		ClubMatch *match = &context->matches[i];
		match->index = indices[i];
		memcpy(match->shortName, gameContext.clubs[indices[i]].shortName, CLUB_SHORT_NAME_LENGTH);
		match->shortName[CLUB_SHORT_NAME_LENGTH - 1] = '\0';
	}

	cache_unlockClubs();

	LOG_DEBUG(
		"Searched %" PRIu64 " clubs in %" PRId64 " microseconds",
		context->count,
		platform_getMicroseconds() - timeStart
	);

	// Schedule UI update on the main thread
	g_idle_add(updateUIWithResults, context);
}

static gpointer threadFunction(gpointer arg) {
	runSearch(arg);
	return NULL;
}

static void runThreadedSearch(SearchContext *context) {
	GError *error = NULL;
	GThread *thread = g_thread_try_new("club-search", threadFunction, context, &error);
	if (thread == NULL) {
		LOG_ERROR("Failed to create club-search thread: %s", error->message);
		g_clear_error(&error);
		free(context);
		return;
	}

	g_thread_unref(thread);
}
