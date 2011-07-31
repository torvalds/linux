/*
 * Copyright (C) 2008 Nir Tzachar <nir.tzachar@gmail.com?
 * Released under the terms of the GNU GPL v2.0.
 *
 * Derived from menuconfig.
 *
 */
#define LKC_DIRECT_LINK
#include "lkc.h"
#include "nconf.h"

static const char nconf_readme[] = N_(
"Overview\n"
"--------\n"
"Some kernel features may be built directly into the kernel.\n"
"Some may be made into loadable runtime modules.  Some features\n"
"may be completely removed altogether.  There are also certain\n"
"kernel parameters which are not really features, but must be\n"
"entered in as decimal or hexadecimal numbers or possibly text.\n"
"\n"
"Menu items beginning with following braces represent features that\n"
"  [ ] can be built in or removed\n"
"  < > can be built in, modularized or removed\n"
"  { } can be built in or modularized (selected by other feature)\n"
"  - - are selected by other feature,\n"
"  XXX cannot be selected. use Symbol Info to find out why,\n"
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
"   Shortcut: Press the option's highlighted letter (hotkey).\n"
"             Pressing a hotkey more than once will sequence\n"
"             through all visible items which use that hotkey.\n"
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
"between different kernel configurations.\n"
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
"If you prefer to have all of the kernel options listed in a single\n"
"menu, rather than the default multimenu hierarchy, run the nconfig\n"
"with NCONFIG_MODE environment variable set to single_menu. Example:\n"
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
" Pressing SpaceBar toggles between the above options\n"
" Press <Esc> or <left-arrow> to go back one menu, \n"
" <?> or <h> for Help, </> for Search.\n"
" <1> is interchangable with <F1>, <2> with <F2>, etc.\n"
" Legend: [*] built-in  [ ] excluded  <M> module  < > module capable.\n"
" <Esc> always leaves the current window\n"),
menu_instructions[] = N_(
" Arrow keys navigate the menu.\n"
" <Enter> or <right-arrow> selects submenus --->.\n"
" Capital Letters are hotkeys.\n"
" Pressing <Y> includes, <N> excludes, <M> modularizes features.\n"
" Pressing SpaceBar toggles between the above options\n"
" Press <Esc>, <F3> or <left-arrow> to go back one menu, \n"
" <?>, <F1> or <h> for Help, </> for Search.\n"
" <1> is interchangable with <F1>, <2> with <F2>, etc.\n"
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
"There is no help available for this kernel option.\n"),
load_config_text[] = N_(
"Enter the name of the configuration file you wish to load.\n"
"Accept the name shown to restore the configuration you\n"
"last retrieved.  Leave blank to abort."),
load_config_help[] = N_(
"\n"
"For various reasons, one may wish to keep several different kernel\n"
"configurations available on a single machine.\n"
"\n"
"If you have saved a previous configuration in a file other than the\n"
"kernel's default, entering the name of the file here will allow you\n"
"to modify that configuration.\n"
"\n"
"If you are uncertain, then you have probably never used alternate\n"
"configuration files.  You should therefor leave this blank to abort.\n"),
save_config_text[] = N_(
"Enter a filename to which this configuration should be saved\n"
"as an alternate.  Leave blank to abort."),
save_config_help[] = N_(
"\n"
"For various reasons, one may wish to keep different kernel\n"
"configurations available on a single machine.\n"
"\n"
"Entering a file name here will allow you to later retrieve, modify\n"
"and use the current configuration as an alternate to whatever\n"
"configuration options you have selected at that time.\n"
"\n"
"If you are uncertain what all this means then you should probably\n"
"leave this blank.\n"),
search_help[] = N_(
"\n"
"Search for CONFIG_ symbols and display their relations.\n"
"Regular expressions are allowed.\n"
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
"  this CONFIG_ symbol\n"
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
"Examples: USB   = > find all CONFIG_ symbols containing USB\n"
"          ^USB => find all CONFIG_ symbols starting with USB\n"
"          USB$ => find all CONFIG_ symbols ending with USB\n"
"\n");

struct mitem {
	char str[256];
	char tag;
	void *usrptr;
	int is_hot;
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
/* this array is used to implement hot keys. it is updated in item_make and
 * resetted in clean_items. It would be better to use a hash, but lets keep it
 * simple... */
#define MAX_SAME_KEY MAX_MENU_ITEMS
struct {
	int count;
	int ptrs[MAX_MENU_ITEMS];
} hotkeys[1<<(sizeof(char)*8)];

static void conf(struct menu *menu);
static void conf_choice(struct menu *menu);
static void conf_string(struct menu *menu);
static void conf_load(void);
static void conf_save(void);
static void show_help(struct menu *menu);
static int do_exit(void);
static void setup_windows(void);

typedef void (*function_key_handler_t)(int *key, struct menu *menu);
static void handle_f1(int *key, struct menu *current_item);
static void handle_f2(int *key, struct menu *current_item);
static void handle_f3(int *key, struct menu *current_item);
static void handle_f4(int *key, struct menu *current_item);
static void handle_f5(int *key, struct menu *current_item);
static void handle_f6(int *key, struct menu *current_item);
static void handle_f7(int *key, struct menu *current_item);
static void handle_f8(int *key, struct menu *current_item);

struct function_keys {
	const char *key_str;
	const char *func;
	function_key key;
	function_key_handler_t handler;
};

static const int function_keys_num = 8;
struct function_keys function_keys[] = {
	{
		.key_str = "F1",
		.func = "Help",
		.key = F_HELP,
		.handler = handle_f1,
	},
	{
		.key_str = "F2",
		.func = "Symbol Info",
		.key = F_SYMBOL,
		.handler = handle_f2,
	},
	{
		.key_str = "F3",
		.func = "Instructions",
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
		.func = "Exit",
		.key = F_EXIT,
		.handler = handle_f8,
	},
};

static void print_function_line(void)
{
	int i;
	int offset = 1;
	const int skip = 1;

	for (i = 0; i < function_keys_num; i++) {
		wattrset(main_window, attributes[FUNCTION_HIGHLIGHT]);
		mvwprintw(main_window, LINES-3, offset,
				"%s",
				function_keys[i].key_str);
		wattrset(main_window, attributes[FUNCTION_TEXT]);
		offset += strlen(function_keys[i].key_str);
		mvwprintw(main_window, LINES-3,
				offset, "%s",
				function_keys[i].func);
		offset += strlen(function_keys[i].func) + skip;
	}
	wattrset(main_window, attributes[NORMAL]);
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

/* exit */
static void handle_f8(int *key, struct menu *current_item)
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
	bzero(hotkeys, sizeof(hotkeys));
	items_num = 0;
}

/* return the index of the next hot item, or -1 if no such item exists */
static int get_next_hot(int c)
{
	static int hot_index;
	static int hot_char;

	if (c < 0 || c > 255 || hotkeys[c].count <= 0)
		return -1;

	if (hot_char == c) {
		hot_index = (hot_index+1)%hotkeys[c].count;
		return hotkeys[c].ptrs[hot_index];
	} else {
		hot_char = c;
		hot_index = 0;
		return hotkeys[c].ptrs[0];
	}
}

/* can the char c be a hot key? no, if c is a common shortcut used elsewhere */
static int canbhot(char c)
{
	c = tolower(c);
	return isalnum(c) && c != 'y' && c != 'm' && c != 'h' &&
		c != 'n' && c != '?';
}

/* check if str already contains a hot key. */
static int is_hot(int index)
{
	return k_menu_items[index].is_hot;
}

/* find the first possible hot key, and mark it.
 * index is the index of the item in the menu
 * return 0 on success*/
static int make_hot(char *dest, int len, const char *org, int index)
{
	int position = -1;
	int i;
	int tmp;
	int c;
	int org_len = strlen(org);

	if (org == NULL || is_hot(index))
		return 1;

	/* make sure not to make hot keys out of markers.
	 * find where to start looking for a hot key
	 */
	i = 0;
	/* skip white space */
	while (i < org_len && org[i] == ' ')
		i++;
	if (i == org_len)
		return -1;
	/* if encountering '(' or '<' or '[', find the match and look from there
	 **/
	if (org[i] == '[' || org[i] == '<' || org[i] == '(') {
		i++;
		for (; i < org_len; i++)
			if (org[i] == ']' || org[i] == '>' || org[i] == ')')
				break;
	}
	if (i == org_len)
		return -1;
	for (; i < org_len; i++) {
		if (canbhot(org[i]) && org[i-1] != '<' && org[i-1] != '(') {
			position = i;
			break;
		}
	}
	if (position == -1)
		return 1;

	/* ok, char at org[position] should be a hot key to this item */
	c = tolower(org[position]);
	tmp = hotkeys[c].count;
	hotkeys[c].ptrs[tmp] = index;
	hotkeys[c].count++;
	/*
	   snprintf(dest, len, "%.*s(%c)%s", position, org, org[position],
	   &org[position+1]);
	   */
	/* make org[position] uppercase, and all leading letter small case */
	strncpy(dest, org, len);
	for (i = 0; i < position; i++)
		dest[i] = tolower(dest[i]);
	dest[position] = toupper(dest[position]);
	k_menu_items[index].is_hot = 1;
	return 0;
}

/* Make a new item. Add a hotkey mark in the first possible letter.
 * As ncurses does not allow any attributes inside menue item, we mark the
 * hot key as the first capitalized letter in the string */
static void item_make(struct menu *menu, char tag, const char *fmt, ...)
{
	va_list ap;
	char tmp_str[256];

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
	vsnprintf(tmp_str, sizeof(tmp_str), fmt, ap);
	if (!k_menu_items[items_num].is_visible)
		memcpy(tmp_str, "XXX", 3);
	va_end(ap);
	if (make_hot(
		k_menu_items[items_num].str,
		sizeof(k_menu_items[items_num].str), tmp_str, items_num) != 0)
		strncpy(k_menu_items[items_num].str,
			tmp_str,
			sizeof(k_menu_items[items_num].str));

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
	if (make_hot(k_menu_items[index].str,
			sizeof(k_menu_items[index].str), tmp_str, index) != 0)
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
	struct symbol *sym;

	sym = sym_lookup("KERNELVERSION", 0);
	sym_calc_value(sym);
	size = snprintf(menu_backtitle, sizeof(menu_backtitle),
			_("%s - Linux Kernel v%s Configuration"),
			config_filename, sym_get_string_value(sym));
	if (size >= sizeof(menu_backtitle))
		menu_backtitle[sizeof(menu_backtitle)-1] = '\0';

	size = snprintf(filename, sizeof(filename), "%s", config_filename);
	if (size >= sizeof(filename))
		filename[sizeof(filename)-1] = '\0';
	return menu_backtitle;
}

/* command = 0 is supress, 1 is restore */
static void supress_stdout(int command)
{
	static FILE *org_stdout;
	static FILE *org_stderr;

	if (command == 0) {
		org_stdout = stdout;
		org_stderr = stderr;
		stdout = fopen("/dev/null", "a");
		stderr = fopen("/dev/null", "a");
	} else {
		fclose(stdout);
		fclose(stderr);
		stdout = org_stdout;
		stderr = org_stderr;
	}
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
			_("Do you wish to save your "
				"new kernel configuration?\n"
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
		supress_stdout(0);
		res = conf_write(filename);
		supress_stdout(1);
		if (res)
			btn_dialog(
				main_window,
				_("Error during writing of the kernel "
				  "configuration.\n"
				  "Your kernel configuration "
				  "changes were NOT saved."),
				  1,
				  "<OK>");
		else {
			char buf[1024];
			snprintf(buf, 1024,
				_("Configuration written to %s\n"
				  "End of Linux kernel configuration.\n"
				  "Execute 'make' to build the kernel or try"
				  " 'make help'."), filename);
			btn_dialog(
				main_window,
				buf,
				1,
				"<OK>");
		}
		break;
	default:
		btn_dialog(
			main_window,
			_("Your kernel configuration changes were NOT saved."),
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
			_("Enter CONFIG_ (sub)string to search for "
				"(with or without \"CONFIG\")"),
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

	/* strip CONFIG_ if necessary */
	dialog_input = dialog_input_result;
	if (strncasecmp(dialog_input_result, "CONFIG_", 7) == 0)
		dialog_input += 7;

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
	int maxy, maxx;

	scale_menu(curses_menu, &maxy, &maxx);
	set_top_row(curses_menu, *last_top_row);
	toprow = top_row(curses_menu);
	if (selected_index >= toprow && selected_index < toprow+maxy) {
		/* we can only move the selected item. no need to scroll */
		set_current_item(curses_menu,
				curses_menu_items[selected_index]);
	} else {
		toprow = max(selected_index-maxy/2, 0);
		if (toprow >= item_count(curses_menu)-maxy)
			toprow = item_count(curses_menu)-mwin_max_lines;
		set_top_row(curses_menu, toprow);
		set_current_item(curses_menu,
				curses_menu_items[selected_index]);
	}
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
	wattrset(main_window, attributes[NORMAL]);
	print_in_middle(stdscr, 1, 0, COLS,
			menu_backtitle,
			attributes[MAIN_HEADING]);

	wattrset(main_window, attributes[MAIN_MENU_BOX]);
	box(main_window, 0, 0);
	wattrset(main_window, attributes[MAIN_MENU_HEADING]);
	mvwprintw(main_window, 0, 3, " %s ", prompt);
	wattrset(main_window, attributes[NORMAL]);

	set_menu_items(curses_menu, curses_menu_items);

	/* position the menu at the middle of the screen */
	scale_menu(curses_menu, &maxy, &maxx);
	maxx = min(maxx, mwin_max_cols-2);
	maxy = mwin_max_lines-2;
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


static void conf(struct menu *menu)
{
	char pattern[256];
	struct menu *submenu = 0;
	const char *prompt = menu_get_prompt(menu);
	struct symbol *sym;
	struct menu *active_menu = NULL;
	int res;
	int current_index = 0;
	int last_top_row = 0;

	bzero(pattern, sizeof(pattern));

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
		while (!global_exit && (res = wgetch(menu_win(curses_menu)))) {
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
				res == 'm' || res == '/')
				break;
			else if (canbhot(res)) {
				/* check for hot keys: */
				int tmp = get_next_hot(res);
				if (tmp != -1)
					center_item(tmp, &last_top_row);
			}
			refresh_all_windows(main_window);
		}

		refresh_all_windows(main_window);
		/* if ESC  or left*/
		if (res == 27 || (menu != &rootmenu && res == KEY_LEFT))
			break;

		/* remember location in the menu */
		last_top_row = top_row(curses_menu);
		current_index = curses_item_index();

		if (!item_tag())
			continue;

		submenu = (struct menu *) item_data();
		active_menu = (struct menu *)item_data();
		if (!submenu || !menu_is_visible(submenu))
			continue;
		if (submenu)
			sym = submenu->sym;
		else
			sym = NULL;

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
		case '/':
			search_conf();
			break;
		}
	}
}

static void show_help(struct menu *menu)
{
	struct gstr help = str_new();

	if (menu && menu->sym && menu_has_help(menu)) {
		if (menu->sym->name) {
			str_printf(&help, "CONFIG_%s:\n\n", menu->sym->name);
			str_append(&help, _(menu_get_help(menu)));
			str_append(&help, "\n");
			get_symbol_str(&help, menu->sym);
		}
	} else {
		str_append(&help, nohelp_text);
	}
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
			else
				item_make(child, ':', "    %s",
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
		while (!global_exit && (res = wgetch(menu_win(curses_menu)))) {
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
				res == KEY_LEFT)
				break;
			else if (canbhot(res)) {
				/* check for hot keys: */
				int tmp = get_next_hot(res);
				if (tmp != -1)
					center_item(tmp, &last_top_row);
			}
			refresh_all_windows(main_window);
		}
		/* if ESC or left */
		if (res == 27 || res == KEY_LEFT)
			break;

		child = item_data();
		if (!child || !menu_is_visible(child))
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
			supress_stdout(0);
			res = conf_write(dialog_input_result);
			supress_stdout(1);
			if (!res) {
				char buf[1024];
				sprintf(buf, "%s %s",
					_("configuration file saved to: "),
					dialog_input_result);
				btn_dialog(main_window,
					   buf, 1, "<OK>");
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
	mwin_max_lines = LINES-6;
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
	menu_opts_off(curses_menu, O_SHOWMATCH);
	menu_opts_on(curses_menu, O_ONEVALUE);
	menu_opts_on(curses_menu, O_NONCYCLIC);
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

