// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "player-list.h"
#include "app/entities.h"
#include "app/player.h"
#include "app/ui.h"
#include "app/helpers/date.h"
#include "app/helpers/formatter.h"


/**
 * Opens a player list window that owns `state`, which is freed with the window. The state is also
 * the context's data, so the refresh paths in ui.c hand it back to the window's own renderer.
 */
WindowContext playerList_openWindow(
	const WindowType type,
	const char *title,
	void *state,
	const GDestroyNotify freeState
) {
	const WindowContext context = openWindow("player-list", "window:player-list", type, state);
	g_object_set_data_full(G_OBJECT(context.window), "player-list:state", state, freeState);
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(context.builder, "label:title")), title);

	return context;
}

void playerList_setSubtitle(const WindowContext context, const char *text) {
	gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(context.builder, "label:subtitle")), text);
}

GtkBox *playerList_getControls(const WindowContext context) {
	GtkWidget *controls = GTK_WIDGET(gtk_builder_get_object(context.builder, "box:controls"));
	gtk_widget_set_visible(controls, true);
	return GTK_BOX(controls);
}

/** Clears the grid and writes its header row. */
void playerGrid_begin(PlayerGrid *grid, const WindowContext context, const char *const *headings, const int32_t count) {
	grid->grid = GTK_GRID(gtk_builder_get_object(context.builder, "grid:players"));
	grid->row = 0;
	grid->columnCount = count;

	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(GTK_WIDGET(grid->grid))) != NULL) {
		gtk_grid_remove(grid->grid, child);
	}

	for (int32_t i = 0; i < count; ++i) {
		GtkWidget *label = gtk_label_new(headings[i]);
		gtk_label_set_xalign(GTK_LABEL(label), i == 0 ? 0.f : 0.5f);
		gtk_widget_add_css_class(label, "player-grid-heading");
		gtk_grid_attach(grid->grid, label, i, 0, 1, 1);
	}
	grid->row = 1;
}

/** A heading that spans the whole grid, e.g. the slot a group of upgrades is for. */
void playerGrid_appendGroup(PlayerGrid *grid, const char *markup) {
	GtkWidget *label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), markup);
	gtk_label_set_xalign(GTK_LABEL(label), 0.f);
	gtk_widget_add_css_class(label, "player-grid-group");
	gtk_grid_attach(grid->grid, label, 0, grid->row++, grid->columnCount, 1);
}

void playerGrid_appendMessage(PlayerGrid *grid, const char *text) {
	GtkWidget *label = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(label), 0.f);
	gtk_label_set_wrap(GTK_LABEL(label), true);
	gtk_widget_add_css_class(label, "player-grid-message");
	gtk_grid_attach(grid->grid, label, 0, grid->row++, grid->columnCount, 1);
}

void playerGrid_appendRow(PlayerGrid *grid, GtkWidget **cells, const int32_t count) {
	const bool isOdd = grid->row % 2 == 1;
	for (int32_t i = 0; i < count; ++i) {
		gtk_widget_add_css_class(cells[i], "player-grid-cell");
		if (isOdd) {
			gtk_widget_add_css_class(cells[i], "player-grid-cell--alternate");
		}
		gtk_grid_attach(grid->grid, cells[i], i, grid->row, 1, 1);
	}
	++grid->row;
}

static void onNameClicked(GtkGestureClick *gesture, const int clickCount, const double x, const double y, gpointer data) {
	(void)gesture;
	(void)x;
	(void)y;

	if (clickCount == 2) {
		ui_createPlayerInfoWindow(data);
	}
}

/**
 * The player's flag and name. Double-clicking opens Player Info, which copies the player, so the
 * pointer only has to live as long as this row: callers re-render their rows whenever the data
 * behind them is replaced.
 */
GtkWidget *playerGrid_nameCell(const Player *player) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

	const Nation *nation = entities_getNation(player->nationality[0]);
	if (nation != NULL) {
		char pathToFlag[256];
		snprintf(pathToFlag, sizeof(pathToFlag), RESOURCE_BASE "/assets/flags/%s.png", nation->code);
		GtkWidget *flag = gtk_image_new_from_resource(pathToFlag);
		gtk_widget_set_tooltip_text(flag, nation->name);
		gtk_box_append(GTK_BOX(box), flag);
	}

	char name[PLAYER_DISPLAY_NAME_LENGTH];
	player_getDisplayName(player, name, sizeof(name));
	GtkWidget *label = gtk_label_new(name);
	gtk_label_set_xalign(GTK_LABEL(label), 0.f);
	gtk_widget_set_hexpand(label, true);
	gtk_widget_add_css_class(label, "player-grid-name");
	gtk_widget_set_tooltip_text(label, "Double-click for player info");
	gtk_box_append(GTK_BOX(box), label);

	GtkGesture *gesture = gtk_gesture_click_new();
	g_signal_connect(gesture, "pressed", G_CALLBACK(onNameClicked), (gpointer)player);
	gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(gesture));

	return box;
}

GtkWidget *playerGrid_textCell(const char *markup, const float xalign) {
	GtkWidget *label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), markup);
	gtk_label_set_xalign(GTK_LABEL(label), xalign);
	return label;
}

GtkWidget *playerGrid_clubCell(const Player *player) {
	const Club *club = entities_getClub(player->clubIndex);
	GtkWidget *label = gtk_label_new(player->clubIndex == -1 ? "Free agent" : club != NULL ? club->shortName : "");
	gtk_label_set_xalign(GTK_LABEL(label), 0.f);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 22);
	return label;
}

GtkWidget *playerGrid_ratingCell(const float rating) {
	char buffer[FORMATTER_RATING_SIZE];
	formatter_formatRating(rating, buffer);
	return playerGrid_textCell(buffer, 1.f);
}

GtkWidget *playerGrid_valueCell(const uint32_t value) {
	char buffer[FORMATTER_MONEY_SIZE];
	formatter_formatMoneyShort(value, false, buffer);
	return playerGrid_textCell(buffer, 1.f);
}

GtkWidget *playerGrid_contractCell(const Player *player) {
	if (player->clubIndex == -1 || player->contractEnd.year == 0) {
		return playerGrid_textCell("", 0.f);
	}

	char date[DATE_FORMAT_SIZE];
	date_format(player->contractEnd, date, sizeof(date));
	GtkWidget *label = gtk_label_new(date);
	gtk_label_set_xalign(GTK_LABEL(label), 0.f);
	if (date_monthsUntil(player->contractEnd) <= 6) {
		gtk_widget_add_css_class(label, "contract-expiring");
	}
	return label;
}
