// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 *
 * Introduced single menu mode (show all sub-menus in one large tree).
 * 2002-11-06 Petr Baudis <pasky@ucw.cz>
 *
 * i18n, 2005, Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>

#include "lkc.h"
#include "lxdialog/dialog.h"

#define JUMP_NB			9

static const char mconf_readme[] =
"Overview\n"
"--------\n"
"This interface lets you select features and parameters for the build.\n"
"Features can either be built-in, modularized, or ignored. Parameters\n"
"must be entered in as decimal or hexadecimal numbers or text.\n"
"\n"
"Menu items beginning with following braces represent features that\n"
"  [ ] can be built in or removed\n"
"  < > can be built in, modularized or removed\n"
"  { } can be built in or modularized (selected by other feature)\n"
"  - - are selected by other feature,\n"
"while *, M or whitespace inside braces means to build in, build as\n"
"a module or to exclude the feature respectively.\n"
"\n"
"To change any of these features, highlight it with the cursor\n"
"keys and press <Y> to build it in, <M> to make it a module or\n"
"<N> to remove it.  You may also press the <Space Bar> to cycle\n"
"through the available options (i.e. Y->N->M->Y).\n"
"\n"
"Some additional keyboard hints:\n"
"\n"
"Menus\n"
"----------\n"
"o  Use the Up/Down arrow keys (cursor keys) to highlight the item you\n"
"   wish to change or the submenu you wish to select and press <Enter>.\n"
"   Submenus are designated by \"--->\", empty ones by \"----\".\n"
"\n"
"   Shortcut: Press the option's highlighted letter (hotkey).\n"
"             Pressing a hotkey more than once will sequence\n"
"             through all visible items which use that hotkey.\n"
"\n"
"   You may also use the <PAGE UP> and <PAGE DOWN> keys to scroll\n"
"   unseen options into view.\n"
"\n"
"o  To exit a menu use the cursor keys to highlight the <Exit> button\n"
"   and press <ENTER>.\n"
"\n"
"   Shortcut: Press <ESC><ESC> or <E> or <X> if there is no hotkey\n"
"             using those letters.  You may press a single <ESC>, but\n"
"             there is a delayed response which you may find annoying.\n"
"\n"
"   Also, the <TAB> and cursor keys will cycle between <Select>,\n"
"   <Exit>, <Help>, <Save>, and <Load>.\n"
"\n"
"o  To get help with an item, use the cursor keys to highlight <Help>\n"
"   and press <ENTER>.\n"
"\n"
"   Shortcut: Press <H> or <?>.\n"
"\n"
"o  To toggle the display of hidden options, press <Z>.\n"
"\n"
"\n"
"Radiolists  (Choice lists)\n"
"-----------\n"
"o  Use the cursor keys to select the option you wish to set and press\n"
"   <S> or the <SPACE BAR>.\n"
"\n"
"   Shortcut: Press the first letter of the option you wish to set then\n"
"             press <S> or <SPACE BAR>.\n"
"\n"
"o  To see available help for the item, use the cursor keys to highlight\n"
"   <Help> and Press <ENTER>.\n"
"\n"
"   Shortcut: Press <H> or <?>.\n"
"\n"
"   Also, the <TAB> and cursor keys will cycle between <Select> and\n"
"   <Help>\n"
"\n"
"\n"
"Data Entry\n"
"-----------\n"
"o  Enter the requested information and press <ENTER>\n"
"   If you are entering hexadecimal values, it is not necessary to\n"
"   add the '0x' prefix to the entry.\n"
"\n"
"o  For help, use the <TAB> or cursor keys to highlight the help option\n"
"   and press <ENTER>.  You can try <TAB><H> as well.\n"
"\n"
"\n"
"Text Box    (Help Window)\n"
"--------\n"
"o  Use the cursor keys to scroll up/down/left/right.  The VI editor\n"
"   keys h,j,k,l function here as do <u>, <d>, <SPACE BAR> and <B> for\n"
"   those who are familiar with less and lynx.\n"
"\n"
"o  Press <E>, <X>, <q>, <Enter> or <Esc><Esc> to exit.\n"
"\n"
"\n"
"Alternate Configuration Files\n"
"-----------------------------\n"
"Menuconfig supports the use of alternate configuration files for\n"
"those who, for various reasons, find it necessary to switch\n"
"between different configurations.\n"
"\n"
"The <Save> button will let you save the current configuration to\n"
"a file of your choosing.  Use the <Load> button to load a previously\n"
"saved alternate configuration.\n"
"\n"
"Even if you don't use alternate configuration files, but you find\n"
"during a Menuconfig session that you have completely messed up your\n"
"settings, you may use the <Load> button to restore your previously\n"
"saved settings from \".config\" without restarting Menuconfig.\n"
"\n"
"Other information\n"
"-----------------\n"
"If you use Menuconfig in an XTERM window, make sure you have your\n"
"$TERM variable set to point to an xterm definition which supports\n"
"color.  Otherwise, Menuconfig will look rather bad.  Menuconfig will\n"
"not display correctly in an RXVT window because rxvt displays only one\n"
"intensity of color, bright.\n"
"\n"
"Menuconfig will display larger menus on screens or xterms which are\n"
"set to display more than the standard 25 row by 80 column geometry.\n"
"In order for this to work, the \"stty size\" command must be able to\n"
"display the screen's current row and column geometry.  I STRONGLY\n"
"RECOMMEND that you make sure you do NOT have the shell variables\n"
"LINES and COLUMNS exported into your environment.  Some distributions\n"
"export those variables via /etc/profile.  Some ncurses programs can\n"
"become confused when those variables (LINES & COLUMNS) don't reflect\n"
"the true screen size.\n"
"\n"
"Optional personality available\n"
"------------------------------\n"
"If you prefer to have all of the options listed in a single menu,\n"
"rather than the default multimenu hierarchy, run the menuconfig with\n"
"MENUCONFIG_MODE environment variable set to single_menu. Example:\n"
"\n"
"make MENUCONFIG_MODE=single_menu menuconfig\n"
"\n"
"<Enter> will then unroll the appropriate category, or enfold it if it\n"
"is already unrolled.\n"
"\n"
"Note that this mode can eventually be a little more CPU expensive\n"
"(especially with a larger number of unrolled categories) than the\n"
"default mode.\n"
"\n"

"Search\n"
"-------\n"
"Pressing the forward-slash (/) anywhere brings up a search dialog box.\n"
"\n"

"Different color themes available\n"
"--------------------------------\n"
"It is possible to select different color themes using the variable\n"
"MENUCONFIG_COLOR. To select a theme use:\n"
"\n"
"make MENUCONFIG_COLOR=<theme> menuconfig\n"
"\n"
"Available themes are\n"
" mono       => selects colors suitable for monochrome displays\n"
" blackbg    => selects a color scheme with black background\n"
" classic    => theme with blue background. The classic look\n"
" bluetitle  => an LCD friendly version of classic. (default)\n"
"\n",
menu_instructions[] =
	"Arrow keys navigate the menu.  "
	"<Enter> selects submenus ---> (or empty submenus ----).  "
	"Highlighted letters are hotkeys.  "
	"Pressing <Y> includes, <N> excludes, <M> modularizes features.  "
	"Press <Esc><Esc> to exit, <?> for Help, </> for Search.  "
	"Legend: [*] built-in  [ ] excluded  <M> module  < > module capable",
radiolist_instructions[] =
	"Use the arrow keys to navigate this window or "
	"press the hotkey of the item you wish to select "
	"followed by the <SPACE BAR>. "
	"Press <?> for additional information about this option.",
inputbox_instructions_int[] =
	"Please enter a decimal value. "
	"Fractions will not be accepted.  "
	"Use the <TAB> key to move from the input field to the buttons below it.",
inputbox_instructions_hex[] =
	"Please enter a hexadecimal value. "
	"Use the <TAB> key to move from the input field to the buttons below it.",
inputbox_instructions_string[] =
	"Please enter a string value. "
	"Use the <TAB> key to move from the input field to the buttons below it.",
setmod_text[] =
	"This feature depends on another which has been configured as a module.\n"
	"As a result, this feature will be built as a module.",
load_config_text[] =
	"Enter the name of the configuration file you wish to load.  "
	"Accept the name shown to restore the configuration you "
	"last retrieved.  Leave blank to abort.",
load_config_help[] =
	"\n"
	"For various reasons, one may wish to keep several different\n"
	"configurations available on a single machine.\n"
	"\n"
	"If you have saved a previous configuration in a file other than the\n"
	"default one, entering its name here will allow you to modify that\n"
	"configuration.\n"
	"\n"
	"If you are uncertain, then you have probably never used alternate\n"
	"configuration files. You should therefore leave this blank to abort.\n",
save_config_text[] =
	"Enter a filename to which this configuration should be saved "
	"as an alternate.  Leave blank to abort.",
save_config_help[] =
	"\n"
	"For various reasons, one may wish to keep different configurations\n"
	"available on a single machine.\n"
	"\n"
	"Entering a file name here will allow you to later retrieve, modify\n"
	"and use the current configuration as an alternate to whatever\n"
	"configuration options you have selected at that time.\n"
	"\n"
	"If you are uncertain what all this means then you should probably\n"
	"leave this blank.\n",
search_help[] =
	"\n"
	"Search for symbols and display their relations.\n"
	"Regular expressions are allowed.\n"
	"Example: search for \"^FOO\"\n"
	"Result:\n"
	"-----------------------------------------------------------------\n"
	"Symbol: FOO [=m]\n"
	"Type  : tristate\n"
	"Prompt: Foo bus is used to drive the bar HW\n"
	"  Location:\n"
	"    -> Bus options (PCI, PCMCIA, EISA, ISA)\n"
	"      -> PCI support (PCI [=y])\n"
	"(1)     -> PCI access mode (<choice> [=y])\n"
	"  Defined at drivers/pci/Kconfig:47\n"
	"  Depends on: X86_LOCAL_APIC && X86_IO_APIC || IA64\n"
	"  Selects: LIBCRC32\n"
	"  Selected by: BAR [=n]\n"
	"-----------------------------------------------------------------\n"
	"o The line 'Type:' shows the type of the configuration option for\n"
	"  this symbol (bool, tristate, string, ...)\n"
	"o The line 'Prompt:' shows the text used in the menu structure for\n"
	"  this symbol\n"
	"o The 'Defined at' line tells at what file / line number the symbol\n"
	"  is defined\n"
	"o The 'Depends on:' line tells what symbols need to be defined for\n"
	"  this symbol to be visible in the menu (selectable)\n"
	"o The 'Location:' lines tells where in the menu structure this symbol\n"
	"  is located\n"
	"    A location followed by a [=y] indicates that this is a\n"
	"    selectable menu item - and the current value is displayed inside\n"
	"    brackets.\n"
	"    Press the key in the (#) prefix to jump directly to that\n"
	"    location. You will be returned to the current search results\n"
	"    after exiting this new menu.\n"
	"o The 'Selects:' line tells what symbols will be automatically\n"
	"  selected if this symbol is selected (y or m)\n"
	"o The 'Selected by' line tells what symbol has selected this symbol\n"
	"\n"
	"Only relevant lines are shown.\n"
	"\n\n"
	"Search examples:\n"
	"Examples: USB	=> find all symbols containing USB\n"
	"          ^USB => find all symbols starting with USB\n"
	"          USB$ => find all symbols ending with USB\n"
	"\n";

static int indent;
static struct menu *current_menu;
static int child_count;
static int single_menu_mode;
static int show_all_options;
static int save_and_exit;
static int silent;

static void conf(struct menu *menu, struct menu *active_menu);
static void conf_choice(struct menu *menu);
static void conf_string(struct menu *menu);
static void conf_load(void);
static void conf_save(void);
static int show_textbox_ext(const char *title, char *text, int r, int c,
			    int *keys, int *vscroll, int *hscroll,
			    update_text_fn update_text, void *data);
static void show_textbox(const char *title, const char *text, int r, int c);
static void show_helptext(const char *title, const char *text);
static void show_help(struct menu *menu);

static char filename[PATH_MAX+1];
static void set_config_filename(const char *config_filename)
{
	static char menu_backtitle[PATH_MAX+128];

	snprintf(menu_backtitle, sizeof(menu_backtitle), "%s - %s",
		 config_filename, rootmenu.prompt->text);
	set_dialog_backtitle(menu_backtitle);

	snprintf(filename, sizeof(filename), "%s", config_filename);
}

struct subtitle_part {
	struct list_head entries;
	const char *text;
};
static LIST_HEAD(trail);

static struct subtitle_list *subtitles;
static void set_subtitle(void)
{
	struct subtitle_part *sp;
	struct subtitle_list *pos, *tmp;

	for (pos = subtitles; pos != NULL; pos = tmp) {
		tmp = pos->next;
		free(pos);
	}

	subtitles = NULL;
	list_for_each_entry(sp, &trail, entries) {
		if (sp->text) {
			if (pos) {
				pos->next = xcalloc(1, sizeof(*pos));
				pos = pos->next;
			} else {
				subtitles = pos = xcalloc(1, sizeof(*pos));
			}
			pos->text = sp->text;
		}
	}

	set_dialog_subtitles(subtitles);
}

static void reset_subtitle(void)
{
	struct subtitle_list *pos, *tmp;

	for (pos = subtitles; pos != NULL; pos = tmp) {
		tmp = pos->next;
		free(pos);
	}
	subtitles = NULL;
	set_dialog_subtitles(subtitles);
}

struct search_data {
	struct list_head *head;
	struct menu **targets;
	int *keys;
};

static void update_text(char *buf, size_t start, size_t end, void *_data)
{
	struct search_data *data = _data;
	struct jump_key *pos;
	int k = 0;

	list_for_each_entry(pos, data->head, entries) {
		if (pos->offset >= start && pos->offset < end) {
			char header[4];

			if (k < JUMP_NB) {
				int key = '0' + (pos->index % JUMP_NB) + 1;

				sprintf(header, "(%c)", key);
				data->keys[k] = key;
				data->targets[k] = pos->target;
				k++;
			} else {
				sprintf(header, "   ");
			}

			memcpy(buf + pos->offset, header, sizeof(header) - 1);
		}
	}
	data->keys[k] = 0;
}

static void search_conf(void)
{
	struct symbol **sym_arr;
	struct gstr res;
	struct gstr title;
	char *dialog_input;
	int dres, vscroll = 0, hscroll = 0;
	bool again;
	struct gstr sttext;
	struct subtitle_part stpart;

	title = str_new();
	str_printf( &title, "Enter (sub)string or regexp to search for "
			      "(with or without \"%s\")", CONFIG_);

again:
	dialog_clear();
	dres = dialog_inputbox("Search Configuration Parameter",
			      str_get(&title),
			      10, 75, "");
	switch (dres) {
	case 0:
		break;
	case 1:
		show_helptext("Search Configuration", search_help);
		goto again;
	default:
		str_free(&title);
		return;
	}

	/* strip the prefix if necessary */
	dialog_input = dialog_input_result;
	if (strncasecmp(dialog_input_result, CONFIG_, strlen(CONFIG_)) == 0)
		dialog_input += strlen(CONFIG_);

	sttext = str_new();
	str_printf(&sttext, "Search (%s)", dialog_input_result);
	stpart.text = str_get(&sttext);
	list_add_tail(&stpart.entries, &trail);

	sym_arr = sym_re_search(dialog_input);
	do {
		LIST_HEAD(head);
		struct menu *targets[JUMP_NB];
		int keys[JUMP_NB + 1], i;
		struct search_data data = {
			.head = &head,
			.targets = targets,
			.keys = keys,
		};
		struct jump_key *pos, *tmp;

		res = get_relations_str(sym_arr, &head);
		set_subtitle();
		dres = show_textbox_ext("Search Results", str_get(&res), 0, 0,
					keys, &vscroll, &hscroll, &update_text,
					&data);
		again = false;
		for (i = 0; i < JUMP_NB && keys[i]; i++)
			if (dres == keys[i]) {
				conf(targets[i]->parent, targets[i]);
				again = true;
			}
		str_free(&res);
		list_for_each_entry_safe(pos, tmp, &head, entries)
			free(pos);
	} while (again);
	free(sym_arr);
	str_free(&title);
	list_del(trail.prev);
	str_free(&sttext);
}

static void build_conf(struct menu *menu)
{
	struct symbol *sym;
	struct property *prop;
	struct menu *child;
	int type, tmp, doint = 2;
	tristate val;
	char ch;
	bool visible;

	/*
	 * note: menu_is_visible() has side effect that it will
	 * recalc the value of the symbol.
	 */
	visible = menu_is_visible(menu);
	if (show_all_options && !menu_has_prompt(menu))
		return;
	else if (!show_all_options && !visible)
		return;

	sym = menu->sym;
	prop = menu->prompt;
	if (!sym) {
		if (prop && menu != current_menu) {
			const char *prompt = menu_get_prompt(menu);
			switch (prop->type) {
			case P_MENU:
				child_count++;
				if (single_menu_mode) {
					item_make("%s%*c%s",
						  menu->data ? "-->" : "++>",
						  indent + 1, ' ', prompt);
				} else
					item_make("   %*c%s  %s",
						  indent + 1, ' ', prompt,
						  menu_is_empty(menu) ? "----" : "--->");
				item_set_tag('m');
				item_set_data(menu);
				if (single_menu_mode && menu->data)
					goto conf_childs;
				return;
			case P_COMMENT:
				if (prompt) {
					child_count++;
					item_make("   %*c*** %s ***", indent + 1, ' ', prompt);
					item_set_tag(':');
					item_set_data(menu);
				}
				break;
			default:
				if (prompt) {
					child_count++;
					item_make("---%*c%s", indent + 1, ' ', prompt);
					item_set_tag(':');
					item_set_data(menu);
				}
			}
		} else
			doint = 0;
		goto conf_childs;
	}

	type = sym_get_type(sym);
	if (sym_is_choice(sym)) {
		struct symbol *def_sym = sym_get_choice_value(sym);
		struct menu *def_menu = NULL;

		child_count++;
		for (child = menu->list; child; child = child->next) {
			if (menu_is_visible(child) && child->sym == def_sym)
				def_menu = child;
		}

		val = sym_get_tristate_value(sym);
		if (sym_is_changeable(sym)) {
			switch (type) {
			case S_BOOLEAN:
				item_make("[%c]", val == no ? ' ' : '*');
				break;
			case S_TRISTATE:
				switch (val) {
				case yes: ch = '*'; break;
				case mod: ch = 'M'; break;
				default:  ch = ' '; break;
				}
				item_make("<%c>", ch);
				break;
			}
			item_set_tag('t');
			item_set_data(menu);
		} else {
			item_make("   ");
			item_set_tag(def_menu ? 't' : ':');
			item_set_data(menu);
		}

		item_add_str("%*c%s", indent + 1, ' ', menu_get_prompt(menu));
		if (val == yes) {
			if (def_menu) {
				item_add_str(" (%s)", menu_get_prompt(def_menu));
				item_add_str("  --->");
				if (def_menu->list) {
					indent += 2;
					build_conf(def_menu);
					indent -= 2;
				}
			}
			return;
		}
	} else {
		if (menu == current_menu) {
			item_make("---%*c%s", indent + 1, ' ', menu_get_prompt(menu));
			item_set_tag(':');
			item_set_data(menu);
			goto conf_childs;
		}
		child_count++;
		val = sym_get_tristate_value(sym);
		if (sym_is_choice_value(sym) && val == yes) {
			item_make("   ");
			item_set_tag(':');
			item_set_data(menu);
		} else {
			switch (type) {
			case S_BOOLEAN:
				if (sym_is_changeable(sym))
					item_make("[%c]", val == no ? ' ' : '*');
				else
					item_make("-%c-", val == no ? ' ' : '*');
				item_set_tag('t');
				item_set_data(menu);
				break;
			case S_TRISTATE:
				switch (val) {
				case yes: ch = '*'; break;
				case mod: ch = 'M'; break;
				default:  ch = ' '; break;
				}
				if (sym_is_changeable(sym)) {
					if (sym->rev_dep.tri == mod)
						item_make("{%c}", ch);
					else
						item_make("<%c>", ch);
				} else
					item_make("-%c-", ch);
				item_set_tag('t');
				item_set_data(menu);
				break;
			default:
				tmp = 2 + strlen(sym_get_string_value(sym)); /* () = 2 */
				item_make("(%s)", sym_get_string_value(sym));
				tmp = indent - tmp + 4;
				if (tmp < 0)
					tmp = 0;
				item_add_str("%*c%s%s", tmp, ' ', menu_get_prompt(menu),
					     (sym_has_value(sym) || !sym_is_changeable(sym)) ?
					     "" : " (NEW)");
				item_set_tag('s');
				item_set_data(menu);
				goto conf_childs;
			}
		}
		item_add_str("%*c%s%s", indent + 1, ' ', menu_get_prompt(menu),
			  (sym_has_value(sym) || !sym_is_changeable(sym)) ?
			  "" : " (NEW)");
		if (menu->prompt->type == P_MENU) {
			item_add_str("  %s", menu_is_empty(menu) ? "----" : "--->");
			return;
		}
	}

conf_childs:
	indent += doint;
	for (child = menu->list; child; child = child->next)
		build_conf(child);
	indent -= doint;
}

static void conf(struct menu *menu, struct menu *active_menu)
{
	struct menu *submenu;
	const char *prompt = menu_get_prompt(menu);
	struct subtitle_part stpart;
	struct symbol *sym;
	int res;
	int s_scroll = 0;

	if (menu != &rootmenu)
		stpart.text = menu_get_prompt(menu);
	else
		stpart.text = NULL;
	list_add_tail(&stpart.entries, &trail);

	while (1) {
		item_reset();
		current_menu = menu;
		build_conf(menu);
		if (!child_count)
			break;
		set_subtitle();
		dialog_clear();
		res = dialog_menu(prompt ? prompt : "Main Menu",
				  menu_instructions,
				  active_menu, &s_scroll);
		if (res == 1 || res == KEY_ESC || res == -ERRDISPLAYTOOSMALL)
			break;
		if (item_count() != 0) {
			if (!item_activate_selected())
				continue;
			if (!item_tag())
				continue;
		}
		submenu = item_data();
		active_menu = item_data();
		if (submenu)
			sym = submenu->sym;
		else
			sym = NULL;

		switch (res) {
		case 0:
			switch (item_tag()) {
			case 'm':
				if (single_menu_mode)
					submenu->data = (void *) (long) !submenu->data;
				else
					conf(submenu, NULL);
				break;
			case 't':
				if (sym_is_choice(sym) && sym_get_tristate_value(sym) == yes)
					conf_choice(submenu);
				else if (submenu->prompt->type == P_MENU)
					conf(submenu, NULL);
				break;
			case 's':
				conf_string(submenu);
				break;
			}
			break;
		case 2:
			if (sym)
				show_help(submenu);
			else {
				reset_subtitle();
				show_helptext("README", mconf_readme);
			}
			break;
		case 3:
			reset_subtitle();
			conf_save();
			break;
		case 4:
			reset_subtitle();
			conf_load();
			break;
		case 5:
			if (item_is_tag('t')) {
				if (sym_set_tristate_value(sym, yes))
					break;
				if (sym_set_tristate_value(sym, mod))
					show_textbox(NULL, setmod_text, 6, 74);
			}
			break;
		case 6:
			if (item_is_tag('t'))
				sym_set_tristate_value(sym, no);
			break;
		case 7:
			if (item_is_tag('t'))
				sym_set_tristate_value(sym, mod);
			break;
		case 8:
			if (item_is_tag('t'))
				sym_toggle_tristate_value(sym);
			else if (item_is_tag('m'))
				conf(submenu, NULL);
			break;
		case 9:
			search_conf();
			break;
		case 10:
			show_all_options = !show_all_options;
			break;
		}
	}

	list_del(trail.prev);
}

static int show_textbox_ext(const char *title, char *text, int r, int c, int
			    *keys, int *vscroll, int *hscroll, update_text_fn
			    update_text, void *data)
{
	dialog_clear();
	return dialog_textbox(title, text, r, c, keys, vscroll, hscroll,
			      update_text, data);
}

static void show_textbox(const char *title, const char *text, int r, int c)
{
	show_textbox_ext(title, (char *) text, r, c, (int []) {0}, NULL, NULL,
			 NULL, NULL);
}

static void show_helptext(const char *title, const char *text)
{
	show_textbox(title, text, 0, 0);
}

static void conf_message_callback(const char *s)
{
	if (save_and_exit) {
		if (!silent)
			printf("%s", s);
	} else {
		show_textbox(NULL, s, 6, 60);
	}
}

static void show_help(struct menu *menu)
{
	struct gstr help = str_new();

	help.max_width = getmaxx(stdscr) - 10;
	menu_get_ext_help(menu, &help);

	show_helptext(menu_get_prompt(menu), str_get(&help));
	str_free(&help);
}

static void conf_choice(struct menu *menu)
{
	const char *prompt = menu_get_prompt(menu);
	struct menu *child;
	struct symbol *active;

	active = sym_get_choice_value(menu->sym);
	while (1) {
		int res;
		int selected;
		item_reset();

		current_menu = menu;
		for (child = menu->list; child; child = child->next) {
			if (!menu_is_visible(child))
				continue;
			if (child->sym)
				item_make("%s", menu_get_prompt(child));
			else {
				item_make("*** %s ***", menu_get_prompt(child));
				item_set_tag(':');
			}
			item_set_data(child);
			if (child->sym == active)
				item_set_selected(1);
			if (child->sym == sym_get_choice_value(menu->sym))
				item_set_tag('X');
		}
		dialog_clear();
		res = dialog_checklist(prompt ? prompt : "Main Menu",
					radiolist_instructions,
					MENUBOX_HEIGTH_MIN,
					MENUBOX_WIDTH_MIN,
					CHECKLIST_HEIGTH_MIN);
		selected = item_activate_selected();
		switch (res) {
		case 0:
			if (selected) {
				child = item_data();
				if (!child->sym)
					break;

				sym_set_tristate_value(child->sym, yes);
			}
			return;
		case 1:
			if (selected) {
				child = item_data();
				show_help(child);
				active = child->sym;
			} else
				show_help(menu);
			break;
		case KEY_ESC:
			return;
		case -ERRDISPLAYTOOSMALL:
			return;
		}
	}
}

static void conf_string(struct menu *menu)
{
	const char *prompt = menu_get_prompt(menu);

	while (1) {
		int res;
		const char *heading;

		switch (sym_get_type(menu->sym)) {
		case S_INT:
			heading = inputbox_instructions_int;
			break;
		case S_HEX:
			heading = inputbox_instructions_hex;
			break;
		case S_STRING:
			heading = inputbox_instructions_string;
			break;
		default:
			heading = "Internal mconf error!";
		}
		dialog_clear();
		res = dialog_inputbox(prompt ? prompt : "Main Menu",
				      heading, 10, 75,
				      sym_get_string_value(menu->sym));
		switch (res) {
		case 0:
			if (sym_set_string_value(menu->sym, dialog_input_result))
				return;
			show_textbox(NULL, "You have made an invalid entry.", 5, 43);
			break;
		case 1:
			show_help(menu);
			break;
		case KEY_ESC:
			return;
		}
	}
}

static void conf_load(void)
{

	while (1) {
		int res;
		dialog_clear();
		res = dialog_inputbox(NULL, load_config_text,
				      11, 55, filename);
		switch(res) {
		case 0:
			if (!dialog_input_result[0])
				return;
			if (!conf_read(dialog_input_result)) {
				set_config_filename(dialog_input_result);
				conf_set_changed(true);
				return;
			}
			show_textbox(NULL, "File does not exist!", 5, 38);
			break;
		case 1:
			show_helptext("Load Alternate Configuration", load_config_help);
			break;
		case KEY_ESC:
			return;
		}
	}
}

static void conf_save(void)
{
	while (1) {
		int res;
		dialog_clear();
		res = dialog_inputbox(NULL, save_config_text,
				      11, 55, filename);
		switch(res) {
		case 0:
			if (!dialog_input_result[0])
				return;
			if (!conf_write(dialog_input_result)) {
				set_config_filename(dialog_input_result);
				return;
			}
			show_textbox(NULL, "Can't create file!", 5, 60);
			break;
		case 1:
			show_helptext("Save Alternate Configuration", save_config_help);
			break;
		case KEY_ESC:
			return;
		}
	}
}

static int handle_exit(void)
{
	int res;

	save_and_exit = 1;
	reset_subtitle();
	dialog_clear();
	if (conf_get_changed())
		res = dialog_yesno(NULL,
				   "Do you wish to save your new configuration?\n"
				     "(Press <ESC><ESC> to continue kernel configuration.)",
				   6, 60);
	else
		res = -1;

	end_dialog(saved_x, saved_y);

	switch (res) {
	case 0:
		if (conf_write(filename)) {
			fprintf(stderr, "\n\n"
					  "Error while writing of the configuration.\n"
					  "Your configuration changes were NOT saved."
					  "\n\n");
			return 1;
		}
		conf_write_autoconf(0);
		/* fall through */
	case -1:
		if (!silent)
			printf("\n\n"
				 "*** End of the configuration.\n"
				 "*** Execute 'make' to start the build or try 'make help'."
				 "\n\n");
		res = 0;
		break;
	default:
		if (!silent)
			fprintf(stderr, "\n\n"
					  "Your configuration changes were NOT saved."
					  "\n\n");
		if (res != KEY_ESC)
			res = 0;
	}

	return res;
}

static void sig_handler(int signo)
{
	exit(handle_exit());
}

int main(int ac, char **av)
{
	char *mode;
	int res;

	signal(SIGINT, sig_handler);

	if (ac > 1 && strcmp(av[1], "-s") == 0) {
		silent = 1;
		/* Silence conf_read() until the real callback is set up */
		conf_set_message_callback(NULL);
		av++;
	}
	conf_parse(av[1]);
	conf_read(NULL);

	mode = getenv("MENUCONFIG_MODE");
	if (mode) {
		if (!strcasecmp(mode, "single_menu"))
			single_menu_mode = 1;
	}

	if (init_dialog(NULL)) {
		fprintf(stderr, "Your display is too small to run Menuconfig!\n");
		fprintf(stderr, "It must be at least 19 lines by 80 columns.\n");
		return 1;
	}

	set_config_filename(conf_get_configname());
	conf_set_message_callback(conf_message_callback);
	do {
		conf(&rootmenu, NULL);
		res = handle_exit();
	} while (res == KEY_ESC);

	return res;
}
