// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/entities.h"
#include "app/injury-names.h"
#include "app/maths.h"
#include "app/player.h"
#include "app/ui.h"
#include "app/helpers/formatter.h"
#include "app/helpers/icons.h"
#include "app/helpers/ratings.h"
#include "app/helpers/vector.h"
#include "core/logger.h"
#include "platform/platform.h"
#include "types/position-code-names.h"


typedef struct {
	uint16_t minCondition;
	uint16_t maxCondition;
	uint8_t minAge;
	uint8_t maxAge;
	bool excludeInjured;
} BestElevenFilters;

#define INFEASIBLE_COST 1e9f

static float getPlayerPositionalRating(const Player *player, PositionCode position);
static void hungarian(float **matrix, uint32_t n, uint32_t *outAssignment);
static void assignFormation(
	const Player *players,
	uint32_t playerCount,
	const PositionCode positions[FORMATION_POSITION_COUNT],
	BestElevenFilters filters,
	const bool *excluded,
	BestElevenRow outSlots[FORMATION_POSITION_COUNT]
);
static void renderBestElevenTable(WindowContext context);
static void onFormationSelected(GObject *object, GParamSpec *pspec, gpointer userData);
static void onPlayerNameClicked(
	const GtkGestureClick *gesture,
	int clickCount,
	double x,
	double y,
	const Player *player
);
static void onFilterChange(GObject *object, gpointer userData);
static void onFindUpgradesClicked(GtkButton *button, gpointer userData);

void ui_createBestElevenWindow(void) {
	// The window works from its own copy of the results: the indices point into a buffer the cache
	// is free to replace, and the players themselves are re-matched by uid after every publish.
	const SharedPointer *results = gameContext.searchResults;
	const uint32_t *playerIds = results != NULL ? results->data : NULL;
	PlayerSnapshot *snapshot = playerSnapshot_create(playerIds, playerIds != NULL ? vector_length(playerIds) : 0);
	if (snapshot == NULL) {
		return;
	}

	const WindowContext context = openWindow("best-xi", "window:best-xi", WINDOW_BEST_XI, snapshot);
	g_object_set_data_full(G_OBJECT(context.window), "best-xi:players", snapshot, playerSnapshot_free);

	WindowContext *cbContext = g_new(WindowContext, 1);
	*cbContext = context; // copy the two pointers
	g_object_set_data_full(G_OBJECT(context.window), "best-xi:context", cbContext, g_free);

	GtkDropDown *formationDropDown = GTK_DROP_DOWN(gtk_builder_get_object(context.builder, "dropdown:formation"));

	GtkSpinButton *spinMinAge = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:min-age"));
	GtkSpinButton *spinMaxAge = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:max-age"));
	GtkSpinButton *spinMinCondition = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:min-condition"));
	GtkSpinButton *spinMaxCondition = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:max-condition"));
	GtkWidget *checkboxExcludeInjured = GTK_WIDGET(
		gtk_builder_get_object(context.builder, "checkbox:best-xi:exclude-injured")
	);
	g_signal_connect(formationDropDown, "notify::selected", G_CALLBACK(onFormationSelected), cbContext);
	g_signal_connect(spinMinAge, "value-changed", G_CALLBACK(onFilterChange), cbContext);
	g_signal_connect(spinMaxAge, "value-changed", G_CALLBACK(onFilterChange), cbContext);
	g_signal_connect(spinMinCondition, "value-changed", G_CALLBACK(onFilterChange), cbContext);
	g_signal_connect(spinMaxCondition, "value-changed", G_CALLBACK(onFilterChange), cbContext);
	g_signal_connect(checkboxExcludeInjured, "toggled", G_CALLBACK(onFilterChange), cbContext);
	GtkWidget *checkboxShowBench = GTK_WIDGET(gtk_builder_get_object(context.builder, "checkbox:best-xi:show-bench"));
	g_signal_connect(checkboxShowBench, "toggled", G_CALLBACK(onFilterChange), cbContext);
	GtkWidget *buttonFindUpgrades = GTK_WIDGET(gtk_builder_get_object(context.builder, "button:find-upgrades"));
	g_signal_connect(buttonFindUpgrades, "clicked", G_CALLBACK(onFindUpgradesClicked), cbContext);

	ui_renderBestElevenWindow(context);
	ui_presentWindow(context);
}

void ui_renderBestElevenWindow(const WindowContext context) {
	GtkStringList *formationList = GTK_STRING_LIST(gtk_builder_get_object(context.builder, "string-list:formation"));
	while (gtk_string_list_get_string(formationList, 0) != NULL) {
		gtk_string_list_remove(formationList, 0);
	}
	for (uint64_t i = 0; i < vector_length(gameContext.options.formations); ++i) {
		gtk_string_list_append(formationList, gameContext.options.formations[i].name);
	}

	const PlayerSnapshot *snapshot = context.data;
	Player *players = snapshot != NULL ? snapshot->players : NULL;
	const uint32_t playerCount = snapshot != NULL ? snapshot->count : 0;
	for (uint32_t i = 0; i < playerCount; ++i) {
		getSortedPositionRatings(&players[i]);
	}

	renderBestElevenTable(context);
}

/**
 * The snapshot's players are re-matched to the new cache by uid, so the window keeps showing the
 * same eleven with their latest data. Re-rendering also rebuilds the rows, which is what keeps the
 * click gestures pointing at live entries.
 */
void ui_rebindBestElevenWindow(const WindowContext context, const PlayerLookup *lookup) {
	playerSnapshot_refresh(context.data, lookup);
	renderBestElevenTable(context);
}

static const Formation *getSelectedFormation(const WindowContext context) {
	GtkDropDown *formationDropDown = GTK_DROP_DOWN(gtk_builder_get_object(context.builder, "dropdown:formation"));
	uint32_t selectedFormationIndex = gtk_drop_down_get_selected(formationDropDown);
	if (selectedFormationIndex >= vector_length(gameContext.options.formations)) {
		selectedFormationIndex = 0;
	}
	return &gameContext.options.formations[selectedFormationIndex];
}

static BestElevenFilters getFilters(const WindowContext context) {
	GtkSpinButton *spinMinAge = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:min-age"));
	GtkSpinButton *spinMaxAge = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:max-age"));
	GtkSpinButton *spinMinCondition = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:min-condition"));
	GtkSpinButton *spinMaxCondition = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:max-condition"));
	GtkCheckButton *checkboxExcludeInjured = GTK_CHECK_BUTTON(
		gtk_builder_get_object(context.builder, "checkbox:best-xi:exclude-injured")
	);

	return (BestElevenFilters){
		.excludeInjured = gtk_check_button_get_active(checkboxExcludeInjured),
		.maxAge = (uint8_t)gtk_spin_button_get_value_as_int(spinMaxAge),
		.minAge = (uint8_t)gtk_spin_button_get_value_as_int(spinMinAge),
		// Convert percentages to 0-10000 scale
		.minCondition = (uint16_t)gtk_spin_button_get_value_as_int(spinMinCondition) * 100,
		.maxCondition = (uint16_t)gtk_spin_button_get_value_as_int(spinMaxCondition) * 100,
	};
}

static void appendLabelRow(GtkListBox *listBox, const char *markup, const char *cssClass, const GtkAlign halign) {
	GtkWidget *label = gtk_label_new("");
	gtk_label_set_markup(GTK_LABEL(label), markup);
	gtk_widget_set_halign(label, halign);
	GtkWidget *widgetRow = gtk_list_box_row_new();
	GtkListBoxRow *row = GTK_LIST_BOX_ROW(widgetRow);
	gtk_widget_add_css_class(widgetRow, cssClass);
	gtk_widget_set_hexpand(widgetRow, true);
	gtk_list_box_row_set_activatable(row, false);
	gtk_list_box_row_set_selectable(row, false);
	gtk_list_box_row_set_child(row, label);
	gtk_list_box_append(listBox, widgetRow);
}

/** Appends one slot of a line-up and returns the rating it contributes, or 0 when it is empty. */
static float appendPlayerRow(GtkListBox *listBox, const PositionCode position, const Player *player) {
	GtkWidget *widgetRow = gtk_list_box_row_new();
	GtkListBoxRow *row = GTK_LIST_BOX_ROW(widgetRow);
	gtk_list_box_row_set_activatable(row, false);
	gtk_list_box_row_set_selectable(row, false);
	gtk_list_box_append(listBox, widgetRow);

	GtkWidget *widgetGrid = gtk_grid_new();
	GtkGrid *grid = GTK_GRID(widgetGrid);
	gtk_grid_set_column_spacing(grid, 12);

	GtkWidget *widgetLabelPosition = gtk_label_new(positionCodeNames[position]);
	gtk_label_set_yalign(GTK_LABEL(widgetLabelPosition), GTK_ALIGN_CENTER);
	gtk_widget_add_css_class(widgetLabelPosition, "position");

	GtkWidget *widgetBoxNationality = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *widgetLabelPlayer = gtk_label_new("");
	GtkWidget *widgetLabelAge = gtk_label_new("");
	GtkWidget *widgetHeart;
	GtkWidget *widgetLabelRating = gtk_label_new("");

	if (player != NULL) {
		gtk_widget_set_hexpand(widgetLabelPlayer, true);
		gtk_label_set_xalign(GTK_LABEL(widgetLabelPlayer), 0);
		gtk_label_set_yalign(GTK_LABEL(widgetLabelPlayer), GTK_ALIGN_CENTER);
		gtk_widget_add_css_class(widgetLabelPlayer, "name");

		// Name
		char nameBuffer[PLAYER_DISPLAY_NAME_LENGTH];
		player_getDisplayName(player, nameBuffer, sizeof(nameBuffer));
		gtk_label_set_text(GTK_LABEL(widgetLabelPlayer), nameBuffer);

		// Age
		char ageBuffer[8];
		snprintf(ageBuffer, sizeof(ageBuffer), "%d yrs", player->age);
		gtk_label_set_text(GTK_LABEL(widgetLabelAge), ageBuffer);

		// Rating
		char ratingBuffer[FORMATTER_RATING_SIZE];
		formatter_formatRating(player->ratings[0].value, ratingBuffer);
		gtk_label_set_markup(GTK_LABEL(widgetLabelRating), ratingBuffer);
		gtk_label_set_xalign(GTK_LABEL(widgetLabelRating), 1.f);

		// Country flag
		const Nation *nation = entities_getNation(player->nationality[0]);
		if (nation != NULL) {
			char pathToFlag[256] = {0};
			snprintf(
				pathToFlag,
				sizeof(pathToFlag),
				RESOURCE_BASE "/assets/flags/%s.png",
				nation->code
			);
			GtkWidget *flagImage = gtk_image_new_from_resource(pathToFlag);
			gtk_box_append(GTK_BOX(widgetBoxNationality), flagImage);
			gtk_widget_set_tooltip_text(flagImage, nation->name);
		}

		if (player->injury.duration > 0) {
			widgetHeart = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			GtkWidget *label = gtk_label_new("🚑");
			gtk_box_append(GTK_BOX(widgetHeart), label);
			char buffer[128] = {0};
			snprintf(buffer, 128, "Injured: %s", injuryNames_get(player->injury.nameIndex));
			gtk_widget_set_tooltip_text(label, buffer);
		} else {
			widgetHeart = icons_new(ICON_HEART, icons_conditionQuantise(player->condition));
		}

		GtkGesture *gesture = gtk_gesture_click_new();
		gtk_widget_add_controller(widgetLabelPlayer, GTK_EVENT_CONTROLLER(gesture));
		g_signal_connect(gesture, "pressed", G_CALLBACK(onPlayerNameClicked), (gpointer)player);
	} else {
		widgetHeart = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	}

	uint8_t c = 1;
	gtk_grid_attach(grid, widgetLabelPosition, c++, 0, 1, 1);
	gtk_grid_attach(grid, widgetBoxNationality, c++, 0, 1, 1);
	gtk_grid_attach(grid, widgetLabelPlayer, c++, 0, 1, 1);
	gtk_grid_attach(grid, widgetLabelAge, c++, 0, 1, 1);
	gtk_grid_attach(grid, widgetHeart, c++, 0, 1, 1);
	gtk_grid_attach(grid, widgetLabelRating, c++, 0, 1, 1);
	gtk_list_box_row_set_child(row, widgetGrid);

	return player != NULL ? player->ratings[0].value : 0.f;
}

/** Appends a line-up, followed by its average rating when anyone made it in. */
static void appendLineUp(GtkListBox *listBox, const Formation *formation, const BestElevenRow rows[FORMATION_POSITION_COUNT]) {
	uint8_t playerIncludedCount = 0;
	float ratingTotal = 0;
	for (uint8_t i = 0; i < FORMATION_POSITION_COUNT; ++i) {
		ratingTotal += appendPlayerRow(listBox, formation->positions[i], rows[i].player);
		if (rows[i].player != NULL) {
			++playerIncludedCount;
		}
	}

	if (playerIncludedCount > 0) {
		char ratingBuffer[FORMATTER_RATING_SIZE];
		const float averageRating = ratingTotal / (float)playerIncludedCount;
		formatter_formatRating(averageRating, ratingBuffer);

		char averageRatingBuffer[128];
		snprintf(averageRatingBuffer, sizeof(averageRatingBuffer), "<span weight=\"800\">Average:</span> %s", ratingBuffer);
		appendLabelRow(listBox, averageRatingBuffer, "average-rating", GTK_ALIGN_END);
	}
}

/**
 * The bench is the best line-up that can be made from everyone the first eleven left out, so each
 * slot shows the next-best option for that position.
 */
static void computeBench(
	const Player *players,
	const uint32_t playerCount,
	const Formation *formation,
	const BestElevenFilters filters,
	const BestElevenRow firstEleven[FORMATION_POSITION_COUNT],
	BestElevenRow outBench[FORMATION_POSITION_COUNT]
) {
	bool *excluded = playerCount > 0 ? calloc(playerCount, sizeof(bool)) : NULL;
	for (uint8_t i = 0; i < FORMATION_POSITION_COUNT && excluded != NULL; ++i) {
		if (firstEleven[i].player != NULL) {
			excluded[firstEleven[i].player - players] = true;
		}
	}

	assignFormation(players, playerCount, formation->positions, filters, excluded, outBench);
	free(excluded);
}

static void renderBestElevenTable(const WindowContext context) {
	const Formation *formation = getSelectedFormation(context);
	const PlayerSnapshot *snapshot = context.data;

	GtkListBox *listBox = GTK_LIST_BOX(gtk_builder_get_object(context.builder, "list-box:best-xi"));
	gtk_list_box_remove_all(listBox);

	const Player *players = snapshot != NULL ? snapshot->players : NULL;
	const uint32_t playerCount = snapshot != NULL ? snapshot->count : 0;
	const BestElevenFilters filters = getFilters(context);

	BestElevenRow rows[FORMATION_POSITION_COUNT] = {0};
#ifdef DEBUG
	const int64_t timeStart = platform_getMicroseconds();
#endif
	assignFormation(players, playerCount, formation->positions, filters, NULL, rows);
	LOG_DEBUG(
		"Found best XI for %" PRIu32 " players in %" PRId64 " microseconds",
		playerCount,
		platform_getMicroseconds() - timeStart
	);

	appendLineUp(listBox, formation, rows);

	GtkCheckButton *checkboxShowBench = GTK_CHECK_BUTTON(
		gtk_builder_get_object(context.builder, "checkbox:best-xi:show-bench")
	);
	if (gtk_check_button_get_active(checkboxShowBench)) {
		BestElevenRow bench[FORMATION_POSITION_COUNT] = {0};
		computeBench(players, playerCount, formation, filters, rows, bench);
		appendLabelRow(listBox, "<span weight=\"800\">Bench</span>", "bench-heading", GTK_ALIGN_START);
		appendLineUp(listBox, formation, bench);
	}
}

static void onFindUpgradesClicked(GtkButton *button, gpointer userData) {
	(void)button;
	const WindowContext context = *(const WindowContext*)userData;
	const Formation *formation = getSelectedFormation(context);
	const PlayerSnapshot *snapshot = context.data;
	const Player *players = snapshot != NULL ? snapshot->players : NULL;
	const uint32_t playerCount = snapshot != NULL ? snapshot->count : 0;

	BestElevenRow rows[FORMATION_POSITION_COUNT] = {0};
	assignFormation(players, playerCount, formation->positions, getFilters(context), NULL, rows);

	GtkSpinButton *spinImprovement = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:upgrade-improvement"));
	GtkSpinButton *spinBudget = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:upgrade-budget"));
	GtkSpinButton *spinMaxAge = GTK_SPIN_BUTTON(gtk_builder_get_object(context.builder, "spin:upgrade-max-age"));
	ui_createUpgradeFinderWindow(
		formation->positions,
		rows,
		snapshot,
		(float)gtk_spin_button_get_value(spinImprovement),
		(uint32_t)(gtk_spin_button_get_value(spinBudget) * 1e6),
		(uint8_t)gtk_spin_button_get_value_as_int(spinMaxAge)
	);
}

static void onFormationSelected(GObject *object, GParamSpec *pspec, gpointer userData) {
	(void)object;
	(void)pspec;
	renderBestElevenTable(*(const WindowContext*)userData);
}

static void onFilterChange(GObject *object, gpointer userData) {
	(void)object;
	renderBestElevenTable(*(const WindowContext*)userData);
}

/**
 * O(n³) Hungarian algorithm (shortest-augmenting-path / Jonker–Volgenant variant).
 * Solves the square minimisation assignment problem.
 * @param matrix n×n cost matrix (1-indexed internally)
 * @param n size of matrix
 * @param outAssignment array of indices
 * @returns outAssignment[row] = col (both 0-indexed)
 */
static void hungarian(float **matrix, const uint32_t n, uint32_t *outAssignment) {
	// u[i] = potential for row i (1-indexed), v[j] = potential for col j (1-indexed)
	float *u = calloc(n + 1, sizeof(float));
	float *v = calloc(n + 1, sizeof(float));
	// p[j] = row assigned to column j (1-indexed); p[0] is a sentinel
	uint32_t *p = calloc(n + 1, sizeof(uint32_t));
	int64_t *way = calloc(n + 1, sizeof(int64_t));
	float *minDist = malloc((n + 1) * sizeof(float));
	bool *used = malloc((n + 1) * sizeof(bool));

	for (uint32_t i = 0; i < n; i++) {
		outAssignment[i] = n;
	}

	for (uint32_t i = 0; i < n; i++) {
		p[0] = i + 1;
		int64_t j0 = 0;

		for (uint32_t j = 0; j <= n; ++j) {
			minDist[j] = INFINITY;
		}
		memset(used, 0, (size_t)(n + 1) * sizeof(bool));

		do {
			used[j0] = true;
			const uint32_t i0 = p[j0];
			float delta = INFINITY;
			int64_t j1 = -1;

			for (uint32_t j = 1; j <= n; j++) {
				if (!used[j]) {
					const float cur = matrix[i0 - 1][j - 1] - u[i0] - v[j];
					if (cur < minDist[j]) {
						minDist[j] = cur;
						way[j] = j0;
					}
					if (minDist[j] < delta) {
						delta = minDist[j];
						j1 = (int64_t)j;
					}
				}
			}

			if (j1 < 0) {
				LOG_ERROR("Assignment solver found no reachable column; leaving row %" PRIu32 " unassigned", i);
				j0 = -1;
				break;
			}

			for (uint32_t j = 0; j <= n; j++) {
				if (used[j]) {
					u[p[j]] += delta;
					v[j] -= delta;
				} else {
					minDist[j] -= delta;
				}
			}

			j0 = j1;
		} while (p[j0] != 0);

		if (j0 < 0) {
			continue;
		}

		do {
			const int64_t j1 = way[j0];
			p[j0] = p[j1];
			j0 = j1;
		} while (j0);
	}

	free(used);
	free(minDist);

	// Invert p: assignment[row] = col (0-indexed)
	for (uint32_t j = 1; j <= n; j++) {
		if (p[j] != 0) {
			outAssignment[p[j] - 1] = j - 1;
		}
	}

	free(way);
	free(p);
	free(v);
	free(u);
}

/**
 * Returns a player's rating for the grouped role that owns `position`, or
 * INFEASIBLE_COST if the player is not sufficiently proficient in `position`
 * or has no rating for the corresponding role. Higher is better.
 */
static float getPlayerPositionalRating(const Player *player, const PositionCode position) {
	if (player->positions[position] < MINIMUM_POSITIONAL_PROFICIENCY) {
		return INFEASIBLE_COST;
	}

	// A zero rating means the role was never scored for this player, i.e. they cannot fill it.
	const float rating = getRatingForPosition(player, position);

	return rating > 0.f ? rating : INFEASIBLE_COST;
}

/**
 * Inserts (searchIndex, rating) into a rating-descending top-K list held in
 * topIndices/topValues, keeping at most `capacity` best entries. `*count` is the
 * current size. `searchIndex` is an index into the caller's playerIds array.
 */
static void insertTopK(
	uint32_t *topIndices,
	float *topValues,
	uint8_t *count,
	const uint8_t capacity,
	const uint32_t searchIndex,
	const float rating
) {
	if (*count < capacity) {
		uint8_t pos = (*count)++;
		while (pos > 0 && topValues[pos - 1] < rating) {
			topValues[pos] = topValues[pos - 1];
			topIndices[pos] = topIndices[pos - 1];
			--pos;
		}
		topValues[pos] = rating;
		topIndices[pos] = searchIndex;
	} else if (capacity > 0 && rating > topValues[capacity - 1]) {
		uint8_t pos = capacity - 1;
		while (pos > 0 && topValues[pos - 1] < rating) {
			topValues[pos] = topValues[pos - 1];
			topIndices[pos] = topIndices[pos - 1];
			--pos;
		}
		topValues[pos] = rating;
		topIndices[pos] = searchIndex;
	}
}

/**
 * Assigns players from `players` to each slot in `positions` so as to maximise
 * the cumulative positional rating, skipping any flagged in `excluded` (which may be NULL; it is
 * indexed like `players` and is how the bench leaves out the first eleven). A player is eligible for a slot only if they
 * meet MINIMUM_POSITIONAL_PROFICIENCY for the slot's position and have a rating
 * for the grouped role that owns it. Eligible cost = −rating (minimisation ≡
 * maximisation); ineligible/empty cost is chosen so the solver never prefers it
 * over a real, higher-rated candidate.
 *
 * Optimality-preserving pruning: with only FORMATION_POSITION_COUNT slots, an
 * optimal assignment can only ever draw from the top FORMATION_POSITION_COUNT
 * players per distinct slot-position. We therefore reduce the n players (which
 * may number in the tens of thousands) to a candidate set of at most
 * distinctPositions × FORMATION_POSITION_COUNT (≤ 121) before solving. Cost is
 * O(n · distinctPositions) for the scan plus O(dim³) for the solver, where
 * dim = max(FORMATION_POSITION_COUNT, candidateCount) is bounded by ~121.
 */
static void assignFormation(
	const Player *players,
	const uint32_t playerCount,
	const PositionCode positions[FORMATION_POSITION_COUNT],
	const BestElevenFilters filters,
	const bool *excluded,
	BestElevenRow outSlots[FORMATION_POSITION_COUNT]
) {
	// Default every slot to empty; filled in as the solver assigns players.
	for (uint8_t i = 0; i < FORMATION_POSITION_COUNT; ++i) {
		outSlots[i] = (BestElevenRow){.player = NULL, .rating = 0};
	}

	if (playerCount == 0) {
		LOG_WARN("No players available to fill formation");
		return;
	}
	if (playerCount < FORMATION_POSITION_COUNT) {
		LOG_WARN("Not enough players (%" PRIu32 ") to fill formation; some slots will be empty", playerCount);
	}

	// Distinct positions requested by the formation.
	PositionCode distinctPositions[FORMATION_POSITION_COUNT];
	uint8_t distinctCount = 0;
	for (uint8_t i = 0; i < FORMATION_POSITION_COUNT; ++i) {
		bool seen = false;
		for (uint8_t d = 0; d < distinctCount; ++d) {
			if (distinctPositions[d] == positions[i]) {
				seen = true;
				break;
			}
		}
		if (!seen) {
			distinctPositions[distinctCount++] = positions[i];
		}
	}

	// Collect the union of the top-N eligible players for each distinct position.
	// `candidates` holds indices into players; `chosen` dedupes across positions.
	bool *chosen = calloc(playerCount, sizeof(bool));
	uint32_t *candidates = malloc((size_t)distinctCount * FORMATION_POSITION_COUNT * sizeof(uint32_t));
	uint32_t candidateCount = 0;

	uint32_t topIndices[FORMATION_POSITION_COUNT];
	float topValues[FORMATION_POSITION_COUNT];

	for (uint8_t d = 0; d < distinctCount; ++d) {
		const PositionCode position = distinctPositions[d];
		uint8_t topCount = 0;
		for (uint32_t j = 0; j < playerCount; ++j) {
			const Player *player = &players[j];

			if (
				(excluded != NULL && excluded[j]) ||
				player->age < filters.minAge ||
				player->age > filters.maxAge ||
				player->condition < filters.minCondition ||
				player->condition > filters.maxCondition ||
				(filters.excludeInjured && player->injury.duration)
			) {
				continue;
			}

			const float rating = getPlayerPositionalRating(player, position);
			if (rating >= INFEASIBLE_COST) {
				continue;
			}
			insertTopK(topIndices, topValues, &topCount, FORMATION_POSITION_COUNT, j, rating);
		}
		for (uint8_t k = 0; k < topCount; ++k) {
			const uint32_t searchIndex = topIndices[k];
			if (!chosen[searchIndex]) {
				chosen[searchIndex] = true;
				candidates[candidateCount++] = searchIndex;
			}
		}
	}

	// Square cost matrix over slots (rows) and candidates (cols), padded so the
	// solver always finds a perfect matching. Real eligible pairs get −rating so
	// minimisation maximises total rating; ineligible pairs get INFEASIBLE_COST;
	// padding (dummy rows / empty cols) costs 0 so it is only ever used when no
	// feasible candidate remains.
	const uint32_t dim = candidateCount > FORMATION_POSITION_COUNT ? candidateCount : FORMATION_POSITION_COUNT;

	float **matrix = malloc(dim * sizeof(float*));
	for (uint32_t i = 0; i < dim; ++i) {
		matrix[i] = malloc(dim * sizeof(float));
		for (uint32_t j = 0; j < dim; ++j) {
			if (i < FORMATION_POSITION_COUNT && j < candidateCount) {
				const float rating = getPlayerPositionalRating(&players[candidates[j]], positions[i]);
				matrix[i][j] = rating >= INFEASIBLE_COST ? INFEASIBLE_COST : -rating;
			} else {
				matrix[i][j] = 0.f;
			}
		}
	}

	uint32_t *assignment = malloc(dim * sizeof(uint32_t));
	hungarian(matrix, dim, assignment);

	for (uint8_t i = 0; i < FORMATION_POSITION_COUNT; ++i) {
		const uint32_t col = assignment[i];
		if (col >= candidateCount) {
			continue; // slot matched to padding: no eligible player
		}

		const Player *player = &players[candidates[col]];
		const float rating = getPlayerPositionalRating(player, positions[i]);
		if (rating >= INFEASIBLE_COST) {
			LOG_DEBUG("No player is eligible for position \"%s\"", positionCodeNames[positions[i]]);
			continue;
		}

		outSlots[i] = (BestElevenRow){.player = player, .rating = rating};
	}

	free(assignment);
	for (uint32_t i = 0; i < dim; ++i) {
		free(matrix[i]);
	}
	free(matrix);
	free(candidates);
	free(chosen);
}

static void onPlayerNameClicked(
	const GtkGestureClick *gesture,
	const int clickCount,
	const double x,
	const double y,
	const Player *player
) {
	if (player != NULL && clickCount == 2) {
		ui_createPlayerInfoWindow(player);
	}

	(void)gesture;
	(void)clickCount;
	(void)x;
	(void)y;
}
