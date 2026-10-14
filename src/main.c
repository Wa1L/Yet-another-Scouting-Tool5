// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "app/cache.h"
#include "app/callbacks.h"
#include "app/config.h"
#include "app/injury-names.h"
#include "app/maths.h"
#include "app/options.h"
#include "app/player-table.h"
#include "app/shortlist.h"
#include "app/ui.h"
#include "app/helpers/formatter.h"
#include "app/helpers/game.h"
#include "app/helpers/icons.h"
#include "app/helpers/vector-shared-pointer.h"
#include "app/helpers/vector.h"
#include "core/logger.h"
#include "platform/platform.h"


ProcessContext processContext = {0};
GameContext gameContext = {.gameKey = 1};

static void activate(GtkApplication *app);
static gboolean update(gpointer userData);
static void handleDisconnect(void);
static void handleConnect(void);
static inline void updateWhileConnected(void);
static inline void updateWhileDisconnected(void);
static guint cacheTimeoutId;
static gboolean scheduleCacheRun(gpointer data);

int main(const int argc, char **argv) {
	logger_init();

	LOG_INFO("App started");

	GtkApplication *app = gtk_application_new("com.philarmstead.yast", G_APPLICATION_DEFAULT_FLAGS);
	gameContext.app = app;
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	const int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);

	options_shutdown();
	shortlist_shutdown();
	injuryNames_destroy();
	logger_shutdown();

	return status;
}

static void activate(GtkApplication *app) {
	vector_reserve(gameContext.windows, 10);

	// GtkApplication has already added the bundled resource icon theme by this point, so every
	// window created below picks the application icon up by name.
	gtk_window_set_default_icon_name("com.philarmstead.yast");

	options_init();
	shortlist_init();
	formatter_init();
	icons_init();
	ui_init(app);
	callbacks_init();
	playerTable_init();

	g_timeout_add(1000, update, NULL);
	update(NULL);
}

static gboolean update(gpointer userData) {
	(void)userData;

#ifndef MOCKS_MODE
	if (processContext.handle != NULL) {
		updateWhileConnected();
	} else {
		updateWhileDisconnected();
	}
#else
	updateWhileConnected();
#endif
	return G_SOURCE_CONTINUE;
}


static inline void updateWhileConnected(void) {
	// Get the current date/time
	const DateTime dateTime = game_getDateTime(&processContext);

	// Bail if the date is invalid and assume we're no longer connected
	if (dateTime.days == 0 || dateTime.year == 0) {
		LOG_INFO("Disconnecting because the date is blank");
		handleDisconnect();
		return;
	}

	// Cache the new date if it's different from the last one we saw
	if (
		dateTime.days != gameContext.currentDate.days ||
		dateTime.year != gameContext.currentDate.year ||
		dateTime.time != gameContext.currentDate.time
	) {
		LOG_INFO("Date/time change");
		gameContext.currentDate = dateTime;
		ui_updateInGameDate();
		// This is a hack to force invalidate the cache (TODO improve this)
		gameContext.gameKey = 1;
	}

	// Update the game version if it's different from the last one we saw
	char versionBuffer[GAME_STATUS_STRING_BUFFER_SIZE] = {0};
	game_getVersion(&processContext, versionBuffer, GAME_STATUS_STRING_BUFFER_SIZE);
	if (strncmp(versionBuffer, gameContext.gameVersion, GAME_STATUS_STRING_BUFFER_SIZE) != 0) {
		strncpy(gameContext.gameVersion, versionBuffer, GAME_STATUS_STRING_BUFFER_SIZE);
		LOG_INFO("Game Version: %s", versionBuffer);

		ui_updateGameVersion();
	}

	GameKeyStatus gameKeyStatus;
	const uint64_t gameKey = game_getKey(&processContext, &gameKeyStatus);
	if (gameKey != gameContext.gameKey) {
		gameContext.gameKey = gameKey;

		if (gameKeyStatus == GAME_KEY_FOUND && gameContext.currentDate.year > 1970) {
			// The clubs and nations are kept until the replacements land, so the search widgets
			// keep working through the second or more this rebuild takes.
			cache_clear();
			ui_setCurrentStatus("Caching data");

			if (cacheTimeoutId != 0) {
				g_source_remove(cacheTimeoutId);
			}

			cacheTimeoutId = g_timeout_add(1000, scheduleCacheRun, NULL);
		} else {
			// No rebuild is coming, so nothing stale is worth holding on to.
			cache_reset();
			ui_setCurrentStatus("Cannot read save data");
		}
	}
}

static inline void updateWhileDisconnected(void) {
	cache_reset();
	platform_openProcess(&processContext);

	if (processContext.handle != NULL) {
		handleConnect();
	} else {
		ui_setCurrentStatus("Cannot find running process 'fm.exe'");
	}
}


static void handleDisconnect(void) {
	processContext.handle = NULL;

	gameContext.gameKey = 1; // Setting this to 0 means we can't tell when it's NULL in game
	gameContext.gameVersion[0] = '\0';
	gameContext.currentDate = (DateTime){0};

	cache_reset();

	update(NULL);
}

static void handleConnect(void) {
	LOG_INFO(
		"Process opened with PID: %u (base module address of %p)",
		processContext.pid,
		(void*)processContext.moduleBaseAddress
	);
	update(NULL);
}

static gboolean scheduleCacheRun(gpointer data) {
	(void)data;

	cacheTimeoutId = 0;
	cache_run();

	return G_SOURCE_REMOVE;
}
