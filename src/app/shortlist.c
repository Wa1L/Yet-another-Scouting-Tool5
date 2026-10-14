// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortlist.h"
#include "app/ui.h"
#include "app/helpers/vector.h"
#include "core/logger.h"
#include "platform/platform.h"

#include <stdio.h>
#include <string.h>


#define SHORTLIST_FILE_NAME "shortlist.tsv"
#define SHORTLIST_PATH_BUFFER_SIZE 4096

extern GameContext gameContext;

static ShortlistEntry *entries = NULL;
static char shortlistPath[SHORTLIST_PATH_BUFFER_SIZE];

/**
 * One player per line, tab-separated: uid, baseline year, baseline day, CA, PA, value, attributes
 * as hex, name. The name goes last because it is the only field that can contain spaces, and it is
 * only kept so the list still reads sensibly before the cache has loaded.
 */
static void save(void) {
	if (shortlistPath[0] == '\0') {
		return;
	}

	GString *output = g_string_new("# Yet another Scouting Tool shortlist\n");
	for (uint64_t i = 0; i < vector_length(entries); ++i) {
		const ShortlistEntry *entry = &entries[i];
		g_string_append_printf(
			output,
			"%" PRIu32 "\t%u\t%u\t%u\t%u\t%" PRIu32 "\t",
			entry->uid,
			entry->since.year,
			entry->since.days,
			entry->ca,
			entry->pa,
			entry->value
		);
		for (uint8_t j = 0; j < ATTRIBUTE_COUNT; ++j) {
			g_string_append_printf(output, "%02x", entry->attributes[j]);
		}
		g_string_append_printf(output, "\t%s\n", entry->name);
	}

	GError *error = NULL;
	if (!g_file_set_contents(shortlistPath, output->str, (gssize)output->len, &error)) {
		LOG_WARN("Could not save the shortlist to '%s': %s", shortlistPath, error->message);
		g_clear_error(&error);
	}
	g_string_free(output, TRUE);
}

static bool parseLine(const char *line, ShortlistEntry *outEntry) {
	gchar **fields = g_strsplit(line, "\t", 8);
	bool isValid = g_strv_length(fields) == 8 && strlen(fields[6]) == ATTRIBUTE_COUNT * 2;

	if (isValid) {
		*outEntry = (ShortlistEntry){
			.uid = (uint32_t)g_ascii_strtoull(fields[0], NULL, 10),
			.since = {
				.year = (uint16_t)g_ascii_strtoull(fields[1], NULL, 10),
				.days = (uint16_t)g_ascii_strtoull(fields[2], NULL, 10),
			},
			.ca = (uint8_t)g_ascii_strtoull(fields[3], NULL, 10),
			.pa = (uint8_t)g_ascii_strtoull(fields[4], NULL, 10),
			.value = (uint32_t)g_ascii_strtoull(fields[5], NULL, 10),
		};
		for (uint8_t i = 0; i < ATTRIBUTE_COUNT; ++i) {
			const char hex[3] = {fields[6][i * 2], fields[6][i * 2 + 1], '\0'};
			outEntry->attributes[i] = (uint8_t)g_ascii_strtoull(hex, NULL, 16);
		}
		snprintf(outEntry->name, sizeof(outEntry->name), "%s", g_strchomp(fields[7]));
		isValid = outEntry->uid != 0;
	}

	g_strfreev(fields);

	return isValid;
}

void shortlist_init(void) {
	char directory[SHORTLIST_PATH_BUFFER_SIZE - sizeof(SHORTLIST_FILE_NAME)];
	platform_getExecutableDirectory(directory, sizeof(directory));
	if (directory[0] == '\0') {
		LOG_WARN("Could not resolve executable directory; the shortlist will not be saved");
		return;
	}
	snprintf(shortlistPath, sizeof(shortlistPath), "%s%s", directory, SHORTLIST_FILE_NAME);

	gchar *contents = NULL;
	if (!g_file_get_contents(shortlistPath, &contents, NULL, NULL)) {
		return;
	}

	gchar **lines = g_strsplit(contents, "\n", -1);
	for (guint i = 0; lines[i] != NULL; ++i) {
		if (lines[i][0] == '\0' || lines[i][0] == '#') {
			continue;
		}

		ShortlistEntry entry;
		if (parseLine(lines[i], &entry)) {
			vector_push(entries, entry);
		} else {
			LOG_WARN("Ignoring malformed shortlist line %u", i + 1);
		}
	}

	g_strfreev(lines);
	g_free(contents);

	LOG_INFO("Loaded %zu shortlisted players", vector_length(entries));
}

void shortlist_shutdown(void) {
	vector_free(entries);
}

uint64_t shortlist_count(void) {
	return vector_length(entries);
}

const ShortlistEntry *shortlist_getEntry(const uint64_t index) {
	return index < vector_length(entries) ? &entries[index] : NULL;
}

const ShortlistEntry *shortlist_find(const uint32_t uid) {
	for (uint64_t i = 0; i < vector_length(entries); ++i) {
		if (entries[i].uid == uid) {
			return &entries[i];
		}
	}

	return NULL;
}

static ShortlistEntry createEntry(const Player *player) {
	ShortlistEntry entry = {
		.since = {.days = gameContext.currentDate.days, .year = gameContext.currentDate.year},
		.uid = player->uid,
		.value = player->guideValue,
		.ca = player->ca,
		.pa = player->pa,
	};
	memcpy(entry.attributes, player->attributes, sizeof(entry.attributes));
	player_getDisplayName(player, entry.name, sizeof(entry.name));

	return entry;
}

void shortlist_add(const Player *player) {
	if (player == NULL || player->uid == 0 || shortlist_find(player->uid) != NULL) {
		return;
	}

	vector_push(entries, createEntry(player));
	save();
	ui_refreshShortlistViews();
}

void shortlist_remove(const uint32_t uid) {
	for (uint64_t i = 0; i < vector_length(entries); ++i) {
		if (entries[i].uid == uid) {
			ShortlistEntry removed;
			vector_splice(entries, i, &removed);
			save();
			ui_refreshShortlistViews();
			return;
		}
	}
}

void shortlist_toggle(const Player *player) {
	if (player == NULL) {
		return;
	}

	if (shortlist_find(player->uid) != NULL) {
		shortlist_remove(player->uid);
	} else {
		shortlist_add(player);
	}
}

/** Measures the player's progress from today instead of from when they were added. */
void shortlist_resetBaseline(const Player *player) {
	for (uint64_t i = 0; i < vector_length(entries); ++i) {
		if (entries[i].uid == player->uid) {
			entries[i] = createEntry(player);
			save();
			ui_refreshShortlistViews();
			return;
		}
	}
}

/**
 * The rating the player would have had at the baseline, under today's weights and for the role
 * they are best at today. Recomputing it, rather than storing it, keeps the comparison fair when the
 * weights in options.yaml change.
 */
float shortlist_getBaselineRating(const ShortlistEntry *entry, const Player *player) {
	Player baseline = *player;
	memcpy(baseline.attributes, entry->attributes, sizeof(baseline.attributes));
	getSortedPositionRatings(&baseline);

	for (uint8_t i = 0; i < POSITION_GROUPED_COUNT; ++i) {
		if (baseline.ratings[i].position == player->ratings[0].position) {
			return baseline.ratings[i].value;
		}
	}

	return 0.f;
}
