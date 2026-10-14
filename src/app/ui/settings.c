// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/entities.h"
#include "app/my-club.h"
#include "app/options.h"
#include "app/ui.h"
#include "app/helpers/attributes.h"
#include "app/helpers/text.h"
#include "app/helpers/vector.h"
#include "types/position-code-names.h"

#include <string.h>


#define CLUB_MATCH_LIMIT 25

// Indexed by PositionGrouped, with POSITION_GROUPED_COUNT standing for the general weights. NULL
// terminated, as gtk_drop_down_new_from_strings() expects.
static const char *roleNames[POSITION_GROUPED_COUNT + 2] = {
	"Goalkeeper",
	"Full back",
	"Centre back",
	"Wing back",
	"Defensive midfielder",
	"Midfielder",
	"Winger",
	"Attacking midfielder",
	"Striker",
	"General (every role without its own)",
	NULL,
};

/**
 * The editors hold the user's unsaved changes, so they are only reloaded from the options when the
 * selection changes or after a save; everything is written to options.yaml, which is then reloaded.
 */
typedef struct {
	WindowContext context;
	// My club
	GtkLabel *myClubLabel;
	GtkSearchEntry *clubSearch;
	GtkListBox *clubMatches;
	// Formations
	GtkListBox *formationList;
	GtkEntry *formationName;
	GtkDropDown *formationSlots[FORMATION_POSITION_COUNT];
	GtkLabel *formationStatus;
	int64_t selectedFormation; // -1 while editing a new formation
	// Ratings
	GtkDropDown *role;
	GtkLabel *roleStatus;
	GtkSpinButton *scale;
	GtkSpinButton *weights[ATTRIBUTE_COUNT];
	GtkButton *removeWeights;
	bool isLoadingFormation;
} SettingsState;

static void renderMyClub(SettingsState *state);
static void renderFormationList(SettingsState *state);
static void loadFormation(SettingsState *state);
static void loadRole(SettingsState *state);

static GtkWidget *createHeading(const char *text) {
	GtkWidget *label = gtk_label_new(text);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_add_css_class(label, "title-3");
	return label;
}

static GtkWidget *createHint(const char *text) {
	GtkWidget *label = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(label), 0.f);
	gtk_label_set_wrap(GTK_LABEL(label), true);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 90);
	gtk_widget_add_css_class(label, "settings-hint");
	return label;
}

static GtkWidget *createButton(const char *label, const char *cssClass, GCallback callback, SettingsState *state) {
	GtkWidget *button = gtk_button_new_with_label(label);
	gtk_widget_add_css_class(button, cssClass);
	g_signal_connect(button, "clicked", callback, state);
	return button;
}

static void setStatus(GtkLabel *label, const char *text) {
	gtk_label_set_text(label, text);
}

/* ---------------------------------------------------------------------------------------------
 * My club
 * ------------------------------------------------------------------------------------------- */

static void onClubSearchChanged(GtkSearchEntry *entry, gpointer data) {
	SettingsState *state = data;
	gtk_list_box_remove_all(state->clubMatches);

	char needle[CLUB_LONG_NAME_LENGTH * 2];
	text_fold(gtk_editable_get_text(GTK_EDITABLE(entry)), needle, sizeof(needle));
	g_strstrip(needle);
	if (strlen(needle) < 2) {
		return;
	}

	uint8_t count = 0;
	for (uint64_t i = 0; i < gameContext.clubCount && count < CLUB_MATCH_LIMIT; ++i) {
		const Club *club = &gameContext.clubs[i];
		if (club->name[0] == '\0') {
			continue;
		}
		if (!text_containsFolded(club->name, needle) && !text_containsFolded(club->shortName, needle)) {
			continue;
		}

		GtkWidget *label = gtk_label_new(club->name);
		gtk_label_set_xalign(GTK_LABEL(label), 0.f);
		g_object_set_data(G_OBJECT(label), "club-index", GINT_TO_POINTER((gint)i));
		gtk_list_box_append(state->clubMatches, label);
		++count;
	}

	if (count == 0) {
		GtkWidget *label = gtk_label_new(
			gameContext.clubCount == 0 ? "Clubs appear here once a save is loaded." : "No club matches that name."
		);
		gtk_label_set_xalign(GTK_LABEL(label), 0.f);
		gtk_list_box_append(state->clubMatches, label);
		gtk_list_box_row_set_activatable(gtk_list_box_get_row_at_index(state->clubMatches, 0), false);
	}
}

static void onClubPicked(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
	(void)box;
	SettingsState *state = data;
	GtkWidget *label = gtk_list_box_row_get_child(row);
	const Club *club = entities_getClub(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(label), "club-index")));
	if (club == NULL) {
		return;
	}

	snprintf(gameContext.options.myClub, sizeof(gameContext.options.myClub), "%s", club->name);
	gtk_editable_set_text(GTK_EDITABLE(state->clubSearch), "");
	// Reloading the options re-renders every window, this one included
	options_saveMyClub();
}

static void onClearClubClicked(GtkButton *button, gpointer data) {
	(void)button;
	(void)data;
	gameContext.options.myClub[0] = '\0';
	options_saveMyClub();
}

static void onOpenMySquadClicked(GtkButton *button, gpointer data) {
	(void)button;
	(void)data;
	ui_createMySquadWindow();
}

static void buildMyClubPage(SettingsState *state) {
	GtkBox *page = GTK_BOX(gtk_builder_get_object(state->context.builder, "page:my-club"));

	gtk_box_append(page, createHeading("The club you manage"));
	gtk_box_append(
		page,
		createHint(
			"The app cannot tell which club you manage, so pick it here. It powers the My Squad view (squad depth and "
			"the squad planner for your own players) and the \"Likely to join my club\" search filter."
		)
	);

	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	state->myClubLabel = GTK_LABEL(gtk_label_new(""));
	gtk_widget_add_css_class(GTK_WIDGET(state->myClubLabel), "settings-value");
	gtk_box_append(GTK_BOX(row), GTK_WIDGET(state->myClubLabel));
	gtk_box_append(GTK_BOX(row), createButton("Open My Squad", "btn-secondary", G_CALLBACK(onOpenMySquadClicked), state));
	gtk_box_append(GTK_BOX(row), createButton("Clear", "btn-secondary", G_CALLBACK(onClearClubClicked), state));
	gtk_box_append(page, row);

	state->clubSearch = GTK_SEARCH_ENTRY(gtk_search_entry_new());
	gtk_search_entry_set_placeholder_text(state->clubSearch, "Search for your club");
	gtk_widget_set_size_request(GTK_WIDGET(state->clubSearch), 360, -1);
	gtk_widget_set_halign(GTK_WIDGET(state->clubSearch), GTK_ALIGN_START);
	g_signal_connect(state->clubSearch, "search-changed", G_CALLBACK(onClubSearchChanged), state);
	gtk_box_append(page, GTK_WIDGET(state->clubSearch));

	state->clubMatches = GTK_LIST_BOX(gtk_list_box_new());
	gtk_list_box_set_selection_mode(state->clubMatches, GTK_SELECTION_NONE);
	gtk_list_box_set_activate_on_single_click(state->clubMatches, true);
	gtk_widget_set_size_request(GTK_WIDGET(state->clubMatches), 360, -1);
	gtk_widget_set_halign(GTK_WIDGET(state->clubMatches), GTK_ALIGN_START);
	gtk_widget_add_css_class(GTK_WIDGET(state->clubMatches), "settings-list");
	g_signal_connect(state->clubMatches, "row-activated", G_CALLBACK(onClubPicked), state);
	gtk_box_append(page, GTK_WIDGET(state->clubMatches));

	gtk_box_append(page, createHeading("How \"likely to join\" is worked out"));
	gtk_box_append(
		page,
		createHint(
			"Football Manager decides interest itself, and the app does not read that. Instead it estimates: a player "
			"counts as likely to join when neither their reputation nor their current club's is more than 500 (out of "
			"10,000) above your club's. Free agents only need the first. Treat it as a shortlist to check in the game."
		)
	);
}

static void renderMyClub(SettingsState *state) {
	const char *name = gameContext.options.myClub;
	char text[CLUB_LONG_NAME_LENGTH + 48];
	if (name[0] == '\0') {
		snprintf(text, sizeof(text), "No club chosen yet");
	} else if (gameContext.clubCount > 0 && myClub_getIndex() < 0) {
		snprintf(text, sizeof(text), "%s (not in the loaded save)", name);
	} else {
		snprintf(text, sizeof(text), "%s", name);
	}
	gtk_label_set_text(state->myClubLabel, text);
}

/* ---------------------------------------------------------------------------------------------
 * Formations
 * ------------------------------------------------------------------------------------------- */

static void onFormationSelected(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
	(void)box;
	SettingsState *state = data;
	if (row == NULL || state->isLoadingFormation) {
		return;
	}

	state->selectedFormation = gtk_list_box_row_get_index(row);
	loadFormation(state);
}

static void onNewFormationClicked(GtkButton *button, gpointer data) {
	(void)button;
	SettingsState *state = data;
	// Starts from whatever is in the editor, which makes tweaking an existing shape quick
	state->selectedFormation = -1;
	gtk_list_box_unselect_all(state->formationList);
	gtk_editable_set_text(GTK_EDITABLE(state->formationName), "");
	gtk_widget_grab_focus(GTK_WIDGET(state->formationName));
	setStatus(state->formationStatus, "New formation: name it, pick the positions, then save.");
}

static void onSaveFormationClicked(GtkButton *button, gpointer data) {
	(void)button;
	SettingsState *state = data;

	Formation formation = {0};
	const char *name = gtk_editable_get_text(GTK_EDITABLE(state->formationName));
	snprintf(formation.name, sizeof(formation.name), "%s", name);
	g_strstrip(formation.name);
	if (formation.name[0] == '\0' || strchr(formation.name, '"') != NULL) {
		setStatus(state->formationStatus, "Give the formation a name (without double quotes).");
		return;
	}
	for (uint8_t i = 0; i < FORMATION_POSITION_COUNT; ++i) {
		formation.positions[i] = (PositionCode)gtk_drop_down_get_selected(state->formationSlots[i]);
	}

	Options *options = &gameContext.options;
	if (state->selectedFormation >= 0 && (uint64_t)state->selectedFormation < vector_length(options->formations)) {
		options->formations[state->selectedFormation] = formation;
	} else {
		vector_push(options->formations, formation);
		state->selectedFormation = (int64_t)vector_length(options->formations) - 1;
	}

	options_saveFormations();
	setStatus(state->formationStatus, "Saved. It is now available in the Best XI window.");
}

static void onDeleteFormationClicked(GtkButton *button, gpointer data) {
	(void)button;
	SettingsState *state = data;
	Options *options = &gameContext.options;

	if (state->selectedFormation < 0 || (uint64_t)state->selectedFormation >= vector_length(options->formations)) {
		setStatus(state->formationStatus, "Select a formation to delete.");
		return;
	}
	// The Best XI window always needs one to show
	if (vector_length(options->formations) <= 1) {
		setStatus(state->formationStatus, "Keep at least one formation.");
		return;
	}

	Formation removed;
	vector_splice(options->formations, (size_t)state->selectedFormation, &removed);
	state->selectedFormation = 0;
	options_saveFormations();
	setStatus(state->formationStatus, "Deleted.");
}

static void moveFormation(SettingsState *state, const int64_t offset) {
	Options *options = &gameContext.options;
	const int64_t from = state->selectedFormation;
	const int64_t to = from + offset;
	const int64_t count = (int64_t)vector_length(options->formations);
	if (from < 0 || from >= count || to < 0 || to >= count) {
		return;
	}

	const Formation moved = options->formations[from];
	options->formations[from] = options->formations[to];
	options->formations[to] = moved;
	state->selectedFormation = to;
	options_saveFormations();
	setStatus(state->formationStatus, "The first formation is the one Best XI opens with.");
}

static void onMoveUpClicked(GtkButton *button, gpointer data) {
	(void)button;
	moveFormation(data, -1);
}

static void onMoveDownClicked(GtkButton *button, gpointer data) {
	(void)button;
	moveFormation(data, 1);
}

static void buildFormationsPage(SettingsState *state) {
	GtkBox *page = GTK_BOX(gtk_builder_get_object(state->context.builder, "page:formations"));

	// Left: the list and the actions that act on it
	GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_append(GTK_BOX(left), createHeading("Formations"));
	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll, true);
	gtk_widget_set_size_request(scroll, 220, -1);
	state->formationList = GTK_LIST_BOX(gtk_list_box_new());
	gtk_widget_add_css_class(GTK_WIDGET(state->formationList), "settings-list");
	g_signal_connect(state->formationList, "row-selected", G_CALLBACK(onFormationSelected), state);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(state->formationList));
	gtk_box_append(GTK_BOX(left), scroll);

	GtkWidget *listActions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append(GTK_BOX(listActions), createButton("New", "btn-secondary", G_CALLBACK(onNewFormationClicked), state));
	gtk_box_append(GTK_BOX(listActions), createButton("▲", "btn-secondary", G_CALLBACK(onMoveUpClicked), state));
	gtk_box_append(GTK_BOX(listActions), createButton("▼", "btn-secondary", G_CALLBACK(onMoveDownClicked), state));
	gtk_box_append(GTK_BOX(listActions), createButton("Delete", "btn-secondary", G_CALLBACK(onDeleteFormationClicked), state));
	gtk_box_append(GTK_BOX(left), listActions);
	gtk_box_append(page, left);

	// Right: the editor
	GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_hexpand(right, true);
	gtk_box_append(GTK_BOX(right), createHeading("Edit formation"));
	gtk_box_append(
		GTK_BOX(right),
		createHint("Pick the eleven positions. Best XI fills them with the best combination of players it can find.")
	);

	state->formationName = GTK_ENTRY(gtk_entry_new());
	gtk_entry_set_placeholder_text(state->formationName, "Name, e.g. 3-5-2 wing backs");
	gtk_entry_set_max_length(state->formationName, FORMATION_NAME_LENGTH - 1);
	gtk_widget_set_size_request(GTK_WIDGET(state->formationName), 280, -1);
	gtk_widget_set_halign(GTK_WIDGET(state->formationName), GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(right), GTK_WIDGET(state->formationName));

	// positionCodeNames is not NULL terminated, which the dropdown needs
	const char *positionNames[POSITION_CODE_COUNT + 1] = {NULL};
	for (uint8_t i = 0; i < POSITION_CODE_COUNT; ++i) {
		positionNames[i] = positionCodeNames[i];
	}

	GtkWidget *slots = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(slots), 12);
	gtk_grid_set_row_spacing(GTK_GRID(slots), 8);
	for (uint8_t i = 0; i < FORMATION_POSITION_COUNT; ++i) {
		char label[16];
		snprintf(label, sizeof(label), "Player %u", i + 1);
		GtkWidget *slotLabel = gtk_label_new(label);
		gtk_label_set_xalign(GTK_LABEL(slotLabel), 0.f);
		gtk_widget_add_css_class(slotLabel, "settings-hint");

		state->formationSlots[i] = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(positionNames));
		gtk_widget_set_size_request(GTK_WIDGET(state->formationSlots[i]), 100, -1);

		const int column = (i % 4) * 2;
		const int row = i / 4;
		gtk_grid_attach(GTK_GRID(slots), slotLabel, column, row, 1, 1);
		gtk_grid_attach(GTK_GRID(slots), GTK_WIDGET(state->formationSlots[i]), column + 1, row, 1, 1);
	}
	gtk_box_append(GTK_BOX(right), slots);

	GtkWidget *save = createButton("Save formation", "btn-primary", G_CALLBACK(onSaveFormationClicked), state);
	gtk_widget_set_halign(save, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(right), save);

	state->formationStatus = GTK_LABEL(createHint(""));
	gtk_box_append(GTK_BOX(right), GTK_WIDGET(state->formationStatus));
	gtk_box_append(page, right);
}

static void renderFormationList(SettingsState *state) {
	const Options *options = &gameContext.options;

	state->isLoadingFormation = true;
	gtk_list_box_remove_all(state->formationList);
	for (uint64_t i = 0; i < vector_length(options->formations); ++i) {
		GtkWidget *label = gtk_label_new(options->formations[i].name);
		gtk_label_set_xalign(GTK_LABEL(label), 0.f);
		gtk_list_box_append(state->formationList, label);
	}

	const int64_t count = (int64_t)vector_length(options->formations);
	if (state->selectedFormation >= count) {
		state->selectedFormation = count - 1;
	}
	if (state->selectedFormation >= 0) {
		gtk_list_box_select_row(
			state->formationList,
			gtk_list_box_get_row_at_index(state->formationList, (int)state->selectedFormation)
		);
	}
	state->isLoadingFormation = false;

	loadFormation(state);
}

static void loadFormation(SettingsState *state) {
	const Options *options = &gameContext.options;
	if (state->selectedFormation < 0 || (uint64_t)state->selectedFormation >= vector_length(options->formations)) {
		return;
	}

	const Formation *formation = &options->formations[state->selectedFormation];
	gtk_editable_set_text(GTK_EDITABLE(state->formationName), formation->name);
	for (uint8_t i = 0; i < FORMATION_POSITION_COUNT; ++i) {
		gtk_drop_down_set_selected(state->formationSlots[i], formation->positions[i]);
	}
}

/* ---------------------------------------------------------------------------------------------
 * Ratings
 * ------------------------------------------------------------------------------------------- */

static PositionGrouped getSelectedRole(const SettingsState *state) {
	const guint selected = gtk_drop_down_get_selected(state->role);
	return selected <= POSITION_GROUPED_COUNT ? (PositionGrouped)selected : POSITION_GROUPED_COUNT;
}

static int64_t findWeightsIndex(const PositionGrouped role) {
	const Options *options = &gameContext.options;
	for (uint64_t i = 0; i < vector_length(options->weights); ++i) {
		if (options->weights[i].position == role) {
			return (int64_t)i;
		}
	}
	return -1;
}

/** Shows the role's own weights, or the general ones as a starting point when it has none. */
static void loadRole(SettingsState *state) {
	const Options *options = &gameContext.options;
	const PositionGrouped role = getSelectedRole(state);
	int64_t index = findWeightsIndex(role);
	const bool hasOwnWeights = index >= 0;
	if (!hasOwnWeights) {
		index = findWeightsIndex(POSITION_GROUPED_COUNT);
	}

	for (uint8_t i = 0; i < ATTRIBUTE_COUNT; ++i) {
		gtk_spin_button_set_value(state->weights[i], 0);
	}
	gtk_spin_button_set_value(state->scale, 1);

	if (index >= 0) {
		const PositionWeights *weights = &options->weights[index];
		gtk_spin_button_set_value(state->scale, weights->scale);
		for (uint64_t i = 0; i < vector_length(weights->weights); ++i) {
			if (weights->weights[i].attribute < ATTRIBUTE_COUNT) {
				gtk_spin_button_set_value(state->weights[weights->weights[i].attribute], weights->weights[i].weight);
			}
		}
	}

	gtk_widget_set_sensitive(GTK_WIDGET(state->removeWeights), hasOwnWeights && role != POSITION_GROUPED_COUNT);
	gtk_label_set_text(
		state->roleStatus,
		role == POSITION_GROUPED_COUNT
			? "These weights rate every role that has none of its own."
			: hasOwnWeights
			? "This role has its own weights."
			: "This role has no weights of its own, so it uses the general ones shown here. Saving gives it its own."
	);
}

static void onRoleChanged(GObject *object, GParamSpec *pspec, gpointer data) {
	(void)object;
	(void)pspec;
	loadRole(data);
}

static void onSaveWeightsClicked(GtkButton *button, gpointer data) {
	(void)button;
	SettingsState *state = data;
	Options *options = &gameContext.options;
	const PositionGrouped role = getSelectedRole(state);

	PositionWeights weights = {.position = role, .scale = (float)gtk_spin_button_get_value(state->scale)};
	if (weights.scale <= 0.f) {
		gtk_label_set_text(state->roleStatus, "The scale must be above zero.");
		return;
	}
	for (uint8_t i = 0; i < ATTRIBUTE_COUNT; ++i) {
		const float weight = (float)gtk_spin_button_get_value(state->weights[i]);
		if (weight != 0.f) {
			vector_push(weights.weights, ((RatingWeight){.attribute = i, .weight = weight}));
		}
	}

	const int64_t index = findWeightsIndex(role);
	if (index >= 0) {
		vector_free(options->weights[index].weights);
		options->weights[index] = weights;
	} else {
		vector_push(options->weights, weights);
	}

	// Every player is re-rated as the options reload
	options_saveRatings();
	gtk_label_set_text(state->roleStatus, "Saved. Every player has been re-rated.");
}

static void onRevertWeightsClicked(GtkButton *button, gpointer data) {
	(void)button;
	loadRole(data);
}

static void onRemoveWeightsClicked(GtkButton *button, gpointer data) {
	(void)button;
	SettingsState *state = data;
	const PositionGrouped role = getSelectedRole(state);
	const int64_t index = findWeightsIndex(role);
	if (index < 0 || role == POSITION_GROUPED_COUNT) {
		return;
	}

	PositionWeights removed;
	vector_splice(gameContext.options.weights, (size_t)index, &removed);
	vector_free(removed.weights);
	options_saveRatings();
	loadRole(state);
}

static int compareAttributeNames(const void *a, const void *b) {
	return strcmp(attributes_getName(*(const uint8_t*)a), attributes_getName(*(const uint8_t*)b));
}

static void buildRatingsPage(SettingsState *state) {
	GtkBox *page = GTK_BOX(gtk_builder_get_object(state->context.builder, "page:ratings"));

	gtk_box_append(page, createHeading("Rating weights"));
	gtk_box_append(
		page,
		createHint(
			"A role's rating adds up each attribute (as a fraction of 20) multiplied by its weight, then divides by "
			"the scale. Leave an attribute at 0 to ignore it; negative weights count against a player."
		)
	);

	GtkWidget *roleRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_box_append(GTK_BOX(roleRow), gtk_label_new("Role"));
	state->role = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(roleNames));
	gtk_drop_down_set_selected(state->role, POSITION_GROUPED_COUNT);
	g_signal_connect(state->role, "notify::selected", G_CALLBACK(onRoleChanged), state);
	gtk_box_append(GTK_BOX(roleRow), GTK_WIDGET(state->role));
	gtk_box_append(GTK_BOX(roleRow), gtk_label_new("Scale"));
	state->scale = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0.05, 100, 0.05));
	gtk_spin_button_set_digits(state->scale, 2);
	gtk_box_append(GTK_BOX(roleRow), GTK_WIDGET(state->scale));
	gtk_box_append(page, roleRow);

	state->roleStatus = GTK_LABEL(createHint(""));
	gtk_box_append(page, GTK_WIDGET(state->roleStatus));

	// Three columns of attributes, alphabetical down each column
	uint8_t sorted[ATTRIBUTE_COUNT];
	for (uint8_t i = 0; i < ATTRIBUTE_COUNT; ++i) {
		sorted[i] = i;
	}
	qsort(sorted, ATTRIBUTE_COUNT, sizeof(*sorted), compareAttributeNames);

	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
	const uint8_t rows = (ATTRIBUTE_COUNT + 2) / 3;
	for (uint8_t i = 0; i < ATTRIBUTE_COUNT; ++i) {
		const uint8_t attribute = sorted[i];
		const int column = (i / rows) * 2;
		const int row = i % rows;

		GtkWidget *label = gtk_label_new(attributes_getName(attribute));
		gtk_label_set_xalign(GTK_LABEL(label), 0.f);
		gtk_widget_set_size_request(label, 130, -1);
		if (column > 0) {
			gtk_widget_set_margin_start(label, 18);
		}

		state->weights[attribute] = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(-100, 100, 0.5));
		gtk_spin_button_set_digits(state->weights[attribute], 2);

		gtk_grid_attach(GTK_GRID(grid), label, column, row, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(state->weights[attribute]), column + 1, row, 1, 1);
	}

	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll, true);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
	gtk_box_append(page, scroll);

	GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append(GTK_BOX(actions), createButton("Save weights", "btn-primary", G_CALLBACK(onSaveWeightsClicked), state));
	gtk_box_append(GTK_BOX(actions), createButton("Undo changes", "btn-secondary", G_CALLBACK(onRevertWeightsClicked), state));
	state->removeWeights = GTK_BUTTON(
		createButton("Use the general weights", "btn-secondary", G_CALLBACK(onRemoveWeightsClicked), state)
	);
	gtk_widget_set_tooltip_text(GTK_WIDGET(state->removeWeights), "Remove this role's own weights");
	gtk_box_append(GTK_BOX(actions), GTK_WIDGET(state->removeWeights));
	gtk_box_append(page, actions);
}

/* ---------------------------------------------------------------------------------------------
 * Window
 * ------------------------------------------------------------------------------------------- */

void ui_createSettingsWindow(void) {
	// One settings window is enough; bring the existing one forward instead of opening another
	for (uint64_t i = 0; i < vector_length(gameContext.windows); ++i) {
		if (gameContext.windows[i].type == WINDOW_SETTINGS) {
			gtk_window_present(GTK_WINDOW(gameContext.windows[i].window));
			return;
		}
	}

	SettingsState *state = g_new0(SettingsState, 1);
	state->context = openWindow("settings", "window:settings", WINDOW_SETTINGS, state);
	g_object_set_data_full(G_OBJECT(state->context.window), "settings:state", state, g_free);

	buildMyClubPage(state);
	buildFormationsPage(state);
	buildRatingsPage(state);

	ui_renderSettingsWindow(state->context);
	loadRole(state);
	ui_presentWindow(state->context);
}

/**
 * Runs after the options reload, i.e. after every save here. The formation list is rebuilt, but the
 * weights editor is left alone so a reload cannot throw away changes the user has not saved yet.
 */
void ui_renderSettingsWindow(const WindowContext context) {
	SettingsState *state = context.data;
	renderMyClub(state);
	renderFormationList(state);
	gtk_widget_set_sensitive(
		GTK_WIDGET(state->removeWeights),
		findWeightsIndex(getSelectedRole(state)) >= 0 && getSelectedRole(state) != POSITION_GROUPED_COUNT
	);
}

/** Clubs are republished with every cache run, which can resolve (or lose) the chosen club. */
void ui_rerenderSettingsClub(const WindowContext context) {
	renderMyClub(context.data);
}
