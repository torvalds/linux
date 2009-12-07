/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 * Copyright (C) Johannes Schindelin, 2005
 *
 */
#include "util.h"
#include "cache.h"
#include "exec_cmd.h"

#define MAXNAME (256)

static FILE *config_file;
static const char *config_file_name;
static int config_linenr;
static int config_file_eof;

const char *config_exclusive_filename = NULL;

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
	return isalnum(c) || c == '-';
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
		name[len++] = tolower(c);
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
		int c = get_next_char();
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
		if (get_value(fn, data, var, baselen+1) < 0)
			break;
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

int perf_parse_ulong(const char *value, unsigned long *ret)
{
	if (value && *value) {
		char *end;
		unsigned long val = strtoul(value, &end, 0);
		if (!parse_unit_factor(end, &val))
			return 0;
		*ret = val;
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

int perf_config_int(const char *name, const char *value)
{
	long ret = 0;
	if (!perf_parse_long(value, &ret))
		die_bad_config(name);
	return ret;
}

unsigned long perf_config_ulong(const char *name, const char *value)
{
	unsigned long ret;
	if (!perf_parse_ulong(value, &ret))
		die_bad_config(name);
	return ret;
}

int perf_config_bool_or_int(const char *name, const char *value, int *is_bool)
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

int perf_config_string(const char **dest, const char *var, const char *value)
{
	if (!value)
		return config_error_nonbool(var);
	*dest = strdup(value);
	return 0;
}

static int perf_default_core_config(const char *var __used, const char *value __used)
{
	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

int perf_default_config(const char *var, const char *value, void *dummy __used)
{
	if (!prefixcmp(var, "core."))
		return perf_default_core_config(var, value);

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

int perf_config_from_file(config_fn_t fn, const char *filename, void *data)
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

int perf_config_system(void)
{
	return !perf_env_bool("PERF_CONFIG_NOSYSTEM", 0);
}

int perf_config_global(void)
{
	return !perf_env_bool("PERF_CONFIG_NOGLOBAL", 0);
}

int perf_config(config_fn_t fn, void *data)
{
	int ret = 0, found = 0;
	char *repo_config = NULL;
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
		if (!access(user_config, R_OK)) {
			ret += perf_config_from_file(fn, user_config, data);
			found += 1;
		}
		free(user_config);
	}

	repo_config = perf_pathdup("config");
	if (!access(repo_config, R_OK)) {
		ret += perf_config_from_file(fn, repo_config, data);
		found += 1;
	}
	free(repo_config);
	if (found == 0)
		return -1;
	return ret;
}

/*
 * Find all the stuff for perf_config_set() below.
 */

#define MAX_MATCHES 512

static struct {
	int baselen;
	char* key;
	int do_not_match;
	regex_t* value_regex;
	int multi_replace;
	size_t offset[MAX_MATCHES];
	enum { START, SECTION_SEEN, SECTION_END_SEEN, KEY_SEEN } state;
	int seen;
} store;

static int matches(const char* key, const char* value)
{
	return !strcmp(key, store.key) &&
		(store.value_regex == NULL ||
		 (store.do_not_match ^
		  !regexec(store.value_regex, value, 0, NULL, 0)));
}

static int store_aux(const char* key, const char* value, void *cb __used)
{
	int section_len;
	const char *ep;

	switch (store.state) {
	case KEY_SEEN:
		if (matches(key, value)) {
			if (store.seen == 1 && store.multi_replace == 0) {
				warning("%s has multiple values", key);
			} else if (store.seen >= MAX_MATCHES) {
				error("too many matches for %s", key);
				return 1;
			}

			store.offset[store.seen] = ftell(config_file);
			store.seen++;
		}
		break;
	case SECTION_SEEN:
		/*
		 * What we are looking for is in store.key (both
		 * section and var), and its section part is baselen
		 * long.  We found key (again, both section and var).
		 * We would want to know if this key is in the same
		 * section as what we are looking for.  We already
		 * know we are in the same section as what should
		 * hold store.key.
		 */
		ep = strrchr(key, '.');
		section_len = ep - key;

		if ((section_len != store.baselen) ||
		    memcmp(key, store.key, section_len+1)) {
			store.state = SECTION_END_SEEN;
			break;
		}

		/*
		 * Do not increment matches: this is no match, but we
		 * just made sure we are in the desired section.
		 */
		store.offset[store.seen] = ftell(config_file);
		/* fallthru */
	case SECTION_END_SEEN:
	case START:
		if (matches(key, value)) {
			store.offset[store.seen] = ftell(config_file);
			store.state = KEY_SEEN;
			store.seen++;
		} else {
			if (strrchr(key, '.') - key == store.baselen &&
			      !strncmp(key, store.key, store.baselen)) {
					store.state = SECTION_SEEN;
					store.offset[store.seen] = ftell(config_file);
			}
		}
	default:
		break;
	}
	return 0;
}

static int store_write_section(int fd, const char* key)
{
	const char *dot;
	int i, success;
	struct strbuf sb = STRBUF_INIT;

	dot = memchr(key, '.', store.baselen);
	if (dot) {
		strbuf_addf(&sb, "[%.*s \"", (int)(dot - key), key);
		for (i = dot - key + 1; i < store.baselen; i++) {
			if (key[i] == '"' || key[i] == '\\')
				strbuf_addch(&sb, '\\');
			strbuf_addch(&sb, key[i]);
		}
		strbuf_addstr(&sb, "\"]\n");
	} else {
		strbuf_addf(&sb, "[%.*s]\n", store.baselen, key);
	}

	success = (write_in_full(fd, sb.buf, sb.len) == (ssize_t)sb.len);
	strbuf_release(&sb);

	return success;
}

static int store_write_pair(int fd, const char* key, const char* value)
{
	int i, success;
	int length = strlen(key + store.baselen + 1);
	const char *quote = "";
	struct strbuf sb = STRBUF_INIT;

	/*
	 * Check to see if the value needs to be surrounded with a dq pair.
	 * Note that problematic characters are always backslash-quoted; this
	 * check is about not losing leading or trailing SP and strings that
	 * follow beginning-of-comment characters (i.e. ';' and '#') by the
	 * configuration parser.
	 */
	if (value[0] == ' ')
		quote = "\"";
	for (i = 0; value[i]; i++)
		if (value[i] == ';' || value[i] == '#')
			quote = "\"";
	if (i && value[i - 1] == ' ')
		quote = "\"";

	strbuf_addf(&sb, "\t%.*s = %s",
		    length, key + store.baselen + 1, quote);

	for (i = 0; value[i]; i++)
		switch (value[i]) {
		case '\n':
			strbuf_addstr(&sb, "\\n");
			break;
		case '\t':
			strbuf_addstr(&sb, "\\t");
			break;
		case '"':
		case '\\':
			strbuf_addch(&sb, '\\');
		default:
			strbuf_addch(&sb, value[i]);
			break;
		}
	strbuf_addf(&sb, "%s\n", quote);

	success = (write_in_full(fd, sb.buf, sb.len) == (ssize_t)sb.len);
	strbuf_release(&sb);

	return success;
}

static ssize_t find_beginning_of_line(const char* contents, size_t size,
	size_t offset_, int* found_bracket)
{
	size_t equal_offset = size, bracket_offset = size;
	ssize_t offset;

contline:
	for (offset = offset_-2; offset > 0
			&& contents[offset] != '\n'; offset--)
		switch (contents[offset]) {
			case '=': equal_offset = offset; break;
			case ']': bracket_offset = offset; break;
			default: break;
		}
	if (offset > 0 && contents[offset-1] == '\\') {
		offset_ = offset;
		goto contline;
	}
	if (bracket_offset < equal_offset) {
		*found_bracket = 1;
		offset = bracket_offset+1;
	} else
		offset++;

	return offset;
}

int perf_config_set(const char* key, const char* value)
{
	return perf_config_set_multivar(key, value, NULL, 0);
}

/*
 * If value==NULL, unset in (remove from) config,
 * if value_regex!=NULL, disregard key/value pairs where value does not match.
 * if multi_replace==0, nothing, or only one matching key/value is replaced,
 *     else all matching key/values (regardless how many) are removed,
 *     before the new pair is written.
 *
 * Returns 0 on success.
 *
 * This function does this:
 *
 * - it locks the config file by creating ".perf/config.lock"
 *
 * - it then parses the config using store_aux() as validator to find
 *   the position on the key/value pair to replace. If it is to be unset,
 *   it must be found exactly once.
 *
 * - the config file is mmap()ed and the part before the match (if any) is
 *   written to the lock file, then the changed part and the rest.
 *
 * - the config file is removed and the lock file rename()d to it.
 *
 */
int perf_config_set_multivar(const char* key, const char* value,
	const char* value_regex, int multi_replace)
{
	int i, dot;
	int fd = -1, in_fd;
	int ret = 0;
	char* config_filename;
	const char* last_dot = strrchr(key, '.');

	if (config_exclusive_filename)
		config_filename = strdup(config_exclusive_filename);
	else
		config_filename = perf_pathdup("config");

	/*
	 * Since "key" actually contains the section name and the real
	 * key name separated by a dot, we have to know where the dot is.
	 */

	if (last_dot == NULL) {
		error("key does not contain a section: %s", key);
		ret = 2;
		goto out_free;
	}
	store.baselen = last_dot - key;

	store.multi_replace = multi_replace;

	/*
	 * Validate the key and while at it, lower case it for matching.
	 */
	store.key = malloc(strlen(key) + 1);
	dot = 0;
	for (i = 0; key[i]; i++) {
		unsigned char c = key[i];
		if (c == '.')
			dot = 1;
		/* Leave the extended basename untouched.. */
		if (!dot || i > store.baselen) {
			if (!iskeychar(c) || (i == store.baselen+1 && !isalpha(c))) {
				error("invalid key: %s", key);
				free(store.key);
				ret = 1;
				goto out_free;
			}
			c = tolower(c);
		} else if (c == '\n') {
			error("invalid key (newline): %s", key);
			free(store.key);
			ret = 1;
			goto out_free;
		}
		store.key[i] = c;
	}
	store.key[i] = 0;

	/*
	 * If .perf/config does not exist yet, write a minimal version.
	 */
	in_fd = open(config_filename, O_RDONLY);
	if ( in_fd < 0 ) {
		free(store.key);

		if ( ENOENT != errno ) {
			error("opening %s: %s", config_filename,
			      strerror(errno));
			ret = 3; /* same as "invalid config file" */
			goto out_free;
		}
		/* if nothing to unset, error out */
		if (value == NULL) {
			ret = 5;
			goto out_free;
		}

		store.key = (char*)key;
		if (!store_write_section(fd, key) ||
		    !store_write_pair(fd, key, value))
			goto write_err_out;
	} else {
		struct stat st;
		char *contents;
		ssize_t contents_sz, copy_begin, copy_end;
		int new_line = 0;

		if (value_regex == NULL)
			store.value_regex = NULL;
		else {
			if (value_regex[0] == '!') {
				store.do_not_match = 1;
				value_regex++;
			} else
				store.do_not_match = 0;

			store.value_regex = (regex_t*)malloc(sizeof(regex_t));
			if (regcomp(store.value_regex, value_regex,
					REG_EXTENDED)) {
				error("invalid pattern: %s", value_regex);
				free(store.value_regex);
				ret = 6;
				goto out_free;
			}
		}

		store.offset[0] = 0;
		store.state = START;
		store.seen = 0;

		/*
		 * After this, store.offset will contain the *end* offset
		 * of the last match, or remain at 0 if no match was found.
		 * As a side effect, we make sure to transform only a valid
		 * existing config file.
		 */
		if (perf_config_from_file(store_aux, config_filename, NULL)) {
			error("invalid config file %s", config_filename);
			free(store.key);
			if (store.value_regex != NULL) {
				regfree(store.value_regex);
				free(store.value_regex);
			}
			ret = 3;
			goto out_free;
		}

		free(store.key);
		if (store.value_regex != NULL) {
			regfree(store.value_regex);
			free(store.value_regex);
		}

		/* if nothing to unset, or too many matches, error out */
		if ((store.seen == 0 && value == NULL) ||
				(store.seen > 1 && multi_replace == 0)) {
			ret = 5;
			goto out_free;
		}

		fstat(in_fd, &st);
		contents_sz = xsize_t(st.st_size);
		contents = mmap(NULL, contents_sz, PROT_READ,
			MAP_PRIVATE, in_fd, 0);
		close(in_fd);

		if (store.seen == 0)
			store.seen = 1;

		for (i = 0, copy_begin = 0; i < store.seen; i++) {
			if (store.offset[i] == 0) {
				store.offset[i] = copy_end = contents_sz;
			} else if (store.state != KEY_SEEN) {
				copy_end = store.offset[i];
			} else
				copy_end = find_beginning_of_line(
					contents, contents_sz,
					store.offset[i]-2, &new_line);

			if (copy_end > 0 && contents[copy_end-1] != '\n')
				new_line = 1;

			/* write the first part of the config */
			if (copy_end > copy_begin) {
				if (write_in_full(fd, contents + copy_begin,
						  copy_end - copy_begin) <
				    copy_end - copy_begin)
					goto write_err_out;
				if (new_line &&
				    write_in_full(fd, "\n", 1) != 1)
					goto write_err_out;
			}
			copy_begin = store.offset[i];
		}

		/* write the pair (value == NULL means unset) */
		if (value != NULL) {
			if (store.state == START) {
				if (!store_write_section(fd, key))
					goto write_err_out;
			}
			if (!store_write_pair(fd, key, value))
				goto write_err_out;
		}

		/* write the rest of the config */
		if (copy_begin < contents_sz)
			if (write_in_full(fd, contents + copy_begin,
					  contents_sz - copy_begin) <
			    contents_sz - copy_begin)
				goto write_err_out;

		munmap(contents, contents_sz);
	}

	ret = 0;

out_free:
	free(config_filename);
	return ret;

write_err_out:
	goto out_free;

}

/*
 * Call this to report error for your variable that should not
 * get a boolean value (i.e. "[my] var" means "true").
 */
int config_error_nonbool(const char *var)
{
	return error("Missing value for '%s'", var);
}
