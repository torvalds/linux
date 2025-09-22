/*
 * Public domain, 2008, Todd C. Miller <millert@openbsd.org>
 *
 * $OpenBSD: charclass.h,v 1.3 2020/10/13 04:42:28 guenther Exp $
 */

/*
 * POSIX character class support for fnmatch() and glob().
 */
static const struct cclass {
	const char *name;
	int (*isctype)(int);
} cclasses[] = {
	{ "alnum",	isalnum },
	{ "alpha",	isalpha },
	{ "blank",	isblank },
	{ "cntrl",	iscntrl },
	{ "digit",	isdigit },
	{ "graph",	isgraph },
	{ "lower",	islower },
	{ "print",	isprint },
	{ "punct",	ispunct },
	{ "space",	isspace },
	{ "upper",	isupper },
	{ "xdigit",	isxdigit },
	{ NULL,		NULL }
};

#define NCCLASSES	(sizeof(cclasses) / sizeof(cclasses[0]) - 1)
