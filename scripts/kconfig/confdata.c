/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lkc.h"

static void conf_warning(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

static void conf_message(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

static const char *conf_filename;
static int conf_lineno, conf_warnings, conf_unsaved;

const char conf_defname[] = "arch/$ARCH/defconfig";

static void conf_warning(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s:%d:warning: ", conf_filename, conf_lineno);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	conf_warnings++;
}

static void conf_default_message_callback(const char *fmt, va_list ap)
{
	printf("#\n# ");
	vprintf(fmt, ap);
	printf("\n#\n");
}

static void (*conf_message_callback) (const char *fmt, va_list ap) =
	conf_default_message_callback;
void conf_set_message_callback(void (*fn) (const char *fmt, va_list ap))
{
	conf_message_callback = fn;
}

static void conf_message(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (conf_message_callback)
		conf_message_callback(fmt, ap);
}

const char *conf_get_configname(void)
{
	char *name = getenv("KCONFIG_CONFIG");

	return name ? name : ".config";
}

const char *conf_get_autoconfig_name(void)
{
	char *name = getenv("KCONFIG_AUTOCONFIG");

	return name ? name : "include/config/auto.conf";
}

static char *conf_expand_value(const char *in)
{
	struct symbol *sym;
	const char *src;
	static char res_value[SYMBOL_MAXLENGTH];
	char *dst, name[SYMBOL_MAXLENGTH];

	res_value[0] = 0;
	dst = name;
	while ((src = strchr(in, '$'))) {
		strncat(res_value, in, src - in);
		src++;
		dst = name;
		while (isalnum(*src) || *src == '_')
			*dst++ = *src++;
		*dst = 0;
		sym = sym_lookup(name, 0);
		sym_calc_value(sym);
		strcat(res_value, sym_get_string_value(sym));
		in = src;
	}
	strcat(res_value, in);

	return res_value;
}

char *conf_get_default_confname(void)
{
	struct stat buf;
	static char fullname[PATH_MAX+1];
	char *env, *name;

	name = conf_expand_value(conf_defname);
	env = getenv(SRCTREE);
	if (env) {
		sprintf(fullname, "%s/%s", env, name);
		if (!stat(fullname, &buf))
			return fullname;
	}
	return name;
}

static int conf_set_sym_val(struct symbol *sym, int def, int def_flags, char *p)
{
	char *p2;

	switch (sym->type) {
	case S_TRISTATE:
		if (p[0] == 'm') {
			sym->def[def].tri = mod;
			sym->flags |= def_flags;
			break;
		}
		/* fall through */
	case S_BOOLEAN:
		if (p[0] == 'y') {
			sym->def[def].tri = yes;
			sym->flags |= def_flags;
			break;
		}
		if (p[0] == 'n') {
			sym->def[def].tri = no;
			sym->flags |= def_flags;
			break;
		}
		conf_warning("symbol value '%s' invalid for %s", p, sym->name);
		return 1;
	case S_OTHER:
		if (*p != '"') {
			for (p2 = p; *p2 && !isspace(*p2); p2++)
				;
			sym->type = S_STRING;
			goto done;
		}
		/* fall through */
	case S_STRING:
		if (*p++ != '"')
			break;
		for (p2 = p; (p2 = strpbrk(p2, "\"\\")); p2++) {
			if (*p2 == '"') {
				*p2 = 0;
				break;
			}
			memmove(p2, p2 + 1, strlen(p2));
		}
		if (!p2) {
			conf_warning("invalid string found");
			return 1;
		}
		/* fall through */
	case S_INT:
	case S_HEX:
	done:
		if (sym_string_valid(sym, p)) {
			sym->def[def].val = strdup(p);
			sym->flags |= def_flags;
		} else {
			conf_warning("symbol value '%s' invalid for %s", p, sym->name);
			return 1;
		}
		break;
	default:
		;
	}
	return 0;
}

int conf_read_simple(const char *name, int def)
{
	FILE *in = NULL;
	char line[1024];
	char *p, *p2;
	struct symbol *sym;
	int i, def_flags;

	if (name) {
		in = zconf_fopen(name);
	} else {
		struct property *prop;

		name = conf_get_configname();
		in = zconf_fopen(name);
		if (in)
			goto load;
		sym_add_change_count(1);
		if (!sym_defconfig_list) {
			if (modules_sym)
				sym_calc_value(modules_sym);
			return 1;
		}

		for_all_defaults(sym_defconfig_list, prop) {
			if (expr_calc_value(prop->visible.expr) == no ||
			    prop->expr->type != E_SYMBOL)
				continue;
			name = conf_expand_value(prop->expr->left.sym->name);
			in = zconf_fopen(name);
			if (in) {
				conf_message(_("using defaults found in %s"),
					 name);
				goto load;
			}
		}
	}
	if (!in)
		return 1;

load:
	conf_filename = name;
	conf_lineno = 0;
	conf_warnings = 0;
	conf_unsaved = 0;

	def_flags = SYMBOL_DEF << def;
	for_all_symbols(i, sym) {
		sym->flags |= SYMBOL_CHANGED;
		sym->flags &= ~(def_flags|SYMBOL_VALID);
		if (sym_is_choice(sym))
			sym->flags |= def_flags;
		switch (sym->type) {
		case S_INT:
		case S_HEX:
		case S_STRING:
			if (sym->def[def].val)
				free(sym->def[def].val);
			/* fall through */
		default:
			sym->def[def].val = NULL;
			sym->def[def].tri = no;
		}
	}

	while (fgets(line, sizeof(line), in)) {
		conf_lineno++;
		sym = NULL;
		if (line[0] == '#') {
			if (memcmp(line + 2, CONFIG_, strlen(CONFIG_)))
				continue;
			p = strchr(line + 2 + strlen(CONFIG_), ' ');
			if (!p)
				continue;
			*p++ = 0;
			if (strncmp(p, "is not set", 10))
				continue;
			if (def == S_DEF_USER) {
				sym = sym_find(line + 2 + strlen(CONFIG_));
				if (!sym) {
					sym_add_change_count(1);
					goto setsym;
				}
			} else {
				sym = sym_lookup(line + 2 + strlen(CONFIG_), 0);
				if (sym->type == S_UNKNOWN)
					sym->type = S_BOOLEAN;
			}
			if (sym->flags & def_flags) {
				conf_warning("override: reassigning to symbol %s", sym->name);
			}
			switch (sym->type) {
			case S_BOOLEAN:
			case S_TRISTATE:
				sym->def[def].tri = no;
				sym->flags |= def_flags;
				break;
			default:
				;
			}
		} else if (memcmp(line, CONFIG_, strlen(CONFIG_)) == 0) {
			p = strchr(line + strlen(CONFIG_), '=');
			if (!p)
				continue;
			*p++ = 0;
			p2 = strchr(p, '\n');
			if (p2) {
				*p2-- = 0;
				if (*p2 == '\r')
					*p2 = 0;
			}
			if (def == S_DEF_USER) {
				sym = sym_find(line + strlen(CONFIG_));
				if (!sym) {
					sym_add_change_count(1);
					goto setsym;
				}
			} else {
				sym = sym_lookup(line + strlen(CONFIG_), 0);
				if (sym->type == S_UNKNOWN)
					sym->type = S_OTHER;
			}
			if (sym->flags & def_flags) {
				conf_warning("override: reassigning to symbol %s", sym->name);
			}
			if (conf_set_sym_val(sym, def, def_flags, p))
				continue;
		} else {
			if (line[0] != '\r' && line[0] != '\n')
				conf_warning("unexpected data");
			continue;
		}
setsym:
		if (sym && sym_is_choice_value(sym)) {
			struct symbol *cs = prop_get_symbol(sym_get_choice_prop(sym));
			switch (sym->def[def].tri) {
			case no:
				break;
			case mod:
				if (cs->def[def].tri == yes) {
					conf_warning("%s creates inconsistent choice state", sym->name);
					cs->flags &= ~def_flags;
				}
				break;
			case yes:
				if (cs->def[def].tri != no)
					conf_warning("override: %s changes choice state", sym->name);
				cs->def[def].val = sym;
				break;
			}
			cs->def[def].tri = EXPR_OR(cs->def[def].tri, sym->def[def].tri);
		}
	}
	fclose(in);

	if (modules_sym)
		sym_calc_value(modules_sym);
	return 0;
}

int conf_read(const char *name)
{
	struct symbol *sym;
	int i;

	sym_set_change_count(0);

	if (conf_read_simple(name, S_DEF_USER))
		return 1;

	for_all_symbols(i, sym) {
		sym_calc_value(sym);
		if (sym_is_choice(sym) || (sym->flags & SYMBOL_AUTO))
			continue;
		if (sym_has_value(sym) && (sym->flags & SYMBOL_WRITE)) {
			/* check that calculated value agrees with saved value */
			switch (sym->type) {
			case S_BOOLEAN:
			case S_TRISTATE:
				if (sym->def[S_DEF_USER].tri != sym_get_tristate_value(sym))
					break;
				if (!sym_is_choice(sym))
					continue;
				/* fall through */
			default:
				if (!strcmp(sym->curr.val, sym->def[S_DEF_USER].val))
					continue;
				break;
			}
		} else if (!sym_has_value(sym) && !(sym->flags & SYMBOL_WRITE))
			/* no previous value and not saved */
			continue;
		conf_unsaved++;
		/* maybe print value in verbose mode... */
	}

	for_all_symbols(i, sym) {
		if (sym_has_value(sym) && !sym_is_choice_value(sym)) {
			/* Reset values of generates values, so they'll appear
			 * as new, if they should become visible, but that
			 * doesn't quite work if the Kconfig and the saved
			 * configuration disagree.
			 */
			if (sym->visible == no && !conf_unsaved)
				sym->flags &= ~SYMBOL_DEF_USER;
			switch (sym->type) {
			case S_STRING:
			case S_INT:
			case S_HEX:
				/* Reset a string value if it's out of range */
				if (sym_string_within_range(sym, sym->def[S_DEF_USER].val))
					break;
				sym->flags &= ~(SYMBOL_VALID|SYMBOL_DEF_USER);
				conf_unsaved++;
				break;
			default:
				break;
			}
		}
	}

	sym_add_change_count(conf_warnings || conf_unsaved);

	return 0;
}

/*
 * Kconfig configuration printer
 *
 * This printer is used when generating the resulting configuration after
 * kconfig invocation and `defconfig' files. Unset symbol might be omitted by
 * passing a non-NULL argument to the printer.
 *
 */
static void
kconfig_print_symbol(FILE *fp, struct symbol *sym, const char *value, void *arg)
{

	switch (sym->type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		if (*value == 'n') {
			bool skip_unset = (arg != NULL);

			if (!skip_unset)
				fprintf(fp, "# %s%s is not set\n",
				    CONFIG_, sym->name);
			return;
		}
		break;
	default:
		break;
	}

	fprintf(fp, "%s%s=%s\n", CONFIG_, sym->name, value);
}

static void
kconfig_print_comment(FILE *fp, const char *value, void *arg)
{
	const char *p = value;
	size_t l;

	for (;;) {
		l = strcspn(p, "\n");
		fprintf(fp, "#");
		if (l) {
			fprintf(fp, " ");
			xfwrite(p, l, 1, fp);
			p += l;
		}
		fprintf(fp, "\n");
		if (*p++ == '\0')
			break;
	}
}

static struct conf_printer kconfig_printer_cb =
{
	.print_symbol = kconfig_print_symbol,
	.print_comment = kconfig_print_comment,
};

/*
 * Header printer
 *
 * This printer is used when generating the `include/generated/autoconf.h' file.
 */
static void
header_print_symbol(FILE *fp, struct symbol *sym, const char *value, void *arg)
{

	switch (sym->type) {
	case S_BOOLEAN:
	case S_TRISTATE: {
		const char *suffix = "";

		switch (*value) {
		case 'n':
			break;
		case 'm':
			suffix = "_MODULE";
			/* fall through */
		default:
			fprintf(fp, "#define %s%s%s 1\n",
			    CONFIG_, sym->name, suffix);
		}
		break;
	}
	case S_HEX: {
		const char *prefix = "";

		if (value[0] != '0' || (value[1] != 'x' && value[1] != 'X'))
			prefix = "0x";
		fprintf(fp, "#define %s%s %s%s\n",
		    CONFIG_, sym->name, prefix, value);
		break;
	}
	case S_STRING:
	case S_INT:
		fprintf(fp, "#define %s%s %s\n",
		    CONFIG_, sym->name, value);
		break;
	default:
		break;
	}

}

static void
header_print_comment(FILE *fp, const char *value, void *arg)
{
	const char *p = value;
	size_t l;

	fprintf(fp, "/*\n");
	for (;;) {
		l = strcspn(p, "\n");
		fprintf(fp, " *");
		if (l) {
			fprintf(fp, " ");
			xfwrite(p, l, 1, fp);
			p += l;
		}
		fprintf(fp, "\n");
		if (*p++ == '\0')
			break;
	}
	fprintf(fp, " */\n");
}

static struct conf_printer header_printer_cb =
{
	.print_symbol = header_print_symbol,
	.print_comment = header_print_comment,
};

/*
 * Tristate printer
 *
 * This printer is used when generating the `include/config/tristate.conf' file.
 */
static void
tristate_print_symbol(FILE *fp, struct symbol *sym, const char *value, void *arg)
{

	if (sym->type == S_TRISTATE && *value != 'n')
		fprintf(fp, "%s%s=%c\n", CONFIG_, sym->name, (char)toupper(*value));
}

static struct conf_printer tristate_printer_cb =
{
	.print_symbol = tristate_print_symbol,
	.print_comment = kconfig_print_comment,
};

static void conf_write_symbol(FILE *fp, struct symbol *sym,
			      struct conf_printer *printer, void *printer_arg)
{
	const char *str;

	switch (sym->type) {
	case S_OTHER:
	case S_UNKNOWN:
		break;
	case S_STRING:
		str = sym_get_string_value(sym);
		str = sym_escape_string_value(str);
		printer->print_symbol(fp, sym, str, printer_arg);
		free((void *)str);
		break;
	default:
		str = sym_get_string_value(sym);
		printer->print_symbol(fp, sym, str, printer_arg);
	}
}

static void
conf_write_heading(FILE *fp, struct conf_printer *printer, void *printer_arg)
{
	char buf[256];

	snprintf(buf, sizeof(buf),
	    "\n"
	    "Automatically generated file; DO NOT EDIT.\n"
	    "%s\n",
	    rootmenu.prompt->text);

	printer->print_comment(fp, buf, printer_arg);
}

/*
 * Write out a minimal config.
 * All values that has default values are skipped as this is redundant.
 */
int conf_write_defconfig(const char *filename)
{
	struct symbol *sym;
	struct menu *menu;
	FILE *out;

	out = fopen(filename, "w");
	if (!out)
		return 1;

	sym_clear_all_valid();

	/* Traverse all menus to find all relevant symbols */
	menu = rootmenu.list;

	while (menu != NULL)
	{
		sym = menu->sym;
		if (sym == NULL) {
			if (!menu_is_visible(menu))
				goto next_menu;
		} else if (!sym_is_choice(sym)) {
			sym_calc_value(sym);
			if (!(sym->flags & SYMBOL_WRITE))
				goto next_menu;
			sym->flags &= ~SYMBOL_WRITE;
			/* If we cannot change the symbol - skip */
			if (!sym_is_changable(sym))
				goto next_menu;
			/* If symbol equals to default value - skip */
			if (strcmp(sym_get_string_value(sym), sym_get_string_default(sym)) == 0)
				goto next_menu;

			/*
			 * If symbol is a choice value and equals to the
			 * default for a choice - skip.
			 * But only if value is bool and equal to "y" and
			 * choice is not "optional".
			 * (If choice is "optional" then all values can be "n")
			 */
			if (sym_is_choice_value(sym)) {
				struct symbol *cs;
				struct symbol *ds;

				cs = prop_get_symbol(sym_get_choice_prop(sym));
				ds = sym_choice_default(cs);
				if (!sym_is_optional(cs) && sym == ds) {
					if ((sym->type == S_BOOLEAN) &&
					    sym_get_tristate_value(sym) == yes)
						goto next_menu;
				}
			}
			conf_write_symbol(out, sym, &kconfig_printer_cb, NULL);
		}
next_menu:
		if (menu->list != NULL) {
			menu = menu->list;
		}
		else if (menu->next != NULL) {
			menu = menu->next;
		} else {
			while ((menu = menu->parent)) {
				if (menu->next != NULL) {
					menu = menu->next;
					break;
				}
			}
		}
	}
	fclose(out);
	return 0;
}

int conf_write(const char *name)
{
	FILE *out;
	struct symbol *sym;
	struct menu *menu;
	const char *basename;
	const char *str;
	char dirname[PATH_MAX+1], tmpname[PATH_MAX+1], newname[PATH_MAX+1];
	char *env;

	dirname[0] = 0;
	if (name && name[0]) {
		struct stat st;
		char *slash;

		if (!stat(name, &st) && S_ISDIR(st.st_mode)) {
			strcpy(dirname, name);
			strcat(dirname, "/");
			basename = conf_get_configname();
		} else if ((slash = strrchr(name, '/'))) {
			int size = slash - name + 1;
			memcpy(dirname, name, size);
			dirname[size] = 0;
			if (slash[1])
				basename = slash + 1;
			else
				basename = conf_get_configname();
		} else
			basename = name;
	} else
		basename = conf_get_configname();

	sprintf(newname, "%s%s", dirname, basename);
	env = getenv("KCONFIG_OVERWRITECONFIG");
	if (!env || !*env) {
		sprintf(tmpname, "%s.tmpconfig.%d", dirname, (int)getpid());
		out = fopen(tmpname, "w");
	} else {
		*tmpname = 0;
		out = fopen(newname, "w");
	}
	if (!out)
		return 1;

	conf_write_heading(out, &kconfig_printer_cb, NULL);

	if (!conf_get_changed())
		sym_clear_all_valid();

	menu = rootmenu.list;
	while (menu) {
		sym = menu->sym;
		if (!sym) {
			if (!menu_is_visible(menu))
				goto next;
			str = menu_get_prompt(menu);
			fprintf(out, "\n"
				     "#\n"
				     "# %s\n"
				     "#\n", str);
		} else if (!(sym->flags & SYMBOL_CHOICE)) {
			sym_calc_value(sym);
			if (!(sym->flags & SYMBOL_WRITE))
				goto next;
			sym->flags &= ~SYMBOL_WRITE;

			conf_write_symbol(out, sym, &kconfig_printer_cb, NULL);
		}

next:
		if (menu->list) {
			menu = menu->list;
			continue;
		}
		if (menu->next)
			menu = menu->next;
		else while ((menu = menu->parent)) {
			if (menu->next) {
				menu = menu->next;
				break;
			}
		}
	}
	fclose(out);

	if (*tmpname) {
		strcat(dirname, basename);
		strcat(dirname, ".old");
		rename(newname, dirname);
		if (rename(tmpname, newname))
			return 1;
	}

	conf_message(_("configuration written to %s"), newname);

	sym_set_change_count(0);

	return 0;
}

static int conf_split_config(void)
{
	const char *name;
	char path[PATH_MAX+1];
	char *s, *d, c;
	struct symbol *sym;
	struct stat sb;
	int res, i, fd;

	name = conf_get_autoconfig_name();
	conf_read_simple(name, S_DEF_AUTO);

	if (chdir("include/config"))
		return 1;

	res = 0;
	for_all_symbols(i, sym) {
		sym_calc_value(sym);
		if ((sym->flags & SYMBOL_AUTO) || !sym->name)
			continue;
		if (sym->flags & SYMBOL_WRITE) {
			if (sym->flags & SYMBOL_DEF_AUTO) {
				/*
				 * symbol has old and new value,
				 * so compare them...
				 */
				switch (sym->type) {
				case S_BOOLEAN:
				case S_TRISTATE:
					if (sym_get_tristate_value(sym) ==
					    sym->def[S_DEF_AUTO].tri)
						continue;
					break;
				case S_STRING:
				case S_HEX:
				case S_INT:
					if (!strcmp(sym_get_string_value(sym),
						    sym->def[S_DEF_AUTO].val))
						continue;
					break;
				default:
					break;
				}
			} else {
				/*
				 * If there is no old value, only 'no' (unset)
				 * is allowed as new value.
				 */
				switch (sym->type) {
				case S_BOOLEAN:
				case S_TRISTATE:
					if (sym_get_tristate_value(sym) == no)
						continue;
					break;
				default:
					break;
				}
			}
		} else if (!(sym->flags & SYMBOL_DEF_AUTO))
			/* There is neither an old nor a new value. */
			continue;
		/* else
		 *	There is an old value, but no new value ('no' (unset)
		 *	isn't saved in auto.conf, so the old value is always
		 *	different from 'no').
		 */

		/* Replace all '_' and append ".h" */
		s = sym->name;
		d = path;
		while ((c = *s++)) {
			c = tolower(c);
			*d++ = (c == '_') ? '/' : c;
		}
		strcpy(d, ".h");

		/* Assume directory path already exists. */
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd == -1) {
			if (errno != ENOENT) {
				res = 1;
				break;
			}
			/*
			 * Create directory components,
			 * unless they exist already.
			 */
			d = path;
			while ((d = strchr(d, '/'))) {
				*d = 0;
				if (stat(path, &sb) && mkdir(path, 0755)) {
					res = 1;
					goto out;
				}
				*d++ = '/';
			}
			/* Try it again. */
			fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd == -1) {
				res = 1;
				break;
			}
		}
		close(fd);
	}
out:
	if (chdir("../.."))
		return 1;

	return res;
}

int conf_write_autoconf(void)
{
	struct symbol *sym;
	const char *name;
	FILE *out, *tristate, *out_h;
	int i;

	sym_clear_all_valid();

	file_write_dep("include/config/auto.conf.cmd");

	if (conf_split_config())
		return 1;

	out = fopen(".tmpconfig", "w");
	if (!out)
		return 1;

	tristate = fopen(".tmpconfig_tristate", "w");
	if (!tristate) {
		fclose(out);
		return 1;
	}

	out_h = fopen(".tmpconfig.h", "w");
	if (!out_h) {
		fclose(out);
		fclose(tristate);
		return 1;
	}

	conf_write_heading(out, &kconfig_printer_cb, NULL);

	conf_write_heading(tristate, &tristate_printer_cb, NULL);

	conf_write_heading(out_h, &header_printer_cb, NULL);

	for_all_symbols(i, sym) {
		sym_calc_value(sym);
		if (!(sym->flags & SYMBOL_WRITE) || !sym->name)
			continue;

		/* write symbol to auto.conf, tristate and header files */
		conf_write_symbol(out, sym, &kconfig_printer_cb, (void *)1);

		conf_write_symbol(tristate, sym, &tristate_printer_cb, (void *)1);

		conf_write_symbol(out_h, sym, &header_printer_cb, NULL);
	}
	fclose(out);
	fclose(tristate);
	fclose(out_h);

	name = getenv("KCONFIG_AUTOHEADER");
	if (!name)
		name = "include/generated/autoconf.h";
	if (rename(".tmpconfig.h", name))
		return 1;
	name = getenv("KCONFIG_TRISTATE");
	if (!name)
		name = "include/config/tristate.conf";
	if (rename(".tmpconfig_tristate", name))
		return 1;
	name = conf_get_autoconfig_name();
	/*
	 * This must be the last step, kbuild has a dependency on auto.conf
	 * and this marks the successful completion of the previous steps.
	 */
	if (rename(".tmpconfig", name))
		return 1;

	return 0;
}

static int sym_change_count;
static void (*conf_changed_callback)(void);

void sym_set_change_count(int count)
{
	int _sym_change_count = sym_change_count;
	sym_change_count = count;
	if (conf_changed_callback &&
	    (bool)_sym_change_count != (bool)count)
		conf_changed_callback();
}

void sym_add_change_count(int count)
{
	sym_set_change_count(count + sym_change_count);
}

bool conf_get_changed(void)
{
	return sym_change_count;
}

void conf_set_changed_callback(void (*fn)(void))
{
	conf_changed_callback = fn;
}

static void randomize_choice_values(struct symbol *csym)
{
	struct property *prop;
	struct symbol *sym;
	struct expr *e;
	int cnt, def;

	/*
	 * If choice is mod then we may have more items selected
	 * and if no then no-one.
	 * In both cases stop.
	 */
	if (csym->curr.tri != yes)
		return;

	prop = sym_get_choice_prop(csym);

	/* count entries in choice block */
	cnt = 0;
	expr_list_for_each_sym(prop->expr, e, sym)
		cnt++;

	/*
	 * find a random value and set it to yes,
	 * set the rest to no so we have only one set
	 */
	def = (rand() % cnt);

	cnt = 0;
	expr_list_for_each_sym(prop->expr, e, sym) {
		if (def == cnt++) {
			sym->def[S_DEF_USER].tri = yes;
			csym->def[S_DEF_USER].val = sym;
		}
		else {
			sym->def[S_DEF_USER].tri = no;
		}
	}
	csym->flags |= SYMBOL_DEF_USER;
	/* clear VALID to get value calculated */
	csym->flags &= ~(SYMBOL_VALID);
}

static void set_all_choice_values(struct symbol *csym)
{
	struct property *prop;
	struct symbol *sym;
	struct expr *e;

	prop = sym_get_choice_prop(csym);

	/*
	 * Set all non-assinged choice values to no
	 */
	expr_list_for_each_sym(prop->expr, e, sym) {
		if (!sym_has_value(sym))
			sym->def[S_DEF_USER].tri = no;
	}
	csym->flags |= SYMBOL_DEF_USER;
	/* clear VALID to get value calculated */
	csym->flags &= ~(SYMBOL_VALID);
}

void conf_set_all_new_symbols(enum conf_def_mode mode)
{
	struct symbol *sym, *csym;
	int i, cnt;

	for_all_symbols(i, sym) {
		if (sym_has_value(sym))
			continue;
		switch (sym_get_type(sym)) {
		case S_BOOLEAN:
		case S_TRISTATE:
			switch (mode) {
			case def_yes:
				sym->def[S_DEF_USER].tri = yes;
				break;
			case def_mod:
				sym->def[S_DEF_USER].tri = mod;
				break;
			case def_no:
				sym->def[S_DEF_USER].tri = no;
				break;
			case def_random:
				cnt = sym_get_type(sym) == S_TRISTATE ? 3 : 2;
				sym->def[S_DEF_USER].tri = (tristate)(rand() % cnt);
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
	 * In this case we do nothing.
	 * If curr.tri equals yes then only one symbol can be
	 * selected in a choice block and we set it to yes,
	 * and the rest to no.
	 */
	for_all_symbols(i, csym) {
		if (sym_has_value(csym) || !sym_is_choice(csym))
			continue;

		sym_calc_value(csym);
		if (mode == def_random)
			randomize_choice_values(csym);
		else
			set_all_choice_values(csym);
	}
}
