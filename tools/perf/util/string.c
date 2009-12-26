#include "string.h"
#include "util.h"

static int hex(char ch)
{
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	if ((ch >= 'A') && (ch <= 'F'))
		return ch - 'A' + 10;
	return -1;
}

/*
 * While we find nice hex chars, build a long_val.
 * Return number of chars processed.
 */
int hex2u64(const char *ptr, u64 *long_val)
{
	const char *p = ptr;
	*long_val = 0;

	while (*p) {
		const int hex_val = hex(*p);

		if (hex_val < 0)
			break;

		*long_val = (*long_val << 4) | hex_val;
		p++;
	}

	return p - ptr;
}

char *strxfrchar(char *s, char from, char to)
{
	char *p = s;

	while ((p = strchr(p, from)) != NULL)
		*p++ = to;

	return s;
}

#define K 1024LL
/*
 * perf_atoll()
 * Parse (\d+)(b|B|kb|KB|mb|MB|gb|GB|tb|TB) (e.g. "256MB")
 * and return its numeric value
 */
s64 perf_atoll(const char *str)
{
	unsigned int i;
	s64 length = -1, unit = 1;

	if (!isdigit(str[0]))
		goto out_err;

	for (i = 1; i < strlen(str); i++) {
		switch (str[i]) {
		case 'B':
		case 'b':
			break;
		case 'K':
			if (str[i + 1] != 'B')
				goto out_err;
			else
				goto kilo;
		case 'k':
			if (str[i + 1] != 'b')
				goto out_err;
kilo:
			unit = K;
			break;
		case 'M':
			if (str[i + 1] != 'B')
				goto out_err;
			else
				goto mega;
		case 'm':
			if (str[i + 1] != 'b')
				goto out_err;
mega:
			unit = K * K;
			break;
		case 'G':
			if (str[i + 1] != 'B')
				goto out_err;
			else
				goto giga;
		case 'g':
			if (str[i + 1] != 'b')
				goto out_err;
giga:
			unit = K * K * K;
			break;
		case 'T':
			if (str[i + 1] != 'B')
				goto out_err;
			else
				goto tera;
		case 't':
			if (str[i + 1] != 'b')
				goto out_err;
tera:
			unit = K * K * K * K;
			break;
		case '\0':	/* only specified figures */
			unit = 1;
			break;
		default:
			if (!isdigit(str[i]))
				goto out_err;
			break;
		}
	}

	length = atoll(str) * unit;
	goto out;

out_err:
	length = -1;
out:
	return length;
}

/*
 * Helper function for splitting a string into an argv-like array.
 * originaly copied from lib/argv_split.c
 */
static const char *skip_sep(const char *cp)
{
	while (*cp && isspace(*cp))
		cp++;

	return cp;
}

static const char *skip_arg(const char *cp)
{
	while (*cp && !isspace(*cp))
		cp++;

	return cp;
}

static int count_argc(const char *str)
{
	int count = 0;

	while (*str) {
		str = skip_sep(str);
		if (*str) {
			count++;
			str = skip_arg(str);
		}
	}

	return count;
}

/**
 * argv_free - free an argv
 * @argv - the argument vector to be freed
 *
 * Frees an argv and the strings it points to.
 */
void argv_free(char **argv)
{
	char **p;
	for (p = argv; *p; p++)
		free(*p);

	free(argv);
}

/**
 * argv_split - split a string at whitespace, returning an argv
 * @str: the string to be split
 * @argcp: returned argument count
 *
 * Returns an array of pointers to strings which are split out from
 * @str.  This is performed by strictly splitting on white-space; no
 * quote processing is performed.  Multiple whitespace characters are
 * considered to be a single argument separator.  The returned array
 * is always NULL-terminated.  Returns NULL on memory allocation
 * failure.
 */
char **argv_split(const char *str, int *argcp)
{
	int argc = count_argc(str);
	char **argv = zalloc(sizeof(*argv) * (argc+1));
	char **argvp;

	if (argv == NULL)
		goto out;

	if (argcp)
		*argcp = argc;

	argvp = argv;

	while (*str) {
		str = skip_sep(str);

		if (*str) {
			const char *p = str;
			char *t;

			str = skip_arg(str);

			t = strndup(p, str-p);
			if (t == NULL)
				goto fail;
			*argvp++ = t;
		}
	}
	*argvp = NULL;

out:
	return argv;

fail:
	argv_free(argv);
	return NULL;
}

/* Glob expression pattern matching */
bool strglobmatch(const char *str, const char *pat)
{
	while (*str && *pat && *pat != '*') {
		if (*pat == '?') {
			str++;
			pat++;
		} else
			if (*str++ != *pat++)
				return false;
	}
	/* Check wild card */
	if (*pat == '*') {
		while (*pat == '*')
			pat++;
		if (!*pat)	/* Tail wild card matches all */
			return true;
		while (*str)
			if (strglobmatch(str++, pat))
				return true;
	}
	return !*str && !*pat;
}

