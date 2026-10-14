// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/entities.h"
#include "app/maths.h"
#include "app/player.h"
#include "app/shortlist.h"
#include "app/ui.h"
#include "app/callbacks/club-search.h"
#include "app/callbacks/filters.h"
#include "app/callbacks/nationality-search.h"
#include "app/helpers/date.h"
#include "app/helpers/formatter.h"
#include "app/helpers/icons.h"
#include "app/helpers/vector.h"
#include "core/logger.h"

#include <stdlib.h>
#include <string.h>


static const char *positionGroupedNames[POSITION_GROUPED_COUNT] = {
	"Goalkeeper",
	"Full back",
	"Centre back",
	"Wing back",
	"Defensive midfielder",
	"Midfielder",
	"Winger",
	"Attacking midfielder",
	"Striker"
};

static void onPlayerInfoClicked(
	GtkGestureClick *gesture,
	int clickCount,
	double x,
	double y,
	gpointer userData
) {
	GtkWidget *window = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	GtkWidget *commonNameLabel = GTK_WIDGET(userData);

	if (gtk_widget_pick(window, x, y, GTK_PICK_DEFAULT) != commonNameLabel) {
		gtk_label_set_selectable(GTK_LABEL(commonNameLabel), false);
		gtk_label_set_selectable(GTK_LABEL(commonNameLabel), true);
	}

	(void)clickCount;
}

/**
 * Clicking the club or a flag starts a fresh search for it, so earlier filters do not hide most of
 * the squad, and brings the search window forward to show the results.
 */
static void onClubClicked(GtkGestureClick *gesture, const int clickCount, const double x, const double y, gpointer data) {
	(void)gesture;
	(void)clickCount;
	(void)x;
	(void)y;

	const PlayerSnapshot *snapshot = data;
	if (snapshot->count == 0 || entities_getClub(snapshot->players[0].clubIndex) == NULL) {
		return;
	}

	const int64_t clubIndex = snapshot->players[0].clubIndex;
	callbacks_onFiltersClear();
	clubSearch_select(clubIndex);
	ui_presentSearchWindow();
}

static void onFlagClicked(GtkGestureClick *gesture, const int clickCount, const double x, const double y, gpointer data) {
	(void)gesture;
	(void)clickCount;
	(void)x;
	(void)y;

	const uint8_t nationIndex = (uint8_t)GPOINTER_TO_UINT(data);
	if (entities_getNation(nationIndex) == NULL) {
		return;
	}

	callbacks_onFiltersClear();
	nationalitySearch_select(nationIndex);
	ui_presentSearchWindow();
}

static void onShortlistClicked(GtkButton *button, gpointer data) {
	(void)button;
	const PlayerSnapshot *snapshot = data;
	if (snapshot->count > 0) {
		shortlist_toggle(&snapshot->players[0]);
	}
}

static void onSimilarClicked(GtkButton *button, gpointer data) {
	(void)button;
	const PlayerSnapshot *snapshot = data;
	if (snapshot->count > 0) {
		ui_createSimilarPlayersWindow(&snapshot->players[0]);
	}
}

/** Lower is better for these, so a fall is the improvement. */
static bool isHigherBetter(const uint8_t attribute) {
	return attribute != ATTR_INJ && attribute != ATTR_DIR && attribute != ATTR_CNY;
}

static void setTransferStatus(GtkBuilder *builder, const Player *player) {
	const bool isListed = player->transferStatus & (TRANSFER_STATUS_LISTED | TRANSFER_STATUS_LISTED_BY_REQUEST);
	const bool isLoanListed = player->transferStatus & TRANSFER_STATUS_LOAN_LISTED;

	GtkWidget *pill = GTK_WIDGET(gtk_builder_get_object(builder, "widget:transfer-status"));
	GtkLabel *label = GTK_LABEL(gtk_builder_get_object(builder, "label:transfer-status"));
	gtk_widget_set_visible(pill, player->clubIndex != -1 && (isListed || isLoanListed));
	if (isListed && isLoanListed) {
		gtk_label_set_text(label, "🏷 Transfer & loan listed");
	} else if (isListed) {
		gtk_label_set_text(label, player->transferStatus & TRANSFER_STATUS_LISTED_BY_REQUEST
			? "🏷 Listed by request"
			: "🏷 Transfer listed");
	} else {
		gtk_label_set_text(label, "🏷 Loan listed");
	}
}

static void setContract(GtkBuilder *builder, const Player *player) {
	GtkLabel *label = GTK_LABEL(gtk_builder_get_object(builder, "label:contract"));
	gtk_widget_remove_css_class(GTK_WIDGET(label), "contract-expiring");

	if (player->clubIndex == -1 || player->contractEnd.year == 0) {
		gtk_label_set_text(label, player->clubIndex == -1 ? "No contract" : "");
		return;
	}

	char wage[FORMATTER_MONEY_SIZE];
	char date[DATE_FORMAT_SIZE];
	char buffer[FORMATTER_MONEY_SIZE + DATE_FORMAT_SIZE + 32];
	formatter_formatMoneyShort(player->weeklyWage, false, wage);
	date_format(player->contractEnd, date, sizeof(date));
	snprintf(buffer, sizeof(buffer), "%s p/w  ·  Contract ends %s", wage, date);
	gtk_label_set_text(label, buffer);

	if (date_monthsUntil(player->contractEnd) <= 6) {
		gtk_widget_add_css_class(GTK_WIDGET(label), "contract-expiring");
	}
}

/** "Since 12 August 2026: CA +3 · PA +0 · …", for a shortlisted player. */
static void setTracking(GtkBuilder *builder, const Player *player, const ShortlistEntry *entry) {
	GtkLabel *label = GTK_LABEL(gtk_builder_get_object(builder, "label:tracking"));
	GtkButton *shortlistButton = GTK_BUTTON(gtk_builder_get_object(builder, "button:shortlist"));
	gtk_button_set_label(shortlistButton, entry != NULL ? "★ Shortlisted" : "☆ Shortlist");
	gtk_widget_set_visible(GTK_WIDGET(label), entry != NULL);
	if (entry == NULL) {
		return;
	}

	char date[DATE_FORMAT_SIZE];
	date_format(entry->since, date, sizeof(date));

	char text[32];
	char ca[FORMATTER_DELTA_SIZE];
	char pa[FORMATTER_DELTA_SIZE];
	char rating[FORMATTER_DELTA_SIZE];
	char value[FORMATTER_DELTA_SIZE];

	const int caChange = (int)player->ca - (int)entry->ca;
	snprintf(text, sizeof(text), "CA %+d", caChange);
	formatter_formatDelta(caChange, true, text, ca);

	const int paChange = (int)player->pa - (int)entry->pa;
	snprintf(text, sizeof(text), "PA %+d", paChange);
	formatter_formatDelta(paChange, true, text, pa);

	float ratingChange = player->ratings[0].value - shortlist_getBaselineRating(entry, player);
	if (fabsf(ratingChange) < 0.05f) {
		ratingChange = 0.f;
	}
	snprintf(text, sizeof(text), "Rating %+.1f", ratingChange);
	formatter_formatDelta(ratingChange, true, text, rating);

	const int64_t valueChange = (int64_t)player->guideValue - (int64_t)entry->value;
	char money[FORMATTER_MONEY_SIZE];
	formatter_formatMoneyShort(valueChange, true, money);
	snprintf(text, sizeof(text), "Value %s", money);
	formatter_formatDelta((double)valueChange, true, text, value);

	char markup[FORMATTER_DELTA_SIZE * 4 + DATE_FORMAT_SIZE + 64];
	snprintf(markup, sizeof(markup), "★ Since %s:  %s  ·  %s  ·  %s  ·  %s", date, ca, pa, rating, value);
	gtk_label_set_markup(label, markup);
}

/** The attribute on the 1-20 scale, followed by its change since the player was shortlisted. */
static void setAttributeText(GtkLabel *label, const Player *player, const ShortlistEntry *entry, const uint8_t attribute) {
	const uint8_t value = convertTo20Scale(player->attributes[attribute]);
	const int change = entry != NULL ? (int)value - (int)convertTo20Scale(entry->attributes[attribute]) : 0;

	char text[24];
	if (change == 0) {
		snprintf(text, sizeof(text), "%u", value);
		gtk_label_set_text(label, text);
		return;
	}

	char changeText[8];
	char changeMarkup[FORMATTER_DELTA_SIZE];
	snprintf(changeText, sizeof(changeText), "%+d", change);
	formatter_formatDelta(change, isHigherBetter(attribute), changeText, changeMarkup);
	char markup[FORMATTER_DELTA_SIZE + 48];
	snprintf(markup, sizeof(markup), "<span size=\"small\">%s</span> %u", changeMarkup, value);
	gtk_label_set_markup(label, markup);
}

static void setPercentage(GtkBuilder *builder, const char *id, const int16_t basisPoints, const bool inverted) {
	char buffer[FORMATTER_PERCENTAGE_SIZE];
	formatter_formatPercentage(fabsf((float)basisPoints / 100.f), inverted, buffer);
	gtk_label_set_markup(GTK_LABEL(gtk_builder_get_object(builder, id)), buffer);
}

static void clearBox(GtkBox *box) {
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(GTK_WIDGET(box))) != NULL) {
		gtk_box_remove(box, child);
	}
}

/**
 * The window outlives whatever it was opened from — a row pointing into gameContext.players, or a
 * stack temporary from the "show current player" lookup — so it keeps its own copy, which the cache
 * re-matches to the live player by uid on every publish.
 */
void ui_createPlayerInfoWindow(const Player *player) {
	if (player == NULL) {
		return;
	}

	PlayerSnapshot *snapshot = playerSnapshot_createOne(player);
	if (snapshot == NULL) {
		return;
	}

	const WindowContext context = openWindow("player-info", "window:player-info", WINDOW_PLAYER_INFO, snapshot);
	g_object_set_data_full(G_OBJECT(context.window), "player-info:player", snapshot, playerSnapshot_free);

	ui_renderPlayerInfoWindow(context);

	GtkWidget *clubLabel = GTK_WIDGET(gtk_builder_get_object(context.builder, "label:club-name"));
	gtk_widget_set_cursor_from_name(clubLabel, "pointer");
	GtkGesture *clubGesture = gtk_gesture_click_new();
	g_signal_connect(clubGesture, "released", G_CALLBACK(onClubClicked), snapshot);
	gtk_widget_add_controller(clubLabel, GTK_EVENT_CONTROLLER(clubGesture));

	g_signal_connect(gtk_builder_get_object(context.builder, "button:shortlist"), "clicked", G_CALLBACK(onShortlistClicked), snapshot);
	g_signal_connect(gtk_builder_get_object(context.builder, "button:similar"), "clicked", G_CALLBACK(onSimilarClicked), snapshot);

	GtkWidget *commonNameLabel = GTK_WIDGET(gtk_builder_get_object(context.builder, "label:common-name"));
	GtkGestureClick *clickGesture = GTK_GESTURE_CLICK(gtk_gesture_click_new());
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(clickGesture), GTK_PHASE_CAPTURE);
	g_signal_connect(clickGesture, "pressed", G_CALLBACK(onPlayerInfoClicked), commonNameLabel);
	gtk_widget_add_controller(context.window, GTK_EVENT_CONTROLLER(clickGesture));
	ui_presentWindow(context);

	// This has to come after the window has been presented
	// so the label doesn't come auto-highlighted
	gtk_label_set_selectable(GTK_LABEL(commonNameLabel), true);
}

void ui_rebindPlayerInfoWindow(const WindowContext context, const PlayerLookup *lookup) {
	playerSnapshot_refresh(context.data, lookup);
	ui_renderPlayerInfoWindow(context);
}

void ui_renderPlayerInfoWindow(WindowContext context) {
	const PlayerSnapshot *snapshot = context.data;
	if (snapshot == NULL || snapshot->count == 0) {
		return;
	}

	getSortedPositionRatings(&snapshot->players[0]);
	const Player *player = &snapshot->players[0];

	// Player name
	GtkLabel *commonNameLabel = GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(context.builder, "label:common-name")));
	if (player->commonName[0] == '\0') {
		char buffer[PERSON_FORENAME_LENGTH + PERSON_SURNAME_LENGTH + 2];
		snprintf(buffer, sizeof(buffer), "%s %s", player->forename, player->surname);
		gtk_label_set_text(commonNameLabel, buffer);
	} else {
		gtk_label_set_text(commonNameLabel, player->commonName);
	}

	// Player vitals
	{
		char buffer[8];
		snprintf(buffer, 8, "%d yrs", player->age);
		GtkLabel *ageLabel = GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(context.builder, "label:age")));
		gtk_label_set_text(ageLabel, buffer);

		snprintf(buffer, 8, "%d kg", player->weight);
		GtkLabel *weightLabel = GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(context.builder, "label:weight")));
		gtk_label_set_text(weightLabel, buffer);
		snprintf(buffer, 8, "%d cm", player->height);
		GtkLabel *heightLabel = GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(context.builder, "label:height")));
		gtk_label_set_text(heightLabel, buffer);
	}

	// Player status
	GtkWidget *isFastLearner = GTK_WIDGET(gtk_builder_get_object(context.builder, "widget:is-fast-learner"));
	gtk_widget_set_visible(isFastLearner, player->canDevelopQuickly);
	GtkWidget *isHotProspect = GTK_WIDGET(gtk_builder_get_object(context.builder, "widget:is-hot-prospect"));
	gtk_widget_set_visible(isHotProspect, player->isHotProspect);


	// Player nationalities
	GtkBox *nationalityBox = GTK_BOX(GTK_WIDGET(gtk_builder_get_object(context.builder, "label:nationality")));
	clearBox(nationalityBox);
	uint8_t nationalityIndex = 0;
	while (nationalityIndex < 4 && player->nationality[nationalityIndex] != 0xFF) {
		const Nation *nation = entities_getNation(player->nationality[nationalityIndex]);
		if (nation == NULL) {
			nationalityIndex++;
			continue;
		}

		char pathToFlag[256] = {0};
		snprintf(
			pathToFlag,
			sizeof(pathToFlag),
			RESOURCE_BASE "/assets/flags/%s.png",
			nation->code
		);
		GtkWidget *flagImage = gtk_image_new_from_resource(pathToFlag);
		gtk_box_append(nationalityBox, flagImage);
		char tooltip[MAX_NATION_STRING_LENGTH + 32];
		snprintf(tooltip, sizeof(tooltip), "%s: click to show its players", nation->name);
		gtk_widget_set_tooltip_text(flagImage, tooltip);
		gtk_widget_set_cursor_from_name(flagImage, "pointer");
		GtkGesture *flagGesture = gtk_gesture_click_new();
		g_signal_connect(
			flagGesture,
			"released",
			G_CALLBACK(onFlagClicked),
			GUINT_TO_POINTER(player->nationality[nationalityIndex])
		);
		gtk_widget_add_controller(flagImage, GTK_EVENT_CONTROLLER(flagGesture));
		nationalityIndex++;
	}

	// Club name
	GtkLabel *clubNameLabel = GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(context.builder, "label:club-name")));
	const Club *club = entities_getClub(player->clubIndex);
	if (player->clubIndex == -1) {
		gtk_label_set_text(clubNameLabel, "Free agent");
	} else {
		gtk_label_set_text(clubNameLabel, club != NULL ? club->name : "");
	}

	setContract(context.builder, player);
	setTransferStatus(context.builder, player);
	const ShortlistEntry *shortlistEntry = shortlist_find(player->uid);
	setTracking(context.builder, player, shortlistEntry);

	const PositionWeights *positionWeights = getWeightsForPosition(player->ratings[0].position);
	float max = 0;
	for (uint64_t i = 0; i < vector_length(positionWeights->weights); ++i) {
		if (positionWeights->weights[i].weight > max) {
			max = positionWeights->weights[i].weight;
		}
	}

	char widgetId[64];
	GtkLabel *label;
	GtkWidget *widget;
#define SET_ROW_TEXT_AND_HIGHLIGHT(id, attributeIndex) {																									\
		snprintf(widgetId, 64, "label:%s", id);																																\
		label = GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(context.builder, widgetId)));											\
		setAttributeText(label, player, shortlistEntry, attributeIndex);																			\
		snprintf(widgetId, 64, "row:%s", id);																																	\
		widget = GTK_WIDGET(gtk_builder_get_object(context.builder, widgetId));																\
		gtk_widget_remove_css_class(widget, "attribute-row--high");																						\
		gtk_widget_remove_css_class(widget, "attribute-row--mid");																						\
		for (size_t weightIndex = 0; weightIndex < vector_length(positionWeights->weights); ++weightIndex) {	\
			if (positionWeights->weights[weightIndex].attribute == attributeIndex) {												\
				if (positionWeights->weights[weightIndex].weight > max * 0.5f) {																\
					gtk_widget_add_css_class(widget, "attribute-row--high");																				\
				} else if (positionWeights->weights[weightIndex].weight > max * 0.15f) {												\
					gtk_widget_add_css_class(widget, "attribute-row--mid");																					\
				}																																																	\
				break;																																														\
			}																																																		\
		}																																																			\
	}

	// Ability scores
	{
		GtkLabel *caLabel = GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(context.builder, "label:ca")));
		GtkLabel *paLabel = GTK_LABEL(GTK_WIDGET(gtk_builder_get_object(context.builder, "label:pa")));
		char ability[4] = {0};
		snprintf(ability, 4, "%d", player->ca);
		gtk_label_set_label(caLabel, ability);
		snprintf(ability, 4, "%d", player->pa);
		gtk_label_set_label(paLabel, ability);
	}

	// Footedness
	GtkBox *footednessBox = GTK_BOX(GTK_WIDGET(gtk_builder_get_object(context.builder, "box:footedness")));
	clearBox(footednessBox);
	char iconBuffer[16] = {0};
	GtkWidget *leftShoe = icons_new(ICON_SHOE_LEFT, icons_footQuantise(player->attributes[ATTR_LEF]));
	snprintf(iconBuffer, 16, "Left foot: %d", player->attributes[ATTR_LEF] / 5);
	gtk_widget_set_tooltip_text(leftShoe, iconBuffer);
	gtk_box_append(footednessBox, leftShoe);
	GtkWidget *rightShoe = icons_new(ICON_SHOE_RIGHT, icons_footQuantise(player->attributes[ATTR_RIG]));
	snprintf(iconBuffer, 16, "Right foot: %d", player->attributes[ATTR_RIG] / 5);
	gtk_widget_set_tooltip_text(rightShoe, iconBuffer);
	gtk_box_append(footednessBox, rightShoe);

	GtkBox *moraleBox = GTK_BOX(GTK_WIDGET(gtk_builder_get_object(context.builder, "box:morale")));
	clearBox(moraleBox);
	GtkWidget *face = icons_new(ICON_FACE, icons_moraleQuantise(player->morale));
	snprintf(iconBuffer, 16, "Morale: %d", player->morale);
	gtk_widget_set_tooltip_text(face, iconBuffer);
	gtk_box_append(moraleBox, face);

	setPercentage(context.builder, "label:condition", (int16_t)player->condition, false);
	setPercentage(context.builder, "label:sharpness", (int16_t)player->sharpness, false);
	setPercentage(context.builder, "label:fatigue", player->fatigue, true);

	// Attributes
	GtkWidget *boxGoalkeeper = GTK_WIDGET(gtk_builder_get_object(context.builder, "box:attribute:goalkeeper"));
	GtkWidget *boxTechnical = GTK_WIDGET(gtk_builder_get_object(context.builder, "box:attribute:technical"));

	if (player->positions[0] < 12) {
		gtk_widget_set_visible(boxTechnical, true);
		gtk_widget_set_visible(boxGoalkeeper, false);

		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:corners", ATTR_COR);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:crossing", ATTR_CRO);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:dribbling", ATTR_DRI);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:finishing", ATTR_FIN);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:first-touch", ATTR_FIR);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:free-kicks", ATTR_FRE);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:heading", ATTR_HEA);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:long-shots", ATTR_LON);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:long-throws", ATTR_LTH);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:marking", ATTR_MAR);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:passing", ATTR_PAS);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:penalty-taking", ATTR_PEN);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:tackling", ATTR_TCK);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:technical:technique", ATTR_TEC);
	} else {
		gtk_widget_set_visible(boxGoalkeeper, true);
		gtk_widget_set_visible(boxTechnical, false);

		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:aerial-reach", ATTR_AER);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:command-of-area", ATTR_CMD);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:communication", ATTR_COM);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:eccentricity", ATTR_ECC);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:first-touch", ATTR_FIR);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:handling", ATTR_HAN);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:kicking", ATTR_KIC);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:one-on-ones", ATTR_ONE);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:passing", ATTR_PAS);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:punching-tendency", ATTR_TTP);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:reflexes", ATTR_REF);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:rushing-out-tendency", ATTR_TRO);
		SET_ROW_TEXT_AND_HIGHLIGHT("attribute:goalkeeper:throwing", ATTR_THR);
	}

	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:aggression", ATTR_AGG);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:anticipation", ATTR_ANT);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:bravery", ATTR_BRA);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:concentration", ATTR_CNT);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:composure", ATTR_CMP);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:decisions", ATTR_DEC);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:determination", ATTR_DET);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:flair", ATTR_FLA);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:leadership", ATTR_LDR);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:off-the-ball", ATTR_OTB);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:positioning", ATTR_POS);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:teamwork", ATTR_TEA);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:vision", ATTR_VIS);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:mental:work-rate", ATTR_WOR);

	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:physical:acceleration", ATTR_ACC);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:physical:agility", ATTR_AGI);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:physical:balance", ATTR_BAL);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:physical:jumping-reach", ATTR_JUM);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:physical:natural-fitness", ATTR_NAT);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:physical:pace", ATTR_PAC);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:physical:stamina", ATTR_STA);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:physical:strength", ATTR_STR);

	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:adaptability", ATTR_ADA);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:ambition", ATTR_AMB);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:consistency", ATTR_CON);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:controversy", ATTR_CNY);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:dirtiness", ATTR_DIR);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:important-matches", ATTR_IMP);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:injury-proneness", ATTR_INJ);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:loyalty", ATTR_LOY);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:pressure", ATTR_PRE);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:professionalism", ATTR_PRO);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:sportsmanship", ATTR_SPO);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:temperament", ATTR_TEM);
	SET_ROW_TEXT_AND_HIGHLIGHT("attribute:hidden:versatility", ATTR_VER);

	// Ratings
	{
		GtkBox *boxRoles = GTK_BOX(GTK_WIDGET(gtk_builder_get_object(context.builder, "box:top-roles")));
		clearBox(boxRoles);
		uint8_t i = 0;
		while (i < POSITION_GROUPED_COUNT && player->ratings[i].value > 0.f) {
			GtkWidget *parent = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
			gtk_box_append(boxRoles, parent);

			GtkWidget *labelContainer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			GtkWidget *roleLabel = gtk_label_new(positionGroupedNames[player->ratings[i].position]);
			gtk_label_set_xalign(GTK_LABEL(roleLabel), 0);
			gtk_widget_add_css_class(roleLabel, "heading");
			gtk_widget_set_hexpand(roleLabel, true);
			gtk_box_append(GTK_BOX(labelContainer), roleLabel);

			char valueBuffer[8] = {0};
			snprintf(valueBuffer, 8, "%.1f", player->ratings[i].value);
			GtkWidget *roleValue = gtk_label_new(valueBuffer);
			gtk_widget_add_css_class(roleValue, "heading");
			gtk_widget_add_css_class(roleValue, "accent-orange");
			gtk_widget_add_css_class(roleValue, "role-value");
			gtk_box_append(GTK_BOX(labelContainer), roleValue);

			GtkWidget *progressBar = gtk_progress_bar_new();
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressBar), player->ratings[i].value / 100.0f);
			gtk_widget_add_css_class(progressBar, "role-bar");

			gtk_box_append(GTK_BOX(parent), labelContainer);
			gtk_box_append(GTK_BOX(parent), progressBar);

			i++;
		}
	}
}
