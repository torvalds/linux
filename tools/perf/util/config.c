// SPDX-License-Identifier: GPL-2.0
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
#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include <subcmd/exec-cmd.h>

#include "build-id.h"
#include "callchain.h"
#include "debug.h"
#include "header.h"
#include "mutex.h"
#include "path.h"
#include "srcline.h"
#include "unwind.h"
#include "util/event.h" /* proc_map_timeout */
#include "util/evsel.h" /* evsel__hw_names, evsel__use_bpf_counters */
#include "util/hist.h" /* perf_hist_config */
#include "util/stat.h" /* perf_stat__set_big_num */

#define MAXNAME (256)

#define DEBUG_CACHE_DIR ".debug"

#define METRIC_ONLY_LEN 20

static struct stats walltime_nsecs_stats;

struct perf_stat_config stat_config = {
	.aggr_mode		= AGGR_GLOBAL,
	.aggr_level		= MAX_CACHE_LVL + 1,
	.scale			= true,
	.unit_width		= 4, /* strlen("unit") */
	.run_count		= 1,
	.metric_only_len	= METRIC_ONLY_LEN,
	.walltime_nsecs_stats	= &walltime_nsecs_stats,
	.big_num		= true,
	.ctl_fd			= -1,
	.ctl_fd_ack		= -1,
	.iostat_run		= false,
};

char buildid_dir[MAXPATHLEN]; /* root dir for buildid, binary cache */

static FILE *config_file;
static const char *config_file_name;
static int config_linenr;
static int config_file_eof;
static struct perf_config_set *config_set;

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
	pr_err("bad config file line %d in %s\n", config_linenr, config_file_name);
	return -1;
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

static void bad_config(const char *name)
{
	/* config_file_name is owned by the parsing thread, under config_mutex. */
	pr_warning("bad config value for '%s', ignoring...\n", name);
}

int perf_config_u64(u64 *dest, const char *name, const char *value)
{
	long long ret = 0;

	if (!perf_parse_llong(value, &ret)) {
		bad_config(name);
		return -1;
	}

	*dest = ret;
	return 0;
}

int perf_config_int(int *dest, const char *name, const char *value)
{
	long ret = 0;
	if (!perf_parse_long(value, &ret)) {
		bad_config(name);
		return -1;
	}
	*dest = ret;
	return 0;
}

int perf_config_u8(u8 *dest, const char *name, const char *value)
{
	long ret = 0;

	if (!perf_parse_long(value, &ret)) {
		bad_config(name);
		return -1;
	}
	*dest = ret;
	return 0;
}

static int perf_config_bool_or_int(const char *name, const char *value, int *is_bool)
{
	int ret;

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
	return perf_config_int(&ret, name, value) < 0 ? -1 : ret;
}

int perf_config_bool(const char *name, const char *value)
{
	int discard;
	return !!perf_config_bool_or_int(name, value, &discard);
}

static const char *perf_config_dirname(const char *name, const char *value)
{
	if (!name)
		return NULL;
	return value;
}

static int perf_buildid_config(const char *var, const char *value)
{
	/* same dir for all commands */
	if (!strcmp(var, "buildid.dir")) {
		const char *dir = perf_config_dirname(var, value);

		if (!dir) {
			pr_err("Invalid buildid directory!\n");
			return -1;
		}
		strncpy(buildid_dir, dir, MAXPATHLEN-1);
		buildid_dir[MAXPATHLEN-1] = '\0';
	}

	return 0;
}

static int perf_default_core_config(const char *var, const char *value)
{
	if (!strcmp(var, "core.proc-map-timeout"))
		proc_map_timeout = strtoul(value, NULL, 10);

	if (!strcmp(var, "core.addr2line-timeout"))
		symbol_conf.addr2line_timeout_ms = strtoul(value, NULL, 10);

	if (!strcmp(var, "core.addr2line-disable-warn"))
		symbol_conf.addr2line_disable_warn = perf_config_bool(var, value);

	if (!strcmp(var, "core.hybrid-merge")) {
		/*
		 * Note, this is for sampling tools like perf report and top.
		 * perf stat has its own merging options and the
		 * stat_config.hybrid_merge of "perf stat --hybrid-merge" is
		 * deliberately not set here.
		 */
		symbol_conf.hybrid_merge = perf_config_bool(var, value);
	}

	/* Add other config variables here. */
	return 0;
}

static int perf_ui_config(const char *var, const char *value)
{
	/* Add other config variables here. */
	if (!strcmp(var, "ui.show-headers"))
		symbol_conf.show_hist_headers = perf_config_bool(var, value);

	return 0;
}

void perf_stat__set_big_num(int set)
{
	stat_config.big_num = (set != 0);
}

static void perf_stat__set_no_csv_summary(int set)
{
	stat_config.no_csv_summary = (set != 0);
}

static int perf_stat_config(const char *var, const char *value)
{
	if (!strcmp(var, "stat.big-num"))
		perf_stat__set_big_num(perf_config_bool(var, value));

	if (!strcmp(var, "stat.no-csv-summary"))
		perf_stat__set_no_csv_summary(perf_config_bool(var, value));

	if (!strcmp(var, "stat.bpf-counter-events"))
		evsel__bpf_counter_events = strdup(value);

	/* Add other config variables here. */
	return 0;
}

int perf_default_config(const char *var, const char *value,
			void *dummy __maybe_unused)
{
	if (strstarts(var, "core."))
		return perf_default_core_config(var, value);

	if (strstarts(var, "hist."))
		return perf_hist_config(var, value);

	if (strstarts(var, "ui."))
		return perf_ui_config(var, value);

	if (strstarts(var, "call-graph."))
		return perf_callchain_config(var, value);

	if (strstarts(var, "buildid."))
		return perf_buildid_config(var, value);

	if (strstarts(var, "stat."))
		return perf_stat_config(var, value);

	if (strstarts(var, "addr2line."))
		return addr2line_configure(var, value, dummy);

	if (strstarts(var, "unwind."))
		return unwind__configure(var, value, dummy);

	/* Add other config variables here. */
	return 0;
}

/* Parsing and rewriting share the static parser state. */
static DEFINE_MUTEX(config_mutex);

/* Serializes whole perf_config__set_variable() updates. */
static DEFINE_MUTEX(config_update_mutex);

static int perf_config_from_file(config_fn_t fn, const char *filename, void *data)
{
	int ret;
	FILE *f = fopen(filename, "r");

	mutex_lock(&config_mutex);
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
	mutex_unlock(&config_mutex);
	return ret;
}

/* system_path() allocates, so it is computed once. */
static const char *etc_perfconfig;

static void perf_etc_perfconfig__init(void)
{
	etc_perfconfig = system_path(ETC_PERFCONFIG);
	if (!etc_perfconfig)
		etc_perfconfig = ETC_PERFCONFIG;
}

const char *perf_etc_perfconfig(void)
{
	DO_ONCE(perf_etc_perfconfig__init);
	return etc_perfconfig;
}

static int perf_env_bool(const char *k, int def)
{
	const char *v = getenv(k);
	return v ? perf_config_bool(k, v) : def;
}

int perf_config_system(void)
{
	return !perf_env_bool("PERF_CONFIG_NOSYSTEM", 0);
}

int perf_config_global(void)
{
	return !perf_env_bool("PERF_CONFIG_NOGLOBAL", 0);
}

static char *home_perfconfig(void)
{
	const char *home = NULL;
	char *config;
	struct stat st;
	char path[PATH_MAX];

	home = getenv("HOME");

	/*
	 * Skip reading user config if:
	 *   - there is no place to read it from (HOME)
	 *   - we are asked not to (PERF_CONFIG_NOGLOBAL=1)
	 */
	if (!home || !*home || !perf_config_global())
		return NULL;

	config = strdup(mkpath(path, sizeof(path), "%s/.perfconfig", home));
	if (config == NULL) {
		pr_warning("Not enough memory to process %s/.perfconfig, ignoring it.\n", home);
		return NULL;
	}

	if (stat(config, &st) < 0)
		goto out_free;

	if (st.st_uid && (st.st_uid != geteuid())) {
		pr_warning("File %s not owned by current user or root, ignoring it.\n", config);
		goto out_free;
	}

	if (st.st_size)
		return config;

out_free:
	free(config);
	return NULL;
}

/* home_perfconfig() allocates and warns, so it is computed once. */
static const char *home_config;

static void perf_home_perfconfig__init(void)
{
	home_config = home_perfconfig();
}

const char *perf_home_perfconfig(void)
{
	DO_ONCE(perf_home_perfconfig__init);
	return home_config;
}

static struct perf_config_section *find_section(struct list_head *sections,
						const char *section_name)
{
	struct perf_config_section *section;

	list_for_each_entry(section, sections, node)
		if (!strcmp(section->name, section_name))
			return section;

	return NULL;
}

static struct perf_config_item *find_config_item(const char *name,
						 struct perf_config_section *section)
{
	struct perf_config_item *item;

	list_for_each_entry(item, &section->items, node)
		if (!strcmp(item->name, name))
			return item;

	return NULL;
}

static struct perf_config_section *add_section(struct list_head *sections,
					       const char *section_name)
{
	struct perf_config_section *section = zalloc(sizeof(*section));

	if (!section)
		return NULL;

	INIT_LIST_HEAD(&section->items);
	section->name = strdup(section_name);
	if (!section->name) {
		pr_debug("%s: strdup failed\n", __func__);
		free(section);
		return NULL;
	}

	list_add_tail(&section->node, sections);
	return section;
}

static struct perf_config_item *add_config_item(struct perf_config_section *section,
						const char *name)
{
	struct perf_config_item *item = zalloc(sizeof(*item));

	if (!item)
		return NULL;

	item->name = strdup(name);
	if (!item->name) {
		pr_debug("%s: strdup failed\n", __func__);
		free(item);
		return NULL;
	}

	list_add_tail(&item->node, &section->items);
	return item;
}

static int set_value(struct perf_config_item *item, const char *value)
{
	char *val = strdup(value);

	if (!val)
		return -1;

	zfree(&item->value);
	item->value = val;
	return 0;
}

static int collect_config(const char *var, const char *value,
			  void *perf_config_set)
{
	int ret = -1;
	char *ptr, *key;
	char *section_name, *name;
	struct perf_config_section *section = NULL;
	struct perf_config_item *item = NULL;
	struct perf_config_set *set = perf_config_set;
	struct list_head *sections;

	if (set == NULL)
		return -1;

	sections = &set->sections;
	key = ptr = strdup(var);
	if (!key) {
		pr_debug("%s: strdup failed\n", __func__);
		return -1;
	}

	section_name = strsep(&ptr, ".");
	name = ptr;
	if (name == NULL || value == NULL)
		goto out_free;

	section = find_section(sections, section_name);
	if (!section) {
		section = add_section(sections, section_name);
		if (!section)
			goto out_free;
	}

	item = find_config_item(name, section);
	if (!item) {
		item = add_config_item(section, name);
		if (!item)
			goto out_free;
	}

	/* perf_config_set can contain both user and system config items.
	 * So we should know where each value is from.
	 * The classification would be needed when a particular config file
	 * is overwritten by setting feature i.e. set_config().
	 */
	if (strcmp(config_file_name, perf_etc_perfconfig()) == 0) {
		section->from_system_config = true;
		item->from_system_config = true;
	} else {
		section->from_system_config = false;
		item->from_system_config = false;
	}

	ret = set_value(item, value);

out_free:
	free(key);
	return ret;
}

int perf_config_set__collect(struct perf_config_set *set, const char *file_name,
			     const char *var, const char *value)
{
	int ret;

	mutex_lock(&config_mutex);
	config_file_name = file_name;
	ret = collect_config(var, value, set);
	/* Don't leave the static parser state pointing at the caller's buffer. */
	config_file_name = NULL;
	mutex_unlock(&config_mutex);
	return ret;
}

static int perf_config_set__init(struct perf_config_set *set)
{
	int ret = -1;

	/* Setting $PERF_CONFIG makes perf read _only_ the given config file. */
	if (config_exclusive_filename)
		return perf_config_from_file(collect_config, config_exclusive_filename, set);
	if (perf_config_system() && !access(perf_etc_perfconfig(), R_OK)) {
		if (perf_config_from_file(collect_config, perf_etc_perfconfig(), set) < 0)
			goto out;
	}
	if (perf_config_global() && perf_home_perfconfig()) {
		if (perf_config_from_file(collect_config, perf_home_perfconfig(), set) < 0)
			goto out;
	}

out:
	return ret;
}

struct perf_config_set *perf_config_set__new(void)
{
	struct perf_config_set *set = zalloc(sizeof(*set));

	if (set) {
		INIT_LIST_HEAD(&set->sections);
		perf_config_set__init(set);
	}

	return set;
}

struct perf_config_set *perf_config_set__load_file(const char *file)
{
	struct perf_config_set *set = zalloc(sizeof(*set));

	if (set) {
		INIT_LIST_HEAD(&set->sections);
		perf_config_from_file(collect_config, file, set);
	}

	return set;
}

/* Not config_mutex: building the set parses the config files, which takes it. */
static DEFINE_MUTEX(config_set_mutex);

/* Called with config_set_mutex held. */
static int perf_config__init(void)
{
	if (config_set == NULL)
		config_set = perf_config_set__new();

	return config_set == NULL;
}

int perf_config_set(struct perf_config_set *set,
		    config_fn_t fn, void *data)
{
	int ret = 0;
	char key[BUFSIZ];
	struct perf_config_section *section;
	struct perf_config_item *item;

	perf_config_set__for_each_entry(set, section, item) {
		char *value = item->value;

		if (value) {
			scnprintf(key, sizeof(key), "%s.%s",
				  section->name, item->name);
			ret = fn(key, value, data);
			if (ret < 0) {
				pr_err("Error in the given config file: wrong config key-value pair %s=%s\n",
				       key, value);
				/*
				 * Can't be just a 'break', as perf_config_set__for_each_entry()
				 * expands to two nested for() loops.
				 */
				goto out;
			}
		}
	}
out:
	return ret;
}

int perf_config(config_fn_t fn, void *data)
{
	struct perf_config_set *set;

	/* Not held across the dispatch: a callback can call perf_config() again. */
	mutex_lock(&config_set_mutex);
	if (perf_config__init()) {
		mutex_unlock(&config_set_mutex);
		return -1;
	}
	set = config_set;
	mutex_unlock(&config_set_mutex);

	return perf_config_set(set, fn, data);
}

void perf_config__exit(void)
{
	mutex_lock(&config_set_mutex);
	perf_config_set__delete(config_set);
	config_set = NULL;
	mutex_unlock(&config_set_mutex);
}

int perf_config_set__write(struct perf_config_set *set,
			   const char *file_name, bool system_config)
{
	struct perf_config_section *section = NULL;
	struct perf_config_item *item = NULL;
	int ret = 0;
	FILE *fp;

	mutex_lock(&config_mutex);
	fp = fopen(file_name, "w");
	if (!fp) {
		mutex_unlock(&config_mutex);
		return -1;
	}

	if (fprintf(fp, "# this file is auto-generated.\n") < 0)
		ret = -1;

	/* overwrite configvariables */
	perf_config_sections__for_each_entry(&set->sections, section) {
		if (!system_config && section->from_system_config)
			continue;
		if (fprintf(fp, "[%s]\n", section->name) < 0)
			ret = -1;

		perf_config_items__for_each_entry(&section->items, item) {
			if (!system_config && item->from_system_config)
				continue;
			if (item->value &&
			    fprintf(fp, "\t%s = %s\n", item->name, item->value) < 0)
				ret = -1;
		}
	}
	if (fclose(fp) != 0)
		ret = -1;
	mutex_unlock(&config_mutex);

	return ret;
}

/*
 * Set @var=@value in the config file perf is using: ~/.perfconfig or the
 * file named by PERF_CONFIG.  Same rewrite 'perf config' does, comments
 * are not preserved.
 */
int perf_config__set_variable(const char *var, const char *value)
{
	const char *config_filename;
	bool system_config;
	struct perf_config_set *set = NULL;
	int ret = -1;

	mutex_lock(&config_update_mutex);

	/* Static: the parser publishes it as config_file_name. */
	{
		static char path[PATH_MAX];
		char *user_config = mkpath(path, sizeof(path), "%s/.perfconfig", getenv("HOME"));

		config_filename = config_exclusive_filename ?: user_config;
	}

	/* Rewriting the system wide file keeps its entries, or it is truncated. */
	system_config = strcmp(config_filename, perf_etc_perfconfig()) == 0;

	set = perf_config_set__new();
	if (!set)
		goto out_err;

	if (perf_config_set__collect(set, config_filename, var, value) < 0) {
		pr_err("Failed to add '%s=%s'\n", var, value);
		goto out_err;
	}

	if (perf_config_set__write(set, config_filename, system_config) < 0) {
		pr_err("Failed to set the configs on %s\n", config_filename);
		goto out_err;
	}

	ret = 0;
out_err:
	perf_config_set__delete(set);
	mutex_unlock(&config_update_mutex);
	return ret;
}

static void perf_config_item__delete(struct perf_config_item *item)
{
	zfree(&item->name);
	zfree(&item->value);
	free(item);
}

static void perf_config_section__purge(struct perf_config_section *section)
{
	struct perf_config_item *item, *tmp;

	list_for_each_entry_safe(item, tmp, &section->items, node) {
		list_del_init(&item->node);
		perf_config_item__delete(item);
	}
}

static void perf_config_section__delete(struct perf_config_section *section)
{
	perf_config_section__purge(section);
	zfree(&section->name);
	free(section);
}

static void perf_config_set__purge(struct perf_config_set *set)
{
	struct perf_config_section *section, *tmp;

	list_for_each_entry_safe(section, tmp, &set->sections, node) {
		list_del_init(&section->node);
		perf_config_section__delete(section);
	}
}

void perf_config_set__delete(struct perf_config_set *set)
{
	if (set == NULL)
		return;

	perf_config_set__purge(set);
	free(set);
}

/*
 * Call this to report error for your variable that should not
 * get a boolean value (i.e. "[my] var" means "true").
 */
int config_error_nonbool(const char *var)
{
	pr_err("Missing value for '%s'", var);
	return -1;
}

void set_buildid_dir(const char *dir)
{
	if (dir)
		scnprintf(buildid_dir, MAXPATHLEN, "%s", dir);

	/* default to $HOME/.debug */
	if (buildid_dir[0] == '\0') {
		char *home = getenv("HOME");

		if (home) {
			snprintf(buildid_dir, MAXPATHLEN, "%s/%s",
				 home, DEBUG_CACHE_DIR);
		} else {
			strncpy(buildid_dir, DEBUG_CACHE_DIR, MAXPATHLEN-1);
		}
		buildid_dir[MAXPATHLEN-1] = '\0';
	}
	/* for communicating with external commands */
	setenv("PERF_BUILDID_DIR", buildid_dir, 1);
}

struct perf_config_scan_data {
	const char *name;
	const char *fmt;
	const char *value;
	va_list args;
	int ret;
};

static int perf_config_scan_cb(const char *var, const char *value, void *data)
{
	struct perf_config_scan_data *d = data;

	if (!strcmp(var, d->name))
		d->ret = vsscanf(value, d->fmt, d->args);

	return 0;
}

int perf_config_scan(const char *name, const char *fmt, ...)
{
	struct perf_config_scan_data d = {
		.name = name,
		.fmt = fmt,
	};

	va_start(d.args, fmt);
	perf_config(perf_config_scan_cb, &d);
	va_end(d.args);

	return d.ret;
}

static int perf_config_get_cb(const char *var, const char *value, void *data)
{
	struct perf_config_scan_data *d = data;

	if (!strcmp(var, d->name))
		d->value = value;

	return 0;
}

const char *perf_config_get(const char *name)
{
	struct perf_config_scan_data d = {
		.name = name,
		.value = NULL,
	};

	perf_config(perf_config_get_cb, &d);
	return d.value;
}
