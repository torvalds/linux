/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright © 2002, Jörg Wunsch
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 4.3BSD UI-compatible whereis(1) utility.  Rewritten from scratch
 * since the original 4.3BSD version suffers legal problems that
 * prevent it from being redistributed, and since the 4.4BSD version
 * was pretty inferior in functionality.
 */

#include <sys/types.h>

__FBSDID("$FreeBSD$");

#include <sys/stat.h>
#include <sys/sysctl.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "pathnames.h"

#define	NO_BIN_FOUND	1
#define	NO_MAN_FOUND	2
#define	NO_SRC_FOUND	4

typedef const char *ccharp;

static int opt_a, opt_b, opt_m, opt_q, opt_s, opt_u, opt_x;
static ccharp *bindirs, *mandirs, *sourcedirs;
static char **query;

static const char *sourcepath = PATH_SOURCES;

static char	*colonify(ccharp *);
static int	 contains(ccharp *, const char *);
static void	 decolonify(char *, ccharp **, int *);
static void	 defaults(void);
static void	 scanopts(int, char **);
static void	 usage(void);

/*
 * Throughout this program, a number of strings are dynamically
 * allocated but never freed.  Their memory is written to when
 * splitting the strings into string lists which will later be
 * processed.  Since it's important that those string lists remain
 * valid even after the functions allocating the memory returned,
 * those functions cannot free them.  They could be freed only at end
 * of main(), which is pretty pointless anyway.
 *
 * The overall amount of memory to be allocated for processing the
 * strings is not expected to exceed a few kilobytes.  For that
 * reason, allocation can usually always be assumed to succeed (within
 * a virtual memory environment), thus we simply bail out using
 * abort(3) in case of an allocation failure.
 */

static void
usage(void)
{
	(void)fprintf(stderr,
	     "usage: whereis [-abmqsux] [-BMS dir ... -f] program ...\n");
	exit(EX_USAGE);
}

/*
 * Scan options passed to program.
 *
 * Note that the -B/-M/-S options expect a list of directory
 * names that must be terminated with -f.
 */
static void
scanopts(int argc, char **argv)
{
	int c, i;
	ccharp **dirlist;

	while ((c = getopt(argc, argv, "BMSabfmqsux")) != -1)
		switch (c) {
		case 'B':
			dirlist = &bindirs;
			goto dolist;

		case 'M':
			dirlist = &mandirs;
			goto dolist;

		case 'S':
			dirlist = &sourcedirs;
		  dolist:
			i = 0;
			*dirlist = realloc(*dirlist, (i + 1) * sizeof(char *));
			(*dirlist)[i] = NULL;
			while (optind < argc &&
			       strcmp(argv[optind], "-f") != 0 &&
			       strcmp(argv[optind], "-B") != 0 &&
			       strcmp(argv[optind], "-M") != 0 &&
			       strcmp(argv[optind], "-S") != 0) {
				decolonify(argv[optind], dirlist, &i);
				optind++;
			}
			break;

		case 'a':
			opt_a = 1;
			break;

		case 'b':
			opt_b = 1;
			break;

		case 'f':
			goto breakout;

		case 'm':
			opt_m = 1;
			break;

		case 'q':
			opt_q = 1;
			break;

		case 's':
			opt_s = 1;
			break;

		case 'u':
			opt_u = 1;
			break;

		case 'x':
			opt_x = 1;
			break;

		default:
			usage();
		}
  breakout:
	if (optind == argc)
		usage();
	query = argv + optind;
}

/*
 * Find out whether string `s' is contained in list `cpp'.
 */
static int
contains(ccharp *cpp, const char *s)
{
	ccharp cp;

	if (cpp == NULL)
		return (0);

	while ((cp = *cpp) != NULL) {
		if (strcmp(cp, s) == 0)
			return (1);
		cpp++;
	}
	return (0);
}

/*
 * Split string `s' at colons, and pass it to the string list pointed
 * to by `cppp' (which has `*ip' elements).  Note that the original
 * string is modified by replacing the colon with a NUL byte.  The
 * partial string is only added if it has a length greater than 0, and
 * if it's not already contained in the string list.
 */
static void
decolonify(char *s, ccharp **cppp, int *ip)
{
	char *cp;

	while ((cp = strchr(s, ':')), *s != '\0') {
		if (cp)
			*cp = '\0';
		if (strlen(s) && !contains(*cppp, s)) {
			*cppp = realloc(*cppp, (*ip + 2) * sizeof(char *));
			if (*cppp == NULL)
				abort();
			(*cppp)[*ip] = s;
			(*cppp)[*ip + 1] = NULL;
			(*ip)++;
		}
		if (cp)
			s = cp + 1;
		else
			break;
	}
}

/*
 * Join string list `cpp' into a colon-separated string.
 */
static char *
colonify(ccharp *cpp)
{
	size_t s;
	char *cp;
	int i;

	if (cpp == NULL)
		return (0);

	for (s = 0, i = 0; cpp[i] != NULL; i++)
		s += strlen(cpp[i]) + 1;
	if ((cp = malloc(s + 1)) == NULL)
		abort();
	for (i = 0, *cp = '\0'; cpp[i] != NULL; i++) {
		strcat(cp, cpp[i]);
		strcat(cp, ":");
	}
	cp[s - 1] = '\0';		/* eliminate last colon */

	return (cp);
}

/*
 * Provide defaults for all options and directory lists.
 */
static void
defaults(void)
{
	size_t s;
	char *b, buf[BUFSIZ], *cp;
	int nele;
	FILE *p;
	DIR *dir;
	struct stat sb;
	struct dirent *dirp;

	/* default to -bms if none has been specified */
	if (!opt_b && !opt_m && !opt_s)
		opt_b = opt_m = opt_s = 1;

	/* -b defaults to default path + /usr/libexec +
	 * user's path */
	if (!bindirs) {
		if (sysctlbyname("user.cs_path", (void *)NULL, &s,
				 (void *)NULL, 0) == -1)
			err(EX_OSERR, "sysctlbyname(\"user.cs_path\")");
		if ((b = malloc(s + 1)) == NULL)
			abort();
		if (sysctlbyname("user.cs_path", b, &s, (void *)NULL, 0) == -1)
			err(EX_OSERR, "sysctlbyname(\"user.cs_path\")");
		nele = 0;
		decolonify(b, &bindirs, &nele);
		bindirs = realloc(bindirs, (nele + 2) * sizeof(char *));
		if (bindirs == NULL)
			abort();
		bindirs[nele++] = PATH_LIBEXEC;
		bindirs[nele] = NULL;
		if ((cp = getenv("PATH")) != NULL) {
			/* don't destroy the original environment... */
			b = strdup(cp);
			if (b == NULL)
				abort();
			decolonify(b, &bindirs, &nele);
		}
	}

	/* -m defaults to $(manpath) */
	if (!mandirs) {
		if ((p = popen(MANPATHCMD, "r")) == NULL)
			err(EX_OSERR, "cannot execute manpath command");
		if (fgets(buf, BUFSIZ - 1, p) == NULL ||
		    pclose(p))
			err(EX_OSERR, "error processing manpath results");
		if ((b = strchr(buf, '\n')) != NULL)
			*b = '\0';
		b = strdup(buf);
		if (b == NULL)
			abort();
		nele = 0;
		decolonify(b, &mandirs, &nele);
	}

	/* -s defaults to precompiled list, plus subdirs of /usr/ports */
	if (!sourcedirs) {
		b = strdup(sourcepath);
		if (b == NULL)
			abort();
		nele = 0;
		decolonify(b, &sourcedirs, &nele);

		if (stat(PATH_PORTS, &sb) == -1) {
			if (errno == ENOENT)
				/* no /usr/ports, we are done */
				return;
			err(EX_OSERR, "stat(" PATH_PORTS ")");
		}
		if ((sb.st_mode & S_IFMT) != S_IFDIR)
			/* /usr/ports is not a directory, ignore */
			return;
		if (access(PATH_PORTS, R_OK | X_OK) != 0)
			return;
		if ((dir = opendir(PATH_PORTS)) == NULL)
			err(EX_OSERR, "opendir" PATH_PORTS ")");
		while ((dirp = readdir(dir)) != NULL) {
			/*
			 * Not everything below PATH_PORTS is of
			 * interest.  First, all dot files and
			 * directories (e. g. .snap) can be ignored.
			 * Also, all subdirectories starting with a
			 * capital letter are not going to be
			 * examined, as they are used for internal
			 * purposes (Mk, Tools, ...).  This also
			 * matches a possible CVS subdirectory.
			 * Finally, the distfiles subdirectory is also
			 * special, and should not be considered to
			 * avoid false matches.
			 */
			if (dirp->d_name[0] == '.' ||
			    /*
			     * isupper() not used on purpose: the
			     * check is supposed to default to the C
			     * locale instead of the current user's
			     * locale.
			     */
			    (dirp->d_name[0] >= 'A' && dirp->d_name[0] <= 'Z') ||
			    strcmp(dirp->d_name, "distfiles") == 0)
				continue;
			if ((b = malloc(sizeof PATH_PORTS + 1 + dirp->d_namlen))
			    == NULL)
				abort();
			strcpy(b, PATH_PORTS);
			strcat(b, "/");
			strcat(b, dirp->d_name);
			if (stat(b, &sb) == -1 ||
			    (sb.st_mode & S_IFMT) != S_IFDIR ||
			    access(b, R_OK | X_OK) != 0) {
				free(b);
				continue;
			}
			sourcedirs = realloc(sourcedirs,
					     (nele + 2) * sizeof(char *));
			if (sourcedirs == NULL)
				abort();
			sourcedirs[nele++] = b;
			sourcedirs[nele] = NULL;
		}
		closedir(dir);
	}
}

int
main(int argc, char **argv)
{
	int unusual, i, printed;
	char *bin, buf[BUFSIZ], *cp, *cp2, *man, *name, *src;
	ccharp *dp;
	size_t nlen, olen, s;
	struct stat sb;
	regex_t re, re2;
	regmatch_t matches[2];
	regoff_t rlen;
	FILE *p;

	setlocale(LC_ALL, "");

	scanopts(argc, argv);
	defaults();

	if (mandirs == NULL)
		opt_m = 0;
	if (bindirs == NULL)
		opt_b = 0;
	if (sourcedirs == NULL)
		opt_s = 0;
	if (opt_m + opt_b + opt_s == 0)
		errx(EX_DATAERR, "no directories to search");

	if (opt_m) {
		setenv("MANPATH", colonify(mandirs), 1);
		if ((i = regcomp(&re, MANWHEREISMATCH, REG_EXTENDED)) != 0) {
			regerror(i, &re, buf, BUFSIZ - 1);
			errx(EX_UNAVAILABLE, "regcomp(%s) failed: %s",
			     MANWHEREISMATCH, buf);
		}
	}

	for (; (name = *query) != NULL; query++) {
		/* strip leading path name component */
		if ((cp = strrchr(name, '/')) != NULL)
			name = cp + 1;
		/* strip SCCS or RCS suffix/prefix */
		if (strlen(name) > 2 && strncmp(name, "s.", 2) == 0)
			name += 2;
		if ((s = strlen(name)) > 2 && strcmp(name + s - 2, ",v") == 0)
			name[s - 2] = '\0';
		/* compression suffix */
		s = strlen(name);
		if (s > 2 &&
		    (strcmp(name + s - 2, ".z") == 0 ||
		     strcmp(name + s - 2, ".Z") == 0))
			name[s - 2] = '\0';
		else if (s > 3 &&
			 strcmp(name + s - 3, ".gz") == 0)
			name[s - 3] = '\0';
		else if (s > 4 &&
			 strcmp(name + s - 4, ".bz2") == 0)
			name[s - 4] = '\0';

		unusual = 0;
		bin = man = src = NULL;
		s = strlen(name);

		if (opt_b) {
			/*
			 * Binaries have to match exactly, and must be regular
			 * executable files.
			 */
			unusual = unusual | NO_BIN_FOUND;
			for (dp = bindirs; *dp != NULL; dp++) {
				cp = malloc(strlen(*dp) + 1 + s + 1);
				if (cp == NULL)
					abort();
				strcpy(cp, *dp);
				strcat(cp, "/");
				strcat(cp, name);
				if (stat(cp, &sb) == 0 &&
				    (sb.st_mode & S_IFMT) == S_IFREG &&
				    (sb.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
				    != 0) {
					unusual = unusual & ~NO_BIN_FOUND;
					if (bin == NULL) {
						bin = strdup(cp);
					} else {
						olen = strlen(bin);
						nlen = strlen(cp);
						bin = realloc(bin, 
							      olen + nlen + 2);
						if (bin == NULL)
							abort();
						strcat(bin, " ");
						strcat(bin, cp);
					}
					if (!opt_a) {
						free(cp);
						break;
					}
				}
				free(cp);
			}
		}

		if (opt_m) {
			/*
			 * Ask the man command to perform the search for us.
			 */
			unusual = unusual | NO_MAN_FOUND;
			if (opt_a)
				cp = malloc(sizeof MANWHEREISALLCMD - 2 + s);
			else
				cp = malloc(sizeof MANWHEREISCMD - 2 + s);

			if (cp == NULL)
				abort();

			if (opt_a)
				sprintf(cp, MANWHEREISALLCMD, name);
			else
				sprintf(cp, MANWHEREISCMD, name);

			if ((p = popen(cp, "r")) != NULL) {
			    
				while (fgets(buf, BUFSIZ - 1, p) != NULL) {
					unusual = unusual & ~NO_MAN_FOUND;
				
					if ((cp2 = strchr(buf, '\n')) != NULL)
						*cp2 = '\0';
					if (regexec(&re, buf, 2, 
						    matches, 0) == 0 &&
					    (rlen = matches[1].rm_eo - 
					     matches[1].rm_so) > 0) {
						/*
						 * man -w found formatted
						 * page, need to pick up
						 * source page name.
						 */
						cp2 = malloc(rlen + 1);
						if (cp2 == NULL)
							abort();
						memcpy(cp2, 
						       buf + matches[1].rm_so,
						       rlen);
						cp2[rlen] = '\0';
					} else {
						/*
						 * man -w found plain source
						 * page, use it.
						 */
						cp2 = strdup(buf);
						if (cp2 == NULL)
							abort();
					}

					if (man == NULL) {
						man = strdup(cp2);
					} else {
						olen = strlen(man);
						nlen = strlen(cp2);
						man = realloc(man, 
							      olen + nlen + 2);
						if (man == NULL)
							abort();
						strcat(man, " ");
						strcat(man, cp2);
					}

					free(cp2);
					
					if (!opt_a)
						break;
				}
				pclose(p);
				free(cp);
			}
		}

		if (opt_s) {
			/*
			 * Sources match if a subdir with the exact
			 * name is found.
			 */
			unusual = unusual | NO_SRC_FOUND;
			for (dp = sourcedirs; *dp != NULL; dp++) {
				cp = malloc(strlen(*dp) + 1 + s + 1);
				if (cp == NULL)
					abort();
				strcpy(cp, *dp);
				strcat(cp, "/");
				strcat(cp, name);
				if (stat(cp, &sb) == 0 &&
				    (sb.st_mode & S_IFMT) == S_IFDIR) {
					unusual = unusual & ~NO_SRC_FOUND;
					if (src == NULL) {
						src = strdup(cp);
					} else {
						olen = strlen(src);
						nlen = strlen(cp);
						src = realloc(src, 
							      olen + nlen + 2);
						if (src == NULL)
							abort();
						strcat(src, " ");
						strcat(src, cp);
					}
					if (!opt_a) {
						free(cp);
						break;
					}
				}
				free(cp);
			}
			/*
			 * If still not found, ask locate to search it
			 * for us.  This will find sources for things
			 * like lpr that are well hidden in the
			 * /usr/src tree, but takes a lot longer.
			 * Thus, option -x (`expensive') prevents this
			 * search.
			 *
			 * Do only match locate output that starts
			 * with one of our source directories, and at
			 * least one further level of subdirectories.
			 */
			if (opt_x || (src && !opt_a))
				goto done_sources;

			cp = malloc(sizeof LOCATECMD - 2 + s);
			if (cp == NULL)
				abort();
			sprintf(cp, LOCATECMD, name);
			if ((p = popen(cp, "r")) == NULL)
				goto done_sources;
			while ((src == NULL || opt_a) &&
			       (fgets(buf, BUFSIZ - 1, p)) != NULL) {
				if ((cp2 = strchr(buf, '\n')) != NULL)
					*cp2 = '\0';
				for (dp = sourcedirs;
				     (src == NULL || opt_a) && *dp != NULL;
				     dp++) {
					cp2 = malloc(strlen(*dp) + 9);
					if (cp2 == NULL)
						abort();
					strcpy(cp2, "^");
					strcat(cp2, *dp);
					strcat(cp2, "/[^/]+/");
					if ((i = regcomp(&re2, cp2,
							 REG_EXTENDED|REG_NOSUB))
					    != 0) {
						regerror(i, &re, buf,
							 BUFSIZ - 1);
						errx(EX_UNAVAILABLE,
						     "regcomp(%s) failed: %s",
						     cp2, buf);
					}
					free(cp2);
					if (regexec(&re2, buf, 0,
						    (regmatch_t *)NULL, 0)
					    == 0) {
						unusual = unusual & 
						          ~NO_SRC_FOUND;
						if (src == NULL) {
							src = strdup(buf);
						} else {
							olen = strlen(src);
							nlen = strlen(buf);
							src = realloc(src, 
								      olen + 
								      nlen + 2);
							if (src == NULL)
								abort();
							strcat(src, " ");
							strcat(src, buf);
						}
					}
					regfree(&re2);
				}
			}
			pclose(p);
			free(cp);
		}
	  done_sources:

		if (opt_u && !unusual)
			continue;

		printed = 0;
		if (!opt_q) {
			printf("%s:", name);
			printed++;
		}
		if (bin) {
			if (printed++)
				putchar(' ');
			fputs(bin, stdout);
		}
		if (man) {
			if (printed++)
				putchar(' ');
			fputs(man, stdout);
		}
		if (src) {
			if (printed++)
				putchar(' ');
			fputs(src, stdout);
		}
		if (printed)
			putchar('\n');
	}

	if (opt_m)
		regfree(&re);

	return (0);
}
