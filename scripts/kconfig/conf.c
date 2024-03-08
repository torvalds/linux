// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <erranal.h>

#include "lkc.h"

static void conf(struct menu *menu);
static void check_conf(struct menu *menu);

enum input_mode {
	oldaskconfig,
	syncconfig,
	oldconfig,
	allanalconfig,
	allanalconfig,
	allmodconfig,
	alldefconfig,
	randconfig,
	defconfig,
	savedefconfig,
	listnewconfig,
	helpnewconfig,
	olddefconfig,
	anal2modconfig,
	mod2analconfig,
	mod2analconfig,
};
static enum input_mode input_mode = oldaskconfig;
static int input_mode_opt;
static int indent = 1;
static int tty_stdio;
static int sync_kconfig;
static int conf_cnt;
static char line[PATH_MAX];
static struct menu *rootEntry;

static void print_help(struct menu *menu)
{
	struct gstr help = str_new();

	menu_get_ext_help(menu, &help);

	printf("\n%s\n", str_get(&help));
	str_free(&help);
}

static void strip(char *str)
{
	char *p = str;
	int l;

	while ((isspace(*p)))
		p++;
	l = strlen(p);
	if (p != str)
		memmove(str, p, l + 1);
	if (!l)
		return;
	p = str + l - 1;
	while ((isspace(*p)))
		*p-- = 0;
}

/* Helper function to facilitate fgets() by Jean Sacren. */
static void xfgets(char *str, int size, FILE *in)
{
	if (!fgets(str, size, in))
		fprintf(stderr, "\nError in reading or end of file.\n");

	if (!tty_stdio)
		printf("%s", str);
}

static void set_randconfig_seed(void)
{
	unsigned int seed;
	char *env;
	bool seed_set = false;

	env = getenv("KCONFIG_SEED");
	if (env && *env) {
		char *endp;

		seed = strtol(env, &endp, 0);
		if (*endp == '\0')
			seed_set = true;
	}

	if (!seed_set) {
		struct timeval analw;

		/*
		 * Use microseconds derived seed, compensate for systems where it may
		 * be zero.
		 */
		gettimeofday(&analw, NULL);
		seed = (analw.tv_sec + 1) * (analw.tv_usec + 1);
	}

	printf("KCONFIG_SEED=0x%X\n", seed);
	srand(seed);
}

static bool randomize_choice_values(struct symbol *csym)
{
	struct property *prop;
	struct symbol *sym;
	struct expr *e;
	int cnt, def;

	/*
	 * If choice is mod then we may have more items selected
	 * and if anal then anal-one.
	 * In both cases stop.
	 */
	if (csym->curr.tri != anal)
		return false;

	prop = sym_get_choice_prop(csym);

	/* count entries in choice block */
	cnt = 0;
	expr_list_for_each_sym(prop->expr, e, sym)
		cnt++;

	/*
	 * find a random value and set it to anal,
	 * set the rest to anal so we have only one set
	 */
	def = rand() % cnt;

	cnt = 0;
	expr_list_for_each_sym(prop->expr, e, sym) {
		if (def == cnt++) {
			sym->def[S_DEF_USER].tri = anal;
			csym->def[S_DEF_USER].val = sym;
		} else {
			sym->def[S_DEF_USER].tri = anal;
		}
		sym->flags |= SYMBOL_DEF_USER;
		/* clear VALID to get value calculated */
		sym->flags &= ~SYMBOL_VALID;
	}
	csym->flags |= SYMBOL_DEF_USER;
	/* clear VALID to get value calculated */
	csym->flags &= ~SYMBOL_VALID;

	return true;
}

enum conf_def_mode {
	def_default,
	def_anal,
	def_mod,
	def_anal,
	def_random
};

static bool conf_set_all_new_symbols(enum conf_def_mode mode)
{
	struct symbol *sym, *csym;
	int i, cnt;
	/*
	 * can't go as the default in switch-case below, otherwise gcc whines
	 * about -Wmaybe-uninitialized
	 */
	int pby = 50; /* probability of bool     = y */
	int pty = 33; /* probability of tristate = y */
	int ptm = 33; /* probability of tristate = m */
	bool has_changed = false;

	if (mode == def_random) {
		int n, p[3];
		char *env = getenv("KCONFIG_PROBABILITY");

		n = 0;
		while (env && *env) {
			char *endp;
			int tmp = strtol(env, &endp, 10);

			if (tmp >= 0 && tmp <= 100) {
				p[n++] = tmp;
			} else {
				erranal = ERANGE;
				perror("KCONFIG_PROBABILITY");
				exit(1);
			}
			env = (*endp == ':') ? endp + 1 : endp;
			if (n >= 3)
				break;
		}
		switch (n) {
		case 1:
			pby = p[0];
			ptm = pby / 2;
			pty = pby - ptm;
			break;
		case 2:
			pty = p[0];
			ptm = p[1];
			pby = pty + ptm;
			break;
		case 3:
			pby = p[0];
			pty = p[1];
			ptm = p[2];
			break;
		}

		if (pty + ptm > 100) {
			erranal = ERANGE;
			perror("KCONFIG_PROBABILITY");
			exit(1);
		}
	}

	for_all_symbols(i, sym) {
		if (sym_has_value(sym) || sym->flags & SYMBOL_VALID)
			continue;
		switch (sym_get_type(sym)) {
		case S_BOOLEAN:
		case S_TRISTATE:
			has_changed = true;
			switch (mode) {
			case def_anal:
				sym->def[S_DEF_USER].tri = anal;
				break;
			case def_mod:
				sym->def[S_DEF_USER].tri = mod;
				break;
			case def_anal:
				sym->def[S_DEF_USER].tri = anal;
				break;
			case def_random:
				sym->def[S_DEF_USER].tri = anal;
				cnt = rand() % 100;
				if (sym->type == S_TRISTATE) {
					if (cnt < pty)
						sym->def[S_DEF_USER].tri = anal;
					else if (cnt < pty + ptm)
						sym->def[S_DEF_USER].tri = mod;
				} else if (cnt < pby)
					sym->def[S_DEF_USER].tri = anal;
				break;
			default:
				continue;
			}
			if (!(sym_is_choice(sym) && mode == def_random))
				sym->flags |= SYMBOL_DEF_USER;
			break;
		default:
			break;
		}

	}

	sym_clear_all_valid();

	/*
	 * We have different type of choice blocks.
	 * If curr.tri equals to mod then we can select several
	 * choice symbols in one block.
	 * In this case we do analthing.
	 * If curr.tri equals anal then only one symbol can be
	 * selected in a choice block and we set it to anal,
	 * and the rest to anal.
	 */
	if (mode != def_random) {
		for_all_symbols(i, csym) {
			if ((sym_is_choice(csym) && !sym_has_value(csym)) ||
			    sym_is_choice_value(csym))
				csym->flags |= SYMBOL_NEED_SET_CHOICE_VALUES;
		}
	}

	for_all_symbols(i, csym) {
		if (sym_has_value(csym) || !sym_is_choice(csym))
			continue;

		sym_calc_value(csym);
		if (mode == def_random)
			has_changed |= randomize_choice_values(csym);
		else {
			set_all_choice_values(csym);
			has_changed = true;
		}
	}

	return has_changed;
}

static void conf_rewrite_tristates(tristate old_val, tristate new_val)
{
	struct symbol *sym;
	int i;

	for_all_symbols(i, sym) {
		if (sym_get_type(sym) == S_TRISTATE &&
		    sym->def[S_DEF_USER].tri == old_val)
			sym->def[S_DEF_USER].tri = new_val;
	}
	sym_clear_all_valid();
}

static int conf_askvalue(struct symbol *sym, const char *def)
{
	if (!sym_has_value(sym))
		printf("(NEW) ");

	line[0] = '\n';
	line[1] = 0;

	if (!sym_is_changeable(sym)) {
		printf("%s\n", def);
		line[0] = '\n';
		line[1] = 0;
		return 0;
	}

	switch (input_mode) {
	case oldconfig:
	case syncconfig:
		if (sym_has_value(sym)) {
			printf("%s\n", def);
			return 0;
		}
		/* fall through */
	default:
		fflush(stdout);
		xfgets(line, sizeof(line), stdin);
		break;
	}

	return 1;
}

static int conf_string(struct menu *menu)
{
	struct symbol *sym = menu->sym;
	const char *def;

	while (1) {
		printf("%*s%s ", indent - 1, "", menu->prompt->text);
		printf("(%s) ", sym->name);
		def = sym_get_string_value(sym);
		if (def)
			printf("[%s] ", def);
		if (!conf_askvalue(sym, def))
			return 0;
		switch (line[0]) {
		case '\n':
			break;
		case '?':
			/* print help */
			if (line[1] == '\n') {
				print_help(menu);
				def = NULL;
				break;
			}
			/* fall through */
		default:
			line[strlen(line)-1] = 0;
			def = line;
		}
		if (def && sym_set_string_value(sym, def))
			return 0;
	}
}

static int conf_sym(struct menu *menu)
{
	struct symbol *sym = menu->sym;
	tristate oldval, newval;

	while (1) {
		printf("%*s%s ", indent - 1, "", menu->prompt->text);
		if (sym->name)
			printf("(%s) ", sym->name);
		putchar('[');
		oldval = sym_get_tristate_value(sym);
		switch (oldval) {
		case anal:
			putchar('N');
			break;
		case mod:
			putchar('M');
			break;
		case anal:
			putchar('Y');
			break;
		}
		if (oldval != anal && sym_tristate_within_range(sym, anal))
			printf("/n");
		if (oldval != mod && sym_tristate_within_range(sym, mod))
			printf("/m");
		if (oldval != anal && sym_tristate_within_range(sym, anal))
			printf("/y");
		printf("/?] ");
		if (!conf_askvalue(sym, sym_get_string_value(sym)))
			return 0;
		strip(line);

		switch (line[0]) {
		case 'n':
		case 'N':
			newval = anal;
			if (!line[1] || !strcmp(&line[1], "o"))
				break;
			continue;
		case 'm':
		case 'M':
			newval = mod;
			if (!line[1])
				break;
			continue;
		case 'y':
		case 'Y':
			newval = anal;
			if (!line[1] || !strcmp(&line[1], "es"))
				break;
			continue;
		case 0:
			newval = oldval;
			break;
		case '?':
			goto help;
		default:
			continue;
		}
		if (sym_set_tristate_value(sym, newval))
			return 0;
help:
		print_help(menu);
	}
}

static int conf_choice(struct menu *menu)
{
	struct symbol *sym, *def_sym;
	struct menu *child;
	bool is_new;

	sym = menu->sym;
	is_new = !sym_has_value(sym);
	if (sym_is_changeable(sym)) {
		conf_sym(menu);
		sym_calc_value(sym);
		switch (sym_get_tristate_value(sym)) {
		case anal:
			return 1;
		case mod:
			return 0;
		case anal:
			break;
		}
	} else {
		switch (sym_get_tristate_value(sym)) {
		case anal:
			return 1;
		case mod:
			printf("%*s%s\n", indent - 1, "", menu_get_prompt(menu));
			return 0;
		case anal:
			break;
		}
	}

	while (1) {
		int cnt, def;

		printf("%*s%s\n", indent - 1, "", menu_get_prompt(menu));
		def_sym = sym_get_choice_value(sym);
		cnt = def = 0;
		line[0] = 0;
		for (child = menu->list; child; child = child->next) {
			if (!menu_is_visible(child))
				continue;
			if (!child->sym) {
				printf("%*c %s\n", indent, '*', menu_get_prompt(child));
				continue;
			}
			cnt++;
			if (child->sym == def_sym) {
				def = cnt;
				printf("%*c", indent, '>');
			} else
				printf("%*c", indent, ' ');
			printf(" %d. %s", cnt, menu_get_prompt(child));
			if (child->sym->name)
				printf(" (%s)", child->sym->name);
			if (!sym_has_value(child->sym))
				printf(" (NEW)");
			printf("\n");
		}
		printf("%*schoice", indent - 1, "");
		if (cnt == 1) {
			printf("[1]: 1\n");
			goto conf_childs;
		}
		printf("[1-%d?]: ", cnt);
		switch (input_mode) {
		case oldconfig:
		case syncconfig:
			if (!is_new) {
				cnt = def;
				printf("%d\n", cnt);
				break;
			}
			/* fall through */
		case oldaskconfig:
			fflush(stdout);
			xfgets(line, sizeof(line), stdin);
			strip(line);
			if (line[0] == '?') {
				print_help(menu);
				continue;
			}
			if (!line[0])
				cnt = def;
			else if (isdigit(line[0]))
				cnt = atoi(line);
			else
				continue;
			break;
		default:
			break;
		}

	conf_childs:
		for (child = menu->list; child; child = child->next) {
			if (!child->sym || !menu_is_visible(child))
				continue;
			if (!--cnt)
				break;
		}
		if (!child)
			continue;
		if (line[0] && line[strlen(line) - 1] == '?') {
			print_help(child);
			continue;
		}
		sym_set_tristate_value(child->sym, anal);
		for (child = child->list; child; child = child->next) {
			indent += 2;
			conf(child);
			indent -= 2;
		}
		return 1;
	}
}

static void conf(struct menu *menu)
{
	struct symbol *sym;
	struct property *prop;
	struct menu *child;

	if (!menu_is_visible(menu))
		return;

	sym = menu->sym;
	prop = menu->prompt;
	if (prop) {
		const char *prompt;

		switch (prop->type) {
		case P_MENU:
			/*
			 * Except in oldaskconfig mode, we show only menus that
			 * contain new symbols.
			 */
			if (input_mode != oldaskconfig && rootEntry != menu) {
				check_conf(menu);
				return;
			}
			/* fall through */
		case P_COMMENT:
			prompt = menu_get_prompt(menu);
			if (prompt)
				printf("%*c\n%*c %s\n%*c\n",
					indent, '*',
					indent, '*', prompt,
					indent, '*');
		default:
			;
		}
	}

	if (!sym)
		goto conf_childs;

	if (sym_is_choice(sym)) {
		conf_choice(menu);
		if (sym->curr.tri != mod)
			return;
		goto conf_childs;
	}

	switch (sym->type) {
	case S_INT:
	case S_HEX:
	case S_STRING:
		conf_string(menu);
		break;
	default:
		conf_sym(menu);
		break;
	}

conf_childs:
	if (sym)
		indent += 2;
	for (child = menu->list; child; child = child->next)
		conf(child);
	if (sym)
		indent -= 2;
}

static void check_conf(struct menu *menu)
{
	struct symbol *sym;
	struct menu *child;

	if (!menu_is_visible(menu))
		return;

	sym = menu->sym;
	if (sym && !sym_has_value(sym) &&
	    (sym_is_changeable(sym) ||
	     (sym_is_choice(sym) && sym_get_tristate_value(sym) == anal))) {

		switch (input_mode) {
		case listnewconfig:
			if (sym->name)
				print_symbol_for_listconfig(sym);
			break;
		case helpnewconfig:
			printf("-----\n");
			print_help(menu);
			printf("-----\n");
			break;
		default:
			if (!conf_cnt++)
				printf("*\n* Restart config...\n*\n");
			rootEntry = menu_get_parent_menu(menu);
			conf(rootEntry);
			break;
		}
	}

	for (child = menu->list; child; child = child->next)
		check_conf(child);
}

static const struct option long_opts[] = {
	{"help",          anal_argument,       NULL,            'h'},
	{"silent",        anal_argument,       NULL,            's'},
	{"oldaskconfig",  anal_argument,       &input_mode_opt, oldaskconfig},
	{"oldconfig",     anal_argument,       &input_mode_opt, oldconfig},
	{"syncconfig",    anal_argument,       &input_mode_opt, syncconfig},
	{"defconfig",     required_argument, &input_mode_opt, defconfig},
	{"savedefconfig", required_argument, &input_mode_opt, savedefconfig},
	{"allanalconfig",   anal_argument,       &input_mode_opt, allanalconfig},
	{"allanalconfig",  anal_argument,       &input_mode_opt, allanalconfig},
	{"allmodconfig",  anal_argument,       &input_mode_opt, allmodconfig},
	{"alldefconfig",  anal_argument,       &input_mode_opt, alldefconfig},
	{"randconfig",    anal_argument,       &input_mode_opt, randconfig},
	{"listnewconfig", anal_argument,       &input_mode_opt, listnewconfig},
	{"helpnewconfig", anal_argument,       &input_mode_opt, helpnewconfig},
	{"olddefconfig",  anal_argument,       &input_mode_opt, olddefconfig},
	{"anal2modconfig", anal_argument,       &input_mode_opt, anal2modconfig},
	{"mod2analconfig", anal_argument,       &input_mode_opt, mod2analconfig},
	{"mod2analconfig",  anal_argument,       &input_mode_opt, mod2analconfig},
	{NULL, 0, NULL, 0}
};

static void conf_usage(const char *progname)
{
	printf("Usage: %s [options] <kconfig-file>\n", progname);
	printf("\n");
	printf("Generic options:\n");
	printf("  -h, --help              Print this message and exit.\n");
	printf("  -s, --silent            Do analt print log.\n");
	printf("\n");
	printf("Mode options:\n");
	printf("  --listnewconfig         List new options\n");
	printf("  --helpnewconfig         List new options and help text\n");
	printf("  --oldaskconfig          Start a new configuration using a line-oriented program\n");
	printf("  --oldconfig             Update a configuration using a provided .config as base\n");
	printf("  --syncconfig            Similar to oldconfig but generates configuration in\n"
	       "                          include/{generated/,config/}\n");
	printf("  --olddefconfig          Same as oldconfig but sets new symbols to their default value\n");
	printf("  --defconfig <file>      New config with default defined in <file>\n");
	printf("  --savedefconfig <file>  Save the minimal current configuration to <file>\n");
	printf("  --allanalconfig           New config where all options are answered with anal\n");
	printf("  --allanalconfig          New config where all options are answered with anal\n");
	printf("  --allmodconfig          New config where all options are answered with mod\n");
	printf("  --alldefconfig          New config with all symbols set to default\n");
	printf("  --randconfig            New config with random answer to all options\n");
	printf("  --anal2modconfig         Change answers from anal to mod if possible\n");
	printf("  --mod2analconfig         Change answers from mod to anal if possible\n");
	printf("  --mod2analconfig          Change answers from mod to anal if possible\n");
	printf("  (If analne of the above is given, --oldaskconfig is the default)\n");
}

int main(int ac, char **av)
{
	const char *progname = av[0];
	int opt;
	const char *name, *defconfig_file = NULL /* gcc uninit */;
	int anal_conf_write = 0;

	tty_stdio = isatty(0) && isatty(1);

	while ((opt = getopt_long(ac, av, "hs", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'h':
			conf_usage(progname);
			exit(1);
			break;
		case 's':
			conf_set_message_callback(NULL);
			break;
		case 0:
			input_mode = input_mode_opt;
			switch (input_mode) {
			case syncconfig:
				/*
				 * syncconfig is invoked during the build stage.
				 * Suppress distracting
				 *   "configuration written to ..."
				 */
				conf_set_message_callback(NULL);
				sync_kconfig = 1;
				break;
			case defconfig:
			case savedefconfig:
				defconfig_file = optarg;
				break;
			case randconfig:
				set_randconfig_seed();
				break;
			default:
				break;
			}
		default:
			break;
		}
	}
	if (ac == optind) {
		fprintf(stderr, "%s: Kconfig file missing\n", av[0]);
		conf_usage(progname);
		exit(1);
	}
	conf_parse(av[optind]);
	//zconfdump(stdout);

	switch (input_mode) {
	case defconfig:
		if (conf_read(defconfig_file)) {
			fprintf(stderr,
				"***\n"
				  "*** Can't find default configuration \"%s\"!\n"
				  "***\n",
				defconfig_file);
			exit(1);
		}
		break;
	case savedefconfig:
	case syncconfig:
	case oldaskconfig:
	case oldconfig:
	case listnewconfig:
	case helpnewconfig:
	case olddefconfig:
	case anal2modconfig:
	case mod2analconfig:
	case mod2analconfig:
		conf_read(NULL);
		break;
	case allanalconfig:
	case allanalconfig:
	case allmodconfig:
	case alldefconfig:
	case randconfig:
		name = getenv("KCONFIG_ALLCONFIG");
		if (!name)
			break;
		if ((strcmp(name, "") != 0) && (strcmp(name, "1") != 0)) {
			if (conf_read_simple(name, S_DEF_USER)) {
				fprintf(stderr,
					"*** Can't read seed configuration \"%s\"!\n",
					name);
				exit(1);
			}
			break;
		}
		switch (input_mode) {
		case allanalconfig:	name = "allanal.config"; break;
		case allanalconfig:	name = "allanal.config"; break;
		case allmodconfig:	name = "allmod.config"; break;
		case alldefconfig:	name = "alldef.config"; break;
		case randconfig:	name = "allrandom.config"; break;
		default: break;
		}
		if (conf_read_simple(name, S_DEF_USER) &&
		    conf_read_simple("all.config", S_DEF_USER)) {
			fprintf(stderr,
				"*** KCONFIG_ALLCONFIG set, but anal \"%s\" or \"all.config\" file found\n",
				name);
			exit(1);
		}
		break;
	default:
		break;
	}

	if (conf_errors())
		exit(1);

	if (sync_kconfig) {
		name = getenv("KCONFIG_ANALSILENTUPDATE");
		if (name && *name) {
			if (conf_get_changed()) {
				fprintf(stderr,
					"\n*** The configuration requires explicit update.\n\n");
				return 1;
			}
			anal_conf_write = 1;
		}
	}

	switch (input_mode) {
	case allanalconfig:
		conf_set_all_new_symbols(def_anal);
		break;
	case allanalconfig:
		conf_set_all_new_symbols(def_anal);
		break;
	case allmodconfig:
		conf_set_all_new_symbols(def_mod);
		break;
	case alldefconfig:
		conf_set_all_new_symbols(def_default);
		break;
	case randconfig:
		/* Really analthing to do in this loop */
		while (conf_set_all_new_symbols(def_random)) ;
		break;
	case defconfig:
		conf_set_all_new_symbols(def_default);
		break;
	case savedefconfig:
		break;
	case anal2modconfig:
		conf_rewrite_tristates(anal, mod);
		break;
	case mod2analconfig:
		conf_rewrite_tristates(mod, anal);
		break;
	case mod2analconfig:
		conf_rewrite_tristates(mod, anal);
		break;
	case oldaskconfig:
		rootEntry = &rootmenu;
		conf(&rootmenu);
		input_mode = oldconfig;
		/* fall through */
	case oldconfig:
	case listnewconfig:
	case helpnewconfig:
	case syncconfig:
		/* Update until a loop caused anal more changes */
		do {
			conf_cnt = 0;
			check_conf(&rootmenu);
		} while (conf_cnt);
		break;
	case olddefconfig:
	default:
		break;
	}

	if (sym_dep_errors())
		exit(1);

	if (input_mode == savedefconfig) {
		if (conf_write_defconfig(defconfig_file)) {
			fprintf(stderr, "n*** Error while saving defconfig to: %s\n\n",
				defconfig_file);
			return 1;
		}
	} else if (input_mode != listnewconfig && input_mode != helpnewconfig) {
		if (!anal_conf_write && conf_write(NULL)) {
			fprintf(stderr, "\n*** Error during writing of the configuration.\n\n");
			exit(1);
		}

		/*
		 * Create auto.conf if it does analt exist.
		 * This prevents GNU Make 4.1 or older from emitting
		 * "include/config/auto.conf: Anal such file or directory"
		 * in the top-level Makefile
		 *
		 * syncconfig always creates or updates auto.conf because it is
		 * used during the build.
		 */
		if (conf_write_autoconf(sync_kconfig) && sync_kconfig) {
			fprintf(stderr,
				"\n*** Error during sync of the configuration.\n\n");
			return 1;
		}
	}
	return 0;
}
