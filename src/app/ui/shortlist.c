// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/player.h"
#include "app/shortlist.h"
#include "app/ui.h"
#include "app/helpers/date.h"
#include "app/helpers/formatter.h"
#include "app/helpers/vector.h"
#include "app/ui/player-list.h"

#include <math.h>
#include <stdlib.h>


extern GameContext gameContext;


/**
 * The shortlist changes while the window is open, so rather than keep a fixed snapshot the window
 * rebuilds one from the live cache on every render. The rows point into it, and are always torn
 * down before it is replaced.
 */
typedef struct {
	PlayerSnapshot *players;
} ShortlistWindowState;

static void freeState(void *data) {
	ShortlistWindowState *state = data;
	playerSnapshot_free(state->players);
	g_free(state);
}

void ui_createShortlistWindow(void) {
	// One shortlist window is enough; bring the existing one forward instead of opening another
	for (uint64_t i = 0; i < vector_length(gameContext.windows); ++i) {
		if (gameContext.windows[i].type == WINDOW_SHORTLIST) {
			gtk_window_present(GTK_WINDOW(gameContext.windows[i].window));
			return;
		}
	}

	ShortlistWindowState *state = g_new0(ShortlistWindowState, 1);
	const WindowContext context = playerList_openWindow(WINDOW_SHORTLIST, "Shortlist", state, freeState);
	gtk_window_set_default_size(GTK_WINDOW(context.window), 960, 560);

	ui_renderShortlistWindow(context);
	ui_presentWindow(context);
}

/** "142 (+3)", coloured by whether the change is an improvement. */
static GtkWidget *createDeltaCell(const char *current, const double delta, const char *deltaText) {
	char text[64];
	char markup[FORMATTER_DELTA_SIZE + 64];
	if (delta == 0.0) {
		return playerGrid_textCell(current, 1.f);
	}

	snprintf(text, sizeof(text), "(%s)", deltaText);
	char colouredDelta[FORMATTER_DELTA_SIZE];
	formatter_formatDelta(delta, true, text, colouredDelta);
	snprintf(markup, sizeof(markup), "%s %s", current, colouredDelta);
	return playerGrid_textCell(markup, 1.f);
}

static GtkWidget *createAbilityCell(const uint8_t current, const uint8_t baseline) {
	char currentText[8];
	char deltaText[8];
	snprintf(currentText, sizeof(currentText), "%u", current);
	snprintf(deltaText, sizeof(deltaText), "%+d", (int)current - (int)baseline);
	return createDeltaCell(currentText, (double)current - (double)baseline, deltaText);
}

static void onResetClicked(GtkButton *button, gpointer data) {
	(void)button;
	shortlist_resetBaseline(data);
}

static void onRemoveClicked(GtkButton *button, gpointer data) {
	(void)button;
	shortlist_remove(GPOINTER_TO_UINT(data));
}

static GtkWidget *createActionsCell(const Player *player, const uint32_t uid) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_halign(box, GTK_ALIGN_END);

	if (player != NULL) {
		GtkWidget *reset = gtk_button_new_with_label("↺");
		gtk_widget_set_tooltip_text(reset, "Measure changes from today instead");
		gtk_widget_add_css_class(reset, "icon-btn");
		g_signal_connect(reset, "clicked", G_CALLBACK(onResetClicked), (gpointer)player);
		gtk_box_append(GTK_BOX(box), reset);
	}

	GtkWidget *remove = gtk_button_new_with_label("✕");
	gtk_widget_set_tooltip_text(remove, "Remove from shortlist");
	gtk_widget_add_css_class(remove, "icon-btn");
	g_signal_connect(remove, "clicked", G_CALLBACK(onRemoveClicked), GUINT_TO_POINTER(uid));
	gtk_box_append(GTK_BOX(box), remove);

	return box;
}

void ui_renderShortlistWindow(const WindowContext context) {
	ShortlistWindowState *state = context.data;

	static const char *const headings[] = {
		"Player", "Age", "Club", "CA", "PA", "Rating", "Value", "Contract ends", "Tracking since", "",
	};
	PlayerGrid grid;
	// Clearing the grid destroys the rows that point into the previous snapshot, so only now can it go
	playerGrid_begin(&grid, context, headings, G_N_ELEMENTS(headings));
	playerSnapshot_free(state->players);
	state->players = NULL;

	const uint64_t count = shortlist_count();
	char subtitle[160];
	snprintf(
		subtitle,
		sizeof(subtitle),
		"%" PRIu64 " shortlisted %s. Changes are measured from the date each player was added. Shortlist players from "
		"their Player Info window.",
		count,
		count == 1 ? "player" : "players"
	);
	playerList_setSubtitle(context, subtitle);

	if (count == 0) {
		playerGrid_appendMessage(&grid, "Nobody is shortlisted yet. Open a player and press ☆ Shortlist (or Ctrl+S).");
		return;
	}

	// Copy every shortlisted player we can find out of the cache, so the rows have stable pointers
	PlayerLookup lookup;
	playerLookup_build(&lookup);
	uint32_t *found = g_new(uint32_t, count);
	uint32_t foundCount = 0;
	for (uint64_t i = 0; i < count; ++i) {
		const Player *player = playerLookup_find(&lookup, shortlist_getEntry(i)->uid);
		if (player != NULL) {
			found[foundCount++] = (uint32_t)(player - gameContext.players);
		}
	}
	playerLookup_destroy(&lookup);
	state->players = playerSnapshot_create(found, foundCount);
	g_free(found);

	for (uint64_t i = 0; i < count; ++i) {
		const ShortlistEntry *entry = shortlist_getEntry(i);
		Player *player = NULL;
		for (uint32_t j = 0; state->players != NULL && j < state->players->count; ++j) {
			if (state->players->players[j].uid == entry->uid) {
				player = &state->players->players[j];
				break;
			}
		}

		char since[DATE_FORMAT_SIZE];
		date_format(entry->since, since, sizeof(since));

		if (player == NULL) {
			GtkWidget *name = playerGrid_textCell("", 0.f);
			gtk_label_set_text(GTK_LABEL(name), entry->name);
			GtkWidget *cells[] = {
				name,
				playerGrid_textCell("", 0.f),
				playerGrid_textCell("<i>Not in the loaded save</i>", 0.f),
				playerGrid_textCell("", 0.f),
				playerGrid_textCell("", 0.f),
				playerGrid_textCell("", 0.f),
				playerGrid_textCell("", 0.f),
				playerGrid_textCell("", 0.f),
				playerGrid_textCell(since, 0.f),
				createActionsCell(NULL, entry->uid),
			};
			playerGrid_appendRow(&grid, cells, G_N_ELEMENTS(cells));
			continue;
		}

		player->age = player_getAge(player->dateOfBirth);
		getSortedPositionRatings(player);

		char ageText[8];
		snprintf(ageText, sizeof(ageText), "%u", player->age);

		const float baselineRating = shortlist_getBaselineRating(entry, player);
		char rating[FORMATTER_RATING_PLAIN_SIZE];
		char ratingDelta[16];
		formatter_formatRatingPlain(player->ratings[0].value, rating);
		snprintf(ratingDelta, sizeof(ratingDelta), "%+.1f", player->ratings[0].value - baselineRating);
		// Ignore drift below the precision we display
		const double ratingChange = fabs(player->ratings[0].value - baselineRating) < 0.05
			? 0.0
			: player->ratings[0].value - baselineRating;

		char value[FORMATTER_MONEY_SIZE];
		char valueDelta[FORMATTER_MONEY_SIZE];
		formatter_formatMoneyShort(player->guideValue, false, value);
		formatter_formatMoneyShort((int64_t)player->guideValue - (int64_t)entry->value, true, valueDelta);

		GtkWidget *cells[] = {
			playerGrid_nameCell(player),
			playerGrid_textCell(ageText, 0.5f),
			playerGrid_clubCell(player),
			createAbilityCell(player->ca, entry->ca),
			createAbilityCell(player->pa, entry->pa),
			createDeltaCell(rating, ratingChange, ratingDelta),
			createDeltaCell(value, (double)player->guideValue - (double)entry->value, valueDelta),
			playerGrid_contractCell(player),
			playerGrid_textCell(since, 0.f),
			createActionsCell(player, entry->uid),
		};
		playerGrid_appendRow(&grid, cells, G_N_ELEMENTS(cells));
	}
}
