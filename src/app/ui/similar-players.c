// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/entities.h"
#include "app/player.h"
#include "app/ui.h"
#include "app/helpers/formatter.h"
#include "app/helpers/vector.h"
#include "app/ui/player-list.h"

#include <math.h>


#define SIMILAR_PLAYERS_LIMIT 30

static const char *positionGroupedNames[POSITION_GROUPED_COUNT] = {
	"goalkeeper",
	"full back",
	"centre back",
	"wing back",
	"defensive midfielder",
	"midfielder",
	"winger",
	"attacking midfielder",
	"striker",
};

// The visible attributes a player is compared on. Goalkeepers swap the technical set for their own.
static const uint8_t outfieldAttributes[] = {
	ATTR_COR, ATTR_CRO, ATTR_DRI, ATTR_FIN, ATTR_FIR, ATTR_FRE, ATTR_HEA, ATTR_LON, ATTR_LTH, ATTR_MAR, ATTR_PAS,
	ATTR_PEN, ATTR_TCK, ATTR_TEC,
};
static const uint8_t goalkeeperAttributes[] = {
	ATTR_AER, ATTR_CMD, ATTR_COM, ATTR_ECC, ATTR_FIR, ATTR_HAN, ATTR_KIC, ATTR_ONE, ATTR_PAS, ATTR_TTP, ATTR_REF,
	ATTR_TRO, ATTR_THR,
};
static const uint8_t sharedAttributes[] = {
	ATTR_AGG, ATTR_ANT, ATTR_BRA, ATTR_CNT, ATTR_CMP, ATTR_DEC, ATTR_DET, ATTR_FLA, ATTR_LDR, ATTR_OTB, ATTR_POS,
	ATTR_TEA, ATTR_VIS, ATTR_WOR, ATTR_ACC, ATTR_AGI, ATTR_BAL, ATTR_JUM, ATTR_NAT, ATTR_PAC, ATTR_STA, ATTR_STR,
};

typedef struct {
	Player reference;
	WindowContext context;
	PlayerSnapshot *results;
	float similarity[SIMILAR_PLAYERS_LIMIT]; // Parallel to results->players
	GtkSpinButton *maxAge;
	GtkCheckButton *cheaperOnly;
	GtkCheckButton *samePosition;
} SimilarPlayersState;

static void freeState(void *data) {
	SimilarPlayersState *state = data;
	playerSnapshot_free(state->results);
	g_free(state);
}

static float getGroupRating(const Player *player, const PositionGrouped group) {
	for (uint8_t i = 0; i < POSITION_GROUPED_COUNT; ++i) {
		if (player->ratings[i].position == group && player->ratings[i].value > 0.f) {
			return player->ratings[i].value;
		}
	}

	return 0.f;
}

/**
 * 100 minus the root-mean-square difference between the two players' attributes, which are stored
 * on a 1-100 scale: a player who is one point different on the 1-20 scale everywhere scores 95%.
 */
static float getSimilarity(const Player *a, const Player *b, const bool isGoalkeeper) {
	const uint8_t *specific = isGoalkeeper ? goalkeeperAttributes : outfieldAttributes;
	const size_t specificCount = isGoalkeeper ? G_N_ELEMENTS(goalkeeperAttributes) : G_N_ELEMENTS(outfieldAttributes);

	float sum = 0.f;
	for (size_t i = 0; i < specificCount; ++i) {
		const float difference = (float)a->attributes[specific[i]] - (float)b->attributes[specific[i]];
		sum += difference * difference;
	}
	for (size_t i = 0; i < G_N_ELEMENTS(sharedAttributes); ++i) {
		const float difference = (float)a->attributes[sharedAttributes[i]] - (float)b->attributes[sharedAttributes[i]];
		sum += difference * difference;
	}

	const float rms = sqrtf(sum / (float)(specificCount + G_N_ELEMENTS(sharedAttributes)));
	return rms < 100.f ? 100.f - rms : 0.f;
}

/** Keeps the best SIMILAR_PLAYERS_LIMIT matches in descending order of similarity. */
static void insertMatch(uint32_t *indices, float *scores, uint32_t *count, const uint32_t index, const float score) {
	if (*count == SIMILAR_PLAYERS_LIMIT && score <= scores[SIMILAR_PLAYERS_LIMIT - 1]) {
		return;
	}

	uint32_t position = *count < SIMILAR_PLAYERS_LIMIT ? (*count)++ : SIMILAR_PLAYERS_LIMIT - 1;
	while (position > 0 && scores[position - 1] < score) {
		scores[position] = scores[position - 1];
		indices[position] = indices[position - 1];
		--position;
	}
	scores[position] = score;
	indices[position] = index;
}

static void findSimilarPlayers(SimilarPlayersState *state) {
	const Player *reference = &state->reference;
	const PositionGrouped group = reference->ratings[0].position;
	const bool isGoalkeeper = group == POSITION_GROUPED_GK;
	const uint8_t maxAge = (uint8_t)gtk_spin_button_get_value_as_int(state->maxAge);
	const bool cheaperOnly = gtk_check_button_get_active(state->cheaperOnly);
	const bool samePosition = gtk_check_button_get_active(state->samePosition);

	uint32_t indices[SIMILAR_PLAYERS_LIMIT];
	float scores[SIMILAR_PLAYERS_LIMIT];
	uint32_t count = 0;
	for (uint32_t i = 0; i < gameContext.playerCount; ++i) {
		const Player *candidate = &gameContext.players[i];
		if (
			candidate->uid == 0 ||
			candidate->uid == reference->uid ||
			candidate->age > maxAge ||
			(cheaperOnly && candidate->guideValue > reference->guideValue) ||
			(samePosition && getGroupRating(candidate, group) <= 0.f)
		) {
			continue;
		}

		insertMatch(indices, scores, &count, i, getSimilarity(reference, candidate, isGoalkeeper));
	}

	playerSnapshot_free(state->results);
	state->results = playerSnapshot_create(indices, count);
	for (uint32_t i = 0; i < count; ++i) {
		state->similarity[i] = scores[i];
	}
}

static void render(SimilarPlayersState *state) {
	const WindowContext context = state->context;
	const PositionGrouped group = state->reference.ratings[0].position;

	char ratingHeading[48];
	snprintf(ratingHeading, sizeof(ratingHeading), "Rating (%s)", positionGroupedNames[group]);
	const char *const headings[] = {"Player", "Age", "Club", "CA", ratingHeading, "Value", "Similarity"};
	PlayerGrid grid;
	// Clearing the grid destroys the rows that point into the previous results before they are replaced
	playerGrid_begin(&grid, context, headings, G_N_ELEMENTS(headings));

	if (gameContext.players == NULL) {
		playerGrid_appendMessage(&grid, "Player data is still being cached; this list fills in once it has loaded.");
		playerSnapshot_free(state->results);
		state->results = NULL;
		return;
	}

	findSimilarPlayers(state);
	if (state->results == NULL || state->results->count == 0) {
		playerGrid_appendMessage(&grid, "Nobody matches these options.");
		return;
	}

	for (uint32_t i = 0; i < state->results->count; ++i) {
		const Player *player = &state->results->players[i];

		char age[8];
		char ca[8];
		char similarity[16];
		snprintf(age, sizeof(age), "%u", player->age);
		snprintf(ca, sizeof(ca), "%u", player->ca);
		snprintf(similarity, sizeof(similarity), "%.1f%%", state->similarity[i]);

		GtkWidget *cells[] = {
			playerGrid_nameCell(player),
			playerGrid_textCell(age, 0.5f),
			playerGrid_clubCell(player),
			playerGrid_textCell(ca, 0.5f),
			playerGrid_ratingCell(getGroupRating(player, group)),
			playerGrid_valueCell(player->guideValue),
			playerGrid_textCell(similarity, 1.f),
		};
		playerGrid_appendRow(&grid, cells, G_N_ELEMENTS(cells));
	}
}

static void onOptionChanged(GObject *object, gpointer data) {
	(void)object;
	render(data);
}

void ui_createSimilarPlayersWindow(const Player *player) {
	if (player == NULL || player->ratings[0].value <= 0.f) {
		return;
	}

	SimilarPlayersState *state = g_new0(SimilarPlayersState, 1);
	state->reference = *player;

	char name[PLAYER_DISPLAY_NAME_LENGTH];
	char title[PLAYER_DISPLAY_NAME_LENGTH + 24];
	player_getDisplayName(player, name, sizeof(name));
	snprintf(title, sizeof(title), "Players like %s", name);
	state->context = playerList_openWindow(WINDOW_SIMILAR_PLAYERS, title, state, freeState);
	playerList_setSubtitle(
		state->context,
		"Ranked by how closely their technical (or goalkeeping), mental and physical attributes match. Hidden "
		"attributes are left out, as they do not describe how a player plays."
	);

	GtkBox *controls = playerList_getControls(state->context);
	gtk_box_append(controls, gtk_label_new("Max age"));
	state->maxAge = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(14, 50, 1));
	gtk_spin_button_set_value(state->maxAge, 50);
	gtk_box_append(controls, GTK_WIDGET(state->maxAge));
	state->cheaperOnly = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Cheaper only"));
	gtk_box_append(controls, GTK_WIDGET(state->cheaperOnly));
	state->samePosition = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Can play the same role"));
	gtk_check_button_set_active(state->samePosition, true);
	gtk_box_append(controls, GTK_WIDGET(state->samePosition));

	g_signal_connect(state->maxAge, "value-changed", G_CALLBACK(onOptionChanged), state);
	g_signal_connect(state->cheaperOnly, "toggled", G_CALLBACK(onOptionChanged), state);
	g_signal_connect(state->samePosition, "toggled", G_CALLBACK(onOptionChanged), state);

	render(state);
	ui_presentWindow(state->context);
}

/** The cache was replaced, so the matches are found again against the new data. */
void ui_renderSimilarPlayersWindow(const WindowContext context) {
	render(context.data);
}
