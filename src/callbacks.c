/*
 *      callbacks.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2005-2010 Enrico Tröger <enrico(dot)troeger(at)uvena(dot)de>
 *      Copyright 2006-2010 Nick Treleaven <nick(dot)treleaven(at)btinternet(dot)com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $Id$
 */

/*
 * Callbacks used by Glade. These are mainly in response to menu item and button events in the
 * main window. Callbacks not used by Glade should go elsewhere.
 */

#include "geany.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>
#include <time.h>

#include "callbacks.h"
#include "support.h"

#include "keyfile.h"
#include "document.h"
#include "documentprivate.h"
#include "filetypes.h"
#include "sciwrappers.h"
#include "editor.h"
#include "ui_utils.h"
#include "utils.h"
#include "dialogs.h"
#include "about.h"
#include "msgwindow.h"
#include "build.h"
#include "prefs.h"
#include "templates.h"
#include "sidebar.h"
#include "keybindings.h"
#include "encodings.h"
#include "search.h"
#include "main.h"
#include "symbols.h"
#include "tools.h"
#include "project.h"
#include "navqueue.h"
#include "printing.h"
#include "plugins.h"
#include "log.h"
#include "toolbar.h"
#include "pluginutils.h"


#ifdef HAVE_VTE
# include "vte.h"
#endif

#ifdef HAVE_SOCKET
# include "socket.h"
#endif



/* flag to indicate that an insert callback was triggered from the file menu,
 * so we need to store the current cursor position in editor_info.click_pos. */
static gboolean insert_callback_from_menu = FALSE;

/* represents the state at switching a notebook page(in the left treeviews widget), to not emit
 * the selection-changed signal from tv.tree_openfiles */
/*static gboolean switch_tv_notebook_page = FALSE; */


static gboolean check_no_unsaved(void)
{
	guint i;

	for (i = 0; i < documents_array->len; i++)
	{
		if (documents[i]->is_valid && documents[i]->changed)
		{
			return FALSE;
		}
	}
	return TRUE;	/* no unsaved edits */
}


/* set editor_info.click_pos to the current cursor position if insert_callback_from_menu is TRUE
 * to prevent invalid cursor positions which can cause segfaults */
static void verify_click_pos(GeanyDocument *doc)
{
	if (insert_callback_from_menu)
	{
		editor_info.click_pos = sci_get_current_position(doc->editor->sci);
		insert_callback_from_menu = FALSE;
	}
}


/* should only be called from on_exit_clicked */
static void quit_app(void)
{
	configuration_save();

	if (app->project != NULL)
		project_close(FALSE);	/* save project session files */

	document_close_all();

	main_status.quitting = TRUE;

	main_quit();
}


/* wrapper function to abort exit process if cancel button is pressed */
gboolean
on_exit_clicked                        (GtkWidget *widget, gpointer gdata)
{
	main_status.quitting = TRUE;

	if (! check_no_unsaved())
	{
		if (document_account_for_unsaved())
		{
			quit_app();
			return FALSE;
		}
	}
	else
	if (! prefs.confirm_exit ||
		dialogs_show_question_full(NULL, GTK_STOCK_QUIT, GTK_STOCK_CANCEL, NULL,
			_("Do you really want to quit?")))
	{
		quit_app();
		return FALSE;
	}

	main_status.quitting = FALSE;
	return TRUE;
}


/*
 * GUI callbacks
 */

void
on_new1_activate                       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	document_new_file(NULL, NULL, NULL);
}


void
on_save1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	gint cur_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_widgets.notebook));
	GeanyDocument *doc = document_get_current();

	if (doc != NULL && cur_page >= 0)
	{
		if (document_need_save_as(doc))
			dialogs_show_save_as();
		else
			document_save_file(doc, FALSE);
	}
}


void
on_save_as1_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	dialogs_show_save_as();
}


void
on_save_all1_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	gint i, max = gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_widgets.notebook));
	GeanyDocument *doc, *cur_doc = document_get_current();
	gint count = 0;

	for (i = 0; i < max; i++)
	{
		doc = document_get_from_page(i);
		if (! doc->changed)
			continue;
		if (document_need_save_as(doc))
		{
			/* display unnamed document */
			gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.notebook),
				document_get_notebook_page(doc));
			if (dialogs_show_save_as())
				count++;
		}
		else
		{
			if (document_save_file(doc, FALSE))
				count++;
		}
	}
	if (!count)
		return;

	ui_set_statusbar(FALSE, ngettext("%d file saved.", "%d files saved.", count), count);
	/* saving may have changed window title, sidebar for another doc, so update */
	sidebar_update_tag_list(cur_doc, TRUE);
	ui_set_window_title(cur_doc);
}


void
on_close_all1_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	document_close_all();
}


void
on_close1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	document_close(doc);
}


void
on_quit1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	on_exit_clicked(NULL, NULL);
}


void
on_file1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	gtk_widget_set_sensitive(ui_widgets.recent_files_menuitem,
						g_queue_get_length(ui_prefs.recent_queue) > 0);
#if GTK_CHECK_VERSION(2, 10, 0)
	/* hide Page setup when GTK printing is not used
	 * (on GTK < 2.10 the menu item is hidden completely) */
	ui_widget_show_hide(ui_widgets.print_page_setup,
		printing_prefs.use_gtk_printing || gtk_check_version(2, 10, 0) != NULL);
#endif
}


/* edit actions, c&p & co, from menu bar and from popup menu */
void
on_edit1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GtkWidget *item;
	GeanyDocument *doc = document_get_current();

	ui_update_menu_copy_items(doc);
	ui_update_insert_include_item(doc, 1);

	item = ui_lookup_widget(main_widgets.window, "plugin_preferences1");
#ifndef HAVE_PLUGINS
	gtk_widget_hide(item);
#else
	gtk_widget_set_sensitive(item, plugins_have_preferences());
#endif
}


void
on_undo1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	if (document_can_undo(doc))
	{
		sci_cancel(doc->editor->sci);
		document_undo(doc);
	}
}


void
on_redo1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	if (document_can_redo(doc))
	{
		sci_cancel(doc->editor->sci);
		document_redo(doc);
	}
}


void
on_cut1_activate                       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	GtkWidget *focusw = gtk_window_get_focus(GTK_WINDOW(main_widgets.window));

	if (GTK_IS_EDITABLE(focusw))
		gtk_editable_cut_clipboard(GTK_EDITABLE(focusw));
	else
	if (IS_SCINTILLA(focusw) && doc != NULL)
		sci_cut(doc->editor->sci);
	else
	if (GTK_IS_TEXT_VIEW(focusw))
	{
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(
			GTK_TEXT_VIEW(focusw));
		gtk_text_buffer_cut_clipboard(buffer, gtk_clipboard_get(GDK_NONE), TRUE);
	}
}


void
on_copy1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	GtkWidget *focusw = gtk_window_get_focus(GTK_WINDOW(main_widgets.window));

	if (GTK_IS_EDITABLE(focusw))
		gtk_editable_copy_clipboard(GTK_EDITABLE(focusw));
	else
	if (IS_SCINTILLA(focusw) && doc != NULL)
		sci_copy(doc->editor->sci);
	else
	if (GTK_IS_TEXT_VIEW(focusw))
	{
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(
			GTK_TEXT_VIEW(focusw));
		gtk_text_buffer_copy_clipboard(buffer, gtk_clipboard_get(GDK_NONE));
	}
}


void
on_paste1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	GtkWidget *focusw = gtk_window_get_focus(GTK_WINDOW(main_widgets.window));

	if (GTK_IS_EDITABLE(focusw))
		gtk_editable_paste_clipboard(GTK_EDITABLE(focusw));
	else
	if (IS_SCINTILLA(focusw) && doc != NULL)
	{
		sci_paste(doc->editor->sci);
	}
	else
	if (GTK_IS_TEXT_VIEW(focusw))
	{
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(
			GTK_TEXT_VIEW(focusw));
		gtk_text_buffer_paste_clipboard(buffer, gtk_clipboard_get(GDK_NONE), NULL,
			TRUE);
	}
}


void
on_delete1_activate                    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	GtkWidget *focusw = gtk_window_get_focus(GTK_WINDOW(main_widgets.window));

	if (GTK_IS_EDITABLE(focusw))
		gtk_editable_delete_selection(GTK_EDITABLE(focusw));
	else
	if (IS_SCINTILLA(focusw) && doc != NULL && sci_has_selection(doc->editor->sci))
		sci_clear(doc->editor->sci);
	else
	if (GTK_IS_TEXT_VIEW(focusw))
	{
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(
			GTK_TEXT_VIEW(focusw));
		gtk_text_buffer_delete_selection(buffer, TRUE, TRUE);
	}
}


void
on_preferences1_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	prefs_show_dialog();
}


/* about menu item */
void
on_info1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	about_dialog_show();
}


/* open file */
void
on_open1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	dialogs_show_open_file();
}


/* quit toolbar button */
void
on_toolbutton_quit_clicked             (GtkAction       *action,
                                        gpointer         user_data)
{
	on_exit_clicked(NULL, NULL);
}


/* reload file */
void
on_toolbutton_reload_clicked           (GtkAction       *action,
                                        gpointer         user_data)
{
	on_reload_as_activate(NULL, GINT_TO_POINTER(-1));
}


/* also used for reloading when user_data is -1 */
void
on_reload_as_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	gchar *base_name;
	gint i = GPOINTER_TO_INT(user_data);
	const gchar *charset = NULL;

	g_return_if_fail(doc != NULL);
	g_return_if_fail(doc->file_name != NULL);

	if (i >= 0)
	{
		if (i >= GEANY_ENCODINGS_MAX || encodings[i].charset == NULL)
			return;
		charset = encodings[i].charset;
	}
	else
		charset = doc->encoding;

	base_name = g_path_get_basename(doc->file_name);
	if (!doc->changed ||
		dialogs_show_question_full(NULL, _("_Reload"), GTK_STOCK_CANCEL,
		_("Any unsaved changes will be lost."),
		_("Are you sure you want to reload '%s'?"), base_name))
	{
		document_reload_file(doc, charset);
		if (charset != NULL)
			ui_update_statusbar(doc, -1);
	}
	g_free(base_name);
}


void
on_change_font1_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	dialogs_show_open_font();
}


/* new file */
void
on_toolbutton_new_clicked              (GtkAction       *action,
                                        gpointer         user_data)
{
	document_new_file(NULL, NULL, NULL);
}


/* open file */
void
on_toolbutton_open_clicked             (GtkAction       *action,
                                        gpointer         user_data)
{
	dialogs_show_open_file();
}


/* save file */
void
on_toolbutton_save_clicked             (GtkAction       *action,
                                        gpointer         user_data)
{
	on_save1_activate(NULL, user_data);
}


/* store text, clear search flags so we can use Search->Find Next/Previous */
static void setup_find_next(const gchar *text)
{
	setptr(search_data.text, g_strdup(text));
	search_data.flags = 0;
	search_data.backwards = FALSE;
	search_data.search_bar = TRUE;
}


/* search text */
void
on_toolbar_search_entry_changed(GtkAction *action, const gchar *text, gpointer user_data)
{
	GeanyDocument *doc = document_get_current();
	gboolean result;

	setup_find_next(text);
	result = document_search_bar_find(doc, search_data.text, 0, GPOINTER_TO_INT(user_data));
	if (search_data.search_bar)
		ui_set_search_entry_background(toolbar_get_widget_child_by_name("SearchEntry"), result);
}


/* search text */
void
on_toolbutton_search_clicked           (GtkAction       *action,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	gboolean result;
	GtkWidget *entry = toolbar_get_widget_child_by_name("SearchEntry");

	if (entry != NULL)
	{
		const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));

		setup_find_next(text);
		result = document_search_bar_find(doc, search_data.text, 0, FALSE);
		if (search_data.search_bar)
			ui_set_search_entry_background(entry, result);
	}
	else
		on_find1_activate(NULL, NULL);
}


/* hides toolbar from toolbar popup menu */
void
on_hide_toolbar1_activate              (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GtkWidget *tool_item = ui_lookup_widget(GTK_WIDGET(main_widgets.window), "menu_show_toolbar1");
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(tool_item), FALSE);
}


/* zoom in from menu bar and popup menu */
void
on_zoom_in1_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	static gint done = 1;

	g_return_if_fail(doc != NULL);

	if (done++ % 3 == 0)
		sci_set_line_numbers(doc->editor->sci, editor_prefs.show_linenumber_margin,
				(sci_get_zoom(doc->editor->sci) / 2));
	sci_zoom_in(doc->editor->sci);
}


/* zoom out from menu bar and popup menu */
void
on_zoom_out1_activate                   (GtkMenuItem     *menuitem,
                                         gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	if (sci_get_zoom(doc->editor->sci) == 0)
			sci_set_line_numbers(doc->editor->sci, editor_prefs.show_linenumber_margin, 0);
	sci_zoom_out(doc->editor->sci);
}


void
on_normal_size1_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	sci_zoom_off(doc->editor->sci);
	sci_set_line_numbers(doc->editor->sci, editor_prefs.show_linenumber_margin, 0);
}


/* close tab */
void
on_toolbutton_close_clicked            (GtkAction       *action,
                                        gpointer         user_data)
{
	on_close1_activate(NULL, NULL);
}


void
on_toolbutton_close_all_clicked        (GtkAction       *action,
                                        gpointer         user_data)
{
	on_close_all1_activate(NULL, NULL);
}


void
on_toolbutton_preferences_clicked      (GtkAction       *action,
                                        gpointer         user_data)
{
	on_preferences1_activate(NULL, NULL);
}


static gboolean delayed_check_disk_status(gpointer data)
{
	document_check_disk_status(data, FALSE);
	return FALSE;
}


/* Changes window-title after switching tabs and lots of other things.
 * note: using 'after' makes Scintilla redraw before the UI, appearing more responsive */
void
on_notebook1_switch_page_after         (GtkNotebook     *notebook,
                                        GtkNotebookPage *page,
                                        guint            page_num,
                                        gpointer         user_data)
{
	GeanyDocument *doc;

	if (G_UNLIKELY(main_status.opening_session_files) || G_UNLIKELY(main_status.closing_all))
		return;

	if (page_num == (guint) -1 && page != NULL)
		doc = document_find_by_sci(SCINTILLA(page));
	else
		doc = document_get_from_page(page_num);

	if (doc != NULL)
	{
		sidebar_select_openfiles_item(doc);
		ui_save_buttons_toggle(doc->changed);
		ui_set_window_title(doc);
		ui_update_statusbar(doc, -1);
		ui_update_popup_reundo_items(doc);
		ui_document_show_hide(doc); /* update the document menu */
		build_menu_update(doc);
		sidebar_update_tag_list(doc, FALSE);

		/* We delay the check to avoid weird fast, unintended switching of notebook pages when
		 * the 'file has changed' dialog is shown while the switch event is not yet completely
		 * finished. So, we check after the switch has been performed to be safe. */
		g_idle_add(delayed_check_disk_status, doc);

#ifdef HAVE_VTE
		vte_cwd((doc->real_path != NULL) ? doc->real_path : doc->file_name, FALSE);
#endif

		g_signal_emit_by_name(geany_object, "document-activate", doc);
	}
}


void
on_tv_notebook_switch_page             (GtkNotebook     *notebook,
                                        GtkNotebookPage *page,
                                        guint            page_num,
                                        gpointer         user_data)
{
	/* suppress selection changed signal when switching to the open files list */
	ignore_callback = TRUE;
}


void
on_tv_notebook_switch_page_after       (GtkNotebook     *notebook,
                                        GtkNotebookPage *page,
                                        guint            page_num,
                                        gpointer         user_data)
{
	ignore_callback = FALSE;
}


static void convert_eol(gint mode)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	sci_convert_eols(doc->editor->sci, mode);
	sci_set_eol_mode(doc->editor->sci, mode);
	ui_update_statusbar(doc, -1);
}


void
on_crlf_activate                       (GtkCheckMenuItem *menuitem,
                                        gpointer          user_data)
{
	if (ignore_callback || ! gtk_check_menu_item_get_active(menuitem))
		return;

	convert_eol(SC_EOL_CRLF);
}


void
on_lf_activate                         (GtkCheckMenuItem *menuitem,
                                        gpointer          user_data)
{
	if (ignore_callback || ! gtk_check_menu_item_get_active(menuitem))
		return;

	convert_eol(SC_EOL_LF);
}


void
on_cr_activate                         (GtkCheckMenuItem *menuitem,
                                        gpointer          user_data)
{
	if (ignore_callback || ! gtk_check_menu_item_get_active(menuitem))
		return;

	convert_eol(SC_EOL_CR);
}


void
on_replace_tabs_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	editor_replace_tabs(doc->editor);
}


gboolean
toolbar_popup_menu                     (GtkWidget *widget,
                                        GdkEventButton *event,
                                        gpointer user_data)
{
	if (event->button == 3)
	{
		gtk_menu_popup(GTK_MENU(ui_widgets.toolbar_menu), NULL, NULL, NULL, NULL, event->button, event->time);
		return TRUE;
	}
	return FALSE;
}


void on_toggle_case1_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	GeanyDocument *doc = document_get_current();
	ScintillaObject *sci;
	gchar *text;
	gboolean keep_sel = TRUE;

	g_return_if_fail(doc != NULL);

	sci = doc->editor->sci;
	if (! sci_has_selection(sci))
	{
		keybindings_send_command(GEANY_KEY_GROUP_SELECT, GEANY_KEYS_SELECT_WORD);
		keep_sel = FALSE;
	}
	else
	{
		gchar *result = NULL;
		gint cmd = SCI_LOWERCASE;
		gint text_len = sci_get_selected_text_length(sci);
		gboolean rectsel = scintilla_send_message(sci, SCI_SELECTIONISRECTANGLE, 0, 0);

		text = g_malloc(text_len + 1);
		sci_get_selected_text(sci, text);

		if (utils_str_has_upper(text))
        {
			if (rectsel)
				cmd = SCI_LOWERCASE;
			else
				result = g_utf8_strdown(text, -1);

		}
		else
		{
			if (rectsel)
				cmd = SCI_UPPERCASE;
			else
				result = g_utf8_strup(text, -1);

		}

		if (result != NULL)
		{
			sci_replace_sel(sci, result);
			g_free(result);
			if (keep_sel)
				sci_set_selection_start(sci, sci_get_current_position(sci) - text_len + 1);
		}
		else
			sci_send_command(sci, cmd);

		g_free(text);

	}
}


void
on_show_toolbar1_toggled               (GtkCheckMenuItem *checkmenuitem,
                                        gpointer         user_data)
{
	if (ignore_callback) return;

	toolbar_prefs.visible = (toolbar_prefs.visible) ? FALSE : TRUE;;
	ui_widget_show_hide(GTK_WIDGET(main_widgets.toolbar), toolbar_prefs.visible);
}


void
on_fullscreen1_toggled                 (GtkCheckMenuItem *checkmenuitem,
                                        gpointer         user_data)
{
	if (ignore_callback)
		return;

	ui_prefs.fullscreen = (ui_prefs.fullscreen) ? FALSE : TRUE;
	ui_set_fullscreen();
}


void
on_show_messages_window1_toggled       (GtkCheckMenuItem *checkmenuitem,
                                        gpointer          user_data)
{
	if (ignore_callback)
		return;

	ui_prefs.msgwindow_visible = (ui_prefs.msgwindow_visible) ? FALSE : TRUE;
	msgwin_show_hide(ui_prefs.msgwindow_visible);
}


void
on_markers_margin1_toggled             (GtkCheckMenuItem *checkmenuitem,
                                        gpointer         user_data)
{
	if (ignore_callback)
		return;

	editor_prefs.show_markers_margin = ! editor_prefs.show_markers_margin;
	ui_toggle_editor_features(GEANY_EDITOR_SHOW_MARKERS_MARGIN);
}


void
on_show_line_numbers1_toggled          (GtkCheckMenuItem *checkmenuitem,
                                        gpointer         user_data)
{
	if (ignore_callback)
		return;

	editor_prefs.show_linenumber_margin = ! editor_prefs.show_linenumber_margin;
	ui_toggle_editor_features(GEANY_EDITOR_SHOW_LINE_NUMBERS);
}


void
on_menu_show_white_space1_toggled      (GtkCheckMenuItem *checkmenuitem,
                                        gpointer         user_data)
{
	if (ignore_callback)
		return;

	editor_prefs.show_white_space = ! editor_prefs.show_white_space;
	ui_toggle_editor_features(GEANY_EDITOR_SHOW_WHITE_SPACE);
}


void
on_menu_show_line_endings1_toggled     (GtkCheckMenuItem *checkmenuitem,
                                        gpointer         user_data)
{
	if (ignore_callback)
		return;

	editor_prefs.show_line_endings = ! editor_prefs.show_line_endings;
	ui_toggle_editor_features(GEANY_EDITOR_SHOW_LINE_ENDINGS);
}


void
on_menu_show_indentation_guides1_toggled (GtkCheckMenuItem *checkmenuitem,
                                          gpointer         user_data)
{
	if (ignore_callback)
		return;

	editor_prefs.show_indent_guide = ! editor_prefs.show_indent_guide;
	ui_toggle_editor_features(GEANY_EDITOR_SHOW_INDENTATION_GUIDES);
}


void
on_line_wrapping1_toggled              (GtkCheckMenuItem *checkmenuitem,
                                        gpointer         user_data)
{
	if (! ignore_callback)
	{
		GeanyDocument *doc = document_get_current();
		g_return_if_fail(doc != NULL);

		editor_set_line_wrapping(doc->editor, ! doc->editor->line_wrapping);
	}
}


void
on_set_file_readonly1_toggled          (GtkCheckMenuItem *checkmenuitem,
                                        gpointer         user_data)
{
	if (! ignore_callback)
	{
		GeanyDocument *doc = document_get_current();
		g_return_if_fail(doc != NULL);

		doc->readonly = ! doc->readonly;
		sci_set_readonly(doc->editor->sci, doc->readonly);
		ui_update_tab_status(doc);
		ui_update_statusbar(doc, -1);
	}
}


void
on_use_auto_indentation1_toggled       (GtkCheckMenuItem *checkmenuitem,
                                        gpointer         user_data)
{
	if (! ignore_callback)
	{
		GeanyDocument *doc = document_get_current();
		g_return_if_fail(doc != NULL);

		doc->editor->auto_indent = ! doc->editor->auto_indent;
	}
}


static void find_usage(gboolean in_session)
{
	gint flags;
	gchar *search_text;
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	if (sci_has_selection(doc->editor->sci))
	{	/* take selected text if there is a selection */
		search_text = g_malloc(sci_get_selected_text_length(doc->editor->sci) + 1);
		sci_get_selected_text(doc->editor->sci, search_text);
		flags = SCFIND_MATCHCASE;
	}
	else
	{
		editor_find_current_word(doc->editor, -1,
			editor_info.current_word, GEANY_MAX_WORD_LENGTH, NULL);
		search_text = g_strdup(editor_info.current_word);
		flags = SCFIND_MATCHCASE | SCFIND_WHOLEWORD;
	}

	search_find_usage(search_text, flags, in_session);
	g_free(search_text);
}


void
on_find_document_usage1_activate       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	find_usage(FALSE);
}


void
on_find_usage1_activate                (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	find_usage(TRUE);
}


static void goto_tag(gboolean definition)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	/* update cursor pos for navigating back afterwards */
	if (!sci_has_selection(doc->editor->sci))
		sci_set_current_position(doc->editor->sci, editor_info.click_pos, FALSE);

	/* use the keybinding callback as it checks for selections as well as current word */
	if (definition)
		keybindings_send_command(GEANY_KEY_GROUP_GOTO, GEANY_KEYS_GOTO_TAGDEFINITION);
	else
		keybindings_send_command(GEANY_KEY_GROUP_GOTO, GEANY_KEYS_GOTO_TAGDECLARATION);
}


void
on_goto_tag_definition1                (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	goto_tag(TRUE);
}


void
on_goto_tag_declaration1               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	goto_tag(FALSE);
}


void
on_count_words1_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	tools_word_count();
}


void
on_show_color_chooser1_activate        (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	gchar colour[9];
	GeanyDocument *doc = document_get_current();
	gint pos;

	g_return_if_fail(doc != NULL);

	pos = sci_get_current_position(doc->editor->sci);
	editor_find_current_word(doc->editor, pos, colour, sizeof colour, GEANY_WORDCHARS"#");
	tools_color_chooser(colour);
}


void
on_toolbutton_compile_clicked          (GtkAction       *action,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_BUILD, GEANY_KEYS_BUILD_COMPILE);
}


void
on_find1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	search_show_find_dialog();
}


static void find_again(gboolean change_direction)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	if (search_data.text)
	{
		gboolean forward = ! search_data.backwards;
		gint result = document_find_text(doc, search_data.text, search_data.flags,
			change_direction ? forward : !forward, FALSE, NULL);

		if (result > -1)
			editor_display_current_line(doc->editor, 0.3F);

		if (search_data.search_bar)
			ui_set_search_entry_background(
				toolbar_get_widget_child_by_name("SearchEntry"), (result > -1));
	}
}


void
on_find_next1_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	find_again(FALSE);
}


void
on_find_previous1_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	if (search_data.flags & SCFIND_REGEXP)
		/* Can't reverse search order for a regex (find next ignores search backwards) */
		utils_beep();
	else
		find_again(TRUE);
}


void
on_find_nextsel1_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	search_find_selection(document_get_current(), FALSE);
}


void
on_find_prevsel1_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	search_find_selection(document_get_current(), TRUE);
}


void
on_replace1_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	search_show_replace_dialog();
}


void
on_find_in_files1_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	search_show_find_in_files_dialog(NULL);
}


static void get_line_and_offset_from_text(const gchar *text, gint *line_no, gint *offset)
{
	if (*text == '+' || *text == '-')
	{
		*line_no = atoi(text + 1);
		*offset = (*text == '+') ? 1 : -1;
	}
	else
	{
		*line_no = atoi(text) - 1;
		*offset = 0;
	}
}


void
on_go_to_line_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	static gchar value[16] = "";
	gchar *result;

	result = dialogs_show_input_goto_line(
		_("Go to Line"), _("Enter the line you want to go to:"), value);
	if (result != NULL)
	{
		GeanyDocument *doc = document_get_current();
		gint offset;
		gint line_no;

		g_return_if_fail(doc != NULL);

		get_line_and_offset_from_text(result, &line_no, &offset);
		if (! editor_goto_line(doc->editor, line_no, offset))
			utils_beep();
		/* remember value for future calls */
		g_snprintf(value, sizeof(value), "%s", result);

		g_free(result);
	}
}


void
on_toolbutton_goto_entry_activate(GtkAction *action, const gchar *text, gpointer user_data)
{
	GeanyDocument *doc = document_get_current();
	gint offset;
	gint line_no;

	g_return_if_fail(doc != NULL);

	get_line_and_offset_from_text(text, &line_no, &offset);
	if (! editor_goto_line(doc->editor, line_no, offset))
		utils_beep();
	else
		keybindings_send_command(GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR);
}


void
on_toolbutton_goto_clicked             (GtkAction       *action,
                                        gpointer         user_data)
{
	GtkWidget *entry = toolbar_get_widget_child_by_name("GotoEntry");

	if (entry != NULL)
	{
		const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));

		on_toolbutton_goto_entry_activate(NULL, text, NULL);
	}
	else
		on_go_to_line_activate(NULL, NULL);
}


void
on_help1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	gchar *uri;

	uri = utils_get_help_url(NULL);
	utils_open_browser(uri);
	g_free(uri);
}


void
on_help_shortcuts1_activate            (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_show_shortcuts();
}


void
on_website1_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	utils_open_browser(GEANY_HOMEPAGE);
}


void
on_comments_function_activate          (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	gchar *text;
	const gchar *cur_tag = NULL;
	gint line = -1, pos = 0;

	if (doc == NULL || doc->file_type == NULL)
	{
		ui_set_statusbar(FALSE,
			_("Please set the filetype for the current file before using this function."));
		return;
	}

	/* symbols_get_current_function returns -1 on failure, so sci_get_position_from_line
	 * returns the current position, so it should be safe */
	line = symbols_get_current_function(doc, &cur_tag);
	pos = sci_get_position_from_line(doc->editor->sci, line - 1);

	text = templates_get_template_function(doc, cur_tag);

	sci_insert_text(doc->editor->sci, pos, text);
	g_free(text);
}


void
on_comments_multiline_activate         (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();

	if (doc == NULL || doc->file_type == NULL)
	{
		ui_set_statusbar(FALSE,
			_("Please set the filetype for the current file before using this function."));
		return;
	}

	verify_click_pos(doc); /* make sure that the click_pos is valid */

	if (doc->file_type->comment_open)
		editor_insert_multiline_comment(doc->editor);
	else
		utils_beep();
}


void
on_comments_gpl_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	gchar *text;

	g_return_if_fail(doc != NULL);

	text = templates_get_template_licence(doc, GEANY_TEMPLATE_GPL);

	verify_click_pos(doc); /* make sure that the click_pos is valid */

	sci_insert_text(doc->editor->sci, editor_info.click_pos, text);
	g_free(text);
}


void
on_comments_bsd_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

	GeanyDocument *doc = document_get_current();
	gchar *text;

	g_return_if_fail(doc != NULL);

	text = templates_get_template_licence(doc, GEANY_TEMPLATE_BSD);

	verify_click_pos(doc); /* make sure that the click_pos is valid */

	sci_insert_text(doc->editor->sci, editor_info.click_pos, text);
	g_free(text);

}


void
on_comments_changelog_activate         (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	gchar *text;

	g_return_if_fail(doc != NULL);

	text = templates_get_template_changelog(doc);
	sci_insert_text(doc->editor->sci, 0, text);
	/* sets the cursor to the right position to type the changelog text,
	 * the template has 21 chars + length of name and email */
	sci_goto_pos(doc->editor->sci, 21 + strlen(template_prefs.developer) + strlen(template_prefs.mail), TRUE);

	g_free(text);
}


void
on_comments_fileheader_activate        (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	gchar *text;
	const gchar *fname;
	GeanyFiletype *ft;

	g_return_if_fail(doc != NULL);

	ft = doc->file_type;
	fname = doc->file_name;
	text = templates_get_template_fileheader(FILETYPE_ID(ft), fname);

	sci_insert_text(doc->editor->sci, 0, text);
	sci_goto_pos(doc->editor->sci, 0, FALSE);
	g_free(text);
}


void
on_insert_date_activate                (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	const gchar *format = NULL;
	gchar *time_str;

	g_return_if_fail(doc != NULL);

	/* set default value */
	if (utils_str_equal("", ui_prefs.custom_date_format))
	{
		g_free(ui_prefs.custom_date_format);
		ui_prefs.custom_date_format = g_strdup("%d.%m.%Y");
	}

	if (utils_str_equal(_("dd.mm.yyyy"), (gchar*) user_data))
		format = "%d.%m.%Y";
	else if (utils_str_equal(_("mm.dd.yyyy"), (gchar*) user_data))
		format = "%m.%d.%Y";
	else if (utils_str_equal(_("yyyy/mm/dd"), (gchar*) user_data))
		format = "%Y/%m/%d";
	else if (utils_str_equal(_("dd.mm.yyyy hh:mm:ss"), (gchar*) user_data))
		format = "%d.%m.%Y %H:%M:%S";
	else if (utils_str_equal(_("mm.dd.yyyy hh:mm:ss"), (gchar*) user_data))
		format = "%m.%d.%Y %H:%M:%S";
	else if (utils_str_equal(_("yyyy/mm/dd hh:mm:ss"), (gchar*) user_data))
		format = "%Y/%m/%d %H:%M:%S";
	else if (utils_str_equal(_("_Use Custom Date Format"), (gchar*) user_data))
		format = ui_prefs.custom_date_format;
	else
	{
		setptr(ui_prefs.custom_date_format,
			dialogs_show_input(_("Custom Date Format"),
				_("Enter here a custom date and time format. "
				"You can use any conversion specifiers which can be used with the ANSI C strftime function."),
				ui_prefs.custom_date_format));
		return;
	}

	time_str = utils_get_date_time(format, NULL);
	if (time_str != NULL)
	{
		verify_click_pos(doc); /* make sure that the click_pos is valid */

		sci_insert_text(doc->editor->sci, editor_info.click_pos, time_str);
		sci_goto_pos(doc->editor->sci, editor_info.click_pos + strlen(time_str), FALSE);
		g_free(time_str);
	}
	else
	{
		utils_beep();
		ui_set_statusbar(TRUE,
				_("Date format string could not be converted (possibly too long)."));
	}
}


void
on_insert_include_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	gint pos = -1;
	gchar *text;

	g_return_if_fail(doc != NULL);
	g_return_if_fail(user_data != NULL);

	verify_click_pos(doc); /* make sure that the click_pos is valid */

	if (utils_str_equal(user_data, "blank"))
	{
		text = g_strdup("#include \"\"\n");
		pos = editor_info.click_pos + 10;
	}
	else
	{
		text = g_strconcat("#include <", user_data, ">\n", NULL);
	}

	sci_insert_text(doc->editor->sci, editor_info.click_pos, text);
	g_free(text);
	if (pos >= 0)
		sci_goto_pos(doc->editor->sci, pos, FALSE);
}


void
on_file_properties_activate            (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	dialogs_show_file_properties(doc);
}


void
on_menu_fold_all1_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	editor_fold_all(doc->editor);
}


void
on_menu_unfold_all1_activate           (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	editor_unfold_all(doc->editor);
}


void
on_toolbutton_run_clicked              (GtkAction       *action,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_BUILD, GEANY_KEYS_BUILD_RUN);
}


void
on_menu_remove_indicators1_activate    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	editor_indicator_clear(doc->editor, GEANY_INDICATOR_ERROR);
}


void
on_print1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	printing_print_doc(doc);
}


void
on_menu_select_all1_activate           (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	sci_select_all(doc->editor->sci);
}


void
on_menu_show_sidebar1_toggled          (GtkCheckMenuItem *checkmenuitem,
                                        gpointer          user_data)
{
	if (ignore_callback)
		return;

	ui_prefs.sidebar_visible = ! ui_prefs.sidebar_visible;

	if (! interface_prefs.sidebar_openfiles_visible && ! interface_prefs.sidebar_symbol_visible)
	{
		interface_prefs.sidebar_openfiles_visible = TRUE;
		interface_prefs.sidebar_symbol_visible = TRUE;
	}

#if GTK_CHECK_VERSION(2, 14, 0)
	/* if window has input focus, set it back to the editor before toggling off */
	if (! ui_prefs.sidebar_visible &&
		gtk_container_get_focus_child(GTK_CONTAINER(main_widgets.sidebar_notebook)) != NULL)
	{
		keybindings_send_command(GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR);
	}
#endif

	ui_sidebar_show_hide();
}


void
on_menu_write_unicode_bom1_toggled     (GtkCheckMenuItem *checkmenuitem,
                                        gpointer         user_data)
{
	if (! ignore_callback)
	{
		GeanyDocument *doc = document_get_current();

		g_return_if_fail(doc != NULL);
		if (doc->readonly)
		{
			utils_beep();
			return;
		}

		document_undo_add(doc, UNDO_BOM, GINT_TO_POINTER(doc->has_bom));

		doc->has_bom = ! doc->has_bom;

		ui_update_statusbar(doc, -1);
	}
}


void
on_menu_comment_line1_activate         (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	editor_do_comment(doc->editor, -1, FALSE, FALSE);
}


void
on_menu_uncomment_line1_activate       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	editor_do_uncomment(doc->editor, -1, FALSE);
}


void
on_menu_toggle_line_commentation1_activate
                                       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	editor_do_comment_toggle(doc->editor);
}


void
on_menu_increase_indent1_activate      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	editor_indent(doc->editor, TRUE);
}


void
on_menu_decrease_indent1_activate      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	editor_indent(doc->editor, FALSE);
}


void
on_next_message1_activate              (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	if (! ui_tree_view_find_next(GTK_TREE_VIEW(msgwindow.tree_msg),
		msgwin_goto_messages_file_line))
		ui_set_statusbar(FALSE, _("No more message items."));
}


void
on_previous_message1_activate          (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	if (! ui_tree_view_find_previous(GTK_TREE_VIEW(msgwindow.tree_msg),
		msgwin_goto_messages_file_line))
		ui_set_statusbar(FALSE, _("No more message items."));
}


void
on_menu_comments_multiline_activate    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	insert_callback_from_menu = TRUE;
	on_comments_multiline_activate(menuitem, user_data);
}


void
on_menu_comments_gpl_activate          (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	insert_callback_from_menu = TRUE;
	on_comments_gpl_activate(menuitem, user_data);
}


void
on_menu_comments_bsd_activate          (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	insert_callback_from_menu = TRUE;
	on_comments_bsd_activate(menuitem, user_data);
}


void
on_menu_insert_include_activate        (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	insert_callback_from_menu = TRUE;
	on_insert_include_activate(menuitem, user_data);
}


void
on_menu_insert_date_activate           (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	insert_callback_from_menu = TRUE;
	on_insert_date_activate(menuitem, user_data);
}


void
on_project_new1_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	project_new();
}


void
on_project_open1_activate              (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	project_open();
}


void
on_project_close1_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	project_close(TRUE);
}


void
on_project_properties1_activate        (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	project_properties();
}


void
on_menu_project1_activate              (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	static GtkWidget *item_close = NULL;
	static GtkWidget *item_properties = NULL;

	if (item_close == NULL)
	{
		item_close = ui_lookup_widget(main_widgets.window, "project_close1");
		item_properties = ui_lookup_widget(main_widgets.window, "project_properties1");
	}

	gtk_widget_set_sensitive(item_close, (app->project != NULL));
	gtk_widget_set_sensitive(item_properties, (app->project != NULL));
	gtk_widget_set_sensitive(ui_widgets.recent_projects_menuitem,
						g_queue_get_length(ui_prefs.recent_projects_queue) > 0);
}


void
on_menu_open_selected_file1_activate   (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	gchar *sel = NULL;
	const gchar *wc;

#ifdef G_OS_WIN32
	wc = GEANY_WORDCHARS "./-" "\\";
#else
	wc = GEANY_WORDCHARS "./-";
#endif

	g_return_if_fail(doc != NULL);

	sel = editor_get_default_selection(doc->editor, TRUE, wc);

	if (sel != NULL)
	{
		gchar *locale_filename, *filename = NULL;

		if (g_path_is_absolute(sel))
			filename = g_strdup(sel);
		else
		{	/* relative filename, add the path of the current file */
			gchar *path;

			path = utils_get_current_file_dir_utf8();
			if (!path)
				path = g_get_current_dir();

			filename = g_build_path(G_DIR_SEPARATOR_S, path, sel, NULL);

			if (! g_file_test(filename, G_FILE_TEST_EXISTS) &&
				app->project != NULL && NZV(app->project->base_path))
			{
				/* try the project's base path */
				setptr(path, project_get_base_path());
				setptr(filename, g_build_path(G_DIR_SEPARATOR_S, path, sel, NULL));
			}
			g_free(path);
		}

		locale_filename = utils_get_locale_from_utf8(filename);
		document_open_file(locale_filename, FALSE, NULL, NULL);

		g_free(filename);
		g_free(locale_filename);
		g_free(sel);
	}
}


void
on_remove_markers1_activate            (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();
	g_return_if_fail(doc != NULL);

	sci_marker_delete_all(doc->editor->sci, 0);	/* delete the yellow tag marker */
	sci_marker_delete_all(doc->editor->sci, 1);	/* delete user markers */
	editor_indicator_clear(doc->editor, GEANY_INDICATOR_SEARCH);
}


void
on_load_tags1_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	symbols_show_load_tags_dialog();
}


void
on_context_action1_activate            (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	gchar *word, *command;
	GError *error = NULL;
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	if (sci_has_selection(doc->editor->sci))
	{	/* take selected text if there is a selection */
		word = g_malloc(sci_get_selected_text_length(doc->editor->sci) + 1);
		sci_get_selected_text(doc->editor->sci, word);
	}
	else
	{
		word = g_strdup(editor_info.current_word);
	}

	/* use the filetype specific command if available, fallback to global command otherwise */
	if (doc->file_type != NULL &&
		NZV(doc->file_type->context_action_cmd))
	{
		command = g_strdup(doc->file_type->context_action_cmd);
	}
	else
	{
		command = g_strdup(tool_prefs.context_action_cmd);
	}

	/* substitute the wildcard %s and run the command if it is non empty */
	if (NZV(command))
	{
		utils_str_replace_all(&command, "%s", word);

		if (! g_spawn_command_line_async(command, &error))
		{
			ui_set_statusbar(TRUE, "Context action command failed: %s", error->message);
			g_error_free(error);
		}
	}
	g_free(word);
	g_free(command);
}


void
on_menu_toggle_all_additional_widgets1_activate
                                        (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	static gint hide_all = -1;
	GtkCheckMenuItem *msgw = GTK_CHECK_MENU_ITEM(
		ui_lookup_widget(main_widgets.window, "menu_show_messages_window1"));
	GtkCheckMenuItem *toolbari = GTK_CHECK_MENU_ITEM(
		ui_lookup_widget(main_widgets.window, "menu_show_toolbar1"));

	/* get the initial state (necessary if Geany was closed with hide_all = TRUE) */
	if (G_UNLIKELY(hide_all == -1))
	{
		if (! gtk_check_menu_item_get_active(msgw) &&
			! interface_prefs.show_notebook_tabs &&
			! gtk_check_menu_item_get_active(toolbari))
		{
			hide_all = TRUE;
		}
		else
			hide_all = FALSE;
	}

	hide_all = ! hide_all; /* toggle */

	if (hide_all)
	{
		if (gtk_check_menu_item_get_active(msgw))
			gtk_check_menu_item_set_active(msgw, ! gtk_check_menu_item_get_active(msgw));

		interface_prefs.show_notebook_tabs = FALSE;
		gtk_notebook_set_show_tabs(GTK_NOTEBOOK(main_widgets.notebook), interface_prefs.show_notebook_tabs);

		ui_statusbar_showhide(FALSE);

		if (gtk_check_menu_item_get_active(toolbari))
			gtk_check_menu_item_set_active(toolbari, ! gtk_check_menu_item_get_active(toolbari));
	}
	else
	{

		if (! gtk_check_menu_item_get_active(msgw))
			gtk_check_menu_item_set_active(msgw, ! gtk_check_menu_item_get_active(msgw));

		interface_prefs.show_notebook_tabs = TRUE;
		gtk_notebook_set_show_tabs(GTK_NOTEBOOK(main_widgets.notebook), interface_prefs.show_notebook_tabs);

		ui_statusbar_showhide(TRUE);

		if (! gtk_check_menu_item_get_active(toolbari))
			gtk_check_menu_item_set_active(toolbari, ! gtk_check_menu_item_get_active(toolbari));
	}
}


void
on_forward_activate                    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	navqueue_go_forward();
}


void
on_back_activate                       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	navqueue_go_back();
}


gboolean on_motion_event(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	if (prefs.auto_focus && ! GTK_WIDGET_HAS_FOCUS(widget))
		gtk_widget_grab_focus(widget);

	return FALSE;
}


static void set_indent_type(GtkCheckMenuItem *menuitem, GeanyIndentType type)
{
	GeanyDocument *doc;

	if (ignore_callback || ! gtk_check_menu_item_get_active(menuitem))
		return;

	doc = document_get_current();
	g_return_if_fail(doc != NULL);

	editor_set_indent_type(doc->editor, type);
	ui_update_statusbar(doc, -1);
}


void
on_tabs1_activate                      (GtkCheckMenuItem *menuitem,
                                        gpointer          user_data)
{
	set_indent_type(menuitem, GEANY_INDENT_TYPE_TABS);
}


void
on_spaces1_activate                    (GtkCheckMenuItem *menuitem,
                                        gpointer          user_data)
{
	set_indent_type(menuitem, GEANY_INDENT_TYPE_SPACES);
}


void
on_tabs_and_spaces1_activate           (GtkCheckMenuItem *menuitem,
                                        gpointer          user_data)
{
	set_indent_type(menuitem, GEANY_INDENT_TYPE_BOTH);
}


void
on_strip_trailing_spaces1_activate     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc;

	if (ignore_callback)
		return;

	doc = document_get_current();
	g_return_if_fail(doc != NULL);

	editor_strip_trailing_spaces(doc->editor);
}


void
on_page_setup1_activate                (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
#if GTK_CHECK_VERSION(2, 10, 0)
	printing_page_setup_gtk();
#endif
}


gboolean
on_escape_key_press_event              (GtkWidget       *widget,
                                        GdkEventKey     *event,
                                        gpointer         user_data)
{
	guint state = event->state & gtk_accelerator_get_default_mod_mask();

	/* make pressing escape in the sidebar and toolbar focus the editor */
	if (event->keyval == GDK_Escape && state == 0)
	{
		keybindings_send_command(GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR);
		return TRUE;
	}
	return FALSE;
}


void
on_line_breaking1_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc;

	if (ignore_callback)
		return;

	doc = document_get_current();
	g_return_if_fail(doc != NULL);

	doc->editor->line_breaking = !doc->editor->line_breaking;
}


void
on_replace_spaces_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	editor_replace_spaces(doc->editor);
}


void
on_search1_activate                    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GtkWidget *next_message = ui_lookup_widget(main_widgets.window, "next_message1");
	GtkWidget *previous_message = ui_lookup_widget(main_widgets.window, "previous_message1");
	gboolean have_messages;

	/* enable commands if the messages window has any items */
	have_messages = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(msgwindow.store_msg),
		NULL) > 0;

	gtk_widget_set_sensitive(next_message, have_messages);
	gtk_widget_set_sensitive(previous_message, have_messages);
}


/* simple implementation (vs. close all which doesn't close documents if cancelled) */
void
on_close_other_documents1_activate     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	guint i;
	GeanyDocument *doc, *cur_doc = document_get_current();

	for (i = 0; i < documents_array->len; i++)
	{
		doc = documents[i];

		if (doc == cur_doc || ! doc->is_valid)
			continue;

		if (! document_close(doc))
			break;
	}
}


void
on_menu_reload_configuration1_activate (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	main_reload_configuration();
}


void
on_debug_messages1_activate            (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	log_show_debug_messages_dialog();
}


void
on_send_selection_to_vte1_activate     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
#ifdef HAVE_VTE
	if (vte_info.load_vte)
		vte_send_selection_to_vte();
#endif
}


gboolean on_window_state_event         (GtkWidget           *widget,
                                        GdkEventWindowState *event,
                                        gpointer             user_data)
{

	if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
	{
		static GtkWidget *menuitem = NULL;

		if (menuitem == NULL)
			menuitem = ui_lookup_widget(widget, "menu_fullscreen1");

		ignore_callback = TRUE;

		ui_prefs.fullscreen = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) ? TRUE : FALSE;
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), ui_prefs.fullscreen);

		ignore_callback = FALSE;
	}
	return FALSE;
}


void
on_customize_toolbar1_activate         (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	GtkWidget *widget;
	GtkNotebook *notebook;

	prefs_show_dialog();

	/* select the KB page */
	widget = ui_lookup_widget(ui_widgets.prefs_dialog, "vbox15");
	notebook = GTK_NOTEBOOK(ui_lookup_widget(ui_widgets.prefs_dialog, "notebook2"));

	if (notebook != NULL && widget != NULL)
		gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, widget));
}


void
on_button_customize_toolbar_clicked    (GtkButton       *button,
                                        gpointer         user_data)
{
	toolbar_configure(GTK_WINDOW(ui_widgets.prefs_dialog));
}


void
on_cut_current_line_s_1_activate       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_CLIPBOARD, GEANY_KEYS_CLIPBOARD_CUTLINE);
}


void
on_copy_current_line_s_1_activate      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_CLIPBOARD, GEANY_KEYS_CLIPBOARD_COPYLINE);
}


void
on_delete_current_line_s_1_activate    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_DELETELINE);
}


void
on_duplicate_line_or_selection1_activate
                                        (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_DUPLICATELINE);
}


void
on_select_current_line_s_1_activate    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_SELECT, GEANY_KEYS_SELECT_LINE);
}


void
on_select_current_paragraph1_activate  (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_SELECT, GEANY_KEYS_SELECT_PARAGRAPH);
}


void
on_insert_alternative_white_space1_activate
                                        (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_INSERT, GEANY_KEYS_INSERT_ALTWHITESPACE);
}


void
on_go_to_next_marker1_activate         (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_GOTO, GEANY_KEYS_GOTO_NEXTMARKER);
}


void
on_go_to_previous_marker1_activate     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_GOTO, GEANY_KEYS_GOTO_PREVIOUSMARKER);
}


void
on_reflow_lines_block1_activate        (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_FORMAT, GEANY_KEYS_FORMAT_REFLOWPARAGRAPH);
}


void
on_transpose_current_line1_activate    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_TRANSPOSELINE);
}


void
on_smart_line_indent1_activate         (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
	keybindings_send_command(GEANY_KEY_GROUP_FORMAT, GEANY_KEYS_FORMAT_AUTOINDENT);
}


void
on_plugin_preferences1_activate        (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
#ifdef HAVE_PLUGINS
	plugin_show_configure(NULL);
#endif
}


