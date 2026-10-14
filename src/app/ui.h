// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "app/player-snapshot.h"
#include "app/types.h"


void ui_init(GtkApplication *app);
void ui_update(void);
void ui_updateGameVersion(void);
void ui_updateInGameDate(void);
void ui_clearFilterTags(void);
void ui_createFilterTag(const char *text, GtkEntryBuffer *buffer);
void ui_createClubFilterTag(const char *name, GtkEditable *buffer);
void ui_createNationalityFilterTag(const char *name, GtkEditable *buffer);
void ui_createPositionFilterTag(const char *name, GtkCheckButton *button);
void ui_createCheckFilterTag(const char *text, GtkCheckButton *button);
void ui_createEditableFilterTag(const char *text, GtkEditable *editable);
void ui_createDropDownFilterTag(const char *text, GtkDropDown *dropDown);
void ui_loadStylesheet(const char *fileName, guint priority);

WindowContext openWindow(const char *layoutName, const char *windowName, WindowType type, void *data);
void ui_presentWindow(WindowContext context);
void ui_createPlayerInfoWindow(const Player *player);
void ui_renderPlayerInfoWindow(WindowContext context);
void ui_rebindPlayerInfoWindow(WindowContext context, const PlayerLookup *lookup);
void ui_createBestElevenWindow(void);
void ui_renderBestElevenWindow(WindowContext context);
void ui_rebindBestElevenWindow(WindowContext context, const PlayerLookup *lookup);
void ui_createSquadDepthWindow(void);
void ui_renderSquadDepthWindow(WindowContext context);
void ui_rebindSquadDepthWindow(WindowContext context, const PlayerLookup *lookup);
void ui_createShortlistWindow(void);
void ui_renderShortlistWindow(WindowContext context);
void ui_createSimilarPlayersWindow(const Player *player);
void ui_renderSimilarPlayersWindow(WindowContext context);
void ui_createUpgradeFinderWindow(
	const PositionCode positions[FORMATION_POSITION_COUNT],
	const BestElevenRow rows[FORMATION_POSITION_COUNT],
	const PlayerSnapshot *squad,
	float minImprovement,
	uint32_t maxValue,
	uint8_t maxAge
);
void ui_renderUpgradeFinderWindow(WindowContext context);
void ui_rebindUpgradeFinderWindow(WindowContext context, const PlayerLookup *lookup);
void ui_createMySquadWindow(void);
void ui_createSettingsWindow(void);
void ui_renderSettingsWindow(WindowContext context);
void ui_rerenderSettingsClub(WindowContext context);
void ui_refreshAllWindows(void);
void ui_refreshShortlistViews(void);
void ui_presentSearchWindow(void);
void ui_rebindPlayerWindows(void);
void ui_rerenderPlayerWindows(void);

void ui_setCurrentStatus(const char *status);
