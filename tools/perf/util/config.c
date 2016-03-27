/*
 * config.c
 *
 * Helper functions for parsing config items.
 * Originally copied from GIT source.
 *
 * Copyright (C) Linus Torvalds, 2005
 * Copyright (C) Johannes Schindelin, 2005
 *
 */
#include "util.h"
#include "cache.h"
#include <subcmd/exec-cmd.h>
#include "util/hist.h"  /* perf_hist_config */
#include "util/llvm-utils.h"   /* perf_llvm_config */

#define MAXNAME (256)

#define DEBUG_CACHE_DIR ".debug"


char buildid_dir[MAXPATHLEN]; /* root dir for buildid, binary cache */

static FILE *config_file;
static const char *config_file_name;
static int config_linenr;
static int config_file_eof;

const char *config_exclusive_filename;

static int get_next_char(void)
{
	int c;
	FILE *f;

	c = '\n';
	if ((f = config_file) != NULL) {
		c = fgetc(f);
		if (c == '\r') {
			/* DOS like systems */
			c = fgetc(f);
			if (c != '\n') {
				ungetc(c, f);
				c = '\r';
			}
		}
		if (c == '\n')
			config_linenr++;
		if (c == EOF) {
			config_file_eof = 1;
			c = '\n';
		}
	}
	return c;
}

static char *parse_value(void)
{
	static char value[1024];
	int quote = 0, comment = 0, space = 0;
	size_t len = 0;

	for (;;) {
		int c = get_next_char();

		if (len >= sizeof(value) - 1)
			return NULL;
		if (c == '\n') {
			if (quote)
				return NULL;
			value[len] = 0;
			return value;
		}
		if (comment)
			continue;
		if (isspace(c) && !quote) {
			space = 1;
			continue;
		}
		if (!quote) {
			if (c == ';' || c == '#') {
				comment = 1;
				continue;
			}
		}
		if (space) {
			if (len)
				value[len++] = ' ';
			space = 0;
		}
		if (c == '\\') {
			c = get_next_char();
			switch (c) {
			case '\n':
				continue;
			case 't':
				c = '\t';
				break;
			case 'b':
				c = '\b';
				break;
			case 'n':
				c = '\n';
				break;
			/* Some characters escape as themselves */
			case '\\': case '"':
				break;
			/* Reject unknown escape sequences */
			default:
				return NULL;
			}
			value[len++] = c;
			continue;
		}
		if (c == '"') {
			quote = 1-quote;
			continue;
		}
		value[len++] = c;
	}
}

static inline int iskeychar(int c)
{
	return isalnum(c) || c == '-' || c == '_';
}

static int get_value(config_fn_t fn, void *data, char *name, unsigned int len)
{
	int c;
	char *value;

	/* Get the full name */
	for (;;) {
		c = get_next_char();
		if (config_file_eof)
			break;
		if (!iskeychar(c))
			break;
		name[len++] = c;
		if (len >= MAXNAME)
			return -1;
	}
	name[len] = 0;
	while (c == ' ' || c == '\t')
		c = get_next_char();

	value = NULL;
	if (c != '\n') {
		if (c != '=')
			return -1;
		value = parse_value();
		if (!value)
			return -1;
	}
	return fn(name, value, data);
}

static int get_extended_base_var(char *name, int baselen, int c)
{
	do {
		if (c == '\n')
			return -1;
		c = get_next_char();
	} while (isspace(c));

	/* We require the format to be '[base "extension"]' */
	if (c != '"')
		return -1;
	name[baselen++] = '.';

	for (;;) {
		int ch = get_next_char();

		if (ch == '\n')
			return -1;
		if (ch == '"')
			break;
		if (ch == '\\') {
			ch = get_next_char();
			if (ch == '\n')
				return -1;
		}
		name[baselen++] = ch;
		if (baselen > MAXNAME / 2)
			return -1;
	}

	/* Final ']' */
	if (get_next_char() != ']')
		return -1;
	return baselen;
}

static int get_base_var(char *name)
{
	int baselen = 0;

	for (;;) {
		int c = get_next_char();
		if (config_file_eof)
			return -1;
		if (c == ']')
			return baselen;
		if (isspace(c))
			return get_extended_base_var(name, baselen, c);
		if (!iskeychar(c) && c != '.')
			return -1;
		if (baselen > MAXNAME / 2)
			return -1;
		name[baselen++] = tolower(c);
	}
}

static int perf_parse_file(config_fn_t fn, void *data)
{
	int comment = 0;
	int baselen = 0;
	static char var[MAXNAME];

	/* U+FEFF Byte Order Mark in UTF8 */
	static const unsigned char *utf8_bom = (unsigned char *) "\xef\xbb\xbf";
	const unsigned char *bomptr = utf8_bom;

	for (;;) {
		int line, c = get_next_char();

		if (bomptr && *bomptr) {
			/* We are at the file beginning; skip UTF8-encoded BOM
			 * if present. Sane editors won't put this in on their
			 * own, but e.g. Windows Notepad will do it happily. */
			if ((unsigned char) c == *bomptr) {
				bomptr++;
				continue;
			} else {
				/* Do not tolerate partial BOM. */
				if (bomptr != utf8_bom)
					break;
				/* No BOM at file beginning. Cool. */
				bomptr = NULL;
			}
		}
		if (c == '\n') {
			if (config_file_eof)
				return 0;
			comment = 0;
			continue;
		}
		if (comment || isspace(c))
			continue;
		if (c == '#' || c == ';') {
			comment = 1;
			continue;
		}
		if (c == '[') {
			baselen = get_base_var(var);
			if (baselen <= 0)
				break;
			var[baselen++] = '.';
			var[baselen] = 0;
			continue;
		}
		if (!isalpha(c))
			break;
		var[baselen] = tolower(c);

		/*
		 * The get_value function might or might not reach the '\n',
		 * so saving the current line number for error reporting.
		 */
		line = config_linenr;
		if (get_value(fn, data, var, baselen+1) < 0) {
			config_linenr = line;
			break;
		}
	}
	die("bad config file line %d in %s", config_linenr, config_file_name);
}

static int parse_unit_factor(const char *end, unsigned long *val)
{
	if (!*end)
		return 1;
	else if (!strcasecmp(end, "k")) {
		*val *= 1024;
		return 1;
	}
	else if (!strcasecmp(end, "m")) {
		*val *= 1024 * 1024;
		return 1;
	}
	else if (!strcasecmp(end, "g")) {
		*val *= 1024 * 1024 * 1024;
		return 1;
	}
	return 0;
}

static int perf_parse_llong(const char *value, long long *ret)
{
	if (value && *value) {
		char *end;
		long long val = strtoll(value, &end, 0);
		unsigned long factor = 1;

		if (!parse_unit_factor(end, &factor))
			return 0;
		*ret = val * factor;
		return 1;
	}
	return 0;
}

static int perf_parse_long(const char *value, long *ret)
{
	if (value && *value) {
		char *end;
		long val = strtol(value, &end, 0);
		unsigned long factor = 1;
		if (!parse_unit_factor(end, &factor))
			return 0;
		*ret = val * factor;
		return 1;
	}
	return 0;
}

static void die_bad_config(const char *name)
{
	if (config_file_name)
		die("bad config value for '%s' in %s", name, config_file_name);
	die("bad config value for '%s'", name);
}

u64 perf_config_u64(const char *name, const char *value)
{
	long long ret = 0;

	if (!perf_parse_llong(value, &ret))
		die_bad_config(name);
	return (u64) ret;
}

int perf_config_int(const char *name, const char *value)
{
	long ret = 0;
	if (!perf_parse_long(value, &ret))
		die_bad_config(name);
	return ret;
}

static int perf_config_bool_or_int(const char *name, const char *value, int *is_bool)
{
	*is_bool = 1;
	if (!value)
		return 1;
	if (!*value)
		return 0;
	if (!strcasecmp(value, "true") || !strcasecmp(value, "yes") || !strcasecmp(value, "on"))
		return 1;
	if (!strcasecmp(value, "false") || !strcasecmp(value, "no") || !strcasecmp(value, "off"))
		return 0;
	*is_bool = 0;
	return perf_config_int(name, value);
}

int perf_config_bool(const char *name, const char *value)
{
	int discard;
	return !!perf_config_bool_or_int(name, value, &discard);
}

const char *perf_config_dirname(const char *name, const char *value)
{
	if (!name)
		return NULL;
	return value;
}

static int perf_buildid_config(const char *var, const char *value)
{
	/* same dir for all commands */
	if (!strcmp(var, "buildid.dir")) {
		const char *dirname = perf_config_dirname(var, value);

		if (!dirname)
			return -1;
		strncpy(buildid_dir, dirname, MAXPATHLEN-1);
		buildid_dir[MAXPATHLEN-1] = '\0';
	}

	return 0;
}

static int perf_default_core_config(const char *var __maybe_unused,
				    const char *value __maybe_unused)
{
	/* Add other config variables here. */
	return 0;
}

static int perf_ui_config(const char *var, const char *value)
{
	/* Add other config variables here. */
	if (!strcmp(var, "ui.show-headers")) {
		symbol_conf.show_hist_headers = perf_config_bool(var, value);
		return 0;
	}
	return 0;
}

int perf_default_config(const char *var, const char *value,
			void *dummy __maybe_unused)
{
	if (!prefixcmp(var, "core."))
		return perf_default_core_config(var, value);

	if (!prefixcmp(var, "hist."))
		return perf_hist_config(var, value);

	if (!prefixcmp(var, "ui."))
		return perf_ui_config(var, value);

	if (!prefixcmp(var, "call-graph."))
		return perf_callchain_config(var, value);

	if (!prefixcmp(var, "llvm."))
		return perf_llvm_config(var, value);

	if (!prefixcmp(var, "buildid."))
		return perf_buildid_config(var, value);

	/* Add other config variables here. */
	return 0;
}

static int perf_config_from_file(config_fn_t fn, const char *filename, void *data)
{
	int ret;
	FILE *f = fopen(filename, "r");

	ret = -1;
	if (f) {
		config_file = f;
		config_file_name = filename;
		config_linenr = 1;
		config_file_eof = 0;
		ret = perf_parse_file(fn, data);
		fclose(f);
		config_file_name = NULL;
	}
	return ret;
}

const char *perf_etc_perfconfig(void)
{
	static const char *system_wide;
	if (!system_wide)
		system_wide = system_path(ETC_PERFCONFIG);
	return system_wide;
}

static int perf_env_bool(const char *k, int def)
{
	const char *v = getenv(k);
	return v ? perf_config_bool(k, v) : def;
}

static int perf_config_system(void)
{
	return !perf_env_bool("PERF_CONFIG_NOSYSTEM", 0);
}

static int perf_config_global(void)
{
	return !perf_env_bool("PERF_CONFIG_NOGLOBAL", 0);
}

int perf_config(config_fn_t fn, void *data)
{
	int ret = 0, found = 0;
	const char *home = NULL;

	/* Setting $PERF_CONFIG makes perf read _only_ the given config file. */
	if (config_exclusive_filename)
		return perf_config_from_file(fn, config_exclusive_filename, data);
	if (perf_config_system() && !access(perf_etc_perfconfig(), R_OK)) {
		ret += perf_config_from_file(fn, perf_etc_perfconfig(),
					    data);
		found += 1;
	}

	home = getenv("HOME");
	if (perf_config_global() && home) {
		char *user_config = strdup(mkpath("%s/.perfconfig", home));
		struct stat st;

		if (user_config == NULL) {
			warning("Not enough memory to process %s/.perfconfig, "
				"ignoring it.", home);
			goto out;
		}

		if (stat(user_config, &st) < 0)
			goto out_free;

		if (st.st_uid && (st.st_uid != geteuid())) {
			warning("File %s not owned by current user or root, "
				"ignoring it.", user_config);
			goto out_free;
		}

		if (!st.st_size)
			goto out_free;

		ret += perf_config_from_file(fn, user_config, data);
		found += 1;
out_free:
		free(user_config);
	}
out:
	if (found == 0)
		return -1;
	return ret;
}

/*
 * Call this to report error for your variable that should not
 * get a boolean value (i.e. "[my] var" means "true").
 */
int config_error_nonbool(const char *var)
{
	return error("Missing value for '%s'", var);
}

void set_buildid_dir(const char *dir)
{
	if (dir)
		scnprintf(buildid_dir, MAXPATHLEN-1, "%s", dir);

	/* default to $HOME/.debug */
	if (buildid_dir[0] == '\0') {
		char *v = getenv("HOME");
		if (v) {
			snprintf(buildid_dir, MAXPATHLEN-1, "%s/%s",
				 v, DEBUG_CACHE_DIR);
		} else {
			strncpy(buildid_dir, DEBUG_CACHE_DIR, MAXPATHLEN-1);
		}
		buildid_dir[MAXPATHLEN-1] = '\0';
	}
	/* for communicating with external commands */
	setenv("PERF_BUILDID_DIR", buildid_dir, 1);
}
