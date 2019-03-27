/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * units.c   Copyright (c) 1993 by Adrian Mariano (adrian@cam.cornell.edu)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * Disclaimer:  This software is provided by the author "as is".  The author
 * shall not be liable for any damages caused in any way by this software.
 *
 * I would appreciate (though I do not require) receiving a copy of any
 * improvements you might make to this program.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <histedit.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <capsicum_helpers.h>

#ifndef UNITSFILE
#define UNITSFILE "/usr/share/misc/definitions.units"
#endif

#define MAXUNITS 1000
#define MAXPREFIXES 100

#define MAXSUBUNITS 500

#define PRIMITIVECHAR '!'

static const char *powerstring = "^";
static const char *numfmt = "%.8g";

static struct {
	char *uname;
	char *uval;
}      unittable[MAXUNITS];

struct unittype {
	char *numerator[MAXSUBUNITS];
	char *denominator[MAXSUBUNITS];
	double factor;
	double offset;
	int quantity;
};

static struct {
	char *prefixname;
	char *prefixval;
}      prefixtable[MAXPREFIXES];


static char NULLUNIT[] = "";

#define SEPARATOR      ":"

static int unitcount;
static int prefixcount;
static bool verbose = false;
static bool terse = false;
static const char * outputformat;
static const char * havestr;
static const char * wantstr;

static int	 addsubunit(char *product[], char *toadd);
static int	 addunit(struct unittype *theunit, const char *toadd, int flip, int quantity);
static void	 cancelunit(struct unittype * theunit);
static int	 compare(const void *item1, const void *item2);
static int	 compareproducts(char **one, char **two);
static int	 compareunits(struct unittype * first, struct unittype * second);
static int	 completereduce(struct unittype * unit);
static char	*dupstr(const char *str);
static void	 initializeunit(struct unittype * theunit);
static char	*lookupunit(const char *unit);
static void	 readunits(const char *userfile);
static int	 reduceproduct(struct unittype * theunit, int flip);
static int	 reduceunit(struct unittype * theunit);
static void	 showanswer(struct unittype * have, struct unittype * want);
static void	 showunit(struct unittype * theunit);
static void	 sortunit(struct unittype * theunit);
static void	 usage(void);
static void	 zeroerror(void);

static const char* promptstr = "";

static const char * prompt(EditLine *e __unused) {
	return promptstr;
}

static char *
dupstr(const char *str)
{
	char *ret;

	ret = strdup(str);
	if (!ret)
		err(3, "dupstr");
	return (ret);
}


static void 
readunits(const char *userfile)
{
	FILE *unitfile;
	char line[512], *lineptr;
	int len, linenum, i;
	cap_rights_t unitfilerights;

	unitcount = 0;
	linenum = 0;

	if (userfile) {
		unitfile = fopen(userfile, "r");
		if (!unitfile)
			errx(1, "unable to open units file '%s'", userfile);
	}
	else {
		unitfile = fopen(UNITSFILE, "r");
		if (!unitfile) {
			char *direc, *env;
			char filename[1000];

			env = getenv("PATH");
			if (env) {
				direc = strtok(env, SEPARATOR);
				while (direc) {
					snprintf(filename, sizeof(filename),
					    "%s/%s", direc, UNITSFILE);
					unitfile = fopen(filename, "rt");
					if (unitfile)
						break;
					direc = strtok(NULL, SEPARATOR);
				}
			}
			if (!unitfile)
				errx(1, "can't find units file '%s'", UNITSFILE);
		}
	}
	cap_rights_init(&unitfilerights, CAP_READ, CAP_FSTAT);
	if (caph_rights_limit(fileno(unitfile), &unitfilerights) < 0)
		err(1, "cap_rights_limit() failed");
	while (!feof(unitfile)) {
		if (!fgets(line, sizeof(line), unitfile))
			break;
		linenum++;
		lineptr = line;
		if (*lineptr == '/' || *lineptr == '#')
			continue;
		lineptr += strspn(lineptr, " \n\t");
		len = strcspn(lineptr, " \n\t");
		lineptr[len] = 0;
		if (!strlen(lineptr))
			continue;
		if (lineptr[strlen(lineptr) - 1] == '-') { /* it's a prefix */
			if (prefixcount == MAXPREFIXES) {
				warnx("memory for prefixes exceeded in line %d", linenum);
				continue;
			}
			lineptr[strlen(lineptr) - 1] = 0;
			prefixtable[prefixcount].prefixname = dupstr(lineptr);
			for (i = 0; i < prefixcount; i++)
				if (!strcmp(prefixtable[i].prefixname, lineptr)) {
					warnx("redefinition of prefix '%s' on line %d ignored",
					    lineptr, linenum);
					continue;
				}
			lineptr += len + 1;
			lineptr += strspn(lineptr, " \n\t");
			len = strcspn(lineptr, "\n\t");
			if (len == 0) {
				warnx("unexpected end of prefix on line %d",
				    linenum);
				continue;
			}
			lineptr[len] = 0;
			prefixtable[prefixcount++].prefixval = dupstr(lineptr);
		}
		else {		/* it's not a prefix */
			if (unitcount == MAXUNITS) {
				warnx("memory for units exceeded in line %d", linenum);
				continue;
			}
			unittable[unitcount].uname = dupstr(lineptr);
			for (i = 0; i < unitcount; i++)
				if (!strcmp(unittable[i].uname, lineptr)) {
					warnx("redefinition of unit '%s' on line %d ignored",
					    lineptr, linenum);
					continue;
				}
			lineptr += len + 1;
			lineptr += strspn(lineptr, " \n\t");
			if (!strlen(lineptr)) {
				warnx("unexpected end of unit on line %d",
				    linenum);
				continue;
			}
			len = strcspn(lineptr, "\n\t");
			lineptr[len] = 0;
			unittable[unitcount++].uval = dupstr(lineptr);
		}
	}
	fclose(unitfile);
}

static void 
initializeunit(struct unittype * theunit)
{
	theunit->numerator[0] = theunit->denominator[0] = NULL;
	theunit->factor = 1.0;
	theunit->offset = 0.0;
	theunit->quantity = 0;
}


static int 
addsubunit(char *product[], char *toadd)
{
	char **ptr;

	for (ptr = product; *ptr && *ptr != NULLUNIT; ptr++);
	if (ptr >= product + MAXSUBUNITS) {
		warnx("memory overflow in unit reduction");
		return 1;
	}
	if (!*ptr)
		*(ptr + 1) = NULL;
	*ptr = dupstr(toadd);
	return 0;
}


static void 
showunit(struct unittype * theunit)
{
	char **ptr;
	int printedslash;
	int counter = 1;

	printf(numfmt, theunit->factor);
	if (theunit->offset)
		printf("&%.8g", theunit->offset);
	for (ptr = theunit->numerator; *ptr; ptr++) {
		if (ptr > theunit->numerator && **ptr &&
		    !strcmp(*ptr, *(ptr - 1)))
			counter++;
		else {
			if (counter > 1)
				printf("%s%d", powerstring, counter);
			if (**ptr)
				printf(" %s", *ptr);
			counter = 1;
		}
	}
	if (counter > 1)
		printf("%s%d", powerstring, counter);
	counter = 1;
	printedslash = 0;
	for (ptr = theunit->denominator; *ptr; ptr++) {
		if (ptr > theunit->denominator && **ptr &&
		    !strcmp(*ptr, *(ptr - 1)))
			counter++;
		else {
			if (counter > 1)
				printf("%s%d", powerstring, counter);
			if (**ptr) {
				if (!printedslash)
					printf(" /");
				printedslash = 1;
				printf(" %s", *ptr);
			}
			counter = 1;
		}
	}
	if (counter > 1)
		printf("%s%d", powerstring, counter);
	printf("\n");
}


void 
zeroerror(void)
{
	warnx("unit reduces to zero");
}

/*
   Adds the specified string to the unit.
   Flip is 0 for adding normally, 1 for adding reciprocal.
   Quantity is 1 if this is a quantity to be converted rather than a pure unit.

   Returns 0 for successful addition, nonzero on error.
*/

static int 
addunit(struct unittype * theunit, const char *toadd, int flip, int quantity)
{
	char *scratch, *savescr;
	char *item;
	char *divider, *slash, *offset;
	int doingtop;

	if (!strlen(toadd))
		return 1;
	
	savescr = scratch = dupstr(toadd);
	for (slash = scratch + 1; *slash; slash++)
		if (*slash == '-' &&
		    (tolower(*(slash - 1)) != 'e' ||
		    !strchr(".0123456789", *(slash + 1))))
			*slash = ' ';
	slash = strchr(scratch, '/');
	if (slash)
		*slash = 0;
	doingtop = 1;
	do {
		item = strtok(scratch, " *\t\n/");
		while (item) {
			if (strchr("0123456789.", *item)) { /* item is a number */
				double num, offsetnum;

				if (quantity)
					theunit->quantity = 1;

				offset = strchr(item, '&');
				if (offset) {
					*offset = 0;
					offsetnum = atof(offset+1);
				} else
					offsetnum = 0.0;

				divider = strchr(item, '|');
				if (divider) {
					*divider = 0;
					num = atof(item);
					if (!num) {
						zeroerror();
						free(savescr);
						return 1;
					}
					if (doingtop ^ flip) {
						theunit->factor *= num;
						theunit->offset *= num;
					} else {
						theunit->factor /= num;
						theunit->offset /= num;
					}
					num = atof(divider + 1);
					if (!num) {
						zeroerror();
						free(savescr);
						return 1;
					}
					if (doingtop ^ flip) {
						theunit->factor /= num;
						theunit->offset /= num;
					} else {
						theunit->factor *= num;
						theunit->offset *= num;
					}
				}
				else {
					num = atof(item);
					if (!num) {
						zeroerror();
						free(savescr);
						return 1;
					}
					if (doingtop ^ flip) {
						theunit->factor *= num;
						theunit->offset *= num;
					} else {
						theunit->factor /= num;
						theunit->offset /= num;
					}
				}
				if (doingtop ^ flip)
					theunit->offset += offsetnum;
			}
			else {	/* item is not a number */
				int repeat = 1;

				if (strchr("23456789",
				    item[strlen(item) - 1])) {
					repeat = item[strlen(item) - 1] - '0';
					item[strlen(item) - 1] = 0;
				}
				for (; repeat; repeat--) {
					if (addsubunit(doingtop ^ flip ? theunit->numerator : theunit->denominator, item)) {
						free(savescr);
						return 1;
					}
				}
			}
			item = strtok(NULL, " *\t/\n");
		}
		doingtop--;
		if (slash) {
			scratch = slash + 1;
		}
		else
			doingtop--;
	} while (doingtop >= 0);
	free(savescr);
	return 0;
}


static int 
compare(const void *item1, const void *item2)
{
	return strcmp(*(const char * const *)item1, *(const char * const *)item2);
}


static void 
sortunit(struct unittype * theunit)
{
	char **ptr;
	unsigned int count;

	for (count = 0, ptr = theunit->numerator; *ptr; ptr++, count++);
	qsort(theunit->numerator, count, sizeof(char *), compare);
	for (count = 0, ptr = theunit->denominator; *ptr; ptr++, count++);
	qsort(theunit->denominator, count, sizeof(char *), compare);
}


void 
cancelunit(struct unittype * theunit)
{
	char **den, **num;
	int comp;

	den = theunit->denominator;
	num = theunit->numerator;

	while (*num && *den) {
		comp = strcmp(*den, *num);
		if (!comp) {
/*      if (*den!=NULLUNIT) free(*den);
      if (*num!=NULLUNIT) free(*num);*/
			*den++ = NULLUNIT;
			*num++ = NULLUNIT;
		}
		else if (comp < 0)
			den++;
		else
			num++;
	}
}




/*
   Looks up the definition for the specified unit.
   Returns a pointer to the definition or a null pointer
   if the specified unit does not appear in the units table.
*/

static char buffer[100];	/* buffer for lookupunit answers with
				   prefixes */

char *
lookupunit(const char *unit)
{
	int i;
	char *copy;

	for (i = 0; i < unitcount; i++) {
		if (!strcmp(unittable[i].uname, unit))
			return unittable[i].uval;
	}

	if (unit[strlen(unit) - 1] == '^') {
		copy = dupstr(unit);
		copy[strlen(copy) - 1] = 0;
		for (i = 0; i < unitcount; i++) {
			if (!strcmp(unittable[i].uname, copy)) {
				strlcpy(buffer, copy, sizeof(buffer));
				free(copy);
				return buffer;
			}
		}
		free(copy);
	}
	if (unit[strlen(unit) - 1] == 's') {
		copy = dupstr(unit);
		copy[strlen(copy) - 1] = 0;
		for (i = 0; i < unitcount; i++) {
			if (!strcmp(unittable[i].uname, copy)) {
				strlcpy(buffer, copy, sizeof(buffer));
				free(copy);
				return buffer;
			}
		}
		if (copy[strlen(copy) - 1] == 'e') {
			copy[strlen(copy) - 1] = 0;
			for (i = 0; i < unitcount; i++) {
				if (!strcmp(unittable[i].uname, copy)) {
					strlcpy(buffer, copy, sizeof(buffer));
					free(copy);
					return buffer;
				}
			}
		}
		free(copy);
	}
	for (i = 0; i < prefixcount; i++) {
		size_t len = strlen(prefixtable[i].prefixname);
		if (!strncmp(prefixtable[i].prefixname, unit, len)) {
			if (!strlen(unit + len) || lookupunit(unit + len)) {
				snprintf(buffer, sizeof(buffer), "%s %s",
				    prefixtable[i].prefixval, unit + len);
				return buffer;
			}
		}
	}
	return 0;
}



/*
   reduces a product of symbolic units to primitive units.
   The three low bits are used to return flags:

     bit 0 (1) set on if reductions were performed without error.
     bit 1 (2) set on if no reductions are performed.
     bit 2 (4) set on if an unknown unit is discovered.
*/


#define ERROR 4

static int 
reduceproduct(struct unittype * theunit, int flip)
{

	char *toadd;
	char **product;
	int didsomething = 2;

	if (flip)
		product = theunit->denominator;
	else
		product = theunit->numerator;

	for (; *product; product++) {

		for (;;) {
			if (!strlen(*product))
				break;
			toadd = lookupunit(*product);
			if (!toadd) {
				printf("unknown unit '%s'\n", *product);
				return ERROR;
			}
			if (strchr(toadd, PRIMITIVECHAR))
				break;
			didsomething = 1;
			if (*product != NULLUNIT) {
				free(*product);
				*product = NULLUNIT;
			}
			if (addunit(theunit, toadd, flip, 0))
				return ERROR;
		}
	}
	return didsomething;
}


/*
   Reduces numerator and denominator of the specified unit.
   Returns 0 on success, or 1 on unknown unit error.
*/

static int 
reduceunit(struct unittype * theunit)
{
	int ret;

	ret = 1;
	while (ret & 1) {
		ret = reduceproduct(theunit, 0) | reduceproduct(theunit, 1);
		if (ret & 4)
			return 1;
	}
	return 0;
}


static int 
compareproducts(char **one, char **two)
{
	while (*one || *two) {
		if (!*one && *two != NULLUNIT)
			return 1;
		if (!*two && *one != NULLUNIT)
			return 1;
		if (*one == NULLUNIT)
			one++;
		else if (*two == NULLUNIT)
			two++;
		else if (strcmp(*one, *two))
			return 1;
		else {
			one++;
			two++;
		}
	}
	return 0;
}


/* Return zero if units are compatible, nonzero otherwise */

static int 
compareunits(struct unittype * first, struct unittype * second)
{
	return
	compareproducts(first->numerator, second->numerator) ||
	compareproducts(first->denominator, second->denominator);
}


static int 
completereduce(struct unittype * unit)
{
	if (reduceunit(unit))
		return 1;
	sortunit(unit);
	cancelunit(unit);
	return 0;
}

static void 
showanswer(struct unittype * have, struct unittype * want)
{
	double ans;
	char* oformat;

	if (compareunits(have, want)) {
		printf("conformability error\n");
		if (verbose)
			printf("\t%s = ", havestr);
		else if (!terse)
			printf("\t");
		showunit(have);
		if (!terse) {
			if (verbose)
				printf("\t%s = ", wantstr);
			else
				printf("\t");
			showunit(want);
		}
	}
	else if (have->offset != want->offset) {
		if (want->quantity)
			printf("WARNING: conversion of non-proportional quantities.\n");
		if (have->quantity) {
			asprintf(&oformat, "\t%s\n", outputformat);
			printf(oformat,
			    (have->factor + have->offset-want->offset)/want->factor);
			free(oformat);
		}
		else {
			asprintf(&oformat, "\t (-> x*%sg %sg)\n\t (<- y*%sg %sg)\n",
			    outputformat, outputformat, outputformat, outputformat);
			printf(oformat,
			    have->factor / want->factor,
			    (have->offset-want->offset)/want->factor,
			    want->factor / have->factor,
			    (want->offset - have->offset)/have->factor);
		}
	}
	else {
		ans = have->factor / want->factor;

		if (verbose) {
			printf("\t%s = ", havestr);
			printf(outputformat, ans);
			printf(" * %s", wantstr);
			printf("\n");
		}
		else if (terse) {
			printf(outputformat, ans);
			printf("\n");
		}
		else {
			printf("\t* ");
			printf(outputformat, ans);
			printf("\n");
		}

		if (verbose) {
			printf("\t%s = (1 / ", havestr);
			printf(outputformat, 1/ans);
			printf(") * %s\n", wantstr);
		}
		else if (!terse) {
			printf("\t/ ");
			printf(outputformat, 1/ans);
			printf("\n");
		}
	}
}


static void __dead2
usage(void)
{
	fprintf(stderr,
		"usage: units [-f unitsfile] [-H historyfile] [-UVq] [from-unit to-unit]\n");
	exit(3);
}

static struct option longopts[] = {
	{"help", no_argument, NULL, 'h'},
	{"exponential", no_argument, NULL, 'e'},
	{"file", required_argument, NULL, 'f'},
	{"history", required_argument, NULL, 'H'},
	{"output-format", required_argument, NULL, 'o'},
	{"quiet", no_argument, NULL, 'q'},
	{"terse", no_argument, NULL, 't'},
	{"unitsfile", no_argument, NULL, 'U'},
	{"verbose", no_argument, NULL, 'v'},
	{"version", no_argument, NULL, 'V'},
	{ 0, 0, 0, 0 }
};


int
main(int argc, char **argv)
{

	struct unittype have, want;
	int optchar;
	bool quiet;
	bool readfile;
	bool quit;
	History *inhistory;
	EditLine *el;
	HistEvent ev;
	int inputsz;
	char const * history_file;

	quiet = false;
	readfile = false;
	history_file = NULL;
	outputformat = numfmt;
	quit = false;
	while ((optchar = getopt_long(argc, argv, "+ehf:o:qtvH:UV", longopts, NULL)) != -1) {
		switch (optchar) {
		case 'e':
			outputformat = "%6e";
			break;
		case 'f':
			readfile = true;
			if (strlen(optarg) == 0)
				readunits(NULL);
			else
				readunits(optarg);
			break;
		case 'H':
			history_file = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		case 't':
			terse = true;
			break;
		case 'o':
			outputformat = optarg;
			break;
		case 'v':
			verbose = true;
			break;
		case 'V':
			fprintf(stderr, "FreeBSD units\n");
			/* FALLTHROUGH */
		case 'U':
			if (access(UNITSFILE, F_OK) == 0)
				printf("%s\n", UNITSFILE);
			else
				printf("Units data file not found");
			exit(0);
		case 'h':
			/* FALLTHROUGH */

		default:
			usage();
		}
	}

	if (!readfile)
		readunits(NULL);

	if (optind == argc - 2) {
		if (caph_enter() < 0)
			err(1, "unable to enter capability mode");

		havestr = argv[optind];
		wantstr = argv[optind + 1];
		initializeunit(&have);
		addunit(&have, havestr, 0, 1);
		completereduce(&have);
		initializeunit(&want);
		addunit(&want, wantstr, 0, 1);
		completereduce(&want);
		showanswer(&have, &want);
	} else {
		inhistory = history_init();
		el = el_init(argv[0], stdin, stdout, stderr);
		el_set(el, EL_PROMPT, &prompt);
		el_set(el, EL_EDITOR, "emacs");
		el_set(el, EL_SIGNAL, 1);
		el_set(el, EL_HIST, history, inhistory);
		el_source(el, NULL);
		history(inhistory, &ev, H_SETSIZE, 800);
		if (inhistory == 0)
			err(1, "Could not initialize history");

		if (caph_enter() < 0)
			err(1, "unable to enter capability mode");

		if (!quiet)
			printf("%d units, %d prefixes\n", unitcount,
			    prefixcount);
		while (!quit) {
			do {
				initializeunit(&have);
				if (!quiet)
					promptstr = "You have: ";
				havestr = el_gets(el, &inputsz);
				if (havestr == NULL) {
					quit = true;
					break;
				}
				if (inputsz > 0)
					history(inhistory, &ev, H_ENTER,
					havestr);
			} while (addunit(&have, havestr, 0, 1) ||
			    completereduce(&have));
			if (quit) {
				break;
			}
			do {
				initializeunit(&want);
				if (!quiet)
					promptstr = "You want: ";
				wantstr = el_gets(el, &inputsz);
				if (wantstr == NULL) {
					quit = true;
					break;
				}
				if (inputsz > 0)
					history(inhistory, &ev, H_ENTER,
					wantstr);
			} while (addunit(&want, wantstr, 0, 1) ||
			    completereduce(&want));
			if (quit) {
				break;
			}
			showanswer(&have, &want);
		}

		history_end(inhistory);
		el_end(el);
	}

	return (0);
}
