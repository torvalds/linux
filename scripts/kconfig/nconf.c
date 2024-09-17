// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Nir Tzachar <nir.tzachar@gmail.com>
 *
 * Derived from menuconfig.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "lkc.h"
#include "nconf.h"
#include <ctype.h>

static const char nconf_global_help[] =
"Help windows\n"
"------------\n"
"o  Global help:  Unless in a data entry window, pressing <F1> will give \n"
"   you the global help window, which you are just reading.\n"
"\n"
"o  A short version of the global help is available by pressing <F3>.\n"
"\n"
"o  Local help:  To get help related to the current menu entry, use any\n"
"   of <?> <h>, or if in a data entry window then press <F1>.\n"
"\n"
"\n"
"Menu entries\n"
"------------\n"
"This interface lets you select features and parameters for the kernel\n"
"build.  Kernel features can either be built-in, modularized, or removed.\n"
"Parameters must be entered as text or decimal or hexadecimal numbers.\n"
"\n"
"Menu entries beginning with following braces represent features that\n"
"  [ ]  can be built in or removed\n"
"  < >  can be built in, modularized or removed\n"
"  { }  can be built in or modularized, are selected by another feature\n"
"  - -  are selected by another feature\n"
"  XXX  cannot be selected.  Symbol Info <F2> tells you why.\n"
"*, M or whitespace inside braces means to build in, build as a module\n"
"or to exclude the feature respectively.\n"
"\n"
"To change any of these features, highlight it with the movement keys\n"
"listed below and press <y> to build it in, <m> to make it a module or\n"
"<n> to remove it.  You may press the <Space> key to cycle through the\n"
"available options.\n"
"\n"
"A trailing \"--->\" designates a submenu, a trailing \"----\" an\n"
"empty submenu.\n"
"\n"
"Menu navigation keys\n"
"----------------------------------------------------------------------\n"
"Linewise up                 <Up>    <k>\n"
"Linewise down               <Down>  <j>\n"
"Pagewise up                 <Page Up>\n"
"Pagewise down               <Page Down>\n"
"First entry                 <Home>\n"
"Last entry                  <End>\n"
"Enter a submenu             <Right>  <Enter>\n"
"Go back to parent menu      <Left>   <Esc>  <F5>\n"
"Close a help window         <Enter>  <Esc>  <F5>\n"
"Close entry window, apply   <Enter>\n"
"Close entry window, forget  <Esc>  <F5>\n"
"Start incremental, case-insensitive search for STRING in menu entries,\n"
"    no regex support, STRING is displayed in upper left corner\n"
"                            </>STRING\n"
"    Remove last character   <Backspace>\n"
"    Jump to next hit        <Down>\n"
"    Jump to previous hit    <Up>\n"
"Exit menu search mode       </>  <Esc>\n"
"Search for configuration variables with or without leading CONFIG_\n"
"                            <F8>RegExpr<Enter>\n"
"Verbose search help         <F8><F1>\n"
"----------------------------------------------------------------------\n"
"\n"
"Unless in a data entry window, key <1> may be used instead of <F1>,\n"
"<2> instead of <F2>, etc.\n"
"\n"
"\n"
"Radiolist (Choice list)\n"
"-----------------------\n"
"Use the movement keys listed above to select the option you wish to set\n"
"and press <Space>.\n"
"\n"
"\n"
"Data entry\n"
"----------\n"
"Enter the requested information and press <Enter>.  Hexadecimal values\n"
"may be entered without the \"0x\" prefix.\n"
"\n"
"\n"
"Text Box (Help Window)\n"
"----------------------\n"
"Use movement keys as listed in table above.\n"
"\n"
"Press any of <Enter> <Esc> <q> <F5> <F9> to exit.\n"
"\n"
"\n"
"Alternate configuration files\n"
"-----------------------------\n"
"nconfig supports switching between different configurations.\n"
"Press <F6> to save your current configuration.  Press <F7> and enter\n"
"a file name to load a previously saved configuration.\n"
"\n"
"\n"
"Terminal configuration\n"
"----------------------\n"
"If you use nconfig in a xterm window, make sure your TERM environment\n"
"variable specifies a terminal configuration which supports at least\n"
"16 colors.  Otherwise nconfig will look rather bad.\n"
"\n"
"If the \"stty size\" command reports the current terminalsize correctly,\n"
"nconfig will adapt to sizes larger than the traditional 80x25 \"standard\"\n"
"and display longer menus properly.\n"
"\n"
"\n"
"Single menu mode\n"
"----------------\n"
"If you prefer to have all of the menu entries listed in a single menu,\n"
"rather than the default multimenu hierarchy, run nconfig with\n"
"NCONFIG_MODE environment variable set to single_menu.  Example:\n"
"\n"
"make NCONFIG_MODE=single_menu nconfig\n"
"\n"
"<Enter> will then unfold the appropriate category, or fold it if it\n"
"is already unfolded.  Folded menu entries will be designated by a\n"
"leading \"++>\" and unfolded entries by a leading \"-->\".\n"
"\n"
"Note that this mode can eventually be a little more CPU expensive than\n"
"the default mode, especially with a larger number of unfolded submenus.\n"
"\n",
menu_no_f_instructions[] =
"Legend:  [*] built-in  [ ] excluded  <M> module  < > module capable.\n"
"Submenus are designated by a trailing \"--->\", empty ones by \"----\".\n"
"\n"
"Use the following keys to navigate the menus:\n"
"Move up or down with <Up> and <Down>.\n"
"Enter a submenu with <Enter> or <Right>.\n"
"Exit a submenu to its parent menu with <Esc> or <Left>.\n"
"Pressing <y> includes, <n> excludes, <m> modularizes features.\n"
"Pressing <Space> cycles through the available options.\n"
"To search for menu entries press </>.\n"
"<Esc> always leaves the current window.\n"
"\n"
"You do not have function keys support.\n"
"Press <1> instead of <F1>, <2> instead of <F2>, etc.\n"
"For verbose global help use key <1>.\n"
"For help related to the current menu entry press <?> or <h>.\n",
menu_instructions[] =
"Legend:  [*] built-in  [ ] excluded  <M> module  < > module capable.\n"
"Submenus are designated by a trailing \"--->\", empty ones by \"----\".\n"
"\n"
"Use the following keys to navigate the menus:\n"
"Move up or down with <Up> or <Down>.\n"
"Enter a submenu with <Enter> or <Right>.\n"
"Exit a submenu to its parent menu with <Esc> or <Left>.\n"
"Pressing <y> includes, <n> excludes, <m> modularizes features.\n"
"Pressing <Space> cycles through the available options.\n"
"To search for menu entries press </>.\n"
"<Esc> always leaves the current window.\n"
"\n"
"Pressing <1> may be used instead of <F1>, <2> instead of <F2>, etc.\n"
"For verbose global help press <F1>.\n"
"For help related to the current menu entry press <?> or <h>.\n",
radiolist_instructions[] =
"Press <Up>, <Down>, <Home> or <End> to navigate a radiolist, select\n"
"with <Space>.\n"
"For help related to the current entry press <?> or <h>.\n"
"For global help press <F1>.\n",
inputbox_instructions_int[] =
"Please enter a decimal value.\n"
"Fractions will not be accepted.\n"
"Press <Enter> to apply, <Esc> to cancel.",
inputbox_instructions_hex[] =
"Please enter a hexadecimal value.\n"
"Press <Enter> to apply, <Esc> to cancel.",
inputbox_instructions_string[] =
"Please enter a string value.\n"
"Press <Enter> to apply, <Esc> to cancel.",
setmod_text[] =
"This feature depends on another feature which has been configured as a\n"
"module.  As a result, the current feature will be built as a module too.",
load_config_text[] =
"Enter the name of the configuration file you wish to load.\n"
"Accept the name shown to restore the configuration you last\n"
"retrieved.  Leave empty to abort.",
load_config_help[] =
"For various reasons, one may wish to keep several different\n"
"configurations available on a single machine.\n"
"\n"
"If you have saved a previous configuration in a file other than the\n"
"default one, entering its name here will allow you to load and modify\n"
"that configuration.\n"
"\n"
"Leave empty to abort.\n",
save_config_text[] =
"Enter a filename to which this configuration should be saved\n"
"as an alternate.  Leave empty to abort.",
save_config_help[] =
"For various reasons, one may wish to keep several different\n"
"configurations available on a single machine.\n"
"\n"
"Entering a file name here will allow you to later retrieve, modify\n"
"and use the current configuration as an alternate to whatever\n"
"configuration options you have selected at that time.\n"
"\n"
"Leave empty to abort.\n",
search_help[] =
"Search for symbols (configuration variable names CONFIG_*) and display\n"
"their relations.  Regular expressions are supported.\n"
"Example:  Search for \"^FOO\".\n"
"Result:\n"
"-----------------------------------------------------------------\n"
"Symbol: FOO [ = m]\n"
"Prompt: Foo bus is used to drive the bar HW\n"
"Defined at drivers/pci/Kconfig:47\n"
"Depends on: X86_LOCAL_APIC && X86_IO_APIC || IA64\n"
"Location:\n"
"  -> Bus options (PCI, PCMCIA, EISA, ISA)\n"
"    -> PCI support (PCI [ = y])\n"
"      -> PCI access mode (<choice> [ = y])\n"
"Selects: LIBCRC32\n"
"Selected by: BAR\n"
"-----------------------------------------------------------------\n"
"o  The line 'Prompt:' shows the text displayed for this symbol in\n"
"   the menu hierarchy.\n"
"o  The 'Defined at' line tells at what file / line number the symbol is\n"
"   defined.\n"
"o  The 'Depends on:' line lists symbols that need to be defined for\n"
"   this symbol to be visible and selectable in the menu.\n"
"o  The 'Location:' lines tell, where in the menu structure this symbol\n"
"   is located.  A location followed by a [ = y] indicates that this is\n"
"   a selectable menu item, and the current value is displayed inside\n"
"   brackets.\n"
"o  The 'Selects:' line tells, what symbol will be automatically selected\n"
"   if this symbol is selected (y or m).\n"
"o  The 'Selected by' line tells what symbol has selected this symbol.\n"
"\n"
"Only relevant lines are shown.\n"
"\n\n"
"Search examples:\n"
"USB  => find all symbols containing USB\n"
"^USB => find all symbols starting with USB\n"
"USB$ => find all symbols ending with USB\n"
"\n";

struct mitem {
	char str[256];
	char tag;
	void *usrptr;
	int is_visible;
};

#define MAX_MENU_ITEMS 4096
static int show_all_items;
static int indent;
static struct menu *current_menu;
static int child_count;
static int single_menu_mode;
/* the window in which all information appears */
static WINDOW *main_window;
/* the largest size of the menu window */
static int mwin_max_lines;
static int mwin_max_cols;
/* the window in which we show option buttons */
static MENU *curses_menu;
static ITEM *curses_menu_items[MAX_MENU_ITEMS];
static struct mitem k_menu_items[MAX_MENU_ITEMS];
static unsigned int items_num;
static int global_exit;
/* the currently selected button */
static const char *current_instructions = menu_instructions;

static char *dialog_input_result;
static int dialog_input_result_len;

static void conf(struct menu *menu);
static void conf_choice(struct menu *menu);
static void conf_string(struct menu *menu);
static void conf_load(void);
static void conf_save(void);
static void show_help(struct menu *menu);
static int do_exit(void);
static void setup_windows(void);
static void search_conf(void);

typedef void (*function_key_handler_t)(int *key, struct menu *menu);
static void handle_f1(int *key, struct menu *current_item);
static void handle_f2(int *key, struct menu *current_item);
static void handle_f3(int *key, struct menu *current_item);
static void handle_f4(int *key, struct menu *current_item);
static void handle_f5(int *key, struct menu *current_item);
static void handle_f6(int *key, struct menu *current_item);
static void handle_f7(int *key, struct menu *current_item);
static void handle_f8(int *key, struct menu *current_item);
static void handle_f9(int *key, struct menu *current_item);

struct function_keys {
	const char *key_str;
	const char *func;
	function_key key;
	function_key_handler_t handler;
};

static const int function_keys_num = 9;
static struct function_keys function_keys[] = {
	{
		.key_str = "F1",
		.func = "Help",
		.key = F_HELP,
		.handler = handle_f1,
	},
	{
		.key_str = "F2",
		.func = "SymInfo",
		.key = F_SYMBOL,
		.handler = handle_f2,
	},
	{
		.key_str = "F3",
		.func = "Help 2",
		.key = F_INSTS,
		.handler = handle_f3,
	},
	{
		.key_str = "F4",
		.func = "ShowAll",
		.key = F_CONF,
		.handler = handle_f4,
	},
	{
		.key_str = "F5",
		.func = "Back",
		.key = F_BACK,
		.handler = handle_f5,
	},
	{
		.key_str = "F6",
		.func = "Save",
		.key = F_SAVE,
		.handler = handle_f6,
	},
	{
		.key_str = "F7",
		.func = "Load",
		.key = F_LOAD,
		.handler = handle_f7,
	},
	{
		.key_str = "F8",
		.func = "SymSearch",
		.key = F_SEARCH,
		.handler = handle_f8,
	},
	{
		.key_str = "F9",
		.func = "Exit",
		.key = F_EXIT,
		.handler = handle_f9,
	},
};

static void print_function_line(void)
{
	int i;
	int offset = 1;
	const int skip = 1;
	int lines = getmaxy(stdscr);

	for (i = 0; i < function_keys_num; i++) {
		wattrset(main_window, attr_function_highlight);
		mvwprintw(main_window, lines-3, offset,
				"%s",
				function_keys[i].key_str);
		wattrset(main_window, attr_function_text);
		offset += strlen(function_keys[i].key_str);
		mvwprintw(main_window, lines-3,
				offset, "%s",
				function_keys[i].func);
		offset += strlen(function_keys[i].func) + skip;
	}
	wattrset(main_window, attr_normal);
}

/* help */
static void handle_f1(int *key, struct menu *current_item)
{
	show_scroll_win(main_window,
			"Global help", nconf_global_help);
	return;
}

/* symbole help */
static void handle_f2(int *key, struct menu *current_item)
{
	show_help(current_item);
	return;
}

/* instructions */
static void handle_f3(int *key, struct menu *current_item)
{
	show_scroll_win(main_window,
			"Short help",
			current_instructions);
	return;
}

/* config */
static void handle_f4(int *key, struct menu *current_item)
{
	int res = btn_dialog(main_window,
			"Show all symbols?",
			2,
			"   <Show All>   ",
			"<Don't show all>");
	if (res == 0)
		show_all_items = 1;
	else if (res == 1)
		show_all_items = 0;

	return;
}

/* back */
static void handle_f5(int *key, struct menu *current_item)
{
	*key = KEY_LEFT;
	return;
}

/* save */
static void handle_f6(int *key, struct menu *current_item)
{
	conf_save();
	return;
}

/* load */
static void handle_f7(int *key, struct menu *current_item)
{
	conf_load();
	return;
}

/* search */
static void handle_f8(int *key, struct menu *current_item)
{
	search_conf();
	return;
}

/* exit */
static void handle_f9(int *key, struct menu *current_item)
{
	do_exit();
	return;
}

/* return != 0 to indicate the key was handles */
static int process_special_keys(int *key, struct menu *menu)
{
	int i;

	if (*key == KEY_RESIZE) {
		setup_windows();
		return 1;
	}

	for (i = 0; i < function_keys_num; i++) {
		if (*key == KEY_F(function_keys[i].key) ||
		    *key == '0' + function_keys[i].key){
			function_keys[i].handler(key, menu);
			return 1;
		}
	}

	return 0;
}

static void clean_items(void)
{
	int i;
	for (i = 0; curses_menu_items[i]; i++)
		free_item(curses_menu_items[i]);
	bzero(curses_menu_items, sizeof(curses_menu_items));
	bzero(k_menu_items, sizeof(k_menu_items));
	items_num = 0;
}

typedef enum {MATCH_TINKER_PATTERN_UP, MATCH_TINKER_PATTERN_DOWN,
	FIND_NEXT_MATCH_DOWN, FIND_NEXT_MATCH_UP} match_f;

/* return the index of the matched item, or -1 if no such item exists */
static int get_mext_match(const char *match_str, match_f flag)
{
	int match_start, index;

	/* Do not search if the menu is empty (i.e. items_num == 0) */
	match_start = item_index(current_item(curses_menu));
	if (match_start == ERR)
		return -1;

	if (flag == FIND_NEXT_MATCH_DOWN)
		++match_start;
	else if (flag == FIND_NEXT_MATCH_UP)
		--match_start;

	match_start = (match_start + items_num) % items_num;
	index = match_start;
	while (true) {
		char *str = k_menu_items[index].str;
		if (strcasestr(str, match_str) != NULL)
			return index;
		if (flag == FIND_NEXT_MATCH_UP ||
		    flag == MATCH_TINKER_PATTERN_UP)
			--index;
		else
			++index;
		index = (index + items_num) % items_num;
		if (index == match_start)
			return -1;
	}
}

/* Make a new item. */
static void item_make(struct menu *menu, char tag, const char *fmt, ...)
{
	va_list ap;

	if (items_num > MAX_MENU_ITEMS-1)
		return;

	bzero(&k_menu_items[items_num], sizeof(k_menu_items[0]));
	k_menu_items[items_num].tag = tag;
	k_menu_items[items_num].usrptr = menu;
	if (menu != NULL)
		k_menu_items[items_num].is_visible =
			menu_is_visible(menu);
	else
		k_menu_items[items_num].is_visible = 1;

	va_start(ap, fmt);
	vsnprintf(k_menu_items[items_num].str,
		  sizeof(k_menu_items[items_num].str),
		  fmt, ap);
	va_end(ap);

	if (!k_menu_items[items_num].is_visible)
		memcpy(k_menu_items[items_num].str, "XXX", 3);

	curses_menu_items[items_num] = new_item(
			k_menu_items[items_num].str,
			k_menu_items[items_num].str);
	set_item_userptr(curses_menu_items[items_num],
			&k_menu_items[items_num]);
	/*
	if (!k_menu_items[items_num].is_visible)
		item_opts_off(curses_menu_items[items_num], O_SELECTABLE);
	*/

	items_num++;
	curses_menu_items[items_num] = NULL;
}

/* very hackish. adds a string to the last item added */
static void item_add_str(const char *fmt, ...)
{
	va_list ap;
	int index = items_num-1;
	char new_str[256];
	char tmp_str[256];

	if (index < 0)
		return;

	va_start(ap, fmt);
	vsnprintf(new_str, sizeof(new_str), fmt, ap);
	va_end(ap);
	snprintf(tmp_str, sizeof(tmp_str), "%s%s",
			k_menu_items[index].str, new_str);
	strncpy(k_menu_items[index].str,
		tmp_str,
		sizeof(k_menu_items[index].str));

	free_item(curses_menu_items[index]);
	curses_menu_items[index] = new_item(
			k_menu_items[index].str,
			k_menu_items[index].str);
	set_item_userptr(curses_menu_items[index],
			&k_menu_items[index]);
}

/* get the tag of the currently selected item */
static char item_tag(void)
{
	ITEM *cur;
	struct mitem *mcur;

	cur = current_item(curses_menu);
	if (cur == NULL)
		return 0;
	mcur = (struct mitem *) item_userptr(cur);
	return mcur->tag;
}

static int curses_item_index(void)
{
	return  item_index(current_item(curses_menu));
}

static void *item_data(void)
{
	ITEM *cur;
	struct mitem *mcur;

	cur = current_item(curses_menu);
	if (!cur)
		return NULL;
	mcur = (struct mitem *) item_userptr(cur);
	return mcur->usrptr;

}

static int item_is_tag(char tag)
{
	return item_tag() == tag;
}

static char filename[PATH_MAX+1];
static char menu_backtitle[PATH_MAX+128];
static void set_config_filename(const char *config_filename)
{
	snprintf(menu_backtitle, sizeof(menu_backtitle), "%s - %s",
		 config_filename, rootmenu.prompt->text);

	snprintf(filename, sizeof(filename), "%s", config_filename);
}

/* return = 0 means we are successful.
 * -1 means go on doing what you were doing
 */
static int do_exit(void)
{
	int res;
	if (!conf_get_changed()) {
		global_exit = 1;
		return 0;
	}
	res = btn_dialog(main_window,
			"Do you wish to save your new configuration?\n"
				"<ESC> to cancel and resume nconfig.",
			2,
			"   <save>   ",
			"<don't save>");
	if (res == KEY_EXIT) {
		global_exit = 0;
		return -1;
	}

	/* if we got here, the user really wants to exit */
	switch (res) {
	case 0:
		res = conf_write(filename);
		if (res)
			btn_dialog(
				main_window,
				"Error during writing of configuration.\n"
				  "Your configuration changes were NOT saved.",
				  1,
				  "<OK>");
		conf_write_autoconf(0);
		break;
	default:
		btn_dialog(
			main_window,
			"Your configuration changes were NOT saved.",
			1,
			"<OK>");
		break;
	}
	global_exit = 1;
	return 0;
}


static void search_conf(void)
{
	struct symbol **sym_arr;
	struct gstr res;
	struct gstr title;
	char *dialog_input;
	int dres;

	title = str_new();
	str_printf( &title, "Enter (sub)string or regexp to search for "
			      "(with or without \"%s\")", CONFIG_);

again:
	dres = dialog_inputbox(main_window,
			"Search Configuration Parameter",
			str_get(&title),
			"", &dialog_input_result, &dialog_input_result_len);
	switch (dres) {
	case 0:
		break;
	case 1:
		show_scroll_win(main_window,
				"Search Configuration", search_help);
		goto again;
	default:
		str_free(&title);
		return;
	}

	/* strip the prefix if necessary */
	dialog_input = dialog_input_result;
	if (strncasecmp(dialog_input_result, CONFIG_, strlen(CONFIG_)) == 0)
		dialog_input += strlen(CONFIG_);

	sym_arr = sym_re_search(dialog_input);
	res = get_relations_str(sym_arr, NULL);
	free(sym_arr);
	show_scroll_win(main_window,
			"Search Results", str_get(&res));
	str_free(&res);
	str_free(&title);
}


static void build_conf(struct menu *menu)
{
	struct symbol *sym;
	struct property *prop;
	struct menu *child;
	int type, tmp, doint = 2;
	tristate val;
	char ch;

	if (!menu || (!show_all_items && !menu_is_visible(menu)))
		return;

	sym = menu->sym;
	prop = menu->prompt;
	if (!sym) {
		if (prop && menu != current_menu) {
			const char *prompt = menu_get_prompt(menu);
			enum prop_type ptype;
			ptype = menu->prompt ? menu->prompt->type : P_UNKNOWN;
			switch (ptype) {
			case P_MENU:
				child_count++;
				if (single_menu_mode) {
					item_make(menu, 'm',
						"%s%*c%s",
						menu->data ? "-->" : "++>",
						indent + 1, ' ', prompt);
				} else
					item_make(menu, 'm',
						  "   %*c%s  %s",
						  indent + 1, ' ', prompt,
						  menu_is_empty(menu) ? "----" : "--->");

				if (single_menu_mode && menu->data)
					goto conf_childs;
				return;
			case P_COMMENT:
				if (prompt) {
					child_count++;
					item_make(menu, ':',
						"   %*c*** %s ***",
						indent + 1, ' ',
						prompt);
				}
				break;
			default:
				if (prompt) {
					child_count++;
					item_make(menu, ':', "---%*c%s",
						indent + 1, ' ',
						prompt);
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
				item_make(menu, 't', "[%c]",
						val == no ? ' ' : '*');
				break;
			case S_TRISTATE:
				switch (val) {
				case yes:
					ch = '*';
					break;
				case mod:
					ch = 'M';
					break;
				default:
					ch = ' ';
					break;
				}
				item_make(menu, 't', "<%c>", ch);
				break;
			}
		} else {
			item_make(menu, def_menu ? 't' : ':', "   ");
		}

		item_add_str("%*c%s", indent + 1,
				' ', menu_get_prompt(menu));
		if (val == yes) {
			if (def_menu) {
				item_add_str(" (%s)",
					menu_get_prompt(def_menu));
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
			item_make(menu, ':',
				"---%*c%s", indent + 1,
				' ', menu_get_prompt(menu));
			goto conf_childs;
		}
		child_count++;
		val = sym_get_tristate_value(sym);
		if (sym_is_choice_value(sym) && val == yes) {
			item_make(menu, ':', "   ");
		} else {
			switch (type) {
			case S_BOOLEAN:
				if (sym_is_changeable(sym))
					item_make(menu, 't', "[%c]",
						val == no ? ' ' : '*');
				else
					item_make(menu, 't', "-%c-",
						val == no ? ' ' : '*');
				break;
			case S_TRISTATE:
				switch (val) {
				case yes:
					ch = '*';
					break;
				case mod:
					ch = 'M';
					break;
				default:
					ch = ' ';
					break;
				}
				if (sym_is_changeable(sym)) {
					if (sym->rev_dep.tri == mod)
						item_make(menu,
							't', "{%c}", ch);
					else
						item_make(menu,
							't', "<%c>", ch);
				} else
					item_make(menu, 't', "-%c-", ch);
				break;
			default:
				tmp = 2 + strlen(sym_get_string_value(sym));
				item_make(menu, 's', "    (%s)",
						sym_get_string_value(sym));
				tmp = indent - tmp + 4;
				if (tmp < 0)
					tmp = 0;
				item_add_str("%*c%s%s", tmp, ' ',
						menu_get_prompt(menu),
						(sym_has_value(sym) ||
						 !sym_is_changeable(sym)) ? "" :
						" (NEW)");
				goto conf_childs;
			}
		}
		item_add_str("%*c%s%s", indent + 1, ' ',
				menu_get_prompt(menu),
				(sym_has_value(sym) || !sym_is_changeable(sym)) ?
				"" : " (NEW)");
		if (menu->prompt && menu->prompt->type == P_MENU) {
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

static void reset_menu(void)
{
	unpost_menu(curses_menu);
	clean_items();
}

/* adjust the menu to show this item.
 * prefer not to scroll the menu if possible*/
static void center_item(int selected_index, int *last_top_row)
{
	int toprow;

	set_top_row(curses_menu, *last_top_row);
	toprow = top_row(curses_menu);
	if (selected_index < toprow ||
	    selected_index >= toprow+mwin_max_lines) {
		toprow = max(selected_index-mwin_max_lines/2, 0);
		if (toprow >= item_count(curses_menu)-mwin_max_lines)
			toprow = item_count(curses_menu)-mwin_max_lines;
		set_top_row(curses_menu, toprow);
	}
	set_current_item(curses_menu,
			curses_menu_items[selected_index]);
	*last_top_row = toprow;
	post_menu(curses_menu);
	refresh_all_windows(main_window);
}

/* this function assumes reset_menu has been called before */
static void show_menu(const char *prompt, const char *instructions,
		int selected_index, int *last_top_row)
{
	int maxx, maxy;
	WINDOW *menu_window;

	current_instructions = instructions;

	clear();
	print_in_middle(stdscr, 1, getmaxx(stdscr),
			menu_backtitle,
			attr_main_heading);

	wattrset(main_window, attr_main_menu_box);
	box(main_window, 0, 0);
	wattrset(main_window, attr_main_menu_heading);
	mvwprintw(main_window, 0, 3, " %s ", prompt);
	wattrset(main_window, attr_normal);

	set_menu_items(curses_menu, curses_menu_items);

	/* position the menu at the middle of the screen */
	scale_menu(curses_menu, &maxy, &maxx);
	maxx = min(maxx, mwin_max_cols-2);
	maxy = mwin_max_lines;
	menu_window = derwin(main_window,
			maxy,
			maxx,
			2,
			(mwin_max_cols-maxx)/2);
	keypad(menu_window, TRUE);
	set_menu_win(curses_menu, menu_window);
	set_menu_sub(curses_menu, menu_window);

	/* must reassert this after changing items, otherwise returns to a
	 * default of 16
	 */
	set_menu_format(curses_menu, maxy, 1);
	center_item(selected_index, last_top_row);
	set_menu_format(curses_menu, maxy, 1);

	print_function_line();

	/* Post the menu */
	post_menu(curses_menu);
	refresh_all_windows(main_window);
}

static void adj_match_dir(match_f *match_direction)
{
	if (*match_direction == FIND_NEXT_MATCH_DOWN)
		*match_direction =
			MATCH_TINKER_PATTERN_DOWN;
	else if (*match_direction == FIND_NEXT_MATCH_UP)
		*match_direction =
			MATCH_TINKER_PATTERN_UP;
	/* else, do no change.. */
}

struct match_state
{
	int in_search;
	match_f match_direction;
	char pattern[256];
};

/* Return 0 means I have handled the key. In such a case, ans should hold the
 * item to center, or -1 otherwise.
 * Else return -1 .
 */
static int do_match(int key, struct match_state *state, int *ans)
{
	char c = (char) key;
	int terminate_search = 0;
	*ans = -1;
	if (key == '/' || (state->in_search && key == 27)) {
		move(0, 0);
		refresh();
		clrtoeol();
		state->in_search = 1-state->in_search;
		bzero(state->pattern, sizeof(state->pattern));
		state->match_direction = MATCH_TINKER_PATTERN_DOWN;
		return 0;
	} else if (!state->in_search)
		return 1;

	if (isalnum(c) || isgraph(c) || c == ' ') {
		state->pattern[strlen(state->pattern)] = c;
		state->pattern[strlen(state->pattern)] = '\0';
		adj_match_dir(&state->match_direction);
		*ans = get_mext_match(state->pattern,
				state->match_direction);
	} else if (key == KEY_DOWN) {
		state->match_direction = FIND_NEXT_MATCH_DOWN;
		*ans = get_mext_match(state->pattern,
				state->match_direction);
	} else if (key == KEY_UP) {
		state->match_direction = FIND_NEXT_MATCH_UP;
		*ans = get_mext_match(state->pattern,
				state->match_direction);
	} else if (key == KEY_BACKSPACE || key == 8 || key == 127) {
		state->pattern[strlen(state->pattern)-1] = '\0';
		adj_match_dir(&state->match_direction);
	} else
		terminate_search = 1;

	if (terminate_search) {
		state->in_search = 0;
		bzero(state->pattern, sizeof(state->pattern));
		move(0, 0);
		refresh();
		clrtoeol();
		return -1;
	}
	return 0;
}

static void conf(struct menu *menu)
{
	struct menu *submenu = NULL;
	struct symbol *sym;
	int res;
	int current_index = 0;
	int last_top_row = 0;
	struct match_state match_state = {
		.in_search = 0,
		.match_direction = MATCH_TINKER_PATTERN_DOWN,
		.pattern = "",
	};

	while (!global_exit) {
		reset_menu();
		current_menu = menu;
		build_conf(menu);
		if (!child_count)
			break;

		show_menu(menu_get_prompt(menu), menu_instructions,
			  current_index, &last_top_row);
		keypad((menu_win(curses_menu)), TRUE);
		while (!global_exit) {
			if (match_state.in_search) {
				mvprintw(0, 0,
					"searching: %s", match_state.pattern);
				clrtoeol();
			}
			refresh_all_windows(main_window);
			res = wgetch(menu_win(curses_menu));
			if (!res)
				break;
			if (do_match(res, &match_state, &current_index) == 0) {
				if (current_index != -1)
					center_item(current_index,
						    &last_top_row);
				continue;
			}
			if (process_special_keys(&res,
						(struct menu *) item_data()))
				break;
			switch (res) {
			case KEY_DOWN:
			case 'j':
				menu_driver(curses_menu, REQ_DOWN_ITEM);
				break;
			case KEY_UP:
			case 'k':
				menu_driver(curses_menu, REQ_UP_ITEM);
				break;
			case KEY_NPAGE:
				menu_driver(curses_menu, REQ_SCR_DPAGE);
				break;
			case KEY_PPAGE:
				menu_driver(curses_menu, REQ_SCR_UPAGE);
				break;
			case KEY_HOME:
				menu_driver(curses_menu, REQ_FIRST_ITEM);
				break;
			case KEY_END:
				menu_driver(curses_menu, REQ_LAST_ITEM);
				break;
			case 'h':
			case '?':
				show_help((struct menu *) item_data());
				break;
			}
			if (res == 10 || res == 27 ||
				res == 32 || res == 'n' || res == 'y' ||
				res == KEY_LEFT || res == KEY_RIGHT ||
				res == 'm')
				break;
			refresh_all_windows(main_window);
		}

		refresh_all_windows(main_window);
		/* if ESC or left*/
		if (res == 27 || (menu != &rootmenu && res == KEY_LEFT))
			break;

		/* remember location in the menu */
		last_top_row = top_row(curses_menu);
		current_index = curses_item_index();

		if (!item_tag())
			continue;

		submenu = (struct menu *) item_data();
		if (!submenu || !menu_is_visible(submenu))
			continue;
		sym = submenu->sym;

		switch (res) {
		case ' ':
			if (item_is_tag('t'))
				sym_toggle_tristate_value(sym);
			else if (item_is_tag('m'))
				conf(submenu);
			break;
		case KEY_RIGHT:
		case 10: /* ENTER WAS PRESSED */
			switch (item_tag()) {
			case 'm':
				if (single_menu_mode)
					submenu->data =
						(void *) (long) !submenu->data;
				else
					conf(submenu);
				break;
			case 't':
				if (sym_is_choice(sym) &&
				    sym_get_tristate_value(sym) == yes)
					conf_choice(submenu);
				else if (submenu->prompt &&
					 submenu->prompt->type == P_MENU)
					conf(submenu);
				else if (res == 10)
					sym_toggle_tristate_value(sym);
				break;
			case 's':
				conf_string(submenu);
				break;
			}
			break;
		case 'y':
			if (item_is_tag('t')) {
				if (sym_set_tristate_value(sym, yes))
					break;
				if (sym_set_tristate_value(sym, mod))
					btn_dialog(main_window, setmod_text, 0);
			}
			break;
		case 'n':
			if (item_is_tag('t'))
				sym_set_tristate_value(sym, no);
			break;
		case 'm':
			if (item_is_tag('t'))
				sym_set_tristate_value(sym, mod);
			break;
		}
	}
}

static void conf_message_callback(const char *s)
{
	btn_dialog(main_window, s, 1, "<OK>");
}

static void show_help(struct menu *menu)
{
	struct gstr help;

	if (!menu)
		return;

	help = str_new();
	menu_get_ext_help(menu, &help);
	show_scroll_win(main_window, menu_get_prompt(menu), str_get(&help));
	str_free(&help);
}

static void conf_choice(struct menu *menu)
{
	const char *prompt = menu_get_prompt(menu);
	struct menu *child = NULL;
	struct symbol *active;
	int selected_index = 0;
	int last_top_row = 0;
	int res, i = 0;
	struct match_state match_state = {
		.in_search = 0,
		.match_direction = MATCH_TINKER_PATTERN_DOWN,
		.pattern = "",
	};

	active = sym_get_choice_value(menu->sym);
	/* this is mostly duplicated from the conf() function. */
	while (!global_exit) {
		reset_menu();

		for (i = 0, child = menu->list; child; child = child->next) {
			if (!show_all_items && !menu_is_visible(child))
				continue;

			if (child->sym == sym_get_choice_value(menu->sym))
				item_make(child, ':', "<X> %s",
						menu_get_prompt(child));
			else if (child->sym)
				item_make(child, ':', "    %s",
						menu_get_prompt(child));
			else
				item_make(child, ':', "*** %s ***",
						menu_get_prompt(child));

			if (child->sym == active){
				last_top_row = top_row(curses_menu);
				selected_index = i;
			}
			i++;
		}
		show_menu(prompt ? prompt : "Choice Menu",
				radiolist_instructions,
				selected_index,
				&last_top_row);
		while (!global_exit) {
			if (match_state.in_search) {
				mvprintw(0, 0, "searching: %s",
					 match_state.pattern);
				clrtoeol();
			}
			refresh_all_windows(main_window);
			res = wgetch(menu_win(curses_menu));
			if (!res)
				break;
			if (do_match(res, &match_state, &selected_index) == 0) {
				if (selected_index != -1)
					center_item(selected_index,
						    &last_top_row);
				continue;
			}
			if (process_special_keys(
						&res,
						(struct menu *) item_data()))
				break;
			switch (res) {
			case KEY_DOWN:
			case 'j':
				menu_driver(curses_menu, REQ_DOWN_ITEM);
				break;
			case KEY_UP:
			case 'k':
				menu_driver(curses_menu, REQ_UP_ITEM);
				break;
			case KEY_NPAGE:
				menu_driver(curses_menu, REQ_SCR_DPAGE);
				break;
			case KEY_PPAGE:
				menu_driver(curses_menu, REQ_SCR_UPAGE);
				break;
			case KEY_HOME:
				menu_driver(curses_menu, REQ_FIRST_ITEM);
				break;
			case KEY_END:
				menu_driver(curses_menu, REQ_LAST_ITEM);
				break;
			case 'h':
			case '?':
				show_help((struct menu *) item_data());
				break;
			}
			if (res == 10 || res == 27 || res == ' ' ||
					res == KEY_LEFT){
				break;
			}
			refresh_all_windows(main_window);
		}
		/* if ESC or left */
		if (res == 27 || res == KEY_LEFT)
			break;

		child = item_data();
		if (!child || !menu_is_visible(child) || !child->sym)
			continue;
		switch (res) {
		case ' ':
		case  10:
		case KEY_RIGHT:
			sym_set_tristate_value(child->sym, yes);
			return;
		case 'h':
		case '?':
			show_help(child);
			active = child->sym;
			break;
		case KEY_EXIT:
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
			heading = "Internal nconf error!";
		}
		res = dialog_inputbox(main_window,
				prompt ? prompt : "Main Menu",
				heading,
				sym_get_string_value(menu->sym),
				&dialog_input_result,
				&dialog_input_result_len);
		switch (res) {
		case 0:
			if (sym_set_string_value(menu->sym,
						dialog_input_result))
				return;
			btn_dialog(main_window,
				"You have made an invalid entry.", 0);
			break;
		case 1:
			show_help(menu);
			break;
		case KEY_EXIT:
			return;
		}
	}
}

static void conf_load(void)
{
	while (1) {
		int res;
		res = dialog_inputbox(main_window,
				NULL, load_config_text,
				filename,
				&dialog_input_result,
				&dialog_input_result_len);
		switch (res) {
		case 0:
			if (!dialog_input_result[0])
				return;
			if (!conf_read(dialog_input_result)) {
				set_config_filename(dialog_input_result);
				conf_set_changed(true);
				return;
			}
			btn_dialog(main_window, "File does not exist!", 0);
			break;
		case 1:
			show_scroll_win(main_window,
					"Load Alternate Configuration",
					load_config_help);
			break;
		case KEY_EXIT:
			return;
		}
	}
}

static void conf_save(void)
{
	while (1) {
		int res;
		res = dialog_inputbox(main_window,
				NULL, save_config_text,
				filename,
				&dialog_input_result,
				&dialog_input_result_len);
		switch (res) {
		case 0:
			if (!dialog_input_result[0])
				return;
			res = conf_write(dialog_input_result);
			if (!res) {
				set_config_filename(dialog_input_result);
				return;
			}
			btn_dialog(main_window, "Can't create file!",
				1, "<OK>");
			break;
		case 1:
			show_scroll_win(main_window,
				"Save Alternate Configuration",
				save_config_help);
			break;
		case KEY_EXIT:
			return;
		}
	}
}

static void setup_windows(void)
{
	int lines, columns;

	getmaxyx(stdscr, lines, columns);

	if (main_window != NULL)
		delwin(main_window);

	/* set up the menu and menu window */
	main_window = newwin(lines-2, columns-2, 2, 1);
	keypad(main_window, TRUE);
	mwin_max_lines = lines-7;
	mwin_max_cols = columns-6;

	/* panels order is from bottom to top */
	new_panel(main_window);
}

int main(int ac, char **av)
{
	int lines, columns;
	char *mode;

	if (ac > 1 && strcmp(av[1], "-s") == 0) {
		/* Silence conf_read() until the real callback is set up */
		conf_set_message_callback(NULL);
		av++;
	}
	conf_parse(av[1]);
	conf_read(NULL);

	mode = getenv("NCONFIG_MODE");
	if (mode) {
		if (!strcasecmp(mode, "single_menu"))
			single_menu_mode = 1;
	}

	/* Initialize curses */
	initscr();
	/* set color theme */
	set_colors();

	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);

	getmaxyx(stdscr, lines, columns);
	if (columns < 75 || lines < 20) {
		endwin();
		printf("Your terminal should have at "
			"least 20 lines and 75 columns\n");
		return 1;
	}

	notimeout(stdscr, FALSE);
#if NCURSES_REENTRANT
	set_escdelay(1);
#else
	ESCDELAY = 1;
#endif

	/* set btns menu */
	curses_menu = new_menu(curses_menu_items);
	menu_opts_off(curses_menu, O_SHOWDESC);
	menu_opts_on(curses_menu, O_SHOWMATCH);
	menu_opts_on(curses_menu, O_ONEVALUE);
	menu_opts_on(curses_menu, O_NONCYCLIC);
	menu_opts_on(curses_menu, O_IGNORECASE);
	set_menu_mark(curses_menu, " ");
	set_menu_fore(curses_menu, attr_main_menu_fore);
	set_menu_back(curses_menu, attr_main_menu_back);
	set_menu_grey(curses_menu, attr_main_menu_grey);

	set_config_filename(conf_get_configname());
	setup_windows();

	/* check for KEY_FUNC(1) */
	if (has_key(KEY_F(1)) == FALSE) {
		show_scroll_win(main_window,
				"Instructions",
				menu_no_f_instructions);
	}

	conf_set_message_callback(conf_message_callback);
	/* do the work */
	while (!global_exit) {
		conf(&rootmenu);
		if (!global_exit && do_exit() == 0)
			break;
	}
	/* ok, we are done */
	unpost_menu(curses_menu);
	free_menu(curses_menu);
	delwin(main_window);
	clear();
	refresh();
	endwin();
	return 0;
}
