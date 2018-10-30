/* Hey EMACS -*- linux-c -*- */
/*
 *
 * Copyright (C) 2002-2003 Romain Lievin <roms@tilp.info>
 * Released under the terms of the GNU GPL v2.0.
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include "lkc.h"
#include "images.c"

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <gdk/gdkkeysyms.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

//#define DEBUG

enum {
	SINGLE_VIEW, SPLIT_VIEW, FULL_VIEW
};

enum {
	OPT_NORMAL, OPT_ALL, OPT_PROMPT
};

static gint view_mode = FULL_VIEW;
static gboolean show_name = TRUE;
static gboolean show_range = TRUE;
static gboolean show_value = TRUE;
static gboolean resizeable = FALSE;
static int opt_mode = OPT_NORMAL;

GtkWidget *main_wnd = NULL;
GtkWidget *tree1_w = NULL;	// left  frame
GtkWidget *tree2_w = NULL;	// right frame
GtkWidget *text_w = NULL;
GtkWidget *hpaned = NULL;
GtkWidget *vpaned = NULL;
GtkWidget *back_btn = NULL;
GtkWidget *save_btn = NULL;
GtkWidget *save_menu_item = NULL;

GtkTextTag *tag1, *tag2;
GdkColor color;

GtkTreeStore *tree1, *tree2, *tree;
GtkTreeModel *model1, *model2;
static GtkTreeIter *parents[256];
static gint indent;

static struct menu *current; // current node for SINGLE view
static struct menu *browsed; // browsed node for SPLIT view

enum {
	COL_OPTION, COL_NAME, COL_NO, COL_MOD, COL_YES, COL_VALUE,
	COL_MENU, COL_COLOR, COL_EDIT, COL_PIXBUF,
	COL_PIXVIS, COL_BTNVIS, COL_BTNACT, COL_BTNINC, COL_BTNRAD,
	COL_NUMBER
};

static void display_list(void);
static void display_tree(struct menu *menu);
static void display_tree_part(void);
static void update_tree(struct menu *src, GtkTreeIter * dst);
static void set_node(GtkTreeIter * node, struct menu *menu, gchar ** row);
static gchar **fill_row(struct menu *menu);
static void conf_changed(void);

/* Helping/Debugging Functions */

const char *dbg_sym_flags(int val)
{
	static char buf[256];

	bzero(buf, 256);

	if (val & SYMBOL_CONST)
		strcat(buf, "const/");
	if (val & SYMBOL_CHECK)
		strcat(buf, "check/");
	if (val & SYMBOL_CHOICE)
		strcat(buf, "choice/");
	if (val & SYMBOL_CHOICEVAL)
		strcat(buf, "choiceval/");
	if (val & SYMBOL_VALID)
		strcat(buf, "valid/");
	if (val & SYMBOL_OPTIONAL)
		strcat(buf, "optional/");
	if (val & SYMBOL_WRITE)
		strcat(buf, "write/");
	if (val & SYMBOL_CHANGED)
		strcat(buf, "changed/");
	if (val & SYMBOL_NO_WRITE)
		strcat(buf, "no_write/");

	buf[strlen(buf) - 1] = '\0';

	return buf;
}

void replace_button_icon(GladeXML * xml, GdkDrawable * window,
			 GtkStyle * style, gchar * btn_name, gchar ** xpm)
{
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	GtkToolButton *button;
	GtkWidget *image;

	pixmap = gdk_pixmap_create_from_xpm_d(window, &mask,
					      &style->bg[GTK_STATE_NORMAL],
					      xpm);

	button = GTK_TOOL_BUTTON(glade_xml_get_widget(xml, btn_name));
	image = gtk_image_new_from_pixmap(pixmap, mask);
	gtk_widget_show(image);
	gtk_tool_button_set_icon_widget(button, image);
}

/* Main Window Initialization */
void init_main_window(const gchar * glade_file)
{
	GladeXML *xml;
	GtkWidget *widget;
	GtkTextBuffer *txtbuf;
	GtkStyle *style;

	xml = glade_xml_new(glade_file, "window1", NULL);
	if (!xml)
		g_error("GUI loading failed !\n");
	glade_xml_signal_autoconnect(xml);

	main_wnd = glade_xml_get_widget(xml, "window1");
	hpaned = glade_xml_get_widget(xml, "hpaned1");
	vpaned = glade_xml_get_widget(xml, "vpaned1");
	tree1_w = glade_xml_get_widget(xml, "treeview1");
	tree2_w = glade_xml_get_widget(xml, "treeview2");
	text_w = glade_xml_get_widget(xml, "textview3");

	back_btn = glade_xml_get_widget(xml, "button1");
	gtk_widget_set_sensitive(back_btn, FALSE);

	widget = glade_xml_get_widget(xml, "show_name1");
	gtk_check_menu_item_set_active((GtkCheckMenuItem *) widget,
				       show_name);

	widget = glade_xml_get_widget(xml, "show_range1");
	gtk_check_menu_item_set_active((GtkCheckMenuItem *) widget,
				       show_range);

	widget = glade_xml_get_widget(xml, "show_data1");
	gtk_check_menu_item_set_active((GtkCheckMenuItem *) widget,
				       show_value);

	save_btn = glade_xml_get_widget(xml, "button3");
	save_menu_item = glade_xml_get_widget(xml, "save1");
	conf_set_changed_callback(conf_changed);

	style = gtk_widget_get_style(main_wnd);
	widget = glade_xml_get_widget(xml, "toolbar1");

	replace_button_icon(xml, main_wnd->window, style,
			    "button4", (gchar **) xpm_single_view);
	replace_button_icon(xml, main_wnd->window, style,
			    "button5", (gchar **) xpm_split_view);
	replace_button_icon(xml, main_wnd->window, style,
			    "button6", (gchar **) xpm_tree_view);

	txtbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_w));
	tag1 = gtk_text_buffer_create_tag(txtbuf, "mytag1",
					  "foreground", "red",
					  "weight", PANGO_WEIGHT_BOLD,
					  NULL);
	tag2 = gtk_text_buffer_create_tag(txtbuf, "mytag2",
					  /*"style", PANGO_STYLE_OBLIQUE, */
					  NULL);

	gtk_window_set_title(GTK_WINDOW(main_wnd), rootmenu.prompt->text);

	gtk_widget_show(main_wnd);
}

void init_tree_model(void)
{
	gint i;

	tree = tree2 = gtk_tree_store_new(COL_NUMBER,
					  G_TYPE_STRING, G_TYPE_STRING,
					  G_TYPE_STRING, G_TYPE_STRING,
					  G_TYPE_STRING, G_TYPE_STRING,
					  G_TYPE_POINTER, GDK_TYPE_COLOR,
					  G_TYPE_BOOLEAN, GDK_TYPE_PIXBUF,
					  G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
					  G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
					  G_TYPE_BOOLEAN);
	model2 = GTK_TREE_MODEL(tree2);

	for (parents[0] = NULL, i = 1; i < 256; i++)
		parents[i] = (GtkTreeIter *) g_malloc(sizeof(GtkTreeIter));

	tree1 = gtk_tree_store_new(COL_NUMBER,
				   G_TYPE_STRING, G_TYPE_STRING,
				   G_TYPE_STRING, G_TYPE_STRING,
				   G_TYPE_STRING, G_TYPE_STRING,
				   G_TYPE_POINTER, GDK_TYPE_COLOR,
				   G_TYPE_BOOLEAN, GDK_TYPE_PIXBUF,
				   G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
				   G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
				   G_TYPE_BOOLEAN);
	model1 = GTK_TREE_MODEL(tree1);
}

void init_left_tree(void)
{
	GtkTreeView *view = GTK_TREE_VIEW(tree1_w);
	GtkCellRenderer *renderer;
	GtkTreeSelection *sel;
	GtkTreeViewColumn *column;

	gtk_tree_view_set_model(view, model1);
	gtk_tree_view_set_headers_visible(view, TRUE);
	gtk_tree_view_set_rules_hint(view, TRUE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_append_column(view, column);
	gtk_tree_view_column_set_title(column, "Options");

	renderer = gtk_cell_renderer_toggle_new();
	gtk_tree_view_column_pack_start(GTK_TREE_VIEW_COLUMN(column),
					renderer, FALSE);
	gtk_tree_view_column_set_attributes(GTK_TREE_VIEW_COLUMN(column),
					    renderer,
					    "active", COL_BTNACT,
					    "inconsistent", COL_BTNINC,
					    "visible", COL_BTNVIS,
					    "radio", COL_BTNRAD, NULL);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(GTK_TREE_VIEW_COLUMN(column),
					renderer, FALSE);
	gtk_tree_view_column_set_attributes(GTK_TREE_VIEW_COLUMN(column),
					    renderer,
					    "text", COL_OPTION,
					    "foreground-gdk",
					    COL_COLOR, NULL);

	sel = gtk_tree_view_get_selection(view);
	gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);
	gtk_widget_realize(tree1_w);
}

static void renderer_edited(GtkCellRendererText * cell,
			    const gchar * path_string,
			    const gchar * new_text, gpointer user_data);

void init_right_tree(void)
{
	GtkTreeView *view = GTK_TREE_VIEW(tree2_w);
	GtkCellRenderer *renderer;
	GtkTreeSelection *sel;
	GtkTreeViewColumn *column;
	gint i;

	gtk_tree_view_set_model(view, model2);
	gtk_tree_view_set_headers_visible(view, TRUE);
	gtk_tree_view_set_rules_hint(view, TRUE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_append_column(view, column);
	gtk_tree_view_column_set_title(column, "Options");

	renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(GTK_TREE_VIEW_COLUMN(column),
					renderer, FALSE);
	gtk_tree_view_column_set_attributes(GTK_TREE_VIEW_COLUMN(column),
					    renderer,
					    "pixbuf", COL_PIXBUF,
					    "visible", COL_PIXVIS, NULL);
	renderer = gtk_cell_renderer_toggle_new();
	gtk_tree_view_column_pack_start(GTK_TREE_VIEW_COLUMN(column),
					renderer, FALSE);
	gtk_tree_view_column_set_attributes(GTK_TREE_VIEW_COLUMN(column),
					    renderer,
					    "active", COL_BTNACT,
					    "inconsistent", COL_BTNINC,
					    "visible", COL_BTNVIS,
					    "radio", COL_BTNRAD, NULL);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(GTK_TREE_VIEW_COLUMN(column),
					renderer, FALSE);
	gtk_tree_view_column_set_attributes(GTK_TREE_VIEW_COLUMN(column),
					    renderer,
					    "text", COL_OPTION,
					    "foreground-gdk",
					    COL_COLOR, NULL);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(view, -1,
						    "Name", renderer,
						    "text", COL_NAME,
						    "foreground-gdk",
						    COL_COLOR, NULL);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(view, -1,
						    "N", renderer,
						    "text", COL_NO,
						    "foreground-gdk",
						    COL_COLOR, NULL);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(view, -1,
						    "M", renderer,
						    "text", COL_MOD,
						    "foreground-gdk",
						    COL_COLOR, NULL);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(view, -1,
						    "Y", renderer,
						    "text", COL_YES,
						    "foreground-gdk",
						    COL_COLOR, NULL);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(view, -1,
						    "Value", renderer,
						    "text", COL_VALUE,
						    "editable",
						    COL_EDIT,
						    "foreground-gdk",
						    COL_COLOR, NULL);
	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(renderer_edited), NULL);

	column = gtk_tree_view_get_column(view, COL_NAME);
	gtk_tree_view_column_set_visible(column, show_name);
	column = gtk_tree_view_get_column(view, COL_NO);
	gtk_tree_view_column_set_visible(column, show_range);
	column = gtk_tree_view_get_column(view, COL_MOD);
	gtk_tree_view_column_set_visible(column, show_range);
	column = gtk_tree_view_get_column(view, COL_YES);
	gtk_tree_view_column_set_visible(column, show_range);
	column = gtk_tree_view_get_column(view, COL_VALUE);
	gtk_tree_view_column_set_visible(column, show_value);

	if (resizeable) {
		for (i = 0; i < COL_VALUE; i++) {
			column = gtk_tree_view_get_column(view, i);
			gtk_tree_view_column_set_resizable(column, TRUE);
		}
	}

	sel = gtk_tree_view_get_selection(view);
	gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);
}


/* Utility Functions */


static void text_insert_help(struct menu *menu)
{
	GtkTextBuffer *buffer;
	GtkTextIter start, end;
	const char *prompt = menu_get_prompt(menu);
	struct gstr help = str_new();

	menu_get_ext_help(menu, &help);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_w));
	gtk_text_buffer_get_bounds(buffer, &start, &end);
	gtk_text_buffer_delete(buffer, &start, &end);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_w), 15);

	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_insert_with_tags(buffer, &end, prompt, -1, tag1,
					 NULL);
	gtk_text_buffer_insert_at_cursor(buffer, "\n\n", 2);
	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_insert_with_tags(buffer, &end, str_get(&help), -1, tag2,
					 NULL);
	str_free(&help);
}


static void text_insert_msg(const char *title, const char *message)
{
	GtkTextBuffer *buffer;
	GtkTextIter start, end;
	const char *msg = message;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_w));
	gtk_text_buffer_get_bounds(buffer, &start, &end);
	gtk_text_buffer_delete(buffer, &start, &end);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_w), 15);

	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_insert_with_tags(buffer, &end, title, -1, tag1,
					 NULL);
	gtk_text_buffer_insert_at_cursor(buffer, "\n\n", 2);
	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_insert_with_tags(buffer, &end, msg, -1, tag2,
					 NULL);
}


/* Main Windows Callbacks */

void on_save_activate(GtkMenuItem * menuitem, gpointer user_data);
gboolean on_window1_delete_event(GtkWidget * widget, GdkEvent * event,
				 gpointer user_data)
{
	GtkWidget *dialog, *label;
	gint result;

	if (!conf_get_changed())
		return FALSE;

	dialog = gtk_dialog_new_with_buttons("Warning !",
					     GTK_WINDOW(main_wnd),
					     (GtkDialogFlags)
					     (GTK_DIALOG_MODAL |
					      GTK_DIALOG_DESTROY_WITH_PARENT),
					     GTK_STOCK_OK,
					     GTK_RESPONSE_YES,
					     GTK_STOCK_NO,
					     GTK_RESPONSE_NO,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_CANCEL, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog),
					GTK_RESPONSE_CANCEL);

	label = gtk_label_new("\nSave configuration ?\n");
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);
	gtk_widget_show(label);

	result = gtk_dialog_run(GTK_DIALOG(dialog));
	switch (result) {
	case GTK_RESPONSE_YES:
		on_save_activate(NULL, NULL);
		return FALSE;
	case GTK_RESPONSE_NO:
		return FALSE;
	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:
	default:
		gtk_widget_destroy(dialog);
		return TRUE;
	}

	return FALSE;
}


void on_window1_destroy(GtkObject * object, gpointer user_data)
{
	gtk_main_quit();
}


void
on_window1_size_request(GtkWidget * widget,
			GtkRequisition * requisition, gpointer user_data)
{
	static gint old_h;
	gint w, h;

	if (widget->window == NULL)
		gtk_window_get_default_size(GTK_WINDOW(main_wnd), &w, &h);
	else
		gdk_window_get_size(widget->window, &w, &h);

	if (h == old_h)
		return;
	old_h = h;

	gtk_paned_set_position(GTK_PANED(vpaned), 2 * h / 3);
}


/* Menu & Toolbar Callbacks */


static void
load_filename(GtkFileSelection * file_selector, gpointer user_data)
{
	const gchar *fn;

	fn = gtk_file_selection_get_filename(GTK_FILE_SELECTION
					     (user_data));

	if (conf_read(fn))
		text_insert_msg("Error", "Unable to load configuration !");
	else
		display_tree(&rootmenu);
}

void on_load1_activate(GtkMenuItem * menuitem, gpointer user_data)
{
	GtkWidget *fs;

	fs = gtk_file_selection_new("Load file...");
	g_signal_connect(GTK_OBJECT(GTK_FILE_SELECTION(fs)->ok_button),
			 "clicked",
			 G_CALLBACK(load_filename), (gpointer) fs);
	g_signal_connect_swapped(GTK_OBJECT
				 (GTK_FILE_SELECTION(fs)->ok_button),
				 "clicked", G_CALLBACK(gtk_widget_destroy),
				 (gpointer) fs);
	g_signal_connect_swapped(GTK_OBJECT
				 (GTK_FILE_SELECTION(fs)->cancel_button),
				 "clicked", G_CALLBACK(gtk_widget_destroy),
				 (gpointer) fs);
	gtk_widget_show(fs);
}


void on_save_activate(GtkMenuItem * menuitem, gpointer user_data)
{
	if (conf_write(NULL))
		text_insert_msg("Error", "Unable to save configuration !");
	conf_write_autoconf(0);
}


static void
store_filename(GtkFileSelection * file_selector, gpointer user_data)
{
	const gchar *fn;

	fn = gtk_file_selection_get_filename(GTK_FILE_SELECTION
					     (user_data));

	if (conf_write(fn))
		text_insert_msg("Error", "Unable to save configuration !");

	gtk_widget_destroy(GTK_WIDGET(user_data));
}

void on_save_as1_activate(GtkMenuItem * menuitem, gpointer user_data)
{
	GtkWidget *fs;

	fs = gtk_file_selection_new("Save file as...");
	g_signal_connect(GTK_OBJECT(GTK_FILE_SELECTION(fs)->ok_button),
			 "clicked",
			 G_CALLBACK(store_filename), (gpointer) fs);
	g_signal_connect_swapped(GTK_OBJECT
				 (GTK_FILE_SELECTION(fs)->ok_button),
				 "clicked", G_CALLBACK(gtk_widget_destroy),
				 (gpointer) fs);
	g_signal_connect_swapped(GTK_OBJECT
				 (GTK_FILE_SELECTION(fs)->cancel_button),
				 "clicked", G_CALLBACK(gtk_widget_destroy),
				 (gpointer) fs);
	gtk_widget_show(fs);
}


void on_quit1_activate(GtkMenuItem * menuitem, gpointer user_data)
{
	if (!on_window1_delete_event(NULL, NULL, NULL))
		gtk_widget_destroy(GTK_WIDGET(main_wnd));
}


void on_show_name1_activate(GtkMenuItem * menuitem, gpointer user_data)
{
	GtkTreeViewColumn *col;

	show_name = GTK_CHECK_MENU_ITEM(menuitem)->active;
	col = gtk_tree_view_get_column(GTK_TREE_VIEW(tree2_w), COL_NAME);
	if (col)
		gtk_tree_view_column_set_visible(col, show_name);
}


void on_show_range1_activate(GtkMenuItem * menuitem, gpointer user_data)
{
	GtkTreeViewColumn *col;

	show_range = GTK_CHECK_MENU_ITEM(menuitem)->active;
	col = gtk_tree_view_get_column(GTK_TREE_VIEW(tree2_w), COL_NO);
	if (col)
		gtk_tree_view_column_set_visible(col, show_range);
	col = gtk_tree_view_get_column(GTK_TREE_VIEW(tree2_w), COL_MOD);
	if (col)
		gtk_tree_view_column_set_visible(col, show_range);
	col = gtk_tree_view_get_column(GTK_TREE_VIEW(tree2_w), COL_YES);
	if (col)
		gtk_tree_view_column_set_visible(col, show_range);

}


void on_show_data1_activate(GtkMenuItem * menuitem, gpointer user_data)
{
	GtkTreeViewColumn *col;

	show_value = GTK_CHECK_MENU_ITEM(menuitem)->active;
	col = gtk_tree_view_get_column(GTK_TREE_VIEW(tree2_w), COL_VALUE);
	if (col)
		gtk_tree_view_column_set_visible(col, show_value);
}


void
on_set_option_mode1_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	opt_mode = OPT_NORMAL;
	gtk_tree_store_clear(tree2);
	display_tree(&rootmenu);	/* instead of update_tree to speed-up */
}


void
on_set_option_mode2_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	opt_mode = OPT_ALL;
	gtk_tree_store_clear(tree2);
	display_tree(&rootmenu);	/* instead of update_tree to speed-up */
}


void
on_set_option_mode3_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	opt_mode = OPT_PROMPT;
	gtk_tree_store_clear(tree2);
	display_tree(&rootmenu);	/* instead of update_tree to speed-up */
}


void on_introduction1_activate(GtkMenuItem * menuitem, gpointer user_data)
{
	GtkWidget *dialog;
	const gchar *intro_text = 
	    "Welcome to gkc, the GTK+ graphical configuration tool\n"
	    "For each option, a blank box indicates the feature is disabled, a\n"
	    "check indicates it is enabled, and a dot indicates that it is to\n"
	    "be compiled as a module.  Clicking on the box will cycle through the three states.\n"
	    "\n"
	    "If you do not see an option (e.g., a device driver) that you\n"
	    "believe should be present, try turning on Show All Options\n"
	    "under the Options menu.\n"
	    "Although there is no cross reference yet to help you figure out\n"
	    "what other options must be enabled to support the option you\n"
	    "are interested in, you can still view the help of a grayed-out\n"
	    "option.\n"
	    "\n"
	    "Toggling Show Debug Info under the Options menu will show \n"
	    "the dependencies, which you can then match by examining other options.";

	dialog = gtk_message_dialog_new(GTK_WINDOW(main_wnd),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_INFO,
					GTK_BUTTONS_CLOSE, "%s", intro_text);
	g_signal_connect_swapped(GTK_OBJECT(dialog), "response",
				 G_CALLBACK(gtk_widget_destroy),
				 GTK_OBJECT(dialog));
	gtk_widget_show_all(dialog);
}


void on_about1_activate(GtkMenuItem * menuitem, gpointer user_data)
{
	GtkWidget *dialog;
	const gchar *about_text =
	    "gkc is copyright (c) 2002 Romain Lievin <roms@lpg.ticalc.org>.\n"
	      "Based on the source code from Roman Zippel.\n";

	dialog = gtk_message_dialog_new(GTK_WINDOW(main_wnd),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_INFO,
					GTK_BUTTONS_CLOSE, "%s", about_text);
	g_signal_connect_swapped(GTK_OBJECT(dialog), "response",
				 G_CALLBACK(gtk_widget_destroy),
				 GTK_OBJECT(dialog));
	gtk_widget_show_all(dialog);
}


void on_license1_activate(GtkMenuItem * menuitem, gpointer user_data)
{
	GtkWidget *dialog;
	const gchar *license_text =
	    "gkc is released under the terms of the GNU GPL v2.\n"
	      "For more information, please see the source code or\n"
	      "visit http://www.fsf.org/licenses/licenses.html\n";

	dialog = gtk_message_dialog_new(GTK_WINDOW(main_wnd),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_INFO,
					GTK_BUTTONS_CLOSE, "%s", license_text);
	g_signal_connect_swapped(GTK_OBJECT(dialog), "response",
				 G_CALLBACK(gtk_widget_destroy),
				 GTK_OBJECT(dialog));
	gtk_widget_show_all(dialog);
}


void on_back_clicked(GtkButton * button, gpointer user_data)
{
	enum prop_type ptype;

	current = current->parent;
	ptype = current->prompt ? current->prompt->type : P_UNKNOWN;
	if (ptype != P_MENU)
		current = current->parent;
	display_tree_part();

	if (current == &rootmenu)
		gtk_widget_set_sensitive(back_btn, FALSE);
}


void on_load_clicked(GtkButton * button, gpointer user_data)
{
	on_load1_activate(NULL, user_data);
}


void on_single_clicked(GtkButton * button, gpointer user_data)
{
	view_mode = SINGLE_VIEW;
	gtk_widget_hide(tree1_w);
	current = &rootmenu;
	display_tree_part();
}


void on_split_clicked(GtkButton * button, gpointer user_data)
{
	gint w, h;
	view_mode = SPLIT_VIEW;
	gtk_widget_show(tree1_w);
	gtk_window_get_default_size(GTK_WINDOW(main_wnd), &w, &h);
	gtk_paned_set_position(GTK_PANED(hpaned), w / 2);
	if (tree2)
		gtk_tree_store_clear(tree2);
	display_list();

	/* Disable back btn, like in full mode. */
	gtk_widget_set_sensitive(back_btn, FALSE);
}


void on_full_clicked(GtkButton * button, gpointer user_data)
{
	view_mode = FULL_VIEW;
	gtk_widget_hide(tree1_w);
	if (tree2)
		gtk_tree_store_clear(tree2);
	display_tree(&rootmenu);
	gtk_widget_set_sensitive(back_btn, FALSE);
}


void on_collapse_clicked(GtkButton * button, gpointer user_data)
{
	gtk_tree_view_collapse_all(GTK_TREE_VIEW(tree2_w));
}


void on_expand_clicked(GtkButton * button, gpointer user_data)
{
	gtk_tree_view_expand_all(GTK_TREE_VIEW(tree2_w));
}


/* CTree Callbacks */

/* Change hex/int/string value in the cell */
static void renderer_edited(GtkCellRendererText * cell,
			    const gchar * path_string,
			    const gchar * new_text, gpointer user_data)
{
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	const char *old_def, *new_def;
	struct menu *menu;
	struct symbol *sym;

	if (!gtk_tree_model_get_iter(model2, &iter, path))
		return;

	gtk_tree_model_get(model2, &iter, COL_MENU, &menu, -1);
	sym = menu->sym;

	gtk_tree_model_get(model2, &iter, COL_VALUE, &old_def, -1);
	new_def = new_text;

	sym_set_string_value(sym, new_def);

	update_tree(&rootmenu, NULL);

	gtk_tree_path_free(path);
}

/* Change the value of a symbol and update the tree */
static void change_sym_value(struct menu *menu, gint col)
{
	struct symbol *sym = menu->sym;
	tristate newval;

	if (!sym)
		return;

	if (col == COL_NO)
		newval = no;
	else if (col == COL_MOD)
		newval = mod;
	else if (col == COL_YES)
		newval = yes;
	else
		return;

	switch (sym_get_type(sym)) {
	case S_BOOLEAN:
	case S_TRISTATE:
		if (!sym_tristate_within_range(sym, newval))
			newval = yes;
		sym_set_tristate_value(sym, newval);
		if (view_mode == FULL_VIEW)
			update_tree(&rootmenu, NULL);
		else if (view_mode == SPLIT_VIEW) {
			update_tree(browsed, NULL);
			display_list();
		}
		else if (view_mode == SINGLE_VIEW)
			display_tree_part();	//fixme: keep exp/coll
		break;
	case S_INT:
	case S_HEX:
	case S_STRING:
	default:
		break;
	}
}

static void toggle_sym_value(struct menu *menu)
{
	if (!menu->sym)
		return;

	sym_toggle_tristate_value(menu->sym);
	if (view_mode == FULL_VIEW)
		update_tree(&rootmenu, NULL);
	else if (view_mode == SPLIT_VIEW) {
		update_tree(browsed, NULL);
		display_list();
	}
	else if (view_mode == SINGLE_VIEW)
		display_tree_part();	//fixme: keep exp/coll
}

static gint column2index(GtkTreeViewColumn * column)
{
	gint i;

	for (i = 0; i < COL_NUMBER; i++) {
		GtkTreeViewColumn *col;

		col = gtk_tree_view_get_column(GTK_TREE_VIEW(tree2_w), i);
		if (col == column)
			return i;
	}

	return -1;
}


/* User click: update choice (full) or goes down (single) */
gboolean
on_treeview2_button_press_event(GtkWidget * widget,
				GdkEventButton * event, gpointer user_data)
{
	GtkTreeView *view = GTK_TREE_VIEW(widget);
	GtkTreePath *path;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	struct menu *menu;
	gint col;

#if GTK_CHECK_VERSION(2,1,4) // bug in ctree with earlier version of GTK
	gint tx = (gint) event->x;
	gint ty = (gint) event->y;
	gint cx, cy;

	gtk_tree_view_get_path_at_pos(view, tx, ty, &path, &column, &cx,
				      &cy);
#else
	gtk_tree_view_get_cursor(view, &path, &column);
#endif
	if (path == NULL)
		return FALSE;

	if (!gtk_tree_model_get_iter(model2, &iter, path))
		return FALSE;
	gtk_tree_model_get(model2, &iter, COL_MENU, &menu, -1);

	col = column2index(column);
	if (event->type == GDK_2BUTTON_PRESS) {
		enum prop_type ptype;
		ptype = menu->prompt ? menu->prompt->type : P_UNKNOWN;

		if (ptype == P_MENU && view_mode != FULL_VIEW && col == COL_OPTION) {
			// goes down into menu
			current = menu;
			display_tree_part();
			gtk_widget_set_sensitive(back_btn, TRUE);
		} else if (col == COL_OPTION) {
			toggle_sym_value(menu);
			gtk_tree_view_expand_row(view, path, TRUE);
		}
	} else {
		if (col == COL_VALUE) {
			toggle_sym_value(menu);
			gtk_tree_view_expand_row(view, path, TRUE);
		} else if (col == COL_NO || col == COL_MOD
			   || col == COL_YES) {
			change_sym_value(menu, col);
			gtk_tree_view_expand_row(view, path, TRUE);
		}
	}

	return FALSE;
}

/* Key pressed: update choice */
gboolean
on_treeview2_key_press_event(GtkWidget * widget,
			     GdkEventKey * event, gpointer user_data)
{
	GtkTreeView *view = GTK_TREE_VIEW(widget);
	GtkTreePath *path;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	struct menu *menu;
	gint col;

	gtk_tree_view_get_cursor(view, &path, &column);
	if (path == NULL)
		return FALSE;

	if (event->keyval == GDK_space) {
		if (gtk_tree_view_row_expanded(view, path))
			gtk_tree_view_collapse_row(view, path);
		else
			gtk_tree_view_expand_row(view, path, FALSE);
		return TRUE;
	}
	if (event->keyval == GDK_KP_Enter) {
	}
	if (widget == tree1_w)
		return FALSE;

	gtk_tree_model_get_iter(model2, &iter, path);
	gtk_tree_model_get(model2, &iter, COL_MENU, &menu, -1);

	if (!strcasecmp(event->string, "n"))
		col = COL_NO;
	else if (!strcasecmp(event->string, "m"))
		col = COL_MOD;
	else if (!strcasecmp(event->string, "y"))
		col = COL_YES;
	else
		col = -1;
	change_sym_value(menu, col);

	return FALSE;
}


/* Row selection changed: update help */
void
on_treeview2_cursor_changed(GtkTreeView * treeview, gpointer user_data)
{
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	struct menu *menu;

	selection = gtk_tree_view_get_selection(treeview);
	if (gtk_tree_selection_get_selected(selection, &model2, &iter)) {
		gtk_tree_model_get(model2, &iter, COL_MENU, &menu, -1);
		text_insert_help(menu);
	}
}


/* User click: display sub-tree in the right frame. */
gboolean
on_treeview1_button_press_event(GtkWidget * widget,
				GdkEventButton * event, gpointer user_data)
{
	GtkTreeView *view = GTK_TREE_VIEW(widget);
	GtkTreePath *path;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	struct menu *menu;

	gint tx = (gint) event->x;
	gint ty = (gint) event->y;
	gint cx, cy;

	gtk_tree_view_get_path_at_pos(view, tx, ty, &path, &column, &cx,
				      &cy);
	if (path == NULL)
		return FALSE;

	gtk_tree_model_get_iter(model1, &iter, path);
	gtk_tree_model_get(model1, &iter, COL_MENU, &menu, -1);

	if (event->type == GDK_2BUTTON_PRESS) {
		toggle_sym_value(menu);
		current = menu;
		display_tree_part();
	} else {
		browsed = menu;
		display_tree_part();
	}

	gtk_widget_realize(tree2_w);
	gtk_tree_view_set_cursor(view, path, NULL, FALSE);
	gtk_widget_grab_focus(tree2_w);

	return FALSE;
}


/* Fill a row of strings */
static gchar **fill_row(struct menu *menu)
{
	static gchar *row[COL_NUMBER];
	struct symbol *sym = menu->sym;
	const char *def;
	int stype;
	tristate val;
	enum prop_type ptype;
	int i;

	for (i = COL_OPTION; i <= COL_COLOR; i++)
		g_free(row[i]);
	bzero(row, sizeof(row));

	row[COL_OPTION] =
	    g_strdup_printf("%s %s", menu_get_prompt(menu),
			    sym && !sym_has_value(sym) ? "(NEW)" : "");

	if (opt_mode == OPT_ALL && !menu_is_visible(menu))
		row[COL_COLOR] = g_strdup("DarkGray");
	else if (opt_mode == OPT_PROMPT &&
			menu_has_prompt(menu) && !menu_is_visible(menu))
		row[COL_COLOR] = g_strdup("DarkGray");
	else
		row[COL_COLOR] = g_strdup("Black");

	ptype = menu->prompt ? menu->prompt->type : P_UNKNOWN;
	switch (ptype) {
	case P_MENU:
		row[COL_PIXBUF] = (gchar *) xpm_menu;
		if (view_mode == SINGLE_VIEW)
			row[COL_PIXVIS] = GINT_TO_POINTER(TRUE);
		row[COL_BTNVIS] = GINT_TO_POINTER(FALSE);
		break;
	case P_COMMENT:
		row[COL_PIXBUF] = (gchar *) xpm_void;
		row[COL_PIXVIS] = GINT_TO_POINTER(FALSE);
		row[COL_BTNVIS] = GINT_TO_POINTER(FALSE);
		break;
	default:
		row[COL_PIXBUF] = (gchar *) xpm_void;
		row[COL_PIXVIS] = GINT_TO_POINTER(FALSE);
		row[COL_BTNVIS] = GINT_TO_POINTER(TRUE);
		break;
	}

	if (!sym)
		return row;
	row[COL_NAME] = g_strdup(sym->name);

	sym_calc_value(sym);
	sym->flags &= ~SYMBOL_CHANGED;

	if (sym_is_choice(sym)) {	// parse childs for getting final value
		struct menu *child;
		struct symbol *def_sym = sym_get_choice_value(sym);
		struct menu *def_menu = NULL;

		row[COL_BTNVIS] = GINT_TO_POINTER(FALSE);

		for (child = menu->list; child; child = child->next) {
			if (menu_is_visible(child)
			    && child->sym == def_sym)
				def_menu = child;
		}

		if (def_menu)
			row[COL_VALUE] =
			    g_strdup(menu_get_prompt(def_menu));
	}
	if (sym->flags & SYMBOL_CHOICEVAL)
		row[COL_BTNRAD] = GINT_TO_POINTER(TRUE);

	stype = sym_get_type(sym);
	switch (stype) {
	case S_BOOLEAN:
		if (GPOINTER_TO_INT(row[COL_PIXVIS]) == FALSE)
			row[COL_BTNVIS] = GINT_TO_POINTER(TRUE);
		if (sym_is_choice(sym))
			break;
		/* fall through */
	case S_TRISTATE:
		val = sym_get_tristate_value(sym);
		switch (val) {
		case no:
			row[COL_NO] = g_strdup("N");
			row[COL_VALUE] = g_strdup("N");
			row[COL_BTNACT] = GINT_TO_POINTER(FALSE);
			row[COL_BTNINC] = GINT_TO_POINTER(FALSE);
			break;
		case mod:
			row[COL_MOD] = g_strdup("M");
			row[COL_VALUE] = g_strdup("M");
			row[COL_BTNINC] = GINT_TO_POINTER(TRUE);
			break;
		case yes:
			row[COL_YES] = g_strdup("Y");
			row[COL_VALUE] = g_strdup("Y");
			row[COL_BTNACT] = GINT_TO_POINTER(TRUE);
			row[COL_BTNINC] = GINT_TO_POINTER(FALSE);
			break;
		}

		if (val != no && sym_tristate_within_range(sym, no))
			row[COL_NO] = g_strdup("_");
		if (val != mod && sym_tristate_within_range(sym, mod))
			row[COL_MOD] = g_strdup("_");
		if (val != yes && sym_tristate_within_range(sym, yes))
			row[COL_YES] = g_strdup("_");
		break;
	case S_INT:
	case S_HEX:
	case S_STRING:
		def = sym_get_string_value(sym);
		row[COL_VALUE] = g_strdup(def);
		row[COL_EDIT] = GINT_TO_POINTER(TRUE);
		row[COL_BTNVIS] = GINT_TO_POINTER(FALSE);
		break;
	}

	return row;
}


/* Set the node content with a row of strings */
static void set_node(GtkTreeIter * node, struct menu *menu, gchar ** row)
{
	GdkColor color;
	gboolean success;
	GdkPixbuf *pix;

	pix = gdk_pixbuf_new_from_xpm_data((const char **)
					   row[COL_PIXBUF]);

	gdk_color_parse(row[COL_COLOR], &color);
	gdk_colormap_alloc_colors(gdk_colormap_get_system(), &color, 1,
				  FALSE, FALSE, &success);

	gtk_tree_store_set(tree, node,
			   COL_OPTION, row[COL_OPTION],
			   COL_NAME, row[COL_NAME],
			   COL_NO, row[COL_NO],
			   COL_MOD, row[COL_MOD],
			   COL_YES, row[COL_YES],
			   COL_VALUE, row[COL_VALUE],
			   COL_MENU, (gpointer) menu,
			   COL_COLOR, &color,
			   COL_EDIT, GPOINTER_TO_INT(row[COL_EDIT]),
			   COL_PIXBUF, pix,
			   COL_PIXVIS, GPOINTER_TO_INT(row[COL_PIXVIS]),
			   COL_BTNVIS, GPOINTER_TO_INT(row[COL_BTNVIS]),
			   COL_BTNACT, GPOINTER_TO_INT(row[COL_BTNACT]),
			   COL_BTNINC, GPOINTER_TO_INT(row[COL_BTNINC]),
			   COL_BTNRAD, GPOINTER_TO_INT(row[COL_BTNRAD]),
			   -1);

	g_object_unref(pix);
}


/* Add a node to the tree */
static void place_node(struct menu *menu, char **row)
{
	GtkTreeIter *parent = parents[indent - 1];
	GtkTreeIter *node = parents[indent];

	gtk_tree_store_append(tree, node, parent);
	set_node(node, menu, row);
}


/* Find a node in the GTK+ tree */
static GtkTreeIter found;

/*
 * Find a menu in the GtkTree starting at parent.
 */
GtkTreeIter *gtktree_iter_find_node(GtkTreeIter * parent,
				    struct menu *tofind)
{
	GtkTreeIter iter;
	GtkTreeIter *child = &iter;
	gboolean valid;
	GtkTreeIter *ret;

	valid = gtk_tree_model_iter_children(model2, child, parent);
	while (valid) {
		struct menu *menu;

		gtk_tree_model_get(model2, child, 6, &menu, -1);

		if (menu == tofind) {
			memcpy(&found, child, sizeof(GtkTreeIter));
			return &found;
		}

		ret = gtktree_iter_find_node(child, tofind);
		if (ret)
			return ret;

		valid = gtk_tree_model_iter_next(model2, child);
	}

	return NULL;
}


/*
 * Update the tree by adding/removing entries
 * Does not change other nodes
 */
static void update_tree(struct menu *src, GtkTreeIter * dst)
{
	struct menu *child1;
	GtkTreeIter iter, tmp;
	GtkTreeIter *child2 = &iter;
	gboolean valid;
	GtkTreeIter *sibling;
	struct symbol *sym;
	struct menu *menu1, *menu2;

	if (src == &rootmenu)
		indent = 1;

	valid = gtk_tree_model_iter_children(model2, child2, dst);
	for (child1 = src->list; child1; child1 = child1->next) {

		sym = child1->sym;

	      reparse:
		menu1 = child1;
		if (valid)
			gtk_tree_model_get(model2, child2, COL_MENU,
					   &menu2, -1);
		else
			menu2 = NULL;	// force adding of a first child

#ifdef DEBUG
		printf("%*c%s | %s\n", indent, ' ',
		       menu1 ? menu_get_prompt(menu1) : "nil",
		       menu2 ? menu_get_prompt(menu2) : "nil");
#endif

		if ((opt_mode == OPT_NORMAL && !menu_is_visible(child1)) ||
		    (opt_mode == OPT_PROMPT && !menu_has_prompt(child1)) ||
		    (opt_mode == OPT_ALL    && !menu_get_prompt(child1))) {

			/* remove node */
			if (gtktree_iter_find_node(dst, menu1) != NULL) {
				memcpy(&tmp, child2, sizeof(GtkTreeIter));
				valid = gtk_tree_model_iter_next(model2,
								 child2);
				gtk_tree_store_remove(tree2, &tmp);
				if (!valid)
					return;		/* next parent */
				else
					goto reparse;	/* next child */
			} else
				continue;
		}

		if (menu1 != menu2) {
			if (gtktree_iter_find_node(dst, menu1) == NULL) {	// add node
				if (!valid && !menu2)
					sibling = NULL;
				else
					sibling = child2;
				gtk_tree_store_insert_before(tree2,
							     child2,
							     dst, sibling);
				set_node(child2, menu1, fill_row(menu1));
				if (menu2 == NULL)
					valid = TRUE;
			} else {	// remove node
				memcpy(&tmp, child2, sizeof(GtkTreeIter));
				valid = gtk_tree_model_iter_next(model2,
								 child2);
				gtk_tree_store_remove(tree2, &tmp);
				if (!valid)
					return;	// next parent
				else
					goto reparse;	// next child
			}
		} else if (sym && (sym->flags & SYMBOL_CHANGED)) {
			set_node(child2, menu1, fill_row(menu1));
		}

		indent++;
		update_tree(child1, child2);
		indent--;

		valid = gtk_tree_model_iter_next(model2, child2);
	}
}


/* Display the whole tree (single/split/full view) */
static void display_tree(struct menu *menu)
{
	struct symbol *sym;
	struct property *prop;
	struct menu *child;
	enum prop_type ptype;

	if (menu == &rootmenu) {
		indent = 1;
		current = &rootmenu;
	}

	for (child = menu->list; child; child = child->next) {
		prop = child->prompt;
		sym = child->sym;
		ptype = prop ? prop->type : P_UNKNOWN;

		if (sym)
			sym->flags &= ~SYMBOL_CHANGED;

		if ((view_mode == SPLIT_VIEW)
		    && !(child->flags & MENU_ROOT) && (tree == tree1))
			continue;

		if ((view_mode == SPLIT_VIEW) && (child->flags & MENU_ROOT)
		    && (tree == tree2))
			continue;

		if ((opt_mode == OPT_NORMAL && menu_is_visible(child)) ||
		    (opt_mode == OPT_PROMPT && menu_has_prompt(child)) ||
		    (opt_mode == OPT_ALL    && menu_get_prompt(child)))
			place_node(child, fill_row(child));
#ifdef DEBUG
		printf("%*c%s: ", indent, ' ', menu_get_prompt(child));
		printf("%s", child->flags & MENU_ROOT ? "rootmenu | " : "");
		printf("%s", prop_get_type_name(ptype));
		printf(" | ");
		if (sym) {
			printf("%s", sym_type_name(sym->type));
			printf(" | ");
			printf("%s", dbg_sym_flags(sym->flags));
			printf("\n");
		} else
			printf("\n");
#endif
		if ((view_mode != FULL_VIEW) && (ptype == P_MENU)
		    && (tree == tree2))
			continue;
/*
		if (((menu != &rootmenu) && !(menu->flags & MENU_ROOT))
		    || (view_mode == FULL_VIEW)
		    || (view_mode == SPLIT_VIEW))*/

		/* Change paned position if the view is not in 'split mode' */
		if (view_mode == SINGLE_VIEW || view_mode == FULL_VIEW) {
			gtk_paned_set_position(GTK_PANED(hpaned), 0);
		}

		if (((view_mode == SINGLE_VIEW) && (menu->flags & MENU_ROOT))
		    || (view_mode == FULL_VIEW)
		    || (view_mode == SPLIT_VIEW)) {
			indent++;
			display_tree(child);
			indent--;
		}
	}
}

/* Display a part of the tree starting at current node (single/split view) */
static void display_tree_part(void)
{
	if (tree2)
		gtk_tree_store_clear(tree2);
	if (view_mode == SINGLE_VIEW)
		display_tree(current);
	else if (view_mode == SPLIT_VIEW)
		display_tree(browsed);
	gtk_tree_view_expand_all(GTK_TREE_VIEW(tree2_w));
}

/* Display the list in the left frame (split view) */
static void display_list(void)
{
	if (tree1)
		gtk_tree_store_clear(tree1);

	tree = tree1;
	display_tree(&rootmenu);
	gtk_tree_view_expand_all(GTK_TREE_VIEW(tree1_w));
	tree = tree2;
}

void fixup_rootmenu(struct menu *menu)
{
	struct menu *child;
	static int menu_cnt = 0;

	menu->flags |= MENU_ROOT;
	for (child = menu->list; child; child = child->next) {
		if (child->prompt && child->prompt->type == P_MENU) {
			menu_cnt++;
			fixup_rootmenu(child);
			menu_cnt--;
		} else if (!menu_cnt)
			fixup_rootmenu(child);
	}
}


/* Main */
int main(int ac, char *av[])
{
	const char *name;
	char *env;
	gchar *glade_file;

	/* GTK stuffs */
	gtk_set_locale();
	gtk_init(&ac, &av);
	glade_init();

	//add_pixmap_directory (PACKAGE_DATA_DIR "/" PACKAGE "/pixmaps");
	//add_pixmap_directory (PACKAGE_SOURCE_DIR "/pixmaps");

	/* Determine GUI path */
	env = getenv(SRCTREE);
	if (env)
		glade_file = g_strconcat(env, "/scripts/kconfig/gconf.glade", NULL);
	else if (av[0][0] == '/')
		glade_file = g_strconcat(av[0], ".glade", NULL);
	else
		glade_file = g_strconcat(g_get_current_dir(), "/", av[0], ".glade", NULL);

	/* Conf stuffs */
	if (ac > 1 && av[1][0] == '-') {
		switch (av[1][1]) {
		case 'a':
			//showAll = 1;
			break;
		case 's':
			conf_set_message_callback(NULL);
			break;
		case 'h':
		case '?':
			printf("%s [-s] <config>\n", av[0]);
			exit(0);
		}
		name = av[2];
	} else
		name = av[1];

	conf_parse(name);
	fixup_rootmenu(&rootmenu);
	conf_read(NULL);

	/* Load the interface and connect signals */
	init_main_window(glade_file);
	init_tree_model();
	init_left_tree();
	init_right_tree();

	switch (view_mode) {
	case SINGLE_VIEW:
		display_tree_part();
		break;
	case SPLIT_VIEW:
		display_list();
		break;
	case FULL_VIEW:
		display_tree(&rootmenu);
		break;
	}

	gtk_main();

	return 0;
}

static void conf_changed(void)
{
	bool changed = conf_get_changed();
	gtk_widget_set_sensitive(save_btn, changed);
	gtk_widget_set_sensitive(save_menu_item, changed);
}
