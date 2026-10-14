// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui.h"
#include "app/callbacks.h"
#include "app/cache.h"
#include "app/config.h"
#include "app/search-handler.h"
#include "app/helpers/attributes.h"
#include "app/helpers/date.h"
#include "app/helpers/formatter.h"
#include "app/helpers/vector.h"
#include "platform/platform.h"

#include <gtk/gtk.h>

#include "player-table.h"


extern ProcessContext processContext;
extern GameContext gameContext;

static gboolean onWindowClose(GtkWidget *widget, gpointer userData);
static void onFilterTagClick(GtkWidget *self, GtkEntryBuffer *buffer);
static void onEditableFilterTagClick(GtkWidget *self, GtkEditable *buffer);
static void onPositionFilterTagClick(GtkWidget *self, GtkCheckButton *button);
static void onDropDownFilterTagClick(GtkWidget *self, GtkDropDown *dropDown);

void ui_init(GtkApplication *app) {
	ui_loadStylesheet("styles.css", GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	ui_loadStylesheet(
		gameContext.options.darkMode ? "styles-dark.css" : "styles-light.css",
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1
	);

	// Keep GTK's own widget internals (scrollbars, text carets, etc.) in step with our palette
	g_object_set(
		gtk_settings_get_default(),
		"gtk-application-prefer-dark-theme",
		(gboolean)gameContext.options.darkMode,
		NULL
	);

	// Show main window
	const WindowContext context = openWindow("player-search", "window:player-search", WINDOW_PLAYER_SEARCH, NULL);
	gameContext.builder = context.builder;
	gtk_window_set_application(GTK_WINDOW(context.window), GTK_APPLICATION(app));

	// Write application version
	const GtkLabel *appVersionLabel = GTK_LABEL(gtk_builder_get_object(context.builder, "label:application-version"));
	char appVersionBuffer[64];
	snprintf(
		appVersionBuffer,
		sizeof(appVersionBuffer),
		"%s %d.%d.%d",
		APP_NAME,
		APP_VERSION_MAJOR,
		APP_VERSION_MINOR,
		APP_VERSION_PATCH
	);
	gtk_label_set_text(GTK_LABEL(appVersionLabel), appVersionBuffer);

#ifdef MOCKS_MODE
	// In mock mode, we never have the process-connected callback run
	cache_run();
#endif

	// Cache field buffers
	GtkBuilder *b = gameContext.builder;
	FilterBuffer *buf = &gameContext.filterBuffer;
	buf->minAge = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:age:min")));
	buf->maxAge = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:age:max")));
	buf->minCA = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:ca:min")));
	buf->maxCA = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:ca:max")));
	buf->minPA = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:pa:min")));
	buf->maxPA = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:pa:max")));
	buf->minRating = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:rating:min")));
	buf->maxRating = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:rating:max")));
	buf->minValue = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:value:min")));
	buf->maxValue = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:value:max")));
	buf->minHomeReputation = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:reputation:home:min")));
	buf->maxHomeReputation = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:reputation:home:max")));
	buf->minCurrentReputation = gtk_entry_get_buffer(
		GTK_ENTRY(gtk_builder_get_object(b, "entry:reputation:current:min"))
	);
	buf->maxCurrentReputation = gtk_entry_get_buffer(
		GTK_ENTRY(gtk_builder_get_object(b, "entry:reputation:current:max"))
	);
	buf->minWorldReputation = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:reputation:world:min")));
	buf->maxWorldReputation = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:reputation:world:max")));
	buf->maxWage = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:wage:max")));
	buf->contractMonths = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, "entry:contract:months")));

	// Every attribute dropdown offers the same list: a placeholder, then each attribute by name
	uint8_t sortedAttributes[ATTRIBUTE_COUNT];
	attributes_getSortedByName(sortedAttributes);
	FilterControls *controls = &gameContext.filterControls;
	for (uint8_t i = 0; i < ATTRIBUTE_FILTER_COUNT; ++i) {
		char id[48];
		snprintf(id, sizeof(id), "entry:attribute:%d:min", i);
		buf->minAttribute[i] = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, id)));
		snprintf(id, sizeof(id), "entry:attribute:%d:max", i);
		buf->maxAttribute[i] = gtk_entry_get_buffer(GTK_ENTRY(gtk_builder_get_object(b, id)));

		snprintf(id, sizeof(id), "string-list:attribute:%d", i);
		GtkStringList *list = GTK_STRING_LIST(gtk_builder_get_object(b, id));
		gtk_string_list_append(list, "Any attribute");
		for (uint8_t j = 0; j < ATTRIBUTE_COUNT; ++j) {
			gtk_string_list_append(list, attributes_getName(sortedAttributes[j]));
		}

		snprintf(id, sizeof(id), "dropdown:attribute:%d", i);
		controls->attribute[i] = GTK_DROP_DOWN(gtk_builder_get_object(b, id));
		gtk_drop_down_set_selected(controls->attribute[i], 0);
	}

	controls->name = GTK_SEARCH_ENTRY(gtk_builder_get_object(b, "entry:name"));
	controls->foot = GTK_DROP_DOWN(gtk_builder_get_object(b, "dropdown:foot"));
	controls->transferStatus = GTK_DROP_DOWN(gtk_builder_get_object(b, "dropdown:transfer-status"));
	controls->unattached = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:unattached"));
	controls->likelyToJoin = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:likely-to-join"));
	controls->fastLearner = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:fast-learner"));
	controls->hotProspect = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:hot-prospect"));

	// Cache check boxes
	CheckBox *check = &gameContext.checkboxes;
	check->positionGK = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:gk"));
	check->positionDL = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:dl"));
	check->positionDC = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:dc"));
	check->positionDR = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:dr"));
	check->positionWBL = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:wbl"));
	check->positionDM = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:dm"));
	check->positionWBR = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:wbr"));
	check->positionML = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:ml"));
	check->positionMC = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:mc"));
	check->positionMR = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:mr"));
	check->positionAML = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:aml"));
	check->positionAMC = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:amc"));
	check->positionAMR = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:amr"));
	check->positionST = GTK_CHECK_BUTTON(gtk_builder_get_object(b, "checkbox:position:st"));

	ui_presentWindow(context);
}

void ui_update(void) {
	ui_updateInGameDate();
	ui_updateGameVersion();
}

void ui_updateInGameDate(void) {
	GtkLabel *dateLabel = GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(gameContext.builder, "label:date")));

#ifndef MOCKS_MODE
	if (processContext.handle == NULL || !gameContext.gameKey || gameContext.currentDate.year == 1900) {
		gtk_label_set_text(dateLabel, "");
		return;
	}
#endif

	DayMonthYearTime d = date_prettify(gameContext.currentDate);
	char buffer[64] = {0};
	snprintf(
		buffer,
		64,
		"📅 %s %d%s, %d %s",
		d.month,
		d.day,
		date_getOrdinal(d.day),
		d.year,
		d.timeString
	);
	gtk_label_set_text(dateLabel, buffer);
}

void ui_updateGameVersion(void) {
	GtkLabel *versionLabel = GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(gameContext.builder, "label:version")));
	if (gameContext.gameVersion[0] == '\0') {
		gtk_label_set_text(versionLabel, "");
		return;
	}

	char buffer[64] = {0};
	snprintf(buffer, 64, "📝 %s", gameContext.gameVersion);
	gtk_label_set_text(versionLabel, buffer);
}

WindowContext openWindow(const char *layoutName, const char *windowName, const WindowType type, void *data) {
	char pathToAppLayout[256] = {0};
	snprintf(pathToAppLayout, sizeof(pathToAppLayout), RESOURCE_BASE "/layouts/%s.ui", layoutName);

	// `data` is set before the push because the vector stores a copy: anything assigned to the
	// caller's context afterwards would never reach the copy the refresh paths walk.
	WindowContext context = {.type = type, .data = data};
	context.builder = gtk_builder_new_from_resource(pathToAppLayout);
	context.window = GTK_WIDGET(gtk_builder_get_object(context.builder, windowName));
	g_signal_connect(context.window, "close_request", G_CALLBACK(onWindowClose), NULL);

	GtkEventControllerKey *closeController = GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(closeController), GTK_PHASE_CAPTURE);
	g_signal_connect(closeController, "key-pressed", G_CALLBACK(callbacks_onWindowKeypress), context.window);
	gtk_widget_add_controller(context.window, GTK_EVENT_CONTROLLER(closeController));

	vector_push(gameContext.windows, context);

	return context;
}

/**
 * Presenting sizes the window to the content it has at that moment, and GTK will grow a realised
 * window but never shrink it. Callers therefore populate first and present last, otherwise widgets
 * they go on to hide leave the window permanently taller than it needs to be.
 */
void ui_presentWindow(const WindowContext context) {
	gtk_window_present(GTK_WINDOW(context.window));
}

static gboolean onWindowClose(GtkWidget *widget, gpointer userData) {
	for (uint64_t i = 0; i < vector_length(gameContext.windows); i++) {
		if (gameContext.windows[i].window == widget) {
			WindowContext out;
			vector_splice(gameContext.windows, i, &out);
			g_object_unref(out.builder);
			break;
		}
	}

	(void)userData;
	return GDK_EVENT_PROPAGATE;
}

void ui_refreshAllWindows(void) {
	for (uint64_t i = 0; i < vector_length(gameContext.windows); i++) {
		const WindowContext context = gameContext.windows[i];
		if (context.type == WINDOW_BEST_XI) {
			ui_renderBestElevenWindow(context);
		} else if (context.type == WINDOW_PLAYER_INFO) {
			ui_renderPlayerInfoWindow(context);
		} else if (context.type == WINDOW_SQUAD_DEPTH) {
			ui_renderSquadDepthWindow(context);
		} else if (context.type == WINDOW_SHORTLIST) {
			ui_renderShortlistWindow(context);
		} else if (context.type == WINDOW_SIMILAR_PLAYERS) {
			ui_renderSimilarPlayersWindow(context);
		} else if (context.type == WINDOW_UPGRADE_FINDER) {
			ui_renderUpgradeFinderWindow(context);
		} else if (context.type == WINDOW_SETTINGS) {
			ui_renderSettingsWindow(context);
		} else if (
			gameContext.windows[i].type == WINDOW_PLAYER_SEARCH &&
			(gameContext.filterOptions.filterMask || gameContext.searchResults->data != NULL)
		) {
			searchHandler_doSearch(false);
		}
	}
}

static bool hasPlayerWindow(void) {
	for (uint64_t i = 0; i < vector_length(gameContext.windows); i++) {
		if (gameContext.windows[i].type != WINDOW_PLAYER_SEARCH) {
			return true;
		}
	}

	return false;
}

// `lookup` is NULL when the players themselves have not changed and only need drawing again.
static void renderPlayerWindows(const PlayerLookup *lookup) {
	for (uint64_t i = 0; i < vector_length(gameContext.windows); i++) {
		const WindowContext context = gameContext.windows[i];
		switch (context.type) {
			case WINDOW_BEST_XI:
				ui_rebindBestElevenWindow(context, lookup);
				break;
			case WINDOW_PLAYER_INFO:
				ui_rebindPlayerInfoWindow(context, lookup);
				break;
			case WINDOW_SQUAD_DEPTH:
				ui_rebindSquadDepthWindow(context, lookup);
				break;
			case WINDOW_UPGRADE_FINDER:
				ui_rebindUpgradeFinderWindow(context, lookup);
				break;
			// These two look their players up in the cache afresh whenever they render
			case WINDOW_SHORTLIST:
				ui_renderShortlistWindow(context);
				break;
			case WINDOW_SIMILAR_PLAYERS:
				ui_renderSimilarPlayersWindow(context);
				break;
			// Only the club label: re-rendering the editors would lose unsaved changes
			case WINDOW_SETTINGS:
				ui_rerenderSettingsClub(context);
				break;
			default:
				break;
		}
	}
}

/** Redraws everything that shows whether a player is shortlisted, after the shortlist changes. */
void ui_refreshShortlistViews(void) {
	for (uint64_t i = 0; i < vector_length(gameContext.windows); i++) {
		const WindowContext context = gameContext.windows[i];
		if (context.type == WINDOW_SHORTLIST) {
			ui_renderShortlistWindow(context);
		} else if (context.type == WINDOW_PLAYER_INFO) {
			ui_renderPlayerInfoWindow(context);
		}
	}
}

/** Brings the search window forward, e.g. after a search was started from another window. */
void ui_presentSearchWindow(void) {
	for (uint64_t i = 0; i < vector_length(gameContext.windows); i++) {
		if (gameContext.windows[i].type == WINDOW_PLAYER_SEARCH) {
			gtk_window_present(GTK_WINDOW(gameContext.windows[i].window));
			return;
		}
	}
}

/**
 * Called when the player cache is replaced. Both window types own a snapshot of the players they
 * were opened with, so each one is re-matched to the new buffer by uid — anyone still in the cache
 * picks up their latest data, anyone who has gone keeps the data we last saw — and then re-rendered.
 * The uid lookup costs a pass over the cache, so it is built once and shared.
 */
void ui_rebindPlayerWindows(void) {
	if (!hasPlayerWindow()) {
		return;
	}

	// A failed build leaves an empty lookup, which refreshing treats as "nothing to match against".
	PlayerLookup lookup;
	playerLookup_build(&lookup);
	renderPlayerWindows(&lookup);
	playerLookup_destroy(&lookup);
}

/**
 * Clubs and nations are published on their own, after the players that reference them, so the
 * windows are rendered again to pick up names that were not yet cached the first time round.
 */
void ui_rerenderPlayerWindows(void) {
	if (hasPlayerWindow()) {
		renderPlayerWindows(NULL);
	}
}

void ui_setCurrentStatus(const char *status) {
	GtkLabel *statusLabel = GTK_LABEL(gtk_builder_get_object(gameContext.builder, "label:application-status"));
	gtk_label_set_text(statusLabel, status);
}

typedef struct {
	GtkWidget *box;
	GtkWidget *label;
	GtkWidget *closeButton;
} FilterTag;

static FilterTag createFilterTag(const char *text, const bool isClosable) {
	GtkWidget *box = gtk_box_new(0, 4);
	gtk_widget_add_css_class(box, "chip");

	GtkWidget *label = gtk_label_new(text);
	gtk_widget_add_css_class(label, "chip-text");
	gtk_box_append(GTK_BOX(box), label);

	GtkWidget *closeButton = NULL;
	if (isClosable) {
		GtkWidget *close = gtk_label_new("✕");
		gtk_widget_add_css_class(close, "chip-x");
		closeButton = gtk_button_new();
		gtk_widget_add_css_class(closeButton, "chip-button");
		gtk_widget_set_parent(close, closeButton);
		gtk_box_append(GTK_BOX(box), closeButton);
	}

	GtkBox *parent = GTK_BOX(gtk_builder_get_object(gameContext.builder, "box:filter-tags"));
	gtk_box_append(parent, box);

	return (FilterTag){.box = box, .label = label, .closeButton = closeButton};
}

void ui_createFilterTag(const char *text, GtkEntryBuffer *buffer) {
	const FilterTag tag = createFilterTag(text, buffer != NULL);
	if (buffer != NULL) {
		g_signal_connect(tag.closeButton, "clicked", G_CALLBACK(onFilterTagClick), buffer);
	}
}

void ui_createClubFilterTag(const char *name, GtkEditable *buffer) {
	char textBuffer[CLUB_SHORT_NAME_LENGTH + 7] = {0};
	snprintf(textBuffer, sizeof(textBuffer), "Club: %s", name);
	const FilterTag tag = createFilterTag(textBuffer, true);
	g_signal_connect(tag.closeButton, "clicked", G_CALLBACK(onEditableFilterTagClick), buffer);
	gtk_widget_set_name(tag.label, "tag:club-name");
}

void ui_createNationalityFilterTag(const char *name, GtkEditable *buffer) {
	char textBuffer[MAX_NATION_STRING_LENGTH + 15];
	snprintf(textBuffer, sizeof(textBuffer), "Nationality: %s", name);
	const FilterTag tag = createFilterTag(textBuffer, true);
	g_signal_connect(tag.closeButton, "clicked", G_CALLBACK(onEditableFilterTagClick), buffer);
	gtk_widget_set_name(tag.label, "tag:nationality");
}

void ui_createPositionFilterTag(const char *name, GtkCheckButton *button) {
	char textBuffer[16] = {0};
	snprintf(textBuffer, sizeof(textBuffer), "Pos: %s", name);
	ui_createCheckFilterTag(textBuffer, button);
}

void ui_createCheckFilterTag(const char *text, GtkCheckButton *button) {
	const FilterTag tag = createFilterTag(text, true);
	g_signal_connect(tag.closeButton, "clicked", G_CALLBACK(onPositionFilterTagClick), button);
}

void ui_createEditableFilterTag(const char *text, GtkEditable *editable) {
	const FilterTag tag = createFilterTag(text, true);
	g_signal_connect(tag.closeButton, "clicked", G_CALLBACK(onEditableFilterTagClick), editable);
}

void ui_createDropDownFilterTag(const char *text, GtkDropDown *dropDown) {
	const FilterTag tag = createFilterTag(text, true);
	g_signal_connect(tag.closeButton, "clicked", G_CALLBACK(onDropDownFilterTagClick), dropDown);
}

void ui_clearFilterTags(void) {
	GtkBox *filterTags = GTK_BOX(gtk_builder_get_object(gameContext.builder, "box:filter-tags"));
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(GTK_WIDGET(filterTags))) != NULL) {
		gtk_box_remove(filterTags, GTK_WIDGET(child));
	}
}

static void onTagClick(GtkWidget *self) {
	GtkWidget *box = gtk_widget_get_parent(self);
	GtkWidget *parent = gtk_widget_get_parent(box);
	GtkWidget *closeLabel = gtk_widget_get_first_child(self);
	if (closeLabel != NULL) {
		gtk_widget_unparent(closeLabel);
	}

	gtk_box_remove(GTK_BOX(parent), box);

	searchHandler_cacheFilters();
	if (gameContext.filterOptions.filterMask) {
		searchHandler_doSearch(false);
	} else {
		playerTable_clear();
	}
}

static void onEditableFilterTagClick(GtkWidget *self, GtkEditable *buffer) {
	gtk_editable_set_text(buffer, "");
	onTagClick(self);
}

static void onFilterTagClick(GtkWidget *self, GtkEntryBuffer *buffer) {
	gtk_entry_buffer_set_text(buffer, "", 1);
	onTagClick(self);
}

/*
 * Unsetting the button emits "toggled", which re-runs the search and rebuilds every tag —
 * including this one — so `self` must not be touched afterwards.
 */
static void onPositionFilterTagClick(GtkWidget *self, GtkCheckButton *button) {
	(void)self;
	gtk_check_button_set_active(button, FALSE);
}

// As above: resetting the dropdown re-runs the search, which rebuilds every tag.
static void onDropDownFilterTagClick(GtkWidget *self, GtkDropDown *dropDown) {
	(void)self;
	gtk_drop_down_set_selected(dropDown, 0);
}


void ui_loadStylesheet(const char *fileName, const guint priority) {
	char pathToStylesheet[256] = {0};
	GtkCssProvider *provider = gtk_css_provider_new();

	snprintf(pathToStylesheet, sizeof(pathToStylesheet), RESOURCE_BASE "/layouts/%s", fileName);
	gtk_css_provider_load_from_resource(provider, pathToStylesheet);
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(provider),
		priority
	);
	g_object_unref(provider);
}
