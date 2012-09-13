#include "util.h"
#include "linux/string.h"

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
 * originally copied from lib/argv_split.c
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

/* Character class matching */
static bool __match_charclass(const char *pat, char c, const char **npat)
{
	bool complement = false, ret = true;

	if (*pat == '!') {
		complement = true;
		pat++;
	}
	if (*pat++ == c)	/* First character is special */
		goto end;

	while (*pat && *pat != ']') {	/* Matching */
		if (*pat == '-' && *(pat + 1) != ']') {	/* Range */
			if (*(pat - 1) <= c && c <= *(pat + 1))
				goto end;
			if (*(pat - 1) > *(pat + 1))
				goto error;
			pat += 2;
		} else if (*pat++ == c)
			goto end;
	}
	if (!*pat)
		goto error;
	ret = false;

end:
	while (*pat && *pat != ']')	/* Searching closing */
		pat++;
	if (!*pat)
		goto error;
	*npat = pat + 1;
	return complement ? !ret : ret;

error:
	return false;
}

/* Glob/lazy pattern matching */
static bool __match_glob(const char *str, const char *pat, bool ignore_space)
{
	while (*str && *pat && *pat != '*') {
		if (ignore_space) {
			/* Ignore spaces for lazy matching */
			if (isspace(*str)) {
				str++;
				continue;
			}
			if (isspace(*pat)) {
				pat++;
				continue;
			}
		}
		if (*pat == '?') {	/* Matches any single character */
			str++;
			pat++;
			continue;
		} else if (*pat == '[')	/* Character classes/Ranges */
			if (__match_charclass(pat + 1, *str, &pat)) {
				str++;
				continue;
			} else
				return false;
		else if (*pat == '\\') /* Escaped char match as normal char */
			pat++;
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
			if (__match_glob(str++, pat, ignore_space))
				return true;
	}
	return !*str && !*pat;
}

/**
 * strglobmatch - glob expression pattern matching
 * @str: the target string to match
 * @pat: the pattern string to match
 *
 * This returns true if the @str matches @pat. @pat can includes wildcards
 * ('*','?') and character classes ([CHARS], complementation and ranges are
 * also supported). Also, this supports escape character ('\') to use special
 * characters as normal character.
 *
 * Note: if @pat syntax is broken, this always returns false.
 */
bool strglobmatch(const char *str, const char *pat)
{
	return __match_glob(str, pat, false);
}

/**
 * strlazymatch - matching pattern strings lazily with glob pattern
 * @str: the target string to match
 * @pat: the pattern string to match
 *
 * This is similar to strglobmatch, except this ignores spaces in
 * the target string.
 */
bool strlazymatch(const char *str, const char *pat)
{
	return __match_glob(str, pat, true);
}

/**
 * strtailcmp - Compare the tail of two strings
 * @s1: 1st string to be compared
 * @s2: 2nd string to be compared
 *
 * Return 0 if whole of either string is same as another's tail part.
 */
int strtailcmp(const char *s1, const char *s2)
{
	int i1 = strlen(s1);
	int i2 = strlen(s2);
	while (--i1 >= 0 && --i2 >= 0) {
		if (s1[i1] != s2[i2])
			return s1[i1] - s2[i2];
	}
	return 0;
}

/**
 * rtrim - Removes trailing whitespace from @s.
 * @s: The string to be stripped.
 *
 * Note that the first trailing whitespace is replaced with a %NUL-terminator
 * in the given string @s. Returns @s.
 */
char *rtrim(char *s)
{
	size_t size = strlen(s);
	char *end;

	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace(*end))
		end--;
	*(end + 1) = '\0';

	return s;
}

/**
 * memdup - duplicate region of memory
 * @src: memory region to duplicate
 * @len: memory region length
 */
void *memdup(const void *src, size_t len)
{
	void *p;

	p = malloc(len);
	if (p)
		memcpy(p, src, len);

	return p;
}
