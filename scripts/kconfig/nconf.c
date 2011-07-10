/*
 * Copyright (C) 2008 Nir Tzachar <nir.tzachar@gmail.com?
 * Released under the terms of the GNU GPL v2.0.
 *
 * Derived from menuconfig.
 *
 */
#define _GNU_SOURCE
#include <string.h>

#include "lkc.h"
#include "nconf.h"
#include <ctype.h>

static const char nconf_readme[] = N_(
"Overview\n"
"--------\n"
"This interface let you select features and parameters for the build.\n"
"Features can either be built-in, modularized, or ignored. Parameters\n"
"must be entered in as decimal or hexadecimal numbers or text.\n"
"\n"
"Menu items beginning with following braces represent features that\n"
"  [ ] can be built in or removed\n"
"  < > can be built in, modularized or removed\n"
"  { } can be built in or modularized (selected by other feature)\n"
"  - - are selected by other feature,\n"
"  XXX cannot be selected. Use Symbol Info to find out why,\n"
"while *, M or whitespace inside braces means to build in, build as\n"
"a module or to exclude the feature respectively.\n"
"\n"
"To change any of these features, highlight it with the cursor\n"
"keys and press <Y> to build it in, <M> to make it a module or\n"
"<N> to removed it.  You may also press the <Space Bar> to cycle\n"
"through the available options (ie. Y->N->M->Y).\n"
"\n"
"Some additional keyboard hints:\n"
"\n"
"Menus\n"
"----------\n"
"o  Use the Up/Down arrow keys (cursor keys) to highlight the item\n"
"   you wish to change use <Enter> or <Space>. Goto submenu by \n"
"   pressing <Enter> of <right-arrow>. Use <Esc> or <left-arrow> to go back.\n"
"   Submenus are designated by \"--->\".\n"
"\n"
"   Searching: pressing '/' triggers interactive search mode.\n"
"              nconfig performs a case insensitive search for the string\n"
"              in the menu prompts (no regex support).\n"
"              Pressing the up/down keys highlights the previous/next\n"
"              matching item. Backspace removes one character from the\n"
"              match string. Pressing either '/' again or ESC exits\n"
"              search mode. All other keys behave normally.\n"
"\n"
"   You may also use the <PAGE UP> and <PAGE DOWN> keys to scroll\n"
"   unseen options into view.\n"
"\n"
"o  To exit a menu use the just press <ESC> <F5> <F8> or <left-arrow>.\n"
"\n"
"o  To get help with an item, press <F1>\n"
"   Shortcut: Press <h> or <?>.\n"
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
"o  To see available help for the item, press <F1>\n"
"   Shortcut: Press <H> or <?>.\n"
"\n"
"\n"
"Data Entry\n"
"-----------\n"
"o  Enter the requested information and press <ENTER>\n"
"   If you are entering hexadecimal values, it is not necessary to\n"
"   add the '0x' prefix to the entry.\n"
"\n"
"o  For help, press <F1>.\n"
"\n"
"\n"
"Text Box    (Help Window)\n"
"--------\n"
"o  Use the cursor keys to scroll up/down/left/right.  The VI editor\n"
"   keys h,j,k,l function here as do <SPACE BAR> for those\n"
"   who are familiar with less and lynx.\n"
"\n"
"o  Press <Enter>, <F1>, <F5>, <F7> or <Esc> to exit.\n"
"\n"
"\n"
"Alternate Configuration Files\n"
"-----------------------------\n"
"nconfig supports the use of alternate configuration files for\n"
"those who, for various reasons, find it necessary to switch\n"
"between different configurations.\n"
"\n"
"At the end of the main menu you will find two options.  One is\n"
"for saving the current configuration to a file of your choosing.\n"
"The other option is for loading a previously saved alternate\n"
"configuration.\n"
"\n"
"Even if you don't use alternate configuration files, but you\n"
"find during a nconfig session that you have completely messed\n"
"up your settings, you may use the \"Load Alternate...\" option to\n"
"restore your previously saved settings from \".config\" without\n"
"restarting nconfig.\n"
"\n"
"Other information\n"
"-----------------\n"
"If you use nconfig in an XTERM window make sure you have your\n"
"$TERM variable set to point to a xterm definition which supports color.\n"
"Otherwise, nconfig will look rather bad.  nconfig will not\n"
"display correctly in a RXVT window because rxvt displays only one\n"
"intensity of color, bright.\n"
"\n"
"nconfig will display larger menus on screens or xterms which are\n"
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
"If you prefer to have all of the options listed in a single menu, rather\n"
"than the default multimenu hierarchy, run the nconfig with NCONFIG_MODE\n"
"environment variable set to single_menu. Example:\n"
"\n"
"make NCONFIG_MODE=single_menu nconfig\n"
"\n"
"<Enter> will then unroll the appropriate category, or enfold it if it\n"
"is already unrolled.\n"
"\n"
"Note that this mode can eventually be a little more CPU expensive\n"
"(especially with a larger number of unrolled categories) than the\n"
"default mode.\n"
"\n"),
menu_no_f_instructions[] = N_(
" You do not have function keys support. Please follow the\n"
" following instructions:\n"
" Arrow keys navigate the menu.\n"
" <Enter> or <right-arrow> selects submenus --->.\n"
" Capital Letters are hotkeys.\n"
" Pressing <Y> includes, <N> excludes, <M> modularizes features.\n"
" Pressing SpaceBar toggles between the above options.\n"
" Press <Esc> or <left-arrow> to go back one menu,\n"
" <?> or <h> for Help, </> for Search.\n"
" <1> is interchangeable with <F1>, <2> with <F2>, etc.\n"
" Legend: [*] built-in  [ ] excluded  <M> module  < > module capable.\n"
" <Esc> always leaves the current window.\n"),
menu_instructions[] = N_(
" Arrow keys navigate the menu.\n"
" <Enter> or <right-arrow> selects submenus --->.\n"
" Capital Letters are hotkeys.\n"
" Pressing <Y> includes, <N> excludes, <M> modularizes features.\n"
" Pressing SpaceBar toggles between the above options\n"
" Press <Esc>, <F5> or <left-arrow> to go back one menu,\n"
" <?>, <F1> or <h> for Help, </> for Search.\n"
" <1> is interchangeable with <F1>, <2> with <F2>, etc.\n"
" Legend: [*] built-in  [ ] excluded  <M> module  < > module capable.\n"
" <Esc> always leaves the current window\n"),
radiolist_instructions[] = N_(
" Use the arrow keys to navigate this window or\n"
" press the hotkey of the item you wish to select\n"
" followed by the <SPACE BAR>.\n"
" Press <?>, <F1> or <h> for additional information about this option.\n"),
inputbox_instructions_int[] = N_(
"Please enter a decimal value.\n"
"Fractions will not be accepted.\n"
"Press <RETURN> to accept, <ESC> to cancel."),
inputbox_instructions_hex[] = N_(
"Please enter a hexadecimal value.\n"
"Press <RETURN> to accept, <ESC> to cancel."),
inputbox_instructions_string[] = N_(
"Please enter a string value.\n"
"Press <RETURN> to accept, <ESC> to cancel."),
setmod_text[] = N_(
"This feature depends on another which\n"
"has been configured as a module.\n"
"As a result, this feature will be built as a module."),
nohelp_text[] = N_(
"There is no help available for this option.\n"),
load_config_text[] = N_(
"Enter the name of the configuration file you wish to load.\n"
"Accept the name shown to restore the configuration you\n"
"last retrieved.  Leave blank to abort."),
load_config_help[] = N_(
"\n"
"For various reasons, one may wish to keep several different\n"
"configurations available on a single machine.\n"
"\n"
"If you have saved a previous configuration in a file other than the\n"
"default one, entering its name here will allow you to modify that\n"
"configuration.\n"
"\n"
"If you are uncertain, then you have probably never used alternate\n"
"configuration files.  You should therefor leave this blank to abort.\n"),
save_config_text[] = N_(
"Enter a filename to which this configuration should be saved\n"
"as an alternate.  Leave blank to abort."),
save_config_help[] = N_(
"\n"
"For various reasons, one may wish to keep different configurations\n"
"available on a single machine.\n"
"\n"
"Entering a file name here will allow you to later retrieve, modify\n"
"and use the current configuration as an alternate to whatever\n"
"configuration options you have selected at that time.\n"
"\n"
"If you are uncertain what all this means then you should probably\n"
"leave this blank.\n"),
search_help[] = N_(
"\n"
"Search for symbols and display their relations. Regular expressions\n"
"are allowed.\n"
"Example: search for \"^FOO\"\n"
"Result:\n"
"-----------------------------------------------------------------\n"
"Symbol: FOO [ = m]\n"
"Prompt: Foo bus is used to drive the bar HW\n"
"Defined at drivers/pci/Kconfig:47\n"
"Depends on: X86_LOCAL_APIC && X86_IO_APIC || IA64\n"
"Location:\n"
"  -> Bus options (PCI, PCMCIA, EISA, MCA, ISA)\n"
"    -> PCI support (PCI [ = y])\n"
"      -> PCI access mode (<choice> [ = y])\n"
"Selects: LIBCRC32\n"
"Selected by: BAR\n"
"-----------------------------------------------------------------\n"
"o The line 'Prompt:' shows the text used in the menu structure for\n"
"  this symbol\n"
"o The 'Defined at' line tell at what file / line number the symbol\n"
"  is defined\n"
"o The 'Depends on:' line tell what symbols needs to be defined for\n"
"  this symbol to be visible in the menu (selectable)\n"
"o The 'Location:' lines tell where in the menu structure this symbol\n"
"  is located\n"
"    A location followed by a [ = y] indicate that this is a selectable\n"
"    menu item - and current value is displayed inside brackets.\n"
"o The 'Selects:' line tell what symbol will be automatically\n"
"  selected if this symbol is selected (y or m)\n"
"o The 'Selected by' line tell what symbol has selected this symbol\n"
"\n"
"Only relevant lines are shown.\n"
"\n\n"
"Search examples:\n"
"Examples: USB  => find all symbols containing USB\n"
"          ^USB => find all symbols starting with USB\n"
"          USB$ => find all symbols ending with USB\n"
"\n");

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
static int items_num;
static int global_exit;
/* the currently selected button */
const char *current_instructions = menu_instructions;

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
struct function_keys function_keys[] = {
	{
		.key_str = "F1",
		.func = "Help",
		.key = F_HELP,
		.handler = handle_f1,
	},
	{
		.key_str = "F2",
		.func = "Sym Info",
		.key = F_SYMBOL,
		.handler = handle_f2,
	},
	{
		.key_str = "F3",
		.func = "Insts",
		.key = F_INSTS,
		.handler = handle_f3,
	},
	{
		.key_str = "F4",
		.func = "Config",
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
		.func = "Sym Search",
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

	for (i = 0; i < function_keys_num; i++) {
		(void) wattrset(main_window, attributes[FUNCTION_HIGHLIGHT]);
		mvwprintw(main_window, LINES-3, offset,
				"%s",
				function_keys[i].key_str);
		(void) wattrset(main_window, attributes[FUNCTION_TEXT]);
		offset += strlen(function_keys[i].key_str);
		mvwprintw(main_window, LINES-3,
				offset, "%s",
				function_keys[i].func);
		offset += strlen(function_keys[i].func) + skip;
	}
	(void) wattrset(main_window, attributes[NORMAL]);
}

/* help */
static void handle_f1(int *key, struct menu *current_item)
{
	show_scroll_win(main_window,
			_("README"), _(nconf_readme));
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
			_("Instructions"),
			_(current_instructions));
	return;
}

/* config */
static void handle_f4(int *key, struct menu *current_item)
{
	int res = btn_dialog(main_window,
			_("Show all symbols?"),
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
	int match_start = item_index(current_item(curses_menu));
	int index;

	if (flag == FIND_NEXT_MATCH_DOWN)
		++match_start;
	else if (flag == FIND_NEXT_MATCH_UP)
		--match_start;

	index = match_start;
	index = (index + items_num) % items_num;
	while (true) {
		char *str = k_menu_items[index].str;
		if (strcasestr(str, match_str) != 0)
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
static const char *set_config_filename(const char *config_filename)
{
	int size;

	size = snprintf(menu_backtitle, sizeof(menu_backtitle),
			"%s - %s", config_filename, rootmenu.prompt->text);
	if (size >= sizeof(menu_backtitle))
		menu_backtitle[sizeof(menu_backtitle)-1] = '\0';

	size = snprintf(filename, sizeof(filename), "%s", config_filename);
	if (size >= sizeof(filename))
		filename[sizeof(filename)-1] = '\0';
	return menu_backtitle;
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
			_("Do you wish to save your new configuration?\n"
				"<ESC> to cancel and resume nconfig."),
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
				_("Error during writing of configuration.\n"
				  "Your configuration changes were NOT saved."),
				  1,
				  "<OK>");
		break;
	default:
		btn_dialog(
			main_window,
			_("Your configuration changes were NOT saved."),
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
	char dialog_input_result[100];
	char *dialog_input;
	int dres;
again:
	dres = dialog_inputbox(main_window,
			_("Search Configuration Parameter"),
			_("Enter " CONFIG_ " (sub)string to search for "
				"(with or without \"" CONFIG_ "\")"),
			"", dialog_input_result, 99);
	switch (dres) {
	case 0:
		break;
	case 1:
		show_scroll_win(main_window,
				_("Search Configuration"), search_help);
		goto again;
	default:
		return;
	}

	/* strip the prefix if necessary */
	dialog_input = dialog_input_result;
	if (strncasecmp(dialog_input_result, CONFIG_, strlen(CONFIG_)) == 0)
		dialog_input += strlen(CONFIG_);

	sym_arr = sym_re_search(dialog_input);
	res = get_relations_str(sym_arr);
	free(sym_arr);
	show_scroll_win(main_window,
			_("Search Results"), str_get(&res));
	str_free(&res);
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
				prompt = _(prompt);
				if (single_menu_mode) {
					item_make(menu, 'm',
						"%s%*c%s",
						menu->data ? "-->" : "++>",
						indent + 1, ' ', prompt);
				} else
					item_make(menu, 'm',
						"   %*c%s  --->",
						indent + 1,
						' ', prompt);

				if (single_menu_mode && menu->data)
					goto conf_childs;
				return;
			case P_COMMENT:
				if (prompt) {
					child_count++;
					item_make(menu, ':',
						"   %*c*** %s ***",
						indent + 1, ' ',
						_(prompt));
				}
				break;
			default:
				if (prompt) {
					child_count++;
					item_make(menu, ':', "---%*c%s",
						indent + 1, ' ',
						_(prompt));
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
		if (sym_is_changable(sym)) {
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
				' ', _(menu_get_prompt(menu)));
		if (val == yes) {
			if (def_menu) {
				item_add_str(" (%s)",
					_(menu_get_prompt(def_menu)));
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
				' ', _(menu_get_prompt(menu)));
			goto conf_childs;
		}
		child_count++;
		val = sym_get_tristate_value(sym);
		if (sym_is_choice_value(sym) && val == yes) {
			item_make(menu, ':', "   ");
		} else {
			switch (type) {
			case S_BOOLEAN:
				if (sym_is_changable(sym))
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
				if (sym_is_changable(sym)) {
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
						_(menu_get_prompt(menu)),
						(sym_has_value(sym) ||
						 !sym_is_changable(sym)) ? "" :
						_(" (NEW)"));
				goto conf_childs;
			}
		}
		item_add_str("%*c%s%s", indent + 1, ' ',
				_(menu_get_prompt(menu)),
				(sym_has_value(sym) || !sym_is_changable(sym)) ?
				"" : _(" (NEW)"));
		if (menu->prompt && menu->prompt->type == P_MENU) {
			item_add_str("  --->");
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
	(void) wattrset(main_window, attributes[NORMAL]);
	print_in_middle(stdscr, 1, 0, COLS,
			menu_backtitle,
			attributes[MAIN_HEADING]);

	(void) wattrset(main_window, attributes[MAIN_MENU_BOX]);
	box(main_window, 0, 0);
	(void) wattrset(main_window, attributes[MAIN_MENU_HEADING]);
	mvwprintw(main_window, 0, 3, " %s ", prompt);
	(void) wattrset(main_window, attributes[NORMAL]);

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
	} else if (key == KEY_BACKSPACE || key == 127) {
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
	struct menu *submenu = 0;
	const char *prompt = menu_get_prompt(menu);
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

		show_menu(prompt ? _(prompt) : _("Main Menu"),
				_(menu_instructions),
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
				menu_driver(curses_menu, REQ_DOWN_ITEM);
				break;
			case KEY_UP:
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

static void conf_message_callback(const char *fmt, va_list ap)
{
	char buf[1024];

	vsnprintf(buf, sizeof(buf), fmt, ap);
	btn_dialog(main_window, buf, 1, "<OK>");
}

static void show_help(struct menu *menu)
{
	struct gstr help;

	if (!menu)
		return;

	help = str_new();
	menu_get_ext_help(menu, &help);
	show_scroll_win(main_window, _(menu_get_prompt(menu)), str_get(&help));
	str_free(&help);
}

static void conf_choice(struct menu *menu)
{
	const char *prompt = _(menu_get_prompt(menu));
	struct menu *child = 0;
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
						_(menu_get_prompt(child)));
			else if (child->sym)
				item_make(child, ':', "    %s",
						_(menu_get_prompt(child)));
			else
				item_make(child, ':', "*** %s ***",
						_(menu_get_prompt(child)));

			if (child->sym == active){
				last_top_row = top_row(curses_menu);
				selected_index = i;
			}
			i++;
		}
		show_menu(prompt ? _(prompt) : _("Choice Menu"),
				_(radiolist_instructions),
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
				menu_driver(curses_menu, REQ_DOWN_ITEM);
				break;
			case KEY_UP:
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
	char dialog_input_result[256];

	while (1) {
		int res;
		const char *heading;

		switch (sym_get_type(menu->sym)) {
		case S_INT:
			heading = _(inputbox_instructions_int);
			break;
		case S_HEX:
			heading = _(inputbox_instructions_hex);
			break;
		case S_STRING:
			heading = _(inputbox_instructions_string);
			break;
		default:
			heading = _("Internal nconf error!");
		}
		res = dialog_inputbox(main_window,
				prompt ? _(prompt) : _("Main Menu"),
				heading,
				sym_get_string_value(menu->sym),
				dialog_input_result,
				sizeof(dialog_input_result));
		switch (res) {
		case 0:
			if (sym_set_string_value(menu->sym,
						dialog_input_result))
				return;
			btn_dialog(main_window,
				_("You have made an invalid entry."), 0);
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
	char dialog_input_result[256];
	while (1) {
		int res;
		res = dialog_inputbox(main_window,
				NULL, load_config_text,
				filename,
				dialog_input_result,
				sizeof(dialog_input_result));
		switch (res) {
		case 0:
			if (!dialog_input_result[0])
				return;
			if (!conf_read(dialog_input_result)) {
				set_config_filename(dialog_input_result);
				sym_set_change_count(1);
				return;
			}
			btn_dialog(main_window, _("File does not exist!"), 0);
			break;
		case 1:
			show_scroll_win(main_window,
					_("Load Alternate Configuration"),
					load_config_help);
			break;
		case KEY_EXIT:
			return;
		}
	}
}

static void conf_save(void)
{
	char dialog_input_result[256];
	while (1) {
		int res;
		res = dialog_inputbox(main_window,
				NULL, save_config_text,
				filename,
				dialog_input_result,
				sizeof(dialog_input_result));
		switch (res) {
		case 0:
			if (!dialog_input_result[0])
				return;
			res = conf_write(dialog_input_result);
			if (!res) {
				set_config_filename(dialog_input_result);
				return;
			}
			btn_dialog(main_window, _("Can't create file! "
				"Probably a nonexistent directory."),
				1, "<OK>");
			break;
		case 1:
			show_scroll_win(main_window,
				_("Save Alternate Configuration"),
				save_config_help);
			break;
		case KEY_EXIT:
			return;
		}
	}
}

void setup_windows(void)
{
	if (main_window != NULL)
		delwin(main_window);

	/* set up the menu and menu window */
	main_window = newwin(LINES-2, COLS-2, 2, 1);
	keypad(main_window, TRUE);
	mwin_max_lines = LINES-7;
	mwin_max_cols = COLS-6;

	/* panels order is from bottom to top */
	new_panel(main_window);
}

int main(int ac, char **av)
{
	char *mode;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

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

	if (COLS < 75 || LINES < 20) {
		endwin();
		printf("Your terminal should have at "
			"least 20 lines and 75 columns\n");
		return 1;
	}

	notimeout(stdscr, FALSE);
	ESCDELAY = 1;

	/* set btns menu */
	curses_menu = new_menu(curses_menu_items);
	menu_opts_off(curses_menu, O_SHOWDESC);
	menu_opts_on(curses_menu, O_SHOWMATCH);
	menu_opts_on(curses_menu, O_ONEVALUE);
	menu_opts_on(curses_menu, O_NONCYCLIC);
	menu_opts_on(curses_menu, O_IGNORECASE);
	set_menu_mark(curses_menu, " ");
	set_menu_fore(curses_menu, attributes[MAIN_MENU_FORE]);
	set_menu_back(curses_menu, attributes[MAIN_MENU_BACK]);
	set_menu_grey(curses_menu, attributes[MAIN_MENU_GREY]);

	set_config_filename(conf_get_configname());
	setup_windows();

	/* check for KEY_FUNC(1) */
	if (has_key(KEY_F(1)) == FALSE) {
		show_scroll_win(main_window,
				_("Instructions"),
				_(menu_no_f_instructions));
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

