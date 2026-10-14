// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "options.h"
#include "app/player.h"
#include "app/player-table.h"
#include "app/types.h"
#include "app/ui.h"
#include "app/helpers/attributes.h"
#include "app/helpers/file-watcher.h"
#include "app/helpers/vector.h"
#include "core/logger.h"
#include "platform/platform.h"
#include "types/position-code-names.h"

#include <stdio.h>
#include <string.h>


#define OPTIONS_FILE_NAME "options.yaml"
#define OPTIONS_PATH_BUFFER_SIZE (4096 - 12)
#define OPTIONS_LINE_BUFFER_SIZE 512

static const char *positionGroupedCodes[POSITION_GROUPED_COUNT + 1] = {
	"GK",
	"FB",
	"CB",
	"WB",
	"DM",
	"MC",
	"W",
	"AM",
	"ST",
	"General",
};

extern GameContext gameContext;

// Resolved once at startup and reused by the watcher; empty if it could not be resolved.
static char optionsPath[OPTIONS_PATH_BUFFER_SIZE];
static FileWatcher *optionsWatcher;

static void parseOptionsFile(FILE *file);
static void watchOptionsFile(void);

typedef struct {
	Formation current;
	bool haveCurrent;
	bool positionsSeen;
	bool positionsValid;
	uint8_t positionCount;
} FormationParser;

typedef struct {
	PositionWeights current;
	bool haveCurrent;
	bool inWeights;
} RatingsParser;


static char *trimLeadingWithIndentCount(char *text, uint8_t *indentLevel) {
	while (true) {
		if (*text == ' ') {
			++*indentLevel;
			++text;
		} else if (*text == '\t') {
			*indentLevel = *indentLevel + 2;
			++text;
		} else {
			break;
		}
	}
	return text;
}

static char *trimLeading(char *text) {
	while (*text == ' ' || *text == '\t') {
		++text;
	}
	return text;
}

static void trimTrailing(char *text) {
	size_t length = strlen(text);
	while (length > 0) {
		const char c = text[length - 1];
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
			break;
		}
		text[--length] = '\0';
	}
}

static bool equalsIgnoreCase(const char *a, const char *b) {
	while (*a && *b) {
		int ca = (unsigned char)*a;
		int cb = (unsigned char)*b;
		if (ca >= 'A' && ca <= 'Z') {
			ca += 'a' - 'A';
		}
		if (cb >= 'A' && cb <= 'Z') {
			cb += 'a' - 'A';
		}
		if (ca != cb) {
			return false;
		}
		++a;
		++b;
	}
	return *a == *b;
}

static bool parseBool(const char *value, bool *out) {
	if (
		equalsIgnoreCase(value, "true") ||
		equalsIgnoreCase(value, "yes") ||
		equalsIgnoreCase(value, "1")
	) {
		*out = true;
		return true;
	}

	if (
		equalsIgnoreCase(value, "false") ||
		equalsIgnoreCase(value, "no") ||
		equalsIgnoreCase(value, "0")
	) {
		*out = false;
		return true;
	}

	return false;
}

// Position codes are matched case-insensitively ("GK" and "gk" both work).
static bool positionCodeFromString(const char *token, PositionCode *out) {
	for (int i = 0; i < POSITION_CODE_COUNT; ++i) {
		if (equalsIgnoreCase(token, positionCodeNames[i])) {
			*out = (PositionCode)i;
			return true;
		}
	}
	return false;
}

// Copies a formation name, stripping a single pair of surrounding double quotes if present.
static void copyFormationName(char *destination, const char *value) {
	const size_t length = strlen(value);
	if (length >= 2 && value[0] == '"' && value[length - 1] == '"') {
		snprintf(destination, FORMATION_NAME_LENGTH, "%.*s", (int)(length - 2), value + 1);
	} else {
		snprintf(destination, FORMATION_NAME_LENGTH, "%s", value);
	}
}

// Validates the accumulated formation and, if valid, appends it to the options.
static void formationParser_finalise(FormationParser *parser) {
	if (!parser->haveCurrent) {
		return;
	}

	Options *options = &gameContext.options;
	if (parser->current.name[0] == '\0') {
		LOG_WARN("Skipping formation with no name");
	} else if (!parser->positionsSeen || !parser->positionsValid ||
		parser->positionCount != FORMATION_POSITION_COUNT) {
		LOG_WARN(
			"Skipping formation '%s': expected %d valid positions",
			parser->current.name,
			FORMATION_POSITION_COUNT
		);
	} else {
		vector_push(options->formations, parser->current);
	}

	*parser = (FormationParser){0};
}

static void ratingsParser_finalise(RatingsParser *parser) {
	if (!parser->haveCurrent) {
		return;
	}

	Options *options = &gameContext.options;
	vector_push(options->weights, parser->current);
	*parser = (RatingsParser){0};
}

static void formationParser_begin(FormationParser *parser) {
	formationParser_finalise(parser);
	*parser = (FormationParser){0};
	parser->haveCurrent = true;
}

static void ratingsParser_begin(RatingsParser *parser) {
	ratingsParser_finalise(parser);
	*parser = (RatingsParser){0};
	parser->haveCurrent = true;
}

static void formationParser_positions(FormationParser *parser, char *value) {
	parser->positionsSeen = true;
	parser->positionsValid = true;
	parser->positionCount = 0;

	char *token = strtok(value, " \t");
	while (token) {
		PositionCode code = POSITION_CODE_GK;
		if (!positionCodeFromString(token, &code)) {
			LOG_WARN("Unknown position code '%s' in formation '%s'", token, parser->current.name);
			parser->positionsValid = false;
		} else if (parser->positionCount < FORMATION_POSITION_COUNT) {
			parser->current.positions[parser->positionCount] = code;
		}

		if (parser->positionCount < UINT8_MAX) {
			++parser->positionCount;
		}
		token = strtok(NULL, " \t");
	}
}

static void formationParser_weight(RatingsParser *parser, const char *key, const char *value) {
	uint8_t attribute;
	if (attributes_fromKey(key, &attribute)) {
		vector_push(parser->current.weights, ((RatingWeight){.attribute = attribute, .weight = strtof(value, NULL)}));
	}
}

static inline PositionGrouped readRatingsPosition(const char *value) {
	for (PositionGrouped i = 0; i < POSITION_GROUPED_COUNT; ++i) {
		if (!strcasecmp(value, positionGroupedCodes[i])) {
			return i;
		}
	}
	return POSITION_GROUPED_COUNT;
}

static void formationParser_keyValue(FormationParser *parser, const char *key, char *value) {
	if (!parser->haveCurrent) {
		LOG_WARN("Ignoring formation field '%s' outside a list item", key);
		return;
	}

	if (!strcmp(key, "name")) {
		copyFormationName(parser->current.name, value);
		return;
	}

	if (!strcmp(key, "positions")) {
		formationParser_positions(parser, value);
		return;
	}

	LOG_WARN("Unrecognised formation field '%s'", key);
}

static void ratingsParser_keyValue(RatingsParser *parser, const char *key, char *value) {
	if (!parser->haveCurrent) {
		LOG_WARN("Ignoring ratings field '%s' outside a list item", key);
		return;
	}

	if (!strcmp(key, "position")) {
		parser->current.position = readRatingsPosition(value);
		parser->inWeights = false;
		return;
	}

	if (!strcmp(key, "scale")) {
		parser->current.scale = strtof(value, NULL);
		parser->inWeights = false;
		return;
	}

	if (parser->inWeights) {
		formationParser_weight(parser, key, value);
		return;
	}

	if (!strcmp(key, "weights")) {
		parser->inWeights = true;
		return;
	}

	LOG_WARN("Unrecognised ratings field '%s'", key);
}

// Handles one indented line inside a `formations:` block.
static void formationParser_line(FormationParser *parser, char *cursor) {
	if (cursor[0] == '-') {
		formationParser_begin(parser);
		cursor = trimLeading(cursor + 1);
		if (*cursor == '\0') {
			return;
		}
	}

	char *separator = strchr(cursor, ':');
	if (!separator) {
		LOG_WARN("Ignoring malformed formation entry '%s'", cursor);
		return;
	}

	*separator = '\0';
	char *key = cursor;
	char *value = trimLeading(separator + 1);
	trimTrailing(key);
	trimTrailing(value);
	formationParser_keyValue(parser, key, value);
}


// Handles one indented line inside a `ratings:` block.
static void ratingsParser_line(RatingsParser *parser, char *cursor) {
	if (cursor[0] == '-') {
		if (!parser->inWeights) {
			ratingsParser_begin(parser);
		}
		cursor = trimLeading(cursor + 1);
		if (*cursor == '\0') {
			return;
		}
	}

	char *separator = strchr(cursor, ':');
	if (!separator) {
		LOG_WARN("Ignoring malformed ratings entry '%s'", cursor);
		return;
	}

	*separator = '\0';
	char *key = cursor;
	char *value = trimLeading(separator + 1);
	trimTrailing(key);
	trimTrailing(value);
	ratingsParser_keyValue(parser, key, value);
}

// A space-separated list of column keys, in display order. Anything left out is hidden.
static void parseColumns(const char *value) {
	Options *options = &gameContext.options;
	vector_free(options->columns);

	char buffer[OPTIONS_LINE_BUFFER_SIZE];
	snprintf(buffer, sizeof(buffer), "%s", value);

	char *token = strtok(buffer, " \t");
	while (token) {
		uint16_t column;
		if (playerTable_columnFromKey(token, &column)) {
			vector_push(options->columns, column);
		} else {
			LOG_WARN("Unknown column '%s'", token);
		}
		token = strtok(NULL, " \t");
	}

	options->columnsSet = true;
}

static void applyOption(const char *key, const char *value) {
	if (!strcmp(key, "dark-mode")) {
		bool darkMode = false;
		if (parseBool(value, &darkMode)) {
			gameContext.options.darkMode = darkMode;
		} else {
			LOG_WARN("Invalid boolean value '%s' for option '%s'", value, key);
		}
		return;
	}

	if (!strcmp(key, "columns")) {
		parseColumns(value);
		return;
	}

	if (!strcmp(key, "my-club")) {
		const size_t length = strlen(value);
		if (length >= 2 && value[0] == '"' && value[length - 1] == '"') {
			snprintf(gameContext.options.myClub, sizeof(gameContext.options.myClub), "%.*s", (int)(length - 2), value + 1);
		} else {
			snprintf(gameContext.options.myClub, sizeof(gameContext.options.myClub), "%s", value);
		}
		return;
	}

	LOG_WARN("Unrecognised option key '%s'", key);
}

static void freeWeights(void) {
	Options *options = &gameContext.options;
	for (uint64_t i = 0; i < vector_length(options->weights); ++i) {
		vector_free(options->weights[i].weights);
	}
	vector_free(options->weights);
}

static void setDefaults(void) {
	Options *options = &gameContext.options;
	freeWeights();
	vector_free(options->formations);
	vector_free(options->columns);
	memset(options, 0, sizeof(*options));
	options->darkMode = false;

	vector_push(
		options->formations,
		((Formation){
			.name = "4-4-2",
			.positions = {
			POSITION_CODE_GK,
			POSITION_CODE_DL,
			POSITION_CODE_DC,
			POSITION_CODE_DC,
			POSITION_CODE_DR,
			POSITION_CODE_ML,
			POSITION_CODE_MC,
			POSITION_CODE_MC,
			POSITION_CODE_MR,
			POSITION_CODE_ST,
			POSITION_CODE_ST,
			},
			})
	);
	vector_push(
		options->formations,
		((Formation){
			.name = "4-3-3",
			.positions = {
			POSITION_CODE_GK,
			POSITION_CODE_DL,
			POSITION_CODE_DC,
			POSITION_CODE_DC,
			POSITION_CODE_DR,
			POSITION_CODE_MC,
			POSITION_CODE_MC,
			POSITION_CODE_MC,
			POSITION_CODE_AML,
			POSITION_CODE_AMR,
			POSITION_CODE_ST,
			},
			})
	);
	vector_push(
		options->formations,
		((Formation){
			.name = "4-2-3-1",
			.positions = {
			POSITION_CODE_GK,
			POSITION_CODE_DL,
			POSITION_CODE_DC,
			POSITION_CODE_DC,
			POSITION_CODE_DR,
			POSITION_CODE_DM,
			POSITION_CODE_DM,
			POSITION_CODE_AML,
			POSITION_CODE_AMC,
			POSITION_CODE_AMR,
			POSITION_CODE_ST,
			},
			})
	);
	vector_push(
		options->formations,
		((Formation){
			.name = "4-2-4 IF",
			.positions = {
			POSITION_CODE_GK,
			POSITION_CODE_DL,
			POSITION_CODE_DC,
			POSITION_CODE_DC,
			POSITION_CODE_DR,
			POSITION_CODE_DM,
			POSITION_CODE_DM,
			POSITION_CODE_AML,
			POSITION_CODE_AMR,
			POSITION_CODE_ST,
			POSITION_CODE_ST,
			},
			})
	);

	vector_push(options->weights, ((PositionWeights){.position = POSITION_GROUPED_GK, .scale = 1.1f, .weights = NULL}));
	vector_push(
		options->weights,
		((PositionWeights){.position = POSITION_GROUPED_COUNT, .scale = 1.1f, .weights = NULL})
	);

	// Ref: https://fm-arena.com/find-comment/53835/
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_DET, .weight = 20}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_CON, .weight = 18.53f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_REF, .weight = 19.35f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_AER, .weight = 6.45f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_AGI, .weight = 7.35f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_PAC, .weight = 6.37f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_ACC, .weight = 3.38f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_INJ, .weight = -11.03f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_DIR, .weight = -3.68f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_FIR, .weight = 3.83f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_WOR, .weight = 8.7f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_JUM, .weight = 3.22f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_STA, .weight = 7.35f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_TEC, .weight = 3.53f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_FLA, .weight = 11.03f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_COM, .weight = 3.53f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_PRE, .weight = 3.38f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_PRO, .weight = 2.2f}));
	vector_push(options->weights[0].weights, ((RatingWeight){.attribute = ATTR_NAT, .weight = 2.35f}));

	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_PAC, .weight = 20}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_ACC, .weight = 19.26f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_JUM, .weight = 9.15f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_DRI, .weight = 3.04f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_BAL, .weight = 3.03f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_CON, .weight = 9.57f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_ANT, .weight = 8.51f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_DET, .weight = 9.25f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_AGI, .weight = 3.40f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_STA, .weight = 10.64f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_DIR, .weight = 6.8f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_CMP, .weight = 5.53f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_WOR, .weight = 14.15f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_PRE, .weight = 3.62f}));
	vector_push(options->weights[1].weights, ((RatingWeight){.attribute = ATTR_INJ, .weight = -4.8f}));
}

static void appendFormationsBlock(GString *output) {
	const Options *options = &gameContext.options;
	g_string_append(output, "formations:\n");
	for (uint64_t i = 0; i < vector_length(options->formations); ++i) {
		g_string_append_printf(output, "  - name: \"%s\"\n", options->formations[i].name);
		g_string_append(output, "    positions:");
		for (uint8_t j = 0; j < FORMATION_POSITION_COUNT; ++j) {
			g_string_append_printf(output, " %s", positionCodeNames[options->formations[i].positions[j]]);
		}
		g_string_append_c(output, '\n');
	}
}

static void appendRatingsBlock(GString *output) {
	const Options *options = &gameContext.options;
	g_string_append(output, "ratings:\n");
	for (uint64_t i = 0; i < vector_length(options->weights); ++i) {
		g_string_append_printf(output, "  - position: %s\n", positionGroupedCodes[options->weights[i].position]);
		g_string_append_printf(output, "    scale: %.2f\n", options->weights[i].scale);
		g_string_append(output, "    weights:\n");
		bool isFirstAttribute = true;
		for (uint64_t j = 0; j < vector_length(options->weights[i].weights); ++j) {
			const float weight = options->weights[i].weights[j].weight;
			const uint8_t attribute = options->weights[i].weights[j].attribute;
			if (weight != 0) {
				g_string_append_printf(
					output,
					isFirstAttribute ? "      - %s: %.2f\n" : "        %s: %.2f\n",
					attributes_getKey(attribute),
					weight
				);
				isFirstAttribute = false;
			}
		}
	}
}

// Writes the current in-memory defaults to a new options file so the user has one to edit.
// The output is intentionally in the same form the parser reads back, so it round-trips.
static void writeDefaultOptions(const char *path) {
	GString *output = g_string_new("# Yet another Scouting Tool options\n");
	g_string_append_printf(output, "dark-mode: %s\n", gameContext.options.darkMode ? "true" : "false");
	appendFormationsBlock(output);
	appendRatingsBlock(output);

	GError *error = NULL;
	if (g_file_set_contents(path, output->str, (gssize)output->len, &error)) {
		LOG_INFO("Created default options file '%s'", path);
	} else {
		LOG_WARN("Could not create default options file '%s'; continuing with in-memory defaults", path);
		g_clear_error(&error);
	}
	g_string_free(output, TRUE);
}

void options_init(void) {
	setDefaults();

	char directory[OPTIONS_PATH_BUFFER_SIZE];
	platform_getExecutableDirectory(directory, sizeof(directory));
	if (directory[0] == '\0') {
		LOG_WARN("Could not resolve executable directory; using default options");
		return;
	}

	snprintf(optionsPath, sizeof(optionsPath), "%s%s", directory, OPTIONS_FILE_NAME);

	FILE *file = fopen(optionsPath, "r");
	if (!file) {
		LOG_INFO("Options file '%s' not found; writing defaults", optionsPath);
		writeDefaultOptions(optionsPath);
		watchOptionsFile();
		return;
	}

	parseOptionsFile(file);
	fclose(file);

	LOG_DEBUG("Loaded options from '%s'", optionsPath);

	watchOptionsFile();
}

// Re-reads the options file from scratch, falling back to defaults for anything it no longer
// defines, then repaints every open window so new formations and weights take effect immediately.
static void reloadOptions(void *userData) {
	(void)userData;

	FILE *file = fopen(optionsPath, "r");
	if (!file) {
		LOG_WARN("Options file '%s' disappeared; keeping current options", optionsPath);
		return;
	}

	setDefaults();
	parseOptionsFile(file);
	fclose(file);

	LOG_INFO("Reloaded options from '%s'", optionsPath);

	playerTable_applyColumns();

	// Recalculate player ratings in case the option file's weights have changed
	for (uint64_t i = 0; i < gameContext.playerCount; ++i) {
		getSortedPositionRatings(&gameContext.players[i]);
	}

	ui_loadStylesheet(
		gameContext.options.darkMode ? "styles-dark.css" : "styles-light.css",
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1
	);

	ui_refreshAllWindows();
}

static void watchOptionsFile(void) {
	if (optionsWatcher) {
		return;
	}
	optionsWatcher = fileWatcher_create(optionsPath, reloadOptions, NULL);
}

/**
 * Replaces one top-level key of the options file, along with the indented lines beneath it, with
 * `block` (which must end in a newline), appending it when the file has no such key. Everything else
 * in the file, comments included, is kept as it is. The file is then reloaded straight away rather
 * than left to the watcher, so the change applies even where the file cannot be watched.
 */
static void saveBlock(const char *key, const char *block) {
	if (optionsPath[0] == '\0') {
		LOG_WARN("No options file to save '%s' to", key);
		return;
	}

	gchar *contents = NULL;
	if (!g_file_get_contents(optionsPath, &contents, NULL, NULL)) {
		contents = g_strdup("");
	}

	gchar *prefix = g_strdup_printf("%s:", key);
	GString *output = g_string_new(NULL);
	bool replaced = false;
	bool inBlock = false;
	gchar **lines = g_strsplit(contents, "\n", -1);
	for (guint i = 0; lines[i] != NULL; ++i) {
		const char *line = lines[i];
		// g_strsplit leaves an empty final element after the file's trailing newline
		if (lines[i + 1] == NULL && line[0] == '\0') {
			break;
		}

		if (inBlock) {
			// Indented and blank lines belong to the block being replaced; anything else ends it
			if (line[0] == ' ' || line[0] == '\t' || line[0] == '\0' || line[0] == '\r') {
				continue;
			}
			inBlock = false;
		}

		if (!replaced && g_str_has_prefix(line, prefix)) {
			g_string_append(output, block);
			replaced = true;
			inBlock = true;
			continue;
		}

		g_string_append(output, line);
		g_string_append_c(output, '\n');
	}
	if (!replaced) {
		g_string_append(output, block);
	}

	GError *error = NULL;
	if (g_file_set_contents(optionsPath, output->str, (gssize)output->len, &error)) {
		reloadOptions(NULL);
	} else {
		LOG_WARN("Could not save '%s' to '%s': %s", key, optionsPath, error->message);
		g_clear_error(&error);
	}

	g_strfreev(lines);
	g_free(prefix);
	g_free(contents);
	g_string_free(output, TRUE);
}

void options_saveColumns(void) {
	GString *block = g_string_new("columns:");
	for (uint64_t i = 0; i < vector_length(gameContext.options.columns); ++i) {
		g_string_append_printf(block, " %s", playerTable_columnKey(gameContext.options.columns[i]));
	}
	g_string_append_c(block, '\n');
	saveBlock("columns", block->str);
	g_string_free(block, TRUE);
}

void options_saveFormations(void) {
	GString *block = g_string_new(NULL);
	appendFormationsBlock(block);
	saveBlock("formations", block->str);
	g_string_free(block, TRUE);
}

void options_saveRatings(void) {
	GString *block = g_string_new(NULL);
	appendRatingsBlock(block);
	saveBlock("ratings", block->str);
	g_string_free(block, TRUE);
}

void options_saveMyClub(void) {
	gchar *block = g_strdup_printf("my-club: \"%s\"\n", gameContext.options.myClub);
	saveBlock("my-club", block);
	g_free(block);
}

void options_shutdown(void) {
	fileWatcher_destroy(optionsWatcher);
	optionsWatcher = NULL;
}

// Parses an already-opened options file into `gameContext.options`, overriding the defaults.
static void parseOptionsFile(FILE *file) {
	FormationParser formationParser = {0};
	RatingsParser ratingsParser = {0};
	bool inFormations = false;
	bool inRatings = false;

	char line[OPTIONS_LINE_BUFFER_SIZE];
	while (fgets(line, sizeof(line), file)) {
		const bool indented = line[0] == ' ' || line[0] == '\t';
		uint8_t indentLevel = 0;
		char *cursor = trimLeadingWithIndentCount(line, &indentLevel);
		if (inRatings && indentLevel < 6) {
			ratingsParser.inWeights = false;
		}
		trimTrailing(cursor);

		// Skip blank lines and comments without ending an open formations block.
		if (*cursor == '\0' || *cursor == '#') {
			continue;
		}

		// TODO: Change format from space-separated string to YAML array
		if (inFormations) {
			if (indented) {
				formationParser_line(&formationParser, cursor);
				continue;
			}

			// A non-indented line ends the block; fall through to handle it as a top-level key.
			formationParser_finalise(&formationParser);
			inFormations = false;
		}

		if (inRatings) {
			if (indented) {
				ratingsParser_line(&ratingsParser, cursor);
				continue;
			}

			// A non-indented line ends the block; fall through to handle it as a top-level key.
			ratingsParser_finalise(&ratingsParser);
			inRatings = false;
		}

		char *separator = strchr(cursor, ':');
		if (!separator) {
			LOG_WARN("Ignoring malformed options line '%s'", cursor);
			continue;
		}

		*separator = '\0';
		char *key = cursor;
		char *value = trimLeading(separator + 1);
		trimTrailing(key);
		trimTrailing(value);

		if (*key == '\0') {
			continue;
		}

		if (!strcmp(key, "formations")) {
			vector_free(gameContext.options.formations);
			formationParser = (FormationParser){0};
			inFormations = true;
			if (*value != '\0') {
				LOG_WARN("Ignoring inline value for 'formations'");
			}
			continue;
		}

		if (!strcmp(key, "ratings")) {
			freeWeights();
			inRatings = true;
			if (*value != '\0') {
				LOG_WARN("Ignoring inline value for 'ratings'");
			}
			continue;
		}

		applyOption(key, value);
	}

	formationParser_finalise(&formationParser);
	ratingsParser_finalise(&ratingsParser);
}
