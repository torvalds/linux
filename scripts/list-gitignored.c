// SPDX-License-Identifier: GPL-2.0-only
//
// Traverse the source tree, parsing all .gitignore files, and print file paths
// that are ignored by git.
// The output is suitable to the --exclude-from option of tar.
// This is useful until the --exclude-vcs-ignores option gets working correctly.
//
// Copyright (C) 2023 Masahiro Yamada <masahiroy@kernel.org>
//                      (a lot of code imported from GIT)

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Imported from commit 23c56f7bd5f1667f8b793d796bf30e39545920f6 in GIT
//
//---------------------------(IMPORT FROM GIT BEGIN)---------------------------

// Copied from environment.c

static bool ignore_case;

// Copied from git-compat-util.h

/* Sane ctype - no locale, and works with signed chars */
#undef isascii
#undef isspace
#undef isdigit
#undef isalpha
#undef isalnum
#undef isprint
#undef islower
#undef isupper
#undef tolower
#undef toupper
#undef iscntrl
#undef ispunct
#undef isxdigit

static const unsigned char sane_ctype[256];
#define GIT_SPACE 0x01
#define GIT_DIGIT 0x02
#define GIT_ALPHA 0x04
#define GIT_GLOB_SPECIAL 0x08
#define GIT_REGEX_SPECIAL 0x10
#define GIT_PATHSPEC_MAGIC 0x20
#define GIT_CNTRL 0x40
#define GIT_PUNCT 0x80
#define sane_istest(x,mask) ((sane_ctype[(unsigned char)(x)] & (mask)) != 0)
#define isascii(x) (((x) & ~0x7f) == 0)
#define isspace(x) sane_istest(x,GIT_SPACE)
#define isdigit(x) sane_istest(x,GIT_DIGIT)
#define isalpha(x) sane_istest(x,GIT_ALPHA)
#define isalnum(x) sane_istest(x,GIT_ALPHA | GIT_DIGIT)
#define isprint(x) ((x) >= 0x20 && (x) <= 0x7e)
#define islower(x) sane_iscase(x, 1)
#define isupper(x) sane_iscase(x, 0)
#define is_glob_special(x) sane_istest(x,GIT_GLOB_SPECIAL)
#define iscntrl(x) (sane_istest(x,GIT_CNTRL))
#define ispunct(x) sane_istest(x, GIT_PUNCT | GIT_REGEX_SPECIAL | \
		GIT_GLOB_SPECIAL | GIT_PATHSPEC_MAGIC)
#define isxdigit(x) (hexval_table[(unsigned char)(x)] != -1)
#define tolower(x) sane_case((unsigned char)(x), 0x20)
#define toupper(x) sane_case((unsigned char)(x), 0)

static inline int sane_case(int x, int high)
{
	if (sane_istest(x, GIT_ALPHA))
		x = (x & ~0x20) | high;
	return x;
}

static inline int sane_iscase(int x, int is_lower)
{
	if (!sane_istest(x, GIT_ALPHA))
		return 0;

	if (is_lower)
		return (x & 0x20) != 0;
	else
		return (x & 0x20) == 0;
}

// Copied from ctype.c

enum {
	S = GIT_SPACE,
	A = GIT_ALPHA,
	D = GIT_DIGIT,
	G = GIT_GLOB_SPECIAL,	/* *, ?, [, \\ */
	R = GIT_REGEX_SPECIAL,	/* $, (, ), +, ., ^, {, | */
	P = GIT_PATHSPEC_MAGIC, /* other non-alnum, except for ] and } */
	X = GIT_CNTRL,
	U = GIT_PUNCT,
	Z = GIT_CNTRL | GIT_SPACE
};

static const unsigned char sane_ctype[256] = {
	X, X, X, X, X, X, X, X, X, Z, Z, X, X, Z, X, X,		/*   0.. 15 */
	X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,		/*  16.. 31 */
	S, P, P, P, R, P, P, P, R, R, G, R, P, P, R, P,		/*  32.. 47 */
	D, D, D, D, D, D, D, D, D, D, P, P, P, P, P, G,		/*  48.. 63 */
	P, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,		/*  64.. 79 */
	A, A, A, A, A, A, A, A, A, A, A, G, G, U, R, P,		/*  80.. 95 */
	P, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,		/*  96..111 */
	A, A, A, A, A, A, A, A, A, A, A, R, R, U, P, X,		/* 112..127 */
	/* Nothing in the 128.. range */
};

// Copied from hex.c

static const signed char hexval_table[256] = {
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 00-07 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 08-0f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 10-17 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 18-1f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 20-27 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 28-2f */
	  0,  1,  2,  3,  4,  5,  6,  7,		/* 30-37 */
	  8,  9, -1, -1, -1, -1, -1, -1,		/* 38-3f */
	 -1, 10, 11, 12, 13, 14, 15, -1,		/* 40-47 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 48-4f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 50-57 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 58-5f */
	 -1, 10, 11, 12, 13, 14, 15, -1,		/* 60-67 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 68-67 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 70-77 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 78-7f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 80-87 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 88-8f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 90-97 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 98-9f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* a0-a7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* a8-af */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* b0-b7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* b8-bf */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* c0-c7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* c8-cf */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* d0-d7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* d8-df */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* e0-e7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* e8-ef */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* f0-f7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* f8-ff */
};

// Copied from wildmatch.h

#define WM_CASEFOLD 1
#define WM_PATHNAME 2

#define WM_NOMATCH 1
#define WM_MATCH 0
#define WM_ABORT_ALL -1
#define WM_ABORT_TO_STARSTAR -2

// Copied from wildmatch.c

typedef unsigned char uchar;

// local modification: remove NEGATE_CLASS(2)

#define CC_EQ(class, len, litmatch) ((len) == sizeof (litmatch)-1 \
				    && *(class) == *(litmatch) \
				    && strncmp((char*)class, litmatch, len) == 0)

// local modification: simpilify macros
#define ISBLANK(c) ((c) == ' ' || (c) == '\t')
#define ISGRAPH(c) (isprint(c) && !isspace(c))
#define ISPRINT(c) isprint(c)
#define ISDIGIT(c) isdigit(c)
#define ISALNUM(c) isalnum(c)
#define ISALPHA(c) isalpha(c)
#define ISCNTRL(c) iscntrl(c)
#define ISLOWER(c) islower(c)
#define ISPUNCT(c) ispunct(c)
#define ISSPACE(c) isspace(c)
#define ISUPPER(c) isupper(c)
#define ISXDIGIT(c) isxdigit(c)

/* Match pattern "p" against "text" */
static int dowild(const uchar *p, const uchar *text, unsigned int flags)
{
	uchar p_ch;
	const uchar *pattern = p;

	for ( ; (p_ch = *p) != '\0'; text++, p++) {
		int matched, match_slash, negated;
		uchar t_ch, prev_ch;
		if ((t_ch = *text) == '\0' && p_ch != '*')
			return WM_ABORT_ALL;
		if ((flags & WM_CASEFOLD) && ISUPPER(t_ch))
			t_ch = tolower(t_ch);
		if ((flags & WM_CASEFOLD) && ISUPPER(p_ch))
			p_ch = tolower(p_ch);
		switch (p_ch) {
		case '\\':
			/* Literal match with following character.  Note that the test
			 * in "default" handles the p[1] == '\0' failure case. */
			p_ch = *++p;
			/* FALLTHROUGH */
		default:
			if (t_ch != p_ch)
				return WM_NOMATCH;
			continue;
		case '?':
			/* Match anything but '/'. */
			if ((flags & WM_PATHNAME) && t_ch == '/')
				return WM_NOMATCH;
			continue;
		case '*':
			if (*++p == '*') {
				const uchar *prev_p = p - 2;
				while (*++p == '*') {}
				if (!(flags & WM_PATHNAME))
					/* without WM_PATHNAME, '*' == '**' */
					match_slash = 1;
				else if ((prev_p < pattern || *prev_p == '/') &&
				    (*p == '\0' || *p == '/' ||
				     (p[0] == '\\' && p[1] == '/'))) {
					/*
					 * Assuming we already match 'foo/' and are at
					 * <star star slash>, just assume it matches
					 * nothing and go ahead match the rest of the
					 * pattern with the remaining string. This
					 * helps make foo/<*><*>/bar (<> because
					 * otherwise it breaks C comment syntax) match
					 * both foo/bar and foo/a/bar.
					 */
					if (p[0] == '/' &&
					    dowild(p + 1, text, flags) == WM_MATCH)
						return WM_MATCH;
					match_slash = 1;
				} else /* WM_PATHNAME is set */
					match_slash = 0;
			} else
				/* without WM_PATHNAME, '*' == '**' */
				match_slash = flags & WM_PATHNAME ? 0 : 1;
			if (*p == '\0') {
				/* Trailing "**" matches everything.  Trailing "*" matches
				 * only if there are no more slash characters. */
				if (!match_slash) {
					if (strchr((char *)text, '/'))
						return WM_NOMATCH;
				}
				return WM_MATCH;
			} else if (!match_slash && *p == '/') {
				/*
				 * _one_ asterisk followed by a slash
				 * with WM_PATHNAME matches the next
				 * directory
				 */
				const char *slash = strchr((char*)text, '/');
				if (!slash)
					return WM_NOMATCH;
				text = (const uchar*)slash;
				/* the slash is consumed by the top-level for loop */
				break;
			}
			while (1) {
				if (t_ch == '\0')
					break;
				/*
				 * Try to advance faster when an asterisk is
				 * followed by a literal. We know in this case
				 * that the string before the literal
				 * must belong to "*".
				 * If match_slash is false, do not look past
				 * the first slash as it cannot belong to '*'.
				 */
				if (!is_glob_special(*p)) {
					p_ch = *p;
					if ((flags & WM_CASEFOLD) && ISUPPER(p_ch))
						p_ch = tolower(p_ch);
					while ((t_ch = *text) != '\0' &&
					       (match_slash || t_ch != '/')) {
						if ((flags & WM_CASEFOLD) && ISUPPER(t_ch))
							t_ch = tolower(t_ch);
						if (t_ch == p_ch)
							break;
						text++;
					}
					if (t_ch != p_ch)
						return WM_NOMATCH;
				}
				if ((matched = dowild(p, text, flags)) != WM_NOMATCH) {
					if (!match_slash || matched != WM_ABORT_TO_STARSTAR)
						return matched;
				} else if (!match_slash && t_ch == '/')
					return WM_ABORT_TO_STARSTAR;
				t_ch = *++text;
			}
			return WM_ABORT_ALL;
		case '[':
			p_ch = *++p;
			if (p_ch == '^')
				p_ch = '!';
			/* Assign literal 1/0 because of "matched" comparison. */
			negated = p_ch == '!' ? 1 : 0;
			if (negated) {
				/* Inverted character class. */
				p_ch = *++p;
			}
			prev_ch = 0;
			matched = 0;
			do {
				if (!p_ch)
					return WM_ABORT_ALL;
				if (p_ch == '\\') {
					p_ch = *++p;
					if (!p_ch)
						return WM_ABORT_ALL;
					if (t_ch == p_ch)
						matched = 1;
				} else if (p_ch == '-' && prev_ch && p[1] && p[1] != ']') {
					p_ch = *++p;
					if (p_ch == '\\') {
						p_ch = *++p;
						if (!p_ch)
							return WM_ABORT_ALL;
					}
					if (t_ch <= p_ch && t_ch >= prev_ch)
						matched = 1;
					else if ((flags & WM_CASEFOLD) && ISLOWER(t_ch)) {
						uchar t_ch_upper = toupper(t_ch);
						if (t_ch_upper <= p_ch && t_ch_upper >= prev_ch)
							matched = 1;
					}
					p_ch = 0; /* This makes "prev_ch" get set to 0. */
				} else if (p_ch == '[' && p[1] == ':') {
					const uchar *s;
					int i;
					for (s = p += 2; (p_ch = *p) && p_ch != ']'; p++) {} /*SHARED ITERATOR*/
					if (!p_ch)
						return WM_ABORT_ALL;
					i = p - s - 1;
					if (i < 0 || p[-1] != ':') {
						/* Didn't find ":]", so treat like a normal set. */
						p = s - 2;
						p_ch = '[';
						if (t_ch == p_ch)
							matched = 1;
						continue;
					}
					if (CC_EQ(s,i, "alnum")) {
						if (ISALNUM(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "alpha")) {
						if (ISALPHA(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "blank")) {
						if (ISBLANK(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "cntrl")) {
						if (ISCNTRL(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "digit")) {
						if (ISDIGIT(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "graph")) {
						if (ISGRAPH(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "lower")) {
						if (ISLOWER(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "print")) {
						if (ISPRINT(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "punct")) {
						if (ISPUNCT(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "space")) {
						if (ISSPACE(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "upper")) {
						if (ISUPPER(t_ch))
							matched = 1;
						else if ((flags & WM_CASEFOLD) && ISLOWER(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "xdigit")) {
						if (ISXDIGIT(t_ch))
							matched = 1;
					} else /* malformed [:class:] string */
						return WM_ABORT_ALL;
					p_ch = 0; /* This makes "prev_ch" get set to 0. */
				} else if (t_ch == p_ch)
					matched = 1;
			} while (prev_ch = p_ch, (p_ch = *++p) != ']');
			if (matched == negated ||
			    ((flags & WM_PATHNAME) && t_ch == '/'))
				return WM_NOMATCH;
			continue;
		}
	}

	return *text ? WM_NOMATCH : WM_MATCH;
}

/* Match the "pattern" against the "text" string. */
static int wildmatch(const char *pattern, const char *text, unsigned int flags)
{
	// local modification: move WM_CASEFOLD here
	if (ignore_case)
		flags |= WM_CASEFOLD;

	return dowild((const uchar*)pattern, (const uchar*)text, flags);
}

// Copied from dir.h

#define PATTERN_FLAG_NODIR 1
#define PATTERN_FLAG_ENDSWITH 4
#define PATTERN_FLAG_MUSTBEDIR 8
#define PATTERN_FLAG_NEGATIVE 16

// Copied from dir.c

static int fspathncmp(const char *a, const char *b, size_t count)
{
	return ignore_case ? strncasecmp(a, b, count) : strncmp(a, b, count);
}

static int simple_length(const char *match)
{
	int len = -1;

	for (;;) {
		unsigned char c = *match++;
		len++;
		if (c == '\0' || is_glob_special(c))
			return len;
	}
}

static int no_wildcard(const char *string)
{
	return string[simple_length(string)] == '\0';
}

static void parse_path_pattern(const char **pattern,
			       int *patternlen,
			       unsigned *flags,
			       int *nowildcardlen)
{
	const char *p = *pattern;
	size_t i, len;

	*flags = 0;
	if (*p == '!') {
		*flags |= PATTERN_FLAG_NEGATIVE;
		p++;
	}
	len = strlen(p);
	if (len && p[len - 1] == '/') {
		len--;
		*flags |= PATTERN_FLAG_MUSTBEDIR;
	}
	for (i = 0; i < len; i++) {
		if (p[i] == '/')
			break;
	}
	if (i == len)
		*flags |= PATTERN_FLAG_NODIR;
	*nowildcardlen = simple_length(p);
	/*
	 * we should have excluded the trailing slash from 'p' too,
	 * but that's one more allocation. Instead just make sure
	 * nowildcardlen does not exceed real patternlen
	 */
	if (*nowildcardlen > len)
		*nowildcardlen = len;
	if (*p == '*' && no_wildcard(p + 1))
		*flags |= PATTERN_FLAG_ENDSWITH;
	*pattern = p;
	*patternlen = len;
}

static void trim_trailing_spaces(char *buf)
{
	char *p, *last_space = NULL;

	for (p = buf; *p; p++)
		switch (*p) {
		case ' ':
			if (!last_space)
				last_space = p;
			break;
		case '\\':
			p++;
			if (!*p)
				return;
			/* fallthrough */
		default:
			last_space = NULL;
		}

	if (last_space)
		*last_space = '\0';
}

static int match_basename(const char *basename, int basenamelen,
			  const char *pattern, int prefix, int patternlen,
			  unsigned flags)
{
	if (prefix == patternlen) {
		if (patternlen == basenamelen &&
		    !fspathncmp(pattern, basename, basenamelen))
			return 1;
	} else if (flags & PATTERN_FLAG_ENDSWITH) {
		/* "*literal" matching against "fooliteral" */
		if (patternlen - 1 <= basenamelen &&
		    !fspathncmp(pattern + 1,
				   basename + basenamelen - (patternlen - 1),
				   patternlen - 1))
			return 1;
	} else {
		// local modification: call wildmatch() directly
		if (!wildmatch(pattern, basename, flags))
			return 1;
	}
	return 0;
}

static int match_pathname(const char *pathname, int pathlen,
			  const char *base, int baselen,
			  const char *pattern, int prefix, int patternlen)
{
	// local modification: remove local variables

	/*
	 * match with FNM_PATHNAME; the pattern has base implicitly
	 * in front of it.
	 */
	if (*pattern == '/') {
		pattern++;
		patternlen--;
		prefix--;
	}

	/*
	 * baselen does not count the trailing slash. base[] may or
	 * may not end with a trailing slash though.
	 */
	if (pathlen < baselen + 1 ||
	    (baselen && pathname[baselen] != '/') ||
	    fspathncmp(pathname, base, baselen))
		return 0;

	// local modification: simplified because always baselen > 0
	pathname += baselen + 1;
	pathlen -= baselen + 1;

	if (prefix) {
		/*
		 * if the non-wildcard part is longer than the
		 * remaining pathname, surely it cannot match.
		 */
		if (prefix > pathlen)
			return 0;

		if (fspathncmp(pattern, pathname, prefix))
			return 0;
		pattern += prefix;
		patternlen -= prefix;
		pathname += prefix;
		pathlen -= prefix;

		/*
		 * If the whole pattern did not have a wildcard,
		 * then our prefix match is all we need; we
		 * do not need to call fnmatch at all.
		 */
		if (!patternlen && !pathlen)
			return 1;
	}

	// local modification: call wildmatch() directly
	return !wildmatch(pattern, pathname, WM_PATHNAME);
}

// Copied from git/utf8.c

static const char utf8_bom[] = "\357\273\277";

//----------------------------(IMPORT FROM GIT END)----------------------------

struct pattern {
	unsigned int flags;
	int nowildcardlen;
	int patternlen;
	int dirlen;
	char pattern[];
};

static struct pattern **pattern_list;
static int nr_patterns, alloced_patterns;

// Remember the number of patterns at each directory level
static int *nr_patterns_at;
// Track the current/max directory level;
static int depth, max_depth;
static bool debug_on;
static FILE *out_fp, *stat_fp;
static char *prefix = "";
static char *progname;

static void __attribute__((noreturn)) perror_exit(const char *s)
{
	perror(s);

	exit(EXIT_FAILURE);
}

static void __attribute__((noreturn)) error_exit(const char *fmt, ...)
{
	va_list args;

	fprintf(stderr, "%s: error: ", progname);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	exit(EXIT_FAILURE);
}

static void debug(const char *fmt, ...)
{
	va_list args;
	int i;

	if (!debug_on)
		return;

	fprintf(stderr, "[DEBUG] ");

	for (i = 0; i < depth * 2; i++)
		fputc(' ', stderr);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

static void *xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr)
		perror_exit(progname);

	return ptr;
}

static void *xmalloc(size_t size)
{
	return xrealloc(NULL, size);
}

// similar to last_matching_pattern_from_list() in GIT
static bool is_ignored(const char *path, int pathlen, int dirlen, bool is_dir)
{
	int i;

	// Search in the reverse order because the last matching pattern wins.
	for (i = nr_patterns - 1; i >= 0; i--) {
		struct pattern *p = pattern_list[i];
		unsigned int flags = p->flags;
		const char *gitignore_dir = p->pattern + p->patternlen + 1;
		bool ignored;

		if ((flags & PATTERN_FLAG_MUSTBEDIR) && !is_dir)
			continue;

		if (flags & PATTERN_FLAG_NODIR) {
			if (!match_basename(path + dirlen + 1,
					    pathlen - dirlen - 1,
					    p->pattern,
					    p->nowildcardlen,
					    p->patternlen,
					    p->flags))
				continue;
		} else {
			if (!match_pathname(path, pathlen,
					    gitignore_dir, p->dirlen,
					    p->pattern,
					    p->nowildcardlen,
					    p->patternlen))
				continue;
		}

		debug("%s: matches %s%s%s (%s/.gitignore)\n", path,
		      flags & PATTERN_FLAG_NEGATIVE ? "!" : "", p->pattern,
		      flags & PATTERN_FLAG_MUSTBEDIR ? "/" : "",
		      gitignore_dir);

		ignored = (flags & PATTERN_FLAG_NEGATIVE) == 0;
		if (ignored)
			debug("Ignore: %s\n", path);

		return ignored;
	}

	debug("%s: no match\n", path);

	return false;
}

static void add_pattern(const char *string, const char *dir, int dirlen)
{
	struct pattern *p;
	int patternlen, nowildcardlen;
	unsigned int flags;

	parse_path_pattern(&string, &patternlen, &flags, &nowildcardlen);

	if (patternlen == 0)
		return;

	p = xmalloc(sizeof(*p) + patternlen + dirlen + 2);

	memcpy(p->pattern, string, patternlen);
	p->pattern[patternlen] = 0;
	memcpy(p->pattern + patternlen + 1, dir, dirlen);
	p->pattern[patternlen + 1 + dirlen] = 0;

	p->patternlen = patternlen;
	p->nowildcardlen = nowildcardlen;
	p->dirlen = dirlen;
	p->flags = flags;

	debug("Add pattern: %s%s%s\n",
	      flags & PATTERN_FLAG_NEGATIVE ? "!" : "", p->pattern,
	      flags & PATTERN_FLAG_MUSTBEDIR ? "/" : "");

	if (nr_patterns >= alloced_patterns) {
		alloced_patterns += 128;
		pattern_list = xrealloc(pattern_list,
					sizeof(*pattern_list) * alloced_patterns);
	}

	pattern_list[nr_patterns++] = p;
}

// similar to add_patterns_from_buffer() in GIT
static void add_patterns_from_gitignore(const char *dir, int dirlen)
{
	struct stat st;
	char path[PATH_MAX], *buf, *entry;
	size_t size;
	int fd, pathlen, i;

	pathlen = snprintf(path, sizeof(path), "%s/.gitignore", dir);
	if (pathlen >= sizeof(path))
		error_exit("%s: too long path was truncated\n", path);

	fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (fd < 0) {
		if (errno != ENOENT)
			return perror_exit(path);
		return;
	}

	if (fstat(fd, &st) < 0)
		perror_exit(path);

	size = st.st_size;

	buf = xmalloc(size + 1);
	if (read(fd, buf, st.st_size) != st.st_size)
		perror_exit(path);

	buf[st.st_size] = '\n';
	if (close(fd))
		perror_exit(path);

	debug("Parse %s\n", path);

	entry = buf;

	// skip utf8 bom
	if (!strncmp(entry, utf8_bom, strlen(utf8_bom)))
		entry += strlen(utf8_bom);

	for (i = entry - buf; i < size; i++) {
		if (buf[i] == '\n') {
			if (entry != buf + i && entry[0] != '#') {
				buf[i - (i && buf[i-1] == '\r')] = 0;
				trim_trailing_spaces(entry);
				add_pattern(entry, dir, dirlen);
			}
			entry = buf + i + 1;
		}
	}

	free(buf);
}

// Save the current number of patterns and increment the depth
static void increment_depth(void)
{
	if (depth >= max_depth) {
		max_depth += 1;
		nr_patterns_at = xrealloc(nr_patterns_at,
					  sizeof(*nr_patterns_at) * max_depth);
	}

	nr_patterns_at[depth] = nr_patterns;
	depth++;
}

// Decrement the depth, and free up the patterns of this directory level.
static void decrement_depth(void)
{
	depth--;
	assert(depth >= 0);

	while (nr_patterns > nr_patterns_at[depth])
		free(pattern_list[--nr_patterns]);
}

static void print_path(const char *path)
{
	// The path always starts with "./"
	assert(strlen(path) >= 2);

	// Replace the root directory with a preferred prefix.
	// This is useful for the tar command.
	fprintf(out_fp, "%s%s\n", prefix, path + 2);
}

static void print_stat(const char *path, struct stat *st)
{
	if (!stat_fp)
		return;

	if (!S_ISREG(st->st_mode) && !S_ISLNK(st->st_mode))
		return;

	assert(strlen(path) >= 2);

	fprintf(stat_fp, "%c %9ld %10ld %s\n",
		S_ISLNK(st->st_mode) ? 'l' : '-',
		st->st_size, st->st_mtim.tv_sec, path + 2);
}

// Traverse the entire directory tree, parsing .gitignore files.
// Print file paths that are not tracked by git.
//
// Return true if all files under the directory are ignored, false otherwise.
static bool traverse_directory(const char *dir, int dirlen)
{
	bool all_ignored = true;
	DIR *dirp;

	debug("Enter[%d]: %s\n", depth, dir);
	increment_depth();

	add_patterns_from_gitignore(dir, dirlen);

	dirp = opendir(dir);
	if (!dirp)
		perror_exit(dir);

	while (1) {
		struct dirent *d;
		struct stat st;
		char path[PATH_MAX];
		int pathlen;
		bool ignored;

		errno = 0;
		d = readdir(dirp);
		if (!d) {
			if (errno)
				perror_exit(dir);
			break;
		}

		if (!strcmp(d->d_name, "..") || !strcmp(d->d_name, "."))
			continue;

		pathlen = snprintf(path, sizeof(path), "%s/%s", dir, d->d_name);
		if (pathlen >= sizeof(path))
			error_exit("%s: too long path was truncated\n", path);

		if (lstat(path, &st) < 0)
			perror_exit(path);

		if ((!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) ||
		    is_ignored(path, pathlen, dirlen, S_ISDIR(st.st_mode))) {
			ignored = true;
		} else {
			if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
				// If all the files in a directory are ignored,
				// let's ignore that directory as well. This
				// will avoid empty directories in the tarball.
				ignored = traverse_directory(path, pathlen);
			else
				ignored = false;
		}

		if (ignored) {
			print_path(path);
		} else {
			print_stat(path, &st);
			all_ignored = false;
		}
	}

	if (closedir(dirp))
		perror_exit(dir);

	decrement_depth();
	debug("Leave[%d]: %s\n", depth, dir);

	return all_ignored;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: %s [options]\n"
		"\n"
		"Show files that are ignored by git\n"
		"\n"
		"options:\n"
		"  -d, --debug                  print debug messages to stderr\n"
		"  -e, --exclude PATTERN        add the given exclude pattern\n"
		"  -h, --help                   show this help message and exit\n"
		"  -i, --ignore-case            Ignore case differences between the patterns and the files\n"
		"  -o, --output FILE            output the ignored files to a file (default: '-', i.e. stdout)\n"
		"  -p, --prefix PREFIX          prefix added to each path (default: empty string)\n"
		"  -r, --rootdir DIR            root of the source tree (default: current working directory)\n"
		"  -s, --stat FILE              output the file stat of non-ignored files to a file\n",
		progname);
}

static void open_output(const char *pathname, FILE **fp)
{
	if (strcmp(pathname, "-")) {
		*fp = fopen(pathname, "w");
		if (!*fp)
			perror_exit(pathname);
	} else {
		*fp = stdout;
	}
}

static void close_output(const char *pathname, FILE *fp)
{
	fflush(fp);

	if (ferror(fp))
		error_exit("not all data was written to the output\n");

	if (fclose(fp))
		perror_exit(pathname);
}

int main(int argc, char *argv[])
{
	const char *output = "-";
	const char *rootdir = ".";
	const char *stat = NULL;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	while (1) {
		static struct option long_options[] = {
			{"debug",       no_argument,       NULL, 'd'},
			{"help",        no_argument,       NULL, 'h'},
			{"ignore-case", no_argument,       NULL, 'i'},
			{"output",      required_argument, NULL, 'o'},
			{"prefix",      required_argument, NULL, 'p'},
			{"rootdir",     required_argument, NULL, 'r'},
			{"stat",        required_argument, NULL, 's'},
			{"exclude",     required_argument, NULL, 'x'},
			{},
		};

		int c = getopt_long(argc, argv, "dhino:p:r:s:x:", long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'd':
			debug_on = true;
			break;
		case 'h':
			usage();
			exit(0);
		case 'i':
			ignore_case = true;
			break;
		case 'o':
			output = optarg;
			break;
		case 'p':
			prefix = optarg;
			break;
		case 'r':
			rootdir = optarg;
			break;
		case 's':
			stat = optarg;
			break;
		case 'x':
			add_pattern(optarg, ".", strlen("."));
			break;
		case '?':
			usage();
			/* fallthrough */
		default:
			exit(EXIT_FAILURE);
		}
	}

	open_output(output, &out_fp);
	if (stat && stat[0])
		open_output(stat, &stat_fp);

	if (chdir(rootdir))
		perror_exit(rootdir);

	add_pattern(".git/", ".", strlen("."));

	if (traverse_directory(".", strlen(".")))
		print_path("./");

	assert(depth == 0);

	while (nr_patterns > 0)
		free(pattern_list[--nr_patterns]);
	free(pattern_list);
	free(nr_patterns_at);

	close_output(output, out_fp);
	if (stat_fp)
		close_output(stat, stat_fp);

	return 0;
}
