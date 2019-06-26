// SPDX-License-Identifier: GPL-2.0
#include "string2.h"
#include <linux/kernel.h>
#include <linux/string.h>
#include <stdlib.h>

#include <linux/ctype.h>

const char *graph_dotted_line =
	"---------------------------------------------------------------------"
	"---------------------------------------------------------------------"
	"---------------------------------------------------------------------";
const char *dots =
	"....................................................................."
	"....................................................................."
	".....................................................................";

#define K 1024LL
/*
 * perf_atoll()
 * Parse (\d+)(b|B|kb|KB|mb|MB|gb|GB|tb|TB) (e.g. "256MB")
 * and return its numeric value
 */
s64 perf_atoll(const char *str)
{
	s64 length;
	char *p;
	char c;

	if (!isdigit(str[0]))
		goto out_err;

	length = strtoll(str, &p, 10);
	switch (c = *p++) {
		case 'b': case 'B':
			if (*p)
				goto out_err;

			__fallthrough;
		case '\0':
			return length;
		default:
			goto out_err;
		/* two-letter suffices */
		case 'k': case 'K':
			length <<= 10;
			break;
		case 'm': case 'M':
			length <<= 20;
			break;
		case 'g': case 'G':
			length <<= 30;
			break;
		case 't': case 'T':
			length <<= 40;
			break;
	}
	/* we want the cases to match */
	if (islower(c)) {
		if (strcmp(p, "b") != 0)
			goto out_err;
	} else {
		if (strcmp(p, "B") != 0)
			goto out_err;
	}
	return length;

out_err:
	return -1;
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
		str = skip_spaces(str);
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
	for (p = argv; *p; p++) {
		free(*p);
		*p = NULL;
	}

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
	char **argv = calloc(argc + 1, sizeof(*argv));
	char **argvp;

	if (argv == NULL)
		goto out;

	if (argcp)
		*argcp = argc;

	argvp = argv;

	while (*str) {
		str = skip_spaces(str);

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
static bool __match_glob(const char *str, const char *pat, bool ignore_space,
			bool case_ins)
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
		if (case_ins) {
			if (tolower(*str) != tolower(*pat))
				return false;
		} else if (*str != *pat)
			return false;
		str++;
		pat++;
	}
	/* Check wild card */
	if (*pat == '*') {
		while (*pat == '*')
			pat++;
		if (!*pat)	/* Tail wild card matches all */
			return true;
		while (*str)
			if (__match_glob(str++, pat, ignore_space, case_ins))
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
	return __match_glob(str, pat, false, false);
}

bool strglobmatch_nocase(const char *str, const char *pat)
{
	return __match_glob(str, pat, false, true);
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
	return __match_glob(str, pat, true, false);
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
 * strxfrchar - Locate and replace character in @s
 * @s:    The string to be searched/changed.
 * @from: Source character to be replaced.
 * @to:   Destination character.
 *
 * Return pointer to the changed string.
 */
char *strxfrchar(char *s, char from, char to)
{
	char *p = s;

	while ((p = strchr(p, from)) != NULL)
		*p++ = to;

	return s;
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

char *asprintf_expr_inout_ints(const char *var, bool in, size_t nints, int *ints)
{
	/*
	 * FIXME: replace this with an expression using log10() when we
	 * find a suitable implementation, maybe the one in the dvb drivers...
	 *
	 * "%s == %d || " = log10(MAXINT) * 2 + 8 chars for the operators
	 */
	size_t size = nints * 28 + 1; /* \0 */
	size_t i, printed = 0;
	char *expr = malloc(size);

	if (expr) {
		const char *or_and = "||", *eq_neq = "==";
		char *e = expr;

		if (!in) {
			or_and = "&&";
			eq_neq = "!=";
		}

		for (i = 0; i < nints; ++i) {
			if (printed == size)
				goto out_err_overflow;

			if (i > 0)
				printed += scnprintf(e + printed, size - printed, " %s ", or_and);
			printed += scnprintf(e + printed, size - printed,
					     "%s %s %d", var, eq_neq, ints[i]);
		}
	}

	return expr;

out_err_overflow:
	free(expr);
	return NULL;
}

/* Like strpbrk(), but not break if it is right after a backslash (escaped) */
char *strpbrk_esc(char *str, const char *stopset)
{
	char *ptr;

	do {
		ptr = strpbrk(str, stopset);
		if (ptr == str ||
		    (ptr == str + 1 && *(ptr - 1) != '\\'))
			break;
		str = ptr + 1;
	} while (ptr && *(ptr - 1) == '\\' && *(ptr - 2) != '\\');

	return ptr;
}

/* Like strdup, but do not copy a single backslash */
char *strdup_esc(const char *str)
{
	char *s, *d, *p, *ret = strdup(str);

	if (!ret)
		return NULL;

	d = strchr(ret, '\\');
	if (!d)
		return ret;

	s = d + 1;
	do {
		if (*s == '\0') {
			*d = '\0';
			break;
		}
		p = strchr(s + 1, '\\');
		if (p) {
			memmove(d, s, p - s);
			d += p - s;
			s = p + 1;
		} else
			memmove(d, s, strlen(s) + 1);
	} while (p);

	return ret;
}
