// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/builder-callback.h"

#include <gtk/gtk.h>


BUILDER_CALLBACK void callbacks_onFiltersClear(void);
BUILDER_CALLBACK void callbacks_onFilterRun(void);
BUILDER_CALLBACK void callbacks_onPositionToggled(GtkCheckButton *button, gpointer data);
BUILDER_CALLBACK void callbacks_onFilterDropDownChanged(GObject *object, GParamSpec *pspec, gpointer data);
BUILDER_CALLBACK void callbacks_onNameSearchChanged(GtkSearchEntry *entry, gpointer data);
BUILDER_CALLBACK gboolean callbacks_onFiltersKeypress(
	GtkEventControllerKey *controller,
	guint keyval,
	guint keycode,
	GdkModifierType state,
	gpointer data
);
void callbacks_updateFilterTags(void);
