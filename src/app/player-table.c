// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "player-table.h"
#include "app/entities.h"
#include "app/injury-names.h"
#include "app/options.h"
#include "app/player.h"
#include "app/ui.h"
#include "app/helpers/attributes.h"
#include "app/helpers/date.h"
#include "app/helpers/formatter.h"
#include "app/helpers/vector-shared-pointer.h"
#include "app/helpers/vector.h"
#include "core/logger.h"
#include "platform/platform.h"
#include "types/position-code-names.h"

#include <glib.h>

#include "maths.h"


typedef enum {
	COLUMN_NATIONALITIES,
	COLUMN_NAME,
	COLUMN_AGE,
	COLUMN_POSITIONS,
	COLUMN_FOOT,
	COLUMN_CA,
	COLUMN_PA,
	COLUMN_CA_PA_DELTA,
	COLUMN_STATUS,
	COLUMN_RATING,
	COLUMN_VALUE,
	COLUMN_VALUE_FOR_MONEY,
	COLUMN_WAGE,
	COLUMN_CONTRACT_END,
	COLUMN_CLUB,
	COLUMN_REPUTATION_HOME,
	COLUMN_REPUTATION_CURRENT,
	COLUMN_REPUTATION_WORLD,
	COLUMN_BUILTIN_COUNT,
	// One column per attribute follows, at COLUMN_ATTRIBUTE_FIRST + ATTR_*
	COLUMN_ATTRIBUTE_FIRST = COLUMN_BUILTIN_COUNT,
	COLUMN_COUNT = COLUMN_ATTRIBUTE_FIRST + ATTRIBUTE_COUNT,
} Columns;

typedef struct {
	const char *key; // As written in options.yaml
	const char *title;
} ColumnDefinition;

// Indexed by Columns; attribute columns take their key and title from the attribute itself
static const ColumnDefinition builtinColumns[COLUMN_BUILTIN_COUNT] = {
	[COLUMN_NATIONALITIES] = {"nationality", ""},
	[COLUMN_NAME] = {"name", "Name"},
	[COLUMN_AGE] = {"age", "Age"},
	[COLUMN_POSITIONS] = {"positions", "Positions"},
	[COLUMN_FOOT] = {"foot", "Foot"},
	[COLUMN_CA] = {"ca", "CA"},
	[COLUMN_PA] = {"pa", "PA"},
	[COLUMN_CA_PA_DELTA] = {"diff", "Diff"},
	[COLUMN_STATUS] = {"status", "Status"},
	[COLUMN_RATING] = {"rating", "Rating"},
	[COLUMN_VALUE] = {"value", "Value"},
	[COLUMN_VALUE_FOR_MONEY] = {"value-for-money", "Rtg/£M"},
	[COLUMN_WAGE] = {"wage", "Wage p/w"},
	[COLUMN_CONTRACT_END] = {"contract", "Contract ends"},
	[COLUMN_CLUB] = {"club", "Club"},
	[COLUMN_REPUTATION_HOME] = {"home-rep", "Home rep"},
	[COLUMN_REPUTATION_CURRENT] = {"current-rep", "Current rep"},
	[COLUMN_REPUTATION_WORLD] = {"world-rep", "World rep"},
};

// Contracts ending within this many months are highlighted
#define CONTRACT_EXPIRING_MONTHS 6

typedef struct {
	GtkColumnView *table;
	GtkSingleSelection *selection;
	GtkColumnViewColumn *columns[COLUMN_COUNT];
	GtkCheckButton *columnToggles[COLUMN_COUNT];
	double savedScrollValue;
	gulong scrollGuardId;
} PlayerTableContext;

static PlayerTableContext context = {0};

G_DEFINE_TYPE(SearchPlayerRow, search_player_row, G_TYPE_OBJECT)

static void search_player_row_finalize(GObject *object) {
	G_OBJECT_CLASS(search_player_row_parent_class)->finalize(object);
}

enum {
	PROP_PLAYER = 1,
	PROP_SURNAME,
	PROP_AGE,
	PROP_CA,
	PROP_PA,
	PROP_DIFF,
	PROP_RATING,
	N_PROPS
};


static GParamSpec *properties[N_PROPS];

static void search_player_row_set_property(
	GObject *object,
	const guint propertyId,
	const GValue *value,
	GParamSpec *pspec
) {
	SearchPlayerRow *self = SEARCH_PLAYER_ROW(object);
	if (propertyId == PROP_PLAYER) {
		self->player = (Player*)g_value_get_pointer(value);
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propertyId, pspec);
	}
}

static void search_player_row_get_property(GObject *object, const guint propertyId, GValue *value, GParamSpec *pspec) {
	const SearchPlayerRow *self = SEARCH_PLAYER_ROW(object);
	if (propertyId == PROP_PLAYER) {
		g_value_set_pointer(value, self->player);
	} else if (propertyId == PROP_SURNAME) {
		g_value_set_string(value, self->player->surname);
	} else if (propertyId == PROP_AGE) {
		g_value_set_uint(value, self->player->age);
	} else if (propertyId == PROP_CA) {
		g_value_set_uint(value, self->player->ca);
	} else if (propertyId == PROP_PA) {
		g_value_set_uint(value, self->player->pa);
	} else if (propertyId == PROP_DIFF) {
		g_value_set_uint(value, self->player->pa - self->player->ca);
	} else if (propertyId == PROP_RATING) {
		g_value_set_float(value, self->player->ratings[0].value);
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propertyId, pspec);
	}
}

static void search_player_row_init(SearchPlayerRow *self) {
	// Don't do anything here — let properties handle initialization
	(void)self;
}

static void search_player_row_class_init(SearchPlayerRowClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->set_property = search_player_row_set_property;
	object_class->get_property = search_player_row_get_property;
	object_class->finalize = search_player_row_finalize;

	properties[PROP_PLAYER] = g_param_spec_pointer(
		"player",
		"Player",
		"Player data",
		G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
	);
	properties[PROP_SURNAME] = g_param_spec_string(
		"surname",
		"Surname",
		"Player surname",
		NULL,
		G_PARAM_READABLE
	);
	properties[PROP_AGE] = g_param_spec_uint(
		"age",
		"Age",
		"Player age",
		0,
		100,
		0,
		G_PARAM_READABLE
	);
	properties[PROP_CA] = g_param_spec_uint(
		"ca",
		"CA",
		"Player CA",
		0,
		200,
		0,
		G_PARAM_READABLE
	);
	properties[PROP_PA] = g_param_spec_uint(
		"pa",
		"PA",
		"Player PA",
		0,
		200,
		0,
		G_PARAM_READABLE
	);
	properties[PROP_DIFF] = g_param_spec_uint(
		"diff",
		"Diff",
		"Player PA - CA",
		0,
		200,
		0,
		G_PARAM_READABLE
	);
	properties[PROP_RATING] = g_param_spec_float(
		"rating",
		"Rating",
		"Player's best rating",
		0,
		200,
		0,
		G_PARAM_READABLE
	);

	g_object_class_install_properties(object_class, N_PROPS, properties);
}

static void onRowClicked(
	const GtkGestureClick *gesture,
	const uint8_t clickCount,
	const double x,
	const double y,
	gpointer data
) {
	(void)gesture;
	(void)x;
	(void)y;

	GtkListItem *listItem = GTK_LIST_ITEM(data);
	const SearchPlayerRow *row = gtk_list_item_get_item(listItem);

	if (row != NULL && row->player != NULL && clickCount == 2) {
		ui_createPlayerInfoWindow(row->player);
	}
}

static void bindClickHandler(GtkWidget *widget, GtkListItem *item) {
	GtkGestureClick *gesture = GTK_GESTURE_CLICK(gtk_gesture_click_new());
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(gesture));
	g_signal_connect(gesture, "pressed", G_CALLBACK(onRowClicked), item);
}

static void setupTextLabel(const GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data) {
	(void)factory;
	(void)data;

	GtkWidget *label = gtk_label_new("");
	gtk_list_item_set_child(item, label);

	bindClickHandler(label, item);
}

static void setupBox(const GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data) {
	(void)factory;
	(void)data;

	GtkWidget *box = gtk_box_new(0, 2);
	gtk_list_item_set_child(item, box);

	bindClickHandler(box, item);
}

/**
 * The colourised rating is Pango markup, and text attributes take precedence over CSS, so a
 * selected row cannot recolour it. Render it plain whenever the row is selected instead.
 */
static void applyRating(GtkLabel *label, const float rating, const bool selected) {
	// Shared by both formatters below, so it has to satisfy the larger of the two.
	char buffer[FORMATTER_RATING_SIZE];

	if (selected) {
		formatter_formatRatingPlain(rating, buffer);
		gtk_label_set_text(label, buffer);
	} else {
		formatter_formatRating(rating, buffer);
		gtk_label_set_markup(label, buffer);
	}
}

static void onRatingSelectionChanged(GtkListItem *item, GParamSpec *pspec, gpointer data) {
	(void)pspec;
	(void)data;

	GtkWidget *label = gtk_list_item_get_child(item);
	const SearchPlayerRow *row = gtk_list_item_get_item(item);

	if (label != NULL && row != NULL && row->player != NULL) {
		applyRating(GTK_LABEL(label), row->player->ratings[0].value, gtk_list_item_get_selected(item));
	}
}

/**
 * Connected at setup rather than bind so the handler lives exactly as long as the recycled list
 * item, with no per-bind connect/disconnect churn.
 */
static void setupRatingLabel(const GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data) {
	setupTextLabel(factory, item, data);
	g_signal_connect(item, "notify::selected", G_CALLBACK(onRatingSelectionChanged), NULL);
}

static void printNumeric(GtkWidget *label, const int64_t value) {
	gchar buffer[FORMATTER_NUMBER_SIZE];
	g_snprintf(buffer, sizeof(buffer), "%" PRId64, value);
	if (value > 999 || value < -999) {
		formatter_printNumber(buffer);
	}
	gtk_label_set_text(GTK_LABEL(label), buffer);
}

static void printCurrency(GtkWidget *label, const uint64_t value) {
	gchar buffer[FORMATTER_NUMBER_SIZE];
	g_snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
	formatter_printCurrency(buffer);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_label_set_text(GTK_LABEL(label), buffer);
}

static GString *formatPlayerPositions(const uint8_t *positions) {
	GString *str = g_string_new(NULL);
	bool hasPrevious = false;
	bool sameRow = false;

	if (positions[POSITION_CODE_GK] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		g_string_append(str, positionCodeNames[POSITION_CODE_GK]);
		hasPrevious = true;
	}

	if (positions[POSITION_CODE_DL] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (hasPrevious) {
			g_string_append(str, ", ");
		}
		hasPrevious = true;
		sameRow = true;
		g_string_append(str, positionCodeNames[POSITION_CODE_DL]);
	}

	if (positions[POSITION_CODE_DR] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (sameRow) {
			sameRow = false;
			g_string_append(str, "/R");
		} else if (hasPrevious) {
			g_string_append(str, ", ");
		} else {
			g_string_append(str, positionCodeNames[POSITION_CODE_DR]);
		}
		hasPrevious = true;
	}

	if (positions[POSITION_CODE_DC] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (hasPrevious) {
			g_string_append(str, ", ");
		}
		g_string_append(str, positionCodeNames[POSITION_CODE_DC]);
		hasPrevious = true;
	}


	if (positions[POSITION_CODE_WBL] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (hasPrevious) {
			g_string_append(str, ", ");
		}
		hasPrevious = true;
		sameRow = true;
		g_string_append(str, positionCodeNames[POSITION_CODE_WBL]);
	}

	if (positions[POSITION_CODE_WBR] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (sameRow) {
			sameRow = false;
			g_string_append(str, "/R");
		} else {
			if (hasPrevious) {
				g_string_append(str, ", ");
			}

			g_string_append(str, positionCodeNames[POSITION_CODE_WBR]);
		}
		hasPrevious = true;
	}

	if (positions[POSITION_CODE_DM] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (hasPrevious) {
			g_string_append(str, ", ");
		}
		hasPrevious = true;
		g_string_append(str, positionCodeNames[POSITION_CODE_DM]);
	}

	sameRow = false;

	if (positions[POSITION_CODE_ML] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (hasPrevious) {
			g_string_append(str, ", ");
		}
		hasPrevious = true;
		sameRow = true;
		g_string_append(str, positionCodeNames[POSITION_CODE_ML]);
	}

	if (positions[POSITION_CODE_MR] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (sameRow) {
			g_string_append(str, "/R");
		} else {
			if (hasPrevious) {
				g_string_append(str, ", ");
			}

			g_string_append(str, positionCodeNames[POSITION_CODE_MR]);
		}
		hasPrevious = true;
	}

	sameRow = false;

	if (positions[POSITION_CODE_MC] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (hasPrevious) {
			g_string_append(str, ", ");
		}
		hasPrevious = true;
		g_string_append(str, positionCodeNames[POSITION_CODE_MC]);
	}

	if (positions[POSITION_CODE_AML] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (hasPrevious) {
			g_string_append(str, ", ");
		}
		hasPrevious = true;
		sameRow = true;
		g_string_append(str, positionCodeNames[POSITION_CODE_AML]);
	}

	if (positions[POSITION_CODE_AMR] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (sameRow) {
			g_string_append(str, "/R");
		} else {
			if (hasPrevious) {
				g_string_append(str, ", ");
			}

			g_string_append(str, positionCodeNames[POSITION_CODE_AMR]);
		}
		hasPrevious = true;
	}

	sameRow = false;

	if (positions[POSITION_CODE_AMC] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (hasPrevious) {
			g_string_append(str, ", ");
		}
		hasPrevious = true;
		g_string_append(str, positionCodeNames[POSITION_CODE_AMC]);
	}

	if (positions[POSITION_CODE_ST] >= MINIMUM_POSITIONAL_PROFICIENCY) {
		if (hasPrevious) {
			g_string_append(str, ", ");
		}
		hasPrevious = true;
		g_string_append(str, positionCodeNames[POSITION_CODE_ST]);
	}

	return str;
}

/** Rating points per £1M of guide value. Values under £100K are floored so they do not dominate. */
static float getValueForMoney(const Player *player) {
	const float value = player->guideValue > 100000 ? (float)player->guideValue : 100000.f;
	return player->ratings[0].value / value * 1e6f;
}

static void bindCellValue(const GtkSignalListItemFactory *factory, GtkListItem *item, const uint16_t columnId) {
	(void)factory;

	GtkWidget *label = gtk_list_item_get_child(GTK_LIST_ITEM(item));
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
	const SearchPlayerRow *row = gtk_list_item_get_item(GTK_LIST_ITEM(item));

	if (row == NULL || row->player == NULL) {
		return;
	}

	const Player *player = row->player;
	gtk_widget_remove_css_class(label, "contract-expiring");

	if (columnId >= COLUMN_ATTRIBUTE_FIRST) {
		printNumeric(label, convertTo20Scale(player->attributes[columnId - COLUMN_ATTRIBUTE_FIRST]));
		return;
	}

	switch (columnId) {
		case COLUMN_NAME: {
			char buffer[PLAYER_DISPLAY_NAME_LENGTH];
			player_getDisplayName(player, buffer, sizeof(buffer));
			gtk_label_set_text(GTK_LABEL(label), buffer);
			gtk_label_set_xalign(GTK_LABEL(label), 0);
			break;
		}
		case COLUMN_FOOT:
			gtk_label_set_text(GTK_LABEL(label), player_getFootName(player_getFoot(player)));
			gtk_label_set_xalign(GTK_LABEL(label), 0);
			break;
		case COLUMN_AGE:
			printNumeric(label, player->age);
			break;
		case COLUMN_POSITIONS: {
			GString *str = formatPlayerPositions(row->player->positions);
			gtk_label_set_text(GTK_LABEL(label), str->str);
			gtk_label_set_xalign(GTK_LABEL(label), 0);
			g_string_free(str, TRUE);
			break;
		}
		case COLUMN_CA:
			printNumeric(label, player->ca);
			break;
		case COLUMN_PA:
			printNumeric(label, player->pa);
			break;
		case COLUMN_CA_PA_DELTA:
			printNumeric(label, player->pa - player->ca);
			break;
		case COLUMN_RATING:
			applyRating(GTK_LABEL(label), player->ratings[0].value, gtk_list_item_get_selected(item));
			break;
		case COLUMN_VALUE:
			printCurrency(label, player->guideValue);
			break;
		case COLUMN_VALUE_FOR_MONEY: {
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "%.1f", getValueForMoney(player));
			gtk_label_set_text(GTK_LABEL(label), buffer);
			break;
		}
		case COLUMN_WAGE:
			if (player->clubIndex == -1) {
				gtk_label_set_text(GTK_LABEL(label), "");
			} else {
				printCurrency(label, player->weeklyWage);
			}
			break;
		case COLUMN_CONTRACT_END:
			if (player->clubIndex == -1 || player->contractEnd.year == 0) {
				gtk_label_set_text(GTK_LABEL(label), "");
			} else {
				char buffer[DATE_FORMAT_SIZE];
				date_format(player->contractEnd, buffer, sizeof(buffer));
				gtk_label_set_text(GTK_LABEL(label), buffer);
				if (date_monthsUntil(player->contractEnd) <= CONTRACT_EXPIRING_MONTHS) {
					gtk_widget_add_css_class(label, "contract-expiring");
				}
			}
			gtk_label_set_xalign(GTK_LABEL(label), 0);
			break;
		case COLUMN_CLUB: {
			const Club *club = entities_getClub(player->clubIndex);
			gtk_label_set_text(GTK_LABEL(label), club != NULL ? club->shortName : "");
			gtk_label_set_xalign(GTK_LABEL(label), 0);
			break;
		}
		case COLUMN_REPUTATION_HOME:
			printNumeric(label, player->homeReputation);
			break;
		case COLUMN_REPUTATION_CURRENT:
			printNumeric(label, player->currentReputation);
			break;
		case COLUMN_REPUTATION_WORLD:
			printNumeric(label, player->worldReputation);
			break;
		default:
			break;
	}
}

static void bindStatusValue(const GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data) {
	(void)factory;
	(void)data;

	GtkWidget *box = gtk_list_item_get_child(GTK_LIST_ITEM(item));
	const SearchPlayerRow *row = gtk_list_item_get_item(GTK_LIST_ITEM(item));

	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(GTK_WIDGET(box))) != NULL) {
		gtk_box_remove(GTK_BOX(box), child);
	}

	if (row == NULL || row->player == NULL) {
		return;
	}

	if (row->player->injury.duration > 0) {
		GtkWidget *label = gtk_label_new("🚑");
		gtk_box_append(GTK_BOX(box), label);
		char buffer[128] = {0};
		snprintf(buffer, 128, "Injured: %s", injuryNames_get(row->player->injury.nameIndex));
		gtk_widget_set_tooltip_text(label, buffer);
	}

	if (row->player->canDevelopQuickly) {
		GtkWidget *label = gtk_label_new("🧠");
		gtk_box_append(GTK_BOX(box), label);
		gtk_widget_set_tooltip_text(label, "Can develop quickly");
	}
	if (row->player->isHotProspect) {
		GtkWidget *label = gtk_label_new("🔥");
		gtk_box_append(GTK_BOX(box), label);
		gtk_widget_set_tooltip_text(label, "A hot prospect");
	}
}

static void bindNationalitiesValue(const GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data) {
	(void)factory;
	(void)data;

	GtkWidget *box = gtk_list_item_get_child(GTK_LIST_ITEM(item));
	const SearchPlayerRow *row = gtk_list_item_get_item(GTK_LIST_ITEM(item));

	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(GTK_WIDGET(box))) != NULL) {
		gtk_box_remove(GTK_BOX(box), child);
	}

	if (row == NULL || row->player == NULL) {
		return;
	}

	uint8_t nationalityIndex = 0;
	while (nationalityIndex < 4 && row->player->nationality[nationalityIndex] != 0xFF) {
		const Nation *nation = entities_getNation(row->player->nationality[nationalityIndex]);
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
		gtk_box_append(GTK_BOX(box), flagImage);
		gtk_widget_set_tooltip_text(flagImage, nation->name);
		nationalityIndex++;
	}
}

static gboolean restoreScroll(gpointer data) {
	GtkAdjustment *adj = data;
	const double max = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
	gtk_adjustment_set_value(adj, CLAMP(context.savedScrollValue, 0, max));
	return G_SOURCE_REMOVE;
}

// One-shot correction: the sort makes GTK scroll the focused row into view, and
// that happens on an indeterminate later frame — so instead of guessing when,
// we react. The first time the scroll value changes after a sort, we snap it
// back to the saved position and immediately disconnect, so normal user
// scrolling afterwards is untouched.
static void onScrollGuard(GtkAdjustment *adjustment, gpointer data) {
	(void)data;

	if (context.scrollGuardId != 0) {
		g_signal_handler_disconnect(adjustment, context.scrollGuardId);
		context.scrollGuardId = 0;
	}

	g_idle_add_full(GDK_PRIORITY_REDRAW - 1, restoreScroll, adjustment, NULL);
}

// Runs when the user clicks a column header to change the sort. This fires
// before the GtkSortListModel reorders (we connect first), so we snapshot the
// current scroll position here and arm a one-shot guard that undoes GTK's
// follow-the-focused-row scroll whenever it lands.
static void onSorterChanged(GtkSorter *sorter, const GtkSorterChange change, gpointer data) {
	(void)sorter;
	(void)change;
	(void)data;

	if (context.table == NULL) {
		return;
	}

	GtkAdjustment *adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(context.table));
	if (adjustment == NULL) {
		return;
	}

	context.savedScrollValue = gtk_adjustment_get_value(adjustment);

	if (context.scrollGuardId == 0) {
		context.scrollGuardId = g_signal_connect(
			adjustment,
			"value-changed",
			G_CALLBACK(onScrollGuard),
			NULL
		);
	}
}

static GtkColumnViewColumn *createColumn(const char *title, const uint16_t columnType, GtkSorter *sorter) {
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	if (columnType == COLUMN_NATIONALITIES) {
		g_signal_connect(factory, "setup", G_CALLBACK(setupBox), NULL);
		g_signal_connect(factory, "bind", G_CALLBACK(bindNationalitiesValue), NULL);
	} else if (columnType == COLUMN_STATUS) {
		g_signal_connect(factory, "setup", G_CALLBACK(setupBox), NULL);
		g_signal_connect(factory, "bind", G_CALLBACK(bindStatusValue), NULL);
	} else {
		g_signal_connect(
			factory,
			"setup",
			G_CALLBACK(columnType == COLUMN_RATING ? setupRatingLabel : setupTextLabel),
			NULL
		);
		g_signal_connect(factory, "bind", G_CALLBACK(bindCellValue), GUINT_TO_POINTER(columnType));
	}

	GtkColumnViewColumn *column = gtk_column_view_column_new(title, factory);

	// Attach sorter if provided
	if (sorter != NULL) {
		gtk_column_view_column_set_sorter(column, sorter);
	}

	return column;
}

static double getSortValue(const Player *player, const uint16_t columnId) {
	if (columnId >= COLUMN_ATTRIBUTE_FIRST) {
		return player->attributes[columnId - COLUMN_ATTRIBUTE_FIRST];
	}

	switch (columnId) {
		case COLUMN_FOOT: return player_getFoot(player);
		case COLUMN_VALUE: return player->guideValue;
		case COLUMN_VALUE_FOR_MONEY: return getValueForMoney(player);
		case COLUMN_WAGE: return player->clubIndex == -1 ? 0 : player->weeklyWage;
		// No contract sorts after every real date
		case COLUMN_CONTRACT_END:
			return player->clubIndex == -1 || player->contractEnd.year == 0
				? INT32_MAX
				: date_daysUntil(player->contractEnd);
		case COLUMN_REPUTATION_HOME: return player->homeReputation;
		case COLUMN_REPUTATION_CURRENT: return player->currentReputation;
		case COLUMN_REPUTATION_WORLD: return player->worldReputation;
		default: return 0;
	}
}

static int compareRows(gconstpointer a, gconstpointer b, gpointer data) {
	const Player *playerA = ((const SearchPlayerRow*)a)->player;
	const Player *playerB = ((const SearchPlayerRow*)b)->player;
	const uint16_t columnId = (uint16_t)GPOINTER_TO_UINT(data);

	if (columnId == COLUMN_CLUB) {
		const Club *clubA = entities_getClub(playerA->clubIndex);
		const Club *clubB = entities_getClub(playerB->clubIndex);
		const int order = g_utf8_collate(clubA != NULL ? clubA->shortName : "", clubB != NULL ? clubB->shortName : "");
		return order < 0 ? GTK_ORDERING_SMALLER : order > 0 ? GTK_ORDERING_LARGER : GTK_ORDERING_EQUAL;
	}

	const double valueA = getSortValue(playerA, columnId);
	const double valueB = getSortValue(playerB, columnId);
	return valueA < valueB ? GTK_ORDERING_SMALLER : valueA > valueB ? GTK_ORDERING_LARGER : GTK_ORDERING_EQUAL;
}

static GtkSorter *createCustomSorter(const uint16_t columnId) {
	return GTK_SORTER(gtk_custom_sorter_new(compareRows, GUINT_TO_POINTER(columnId), NULL));
}

static const char *getColumnTitle(const uint16_t columnId) {
	return columnId < COLUMN_BUILTIN_COUNT
		? builtinColumns[columnId].title
		: attributes_getName((uint8_t)(columnId - COLUMN_ATTRIBUTE_FIRST));
}

const char *playerTable_columnKey(const uint16_t columnId) {
	if (columnId < COLUMN_BUILTIN_COUNT) {
		return builtinColumns[columnId].key;
	}

	return columnId < COLUMN_COUNT ? attributes_getKey((uint8_t)(columnId - COLUMN_ATTRIBUTE_FIRST)) : "";
}

// Built-in columns are matched exactly, attributes by their options.yaml name (e.g. "Pace")
bool playerTable_columnFromKey(const char *key, uint16_t *outColumnId) {
	for (uint16_t i = 0; i < COLUMN_BUILTIN_COUNT; ++i) {
		if (!strcmp(key, builtinColumns[i].key)) {
			*outColumnId = i;
			return true;
		}
	}

	uint8_t attribute;
	if (attributes_fromKey(key, &attribute)) {
		*outColumnId = (uint16_t)(COLUMN_ATTRIBUTE_FIRST + attribute);
		return true;
	}

	return false;
}

/**
 * Orders and shows the columns options.yaml lists, hiding the rest. Without a `columns` key every
 * built-in column shows in its default order and the attribute columns stay hidden.
 */
void playerTable_applyColumns(void) {
	if (context.table == NULL) {
		return;
	}

	const Options *options = &gameContext.options;
	bool isVisible[COLUMN_COUNT] = {0};
	bool isPlaced[COLUMN_COUNT] = {0};
	guint position = 0;

	if (options->columnsSet) {
		for (uint64_t i = 0; i < vector_length(options->columns); ++i) {
			const uint16_t columnId = options->columns[i];
			if (columnId < COLUMN_COUNT && !isPlaced[columnId]) {
				gtk_column_view_insert_column(context.table, position++, context.columns[columnId]);
				isVisible[columnId] = true;
				isPlaced[columnId] = true;
			}
		}
	} else {
		for (uint16_t i = 0; i < COLUMN_BUILTIN_COUNT; ++i) {
			isVisible[i] = true;
		}
	}

	for (uint16_t i = 0; i < COLUMN_COUNT; ++i) {
		if (!isPlaced[i]) {
			gtk_column_view_insert_column(context.table, position++, context.columns[i]);
		}
		gtk_column_view_column_set_visible(context.columns[i], isVisible[i]);

		// Kept in step without re-entering onColumnToggled, which would write the options file again
		GtkCheckButton *toggle = context.columnToggles[i];
		if (toggle != NULL && gtk_check_button_get_active(toggle) != isVisible[i]) {
			g_object_set_data(G_OBJECT(toggle), "syncing", GINT_TO_POINTER(1));
			gtk_check_button_set_active(toggle, isVisible[i]);
			g_object_set_data(G_OBJECT(toggle), "syncing", NULL);
		}
	}
}

/**
 * Showing a column appends it to the end; hiding one removes it. Either way the visible columns, in
 * their current order, become the `columns` option and are written back to options.yaml.
 */
static void onColumnToggled(GtkCheckButton *button, gpointer data) {
	if (g_object_get_data(G_OBJECT(button), "syncing") != NULL) {
		return;
	}

	const uint16_t toggledId = (uint16_t)GPOINTER_TO_UINT(data);
	const bool isActive = gtk_check_button_get_active(button);

	uint16_t *columns = NULL;
	GListModel *viewColumns = gtk_column_view_get_columns(context.table);
	const guint count = g_list_model_get_n_items(viewColumns);
	for (guint i = 0; i < count; ++i) {
		GtkColumnViewColumn *column = g_list_model_get_item(viewColumns, i);
		const uint16_t columnId = (uint16_t)GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(column), "column-id"));
		if (gtk_column_view_column_get_visible(column) && columnId != toggledId) {
			vector_push(columns, columnId);
		}
		g_object_unref(column);
	}
	if (isActive) {
		vector_push(columns, toggledId);
	}

	vector_free(gameContext.options.columns);
	gameContext.options.columns = columns;
	gameContext.options.columnsSet = true;

	playerTable_applyColumns();
	options_saveColumns();
}

static void appendColumnToggle(GtkBox *box, const uint16_t columnId) {
	const char *title = columnId == COLUMN_NATIONALITIES ? "Nationality" : getColumnTitle(columnId);
	GtkWidget *toggle = gtk_check_button_new_with_label(title);
	g_signal_connect(toggle, "toggled", G_CALLBACK(onColumnToggled), GUINT_TO_POINTER(columnId));
	gtk_box_append(box, toggle);
	context.columnToggles[columnId] = GTK_CHECK_BUTTON(toggle);
}

static void appendPopoverHeading(GtkBox *box, const char *text) {
	GtkWidget *heading = gtk_label_new(text);
	gtk_widget_set_halign(heading, GTK_ALIGN_START);
	gtk_widget_add_css_class(heading, "section-title");
	gtk_widget_set_margin_top(heading, 6);
	gtk_box_append(box, heading);
}

static void createColumnsPopover(void) {
	GtkMenuButton *menuButton = GTK_MENU_BUTTON(gtk_builder_get_object(gameContext.builder, "menubutton:columns"));

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_add_css_class(box, "columns-menu");
	appendPopoverHeading(GTK_BOX(box), "COLUMNS");
	for (uint16_t i = 0; i < COLUMN_BUILTIN_COUNT; ++i) {
		appendColumnToggle(GTK_BOX(box), i);
	}

	appendPopoverHeading(GTK_BOX(box), "ATTRIBUTES");
	uint8_t sortedAttributes[ATTRIBUTE_COUNT];
	attributes_getSortedByName(sortedAttributes);
	for (uint8_t i = 0; i < ATTRIBUTE_COUNT; ++i) {
		appendColumnToggle(GTK_BOX(box), (uint16_t)(COLUMN_ATTRIBUTE_FIRST + sortedAttributes[i]));
	}

	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 420);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), true);
	gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), true);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);

	GtkWidget *popover = gtk_popover_new();
	gtk_popover_set_child(GTK_POPOVER(popover), scroll);
	gtk_menu_button_set_popover(menuButton, popover);
}

/** Opens Player Info for the selected row. Returns false when nothing is selected. */
bool playerTable_openSelected(void) {
	if (context.selection == NULL) {
		return false;
	}

	const SearchPlayerRow *row = gtk_single_selection_get_selected_item(context.selection);
	if (row == NULL || row->player == NULL) {
		return false;
	}

	ui_createPlayerInfoWindow(row->player);
	return true;
}

bool playerTable_hasFocus(void) {
	if (context.table == NULL) {
		return false;
	}

	GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(context.table));
	GtkWidget *focus = root != NULL ? gtk_root_get_focus(root) : NULL;
	return focus != NULL && (focus == GTK_WIDGET(context.table) || gtk_widget_is_ancestor(focus, GTK_WIDGET(context.table)));
}

void playerTable_init(void) {
	GtkSorter *nameSorter = GTK_SORTER(
		gtk_string_sorter_new(
			gtk_property_expression_new(SEARCH_TYPE_PLAYER_ROW, NULL, "surname")
		)
	);
	GtkSorter *ageSorter = GTK_SORTER(
		gtk_numeric_sorter_new(
			gtk_property_expression_new(SEARCH_TYPE_PLAYER_ROW, NULL, "age")
		)
	);
	GtkSorter *caSorter = GTK_SORTER(
		gtk_numeric_sorter_new(
			gtk_property_expression_new(SEARCH_TYPE_PLAYER_ROW, NULL, "ca")
		)
	);
	GtkSorter *paSorter = GTK_SORTER(
		gtk_numeric_sorter_new(
			gtk_property_expression_new(SEARCH_TYPE_PLAYER_ROW, NULL, "pa")
		)
	);
	GtkSorter *diffSorter = GTK_SORTER(
		gtk_numeric_sorter_new(
			gtk_property_expression_new(SEARCH_TYPE_PLAYER_ROW, NULL, "diff")
		)
	);
	GtkSorter *ratingSorter = GTK_SORTER(
		gtk_numeric_sorter_new(
			gtk_property_expression_new(SEARCH_TYPE_PLAYER_ROW, NULL, "rating")
		)
	);

	GtkColumnView *table = GTK_COLUMN_VIEW(GTK_WIDGET(gtk_builder_get_object(gameContext.builder, "table:player-list")));
	context.table = table;

	GtkSorter *sorters[COLUMN_COUNT] = {
		[COLUMN_NAME] = nameSorter,
		[COLUMN_AGE] = ageSorter,
		[COLUMN_CA] = caSorter,
		[COLUMN_PA] = paSorter,
		[COLUMN_CA_PA_DELTA] = diffSorter,
		[COLUMN_RATING] = ratingSorter,
	};
	for (uint16_t i = 0; i < COLUMN_COUNT; ++i) {
		const bool isUnsortable = i == COLUMN_NATIONALITIES || i == COLUMN_POSITIONS || i == COLUMN_STATUS;
		if (sorters[i] == NULL && !isUnsortable) {
			sorters[i] = createCustomSorter(i);
		}

		GtkColumnViewColumn *column = createColumn(getColumnTitle(i), i, sorters[i]);
		g_object_set_data(G_OBJECT(column), "column-id", GUINT_TO_POINTER(i));
		gtk_column_view_append_column(table, column);
		context.columns[i] = column;
	}

	createColumnsPopover();
	playerTable_applyColumns();

	context.selection = NULL;

	GtkSorter *columnViewSorter = gtk_column_view_get_sorter(context.table);
	if (columnViewSorter != NULL) {
		g_signal_connect(columnViewSorter, "changed", G_CALLBACK(onSorterChanged), NULL);
	}

	gameContext.searchResults = sharedPointer_new(NULL);
	playerTable_clear();

	gtk_column_view_sort_by_column(context.table, context.columns[COLUMN_RATING], GTK_SORT_DESCENDING);
}

static SearchPlayerRow *search_player_row_new(Player *player) {
	return g_object_new(SEARCH_TYPE_PLAYER_ROW, "player", player, NULL);
}

void playerTable_clear(void) {
	if (gameContext.searchResults != NULL && vector_length(gameContext.searchResults->data) > 0) {
		sharedPointer_unref(gameContext.searchResults);
		gameContext.searchResults = sharedPointer_new(NULL);
	}
	playerTable_populate();
}

void playerTable_populate(void) {
#ifdef DEBUG
	const int64_t timeStart = platform_getMicroseconds();
#endif

	GtkAdjustment *adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(context.table));
	gtk_adjustment_set_value(adjustment, 0);

	const uint32_t *playerIds = gameContext.searchResults->data;
	const size_t playerCount = vector_length(playerIds);

	GtkWidget *clearAll = GTK_WIDGET(gtk_builder_get_object(gameContext.builder, "button:clear-all"));
	gtk_widget_set_visible(clearAll, playerCount > 0);

	GListStore *store = g_list_store_new(SEARCH_TYPE_PLAYER_ROW);
	for (size_t i = 0; i < playerCount; ++i) {
		// Defensive: results taken before a cache swap can outrun the current buffer.
		if (playerIds[i] >= gameContext.playerCount) {
			continue;
		}

		Player *player = &gameContext.players[playerIds[i]];
		SearchPlayerRow *row = search_player_row_new(player);
		g_list_store_append(store, row);
		g_object_unref(row);
	}

	GtkSorter *sorter = g_object_ref(gtk_column_view_get_sorter(context.table));
	GtkSortListModel *sortModel = gtk_sort_list_model_new(G_LIST_MODEL(store), sorter);
	GtkSingleSelection *newSelection = gtk_single_selection_new(G_LIST_MODEL(sortModel));
	gtk_single_selection_set_autoselect(newSelection, false);

	GtkSingleSelection *oldSelection = context.selection;

	gtk_column_view_set_model(context.table, GTK_SELECTION_MODEL(newSelection));

	if (oldSelection != NULL)
		g_object_unref(oldSelection);

	context.selection = newSelection;

	GtkLabel *resultsCountLabel = GTK_LABEL(
		GTK_WIDGET(gtk_builder_get_object(gameContext.builder, "label:results-count"))
	);
	char resultCountString[32] = {0};
	char formattedResults[FORMATTER_NUMBER_SIZE] = {0};
	snprintf(formattedResults, sizeof(formattedResults), "%zu", playerCount);
	formatter_printNumber(formattedResults);
	snprintf(
		resultCountString,
		sizeof(resultCountString),
		playerCount == 1 ? "1 result" : "%s results",
		formattedResults
	);
	gtk_label_set_text(resultsCountLabel, resultCountString);

	LOG_DEBUG("Rendered %zu players in %" PRId64 " microseconds", playerCount, platform_getMicroseconds() - timeStart);
}
