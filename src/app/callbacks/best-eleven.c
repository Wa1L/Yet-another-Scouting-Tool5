// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "best-eleven.h"
#include "app/ui.h"


BUILDER_CALLBACK void callbacks_onShowBestEleven(void) {
	ui_createBestElevenWindow();
}

BUILDER_CALLBACK void callbacks_onShowSquadDepth(void) {
	ui_createSquadDepthWindow();
}

BUILDER_CALLBACK void callbacks_onShowShortlist(void) {
	ui_createShortlistWindow();
}

BUILDER_CALLBACK void callbacks_onShowMySquad(void) {
	ui_createMySquadWindow();
}

BUILDER_CALLBACK void callbacks_onShowSettings(void) {
	ui_createSettingsWindow();
}
