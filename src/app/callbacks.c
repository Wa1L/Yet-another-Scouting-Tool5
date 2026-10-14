// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "callbacks.h"
#include "app/player-snapshot.h"
#include "app/player-table.h"
#include "app/shortlist.h"
#include "app/callbacks/best-eleven.h"
#include "app/callbacks/club-search.h"
#include "app/callbacks/filters.h"
#include "app/callbacks/nationality-search.h"
#include "app/callbacks/player.h"
#include "app/helpers/vector.h"
#include "core/logger.h"


extern GameContext gameContext;

void callbacks_init(void) {
	// Capture keypresses within sidebar to trigger search
	GtkEventControllerKey *controllerKey = GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
	g_signal_connect(controllerKey, "key-released", G_CALLBACK(callbacks_onFiltersKeypress), NULL);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(controllerKey), GTK_PHASE_BUBBLE);

	GtkWidget *sidebar = GTK_WIDGET(gtk_builder_get_object(gameContext.builder, "box:sidebar"));
	gtk_widget_add_controller(sidebar, GTK_EVENT_CONTROLLER(controllerKey));

	// Attach datalist callbacks
	clubSearch_init();
	nationalitySearch_init();
}

static const WindowContext *findWindowContext(const GtkWidget *window) {
	for (uint64_t i = 0; i < vector_length(gameContext.windows); i++) {
		if (gameContext.windows[i].window == window) {
			return &gameContext.windows[i];
		}
	}

	return NULL;
}

/**
 * Shortcuts that belong to one kind of window. The search window's open the other views and find
 * players; Player Info's act on the player it shows. Returns TRUE when the key was handled.
 */
static gboolean handleWindowShortcut(const WindowContext *context, const guint keyval, const gboolean primary) {
	if (context == NULL) {
		return FALSE;
	}

	const guint key = gdk_keyval_to_lower(keyval);

	if (context->type == WINDOW_PLAYER_SEARCH) {
		if (primary && key == GDK_KEY_f) {
			gtk_widget_grab_focus(GTK_WIDGET(gameContext.filterControls.name));
			return TRUE;
		}
		if (primary && key == GDK_KEY_b) {
			callbacks_onShowBestEleven();
			return TRUE;
		}
		if (primary && key == GDK_KEY_d) {
			callbacks_onShowSquadDepth();
			return TRUE;
		}
		if (primary && key == GDK_KEY_l) {
			callbacks_onShowShortlist();
			return TRUE;
		}
		if (primary && key == GDK_KEY_m) {
			callbacks_onShowMySquad();
			return TRUE;
		}
		if (primary && key == GDK_KEY_comma) {
			callbacks_onShowSettings();
			return TRUE;
		}
		if (primary && key == GDK_KEY_p) {
			callbacks_onShowCurrentPlayer();
			return TRUE;
		}
		// Enter on a result opens it, the way double-clicking does
		if (!primary && (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) && playerTable_hasFocus()) {
			return playerTable_openSelected();
		}
	} else if (context->type == WINDOW_PLAYER_INFO) {
		const PlayerSnapshot *snapshot = context->data;
		if (primary && key == GDK_KEY_s && snapshot != NULL && snapshot->count > 0) {
			shortlist_toggle(&snapshot->players[0]);
			return TRUE;
		}
	}

	return FALSE;
}

gboolean callbacks_onWindowKeypress(
	GtkEventControllerKey *controller,
	guint keyval,
	guint keycode,
	GdkModifierType state,
	gpointer window
) {
	const gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
	const gboolean meta = (state & GDK_META_MASK) != 0;
	const gboolean alt = (state & GDK_ALT_MASK) != 0;

#ifdef ARCH_MACOS
	const gboolean primary = meta;
	if (meta) {
		if (keyval == GDK_KEY_q || keyval == GDK_KEY_Q) {
			g_application_quit(G_APPLICATION(gameContext.app));
			return TRUE;
		}
		if (keyval == GDK_KEY_w || keyval == GDK_KEY_W) {
			gtk_window_close(GTK_WINDOW(window));
			return TRUE;
		}
	}
#else
	const gboolean primary = ctrl;
	if (alt && keyval == GDK_KEY_F4) {
		g_application_quit(G_APPLICATION(gameContext.app));
		return TRUE;
	}
	if (ctrl && (keyval == GDK_KEY_w || keyval == GDK_KEY_W || keyval == GDK_KEY_F4)) {
		gtk_window_close(GTK_WINDOW(window));
		return TRUE;
	}
#endif

	(void)controller;
	(void)keycode;
	(void)ctrl;
	(void)meta;
	(void)alt;

	return handleWindowShortcut(findWindowContext(window), keyval, primary);
}
