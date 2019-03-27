/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Jonathan Belson <jon@witchspace.com>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stringlist.h>
#include <unistd.h>

#include "kbdmap.h"


static const char *lang_default = DEFAULT_LANG;
static const char *font;
static const char *lang;
static const char *program;
static const char *keymapdir = DEFAULT_VT_KEYMAP_DIR;
static const char *fontdir = DEFAULT_VT_FONT_DIR;
static const char *font_default = DEFAULT_VT_FONT;
static const char *sysconfig = DEFAULT_SYSCONFIG;
static const char *font_current;
static const char *dir;
static const char *menu = "";

static int x11;
static int using_vt;
static int show;
static int verbose;
static int print;


struct keymap {
	char	*desc;
	char	*keym;
	int	mark;
	SLIST_ENTRY(keymap) entries;
};
static SLIST_HEAD(slisthead, keymap) head = SLIST_HEAD_INITIALIZER(head);


/*
 * Get keymap entry for 'key', or NULL of not found
 */
static struct keymap *
get_keymap(const char *key)
{
	struct keymap *km;

	SLIST_FOREACH(km, &head, entries)
		if (!strcmp(km->keym, key))
			return km;

	return NULL;
}

/*
 * Count the number of keymaps we found
 */
static int
get_num_keymaps(void)
{
	struct keymap *km;
	int count = 0;

	SLIST_FOREACH(km, &head, entries)
		count++;

	return count;
}

/*
 * Remove any keymap with given keym
 */
static void
remove_keymap(const char *keym)
{
	struct keymap *km;

	SLIST_FOREACH(km, &head, entries) {
		if (!strcmp(keym, km->keym)) {
			SLIST_REMOVE(&head, km, keymap, entries);
			free(km);
			break;
		}
	}
}

/*
 * Add to hash with 'key'
 */
static void
add_keymap(const char *desc, int mark, const char *keym)
{
	struct keymap *km, *km_new;

	/* Is there already an entry with this key? */
	SLIST_FOREACH(km, &head, entries) {
		if (!strcmp(km->keym, keym)) {
			/* Reuse this entry */
			free(km->desc);
			km->desc = strdup(desc);
			km->mark = mark;
			return;
		}
	}

	km_new = (struct keymap *) malloc (sizeof(struct keymap));
	km_new->desc = strdup(desc);
	km_new->keym = strdup(keym);
	km_new->mark = mark;

	/* Add to keymap list */
	SLIST_INSERT_HEAD(&head, km_new, entries);
}

/*
 * Return 0 if syscons is in use (to select legacy defaults).
 */
static int
check_vt(void)
{
	size_t len;
	char term[3];

	len = 3;
	if (sysctlbyname("kern.vty", &term, &len, NULL, 0) != 0 ||
	    strcmp(term, "vt") != 0)
		return 0;
	return 1;
}

/*
 * Figure out the default language to use.
 */
static const char *
get_locale(void)
{
	const char *locale;

	if ((locale = getenv("LC_ALL")) == NULL &&
	    (locale = getenv("LC_CTYPE")) == NULL &&
	    (locale = getenv("LANG")) == NULL)
		locale = lang_default;

	/* Check for alias */
	if (!strcmp(locale, "C"))
		locale = DEFAULT_LANG;

	return locale;
}

/*
 * Extract filename part
 */
static const char *
extract_name(const char *name)
{
	char *p;

	p = strrchr(name, '/');
	if (p != NULL && p[1] != '\0')
		return p + 1;

	return name;
}

/*
 * Return file extension or NULL
 */
static char *
get_extension(const char *name)
{
	char *p;

	p = strrchr(name, '.');

	if (p != NULL && p[1] != '\0')
		return p;

	return NULL;
}

/*
 * Read font from /etc/rc.conf else return default.
 * Freeing the memory is the caller's responsibility.
 */
static char *
get_font(void)
{
	char line[256], buf[20];
	char *fnt = NULL;

	FILE *fp = fopen(sysconfig, "r");
	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			int a, b, matches;

			if (line[0] == '#')
				continue;

			matches = sscanf(line,
			    " font%dx%d = \"%20[-.0-9a-zA-Z_]",
			    &a, &b, buf);
			if (matches==3) {
				if (strcmp(buf, "NO")) {
					if (fnt)
						free(fnt);
					fnt = strdup(buf);
				}
			}
		}
		fclose(fp);
	} else
		fprintf(stderr, "Could not open %s for reading\n", sysconfig);

	return fnt;
}

/*
 * Set a font using 'vidcontrol'
 */
static void
vidcontrol(const char *fnt)
{
	char *tmp, *p, *q, *cmd;
	char ch;
	int i;

	/* syscons test failed */
	if (x11)
		return;

	if (using_vt) {
		asprintf(&cmd, "vidcontrol -f %s", fnt);
		system(cmd);
		free(cmd);
		return;
	}

	tmp = strdup(fnt);

	/* Extract font size */
	p = strrchr(tmp, '-');
	if (p && p[1] != '\0') {
		p++;
		/* Remove any '.fnt' extension */
		if ((q = strstr(p, ".fnt")))
			*q = '\0';

		/*
		 * Check font size is valid, with no trailing characters
		 *  ('&ch' should not be matched)
		 */
		if (sscanf(p, "%dx%d%c", &i, &i, &ch) != 2)
			fprintf(stderr, "Which font size? %s\n", fnt);
		else {
			asprintf(&cmd, "vidcontrol -f %s %s", p, fnt);
			if (verbose)
				fprintf(stderr, "%s\n", cmd);
			system(cmd);
			free(cmd);
		}
	} else
		fprintf(stderr, "Which font size? %s\n", fnt);

	free(tmp);
}

/*
 * Execute 'kbdcontrol' with the appropriate arguments
 */
static void
do_kbdcontrol(struct keymap *km)
{
	char *kbd_cmd;
	asprintf(&kbd_cmd, "kbdcontrol -l %s/%s", dir, km->keym);

	if (!x11)
		system(kbd_cmd);

	fprintf(stderr, "keymap=\"%s\"\n", km->keym);
	free(kbd_cmd);
}

/*
 * Call 'vidcontrol' with the appropriate arguments
 */
static void
do_vidfont(struct keymap *km)
{
	char *vid_cmd, *tmp, *p, *q;

	asprintf(&vid_cmd, "%s/%s", dir, km->keym);
	vidcontrol(vid_cmd);
	free(vid_cmd);

	tmp = strdup(km->keym);
	p = strrchr(tmp, '-');
	if (p && p[1]!='\0') {
		p++;
		q = get_extension(p);
		if (q) {
			*q = '\0';
			printf("font%s=%s\n", p, km->keym);
		}
	}
	free(tmp);
}

/*
 * Display dialog from 'keymaps[]'
 */
static void
show_dialog(struct keymap **km_sorted, int num_keymaps)
{
	FILE *fp;
	char *cmd, *dialog;
	char tmp_name[] = "/tmp/_kbd_lang.XXXX";
	int fd, i, size;

	fd = mkstemp(tmp_name);
	if (fd == -1) {
		fprintf(stderr, "Could not open temporary file \"%s\"\n",
		    tmp_name);
		exit(1);
	}
	asprintf(&dialog, "/usr/bin/dialog --clear --title \"Keyboard Menu\" "
			  "--menu \"%s\" 0 0 0", menu);

	/* start right font, assume that current font is equal
	 * to default font in /etc/rc.conf
	 *	
	 * $font is the font which require the language $lang; e.g.
	 * russian *need* a koi8 font
	 * $font_current is the current font from /etc/rc.conf
	 */
	if (font && strcmp(font, font_current))
		vidcontrol(font);

	/* Build up the command */
	size = 0;
	for (i=0; i<num_keymaps; i++) {
		/*
		 * Each 'font' is passed as ' "font" ""', so allow the
		 * extra space
		 */
		size += strlen(km_sorted[i]->desc) + 6;
	}

	/* Allow the space for '2> tmpfilename' redirection */
	size += strlen(tmp_name) + 3;

	cmd = (char *) malloc(strlen(dialog) + size + 1);
	strcpy(cmd, dialog);

	for (i=0; i<num_keymaps; i++) {
		strcat(cmd, " \"");
		strcat(cmd, km_sorted[i]->desc);
		strcat(cmd, "\"");
		strcat(cmd, " \"\"");
	}

	strcat(cmd, " 2>");
	strcat(cmd, tmp_name);

	/* Show the dialog.. */
	system(cmd);

	fp = fopen(tmp_name, "r");
	if (fp) {
		char choice[64];
		if (fgets(choice, sizeof(choice), fp) != NULL) {
			/* Find key for desc */
			for (i=0; i<num_keymaps; i++) {
				if (!strcmp(choice, km_sorted[i]->desc)) {
					if (!strcmp(program, "kbdmap"))
						do_kbdcontrol(km_sorted[i]);
					else
						do_vidfont(km_sorted[i]);
					break;
				}
			}
		} else {
			if (font != NULL && strcmp(font, font_current))
				/* Cancelled, restore old font */
				vidcontrol(font_current);
		}
		fclose(fp);
	} else
		fprintf(stderr, "Failed to open temporary file");

	/* Tidy up */
	remove(tmp_name);
	free(cmd);
	free(dialog);
	close(fd);
}

/*
 * Search for 'token' in comma delimited array 'buffer'.
 * Return true for found, false for not found.
 */
static int
find_token(const char *buffer, const char *token)
{
	char *buffer_tmp, *buffer_copy, *inputstring;
	char **ap;
	int found;

	buffer_copy = strdup(buffer);
	buffer_tmp = buffer_copy;
	inputstring = buffer_copy;
	ap = &buffer_tmp;

	found = 0;

	while ((*ap = strsep(&inputstring, ",")) != NULL) {
		if (strcmp(buffer_tmp, token) == 0) {
			found = 1;
			break;
		}
	}

	free(buffer_copy);

	return found;
}

/*
 * Compare function for qsort
 */
static int
compare_keymap(const void *a, const void *b)
{

	/* We've been passed pointers to pointers, so: */
	const struct keymap *km1 = *((const struct keymap * const *) a);
	const struct keymap *km2 = *((const struct keymap * const *) b);

	return strcmp(km1->desc, km2->desc);
}

/*
 * Compare function for qsort
 */
static int
compare_lang(const void *a, const void *b)
{
	const char *l1 = *((const char * const *) a);
	const char *l2 = *((const char * const *) b);

	return strcmp(l1, l2);
}

/*
 * Change '8x8' to '8x08' so qsort will put it before eg. '8x14'
 */
static void
kludge_desc(struct keymap **km_sorted, int num_keymaps)
{
	int i;

	for (i=0; i<num_keymaps; i++) {
		char *p;
		char *km = km_sorted[i]->desc;
		if ((p = strstr(km, "8x8")) != NULL) {
			int len;
			int j;
			int offset;

			offset = p - km;

			/* Make enough space for the extra '0' */
			len = strlen(km);
			km = realloc(km, len + 2);

			for (j=len; j!=offset+1; j--)
				km[j + 1] = km[j];

			km[offset+2] = '0';

			km_sorted[i]->desc = km;
		}
	}
}

/*
 * Reverse 'kludge_desc()' - change '8x08' back to '8x8'
 */
static void
unkludge_desc(struct keymap **km_sorted, int num_keymaps)
{
	int i;

	for (i=0; i<num_keymaps; i++) {
		char *p;
		char *km = km_sorted[i]->desc;
		if ((p = strstr(km, "8x08")) != NULL) {
			p += 2;
			while (*p++)
				p[-1] = p[0];

			km = realloc(km, p - km - 1);
			km_sorted[i]->desc = km;
		}
	}
}

/*
 * Return 0 if file exists and is readable, else -1
 */
static int
check_file(const char *keym)
{
	int status = 0;

	if (access(keym, R_OK) == -1) {
		char *fn;
		asprintf(&fn, "%s/%s", dir, keym);
		if (access(fn, R_OK) == -1) {
			if (verbose)
				fprintf(stderr, "%s not found!\n", fn);
			status = -1;
		}
		free(fn);
	} else {
		if (verbose)
			fprintf(stderr, "No read permission for %s!\n", keym);
		status = -1;
	}

	return status;
}

/*
 * Read options from the relevant configuration file, then
 *  present to user.
 */
static void
menu_read(void)
{
	const char *lg;
	char *p;
	int mark, num_keymaps, items, i;
	char buffer[256], filename[PATH_MAX];
	char keym[64], lng[64], desc[256];
	char dialect[64], lang_abk[64];
	struct keymap *km;
	struct keymap **km_sorted;
	struct dirent *dp;
	StringList *lang_list;
	FILE *fp;
	DIR *dirp;

	lang_list = sl_init();

	sprintf(filename, "%s/INDEX.%s", dir, extract_name(dir));

	/* en_US.ISO8859-1 -> en_..\.ISO8859-1 */
	strlcpy(dialect, lang, sizeof(dialect));
	if (strlen(dialect) >= 6 && dialect[2] == '_') {
		dialect[3] = '.';
		dialect[4] = '.';
	}


	/* en_US.ISO8859-1 -> en */
	strlcpy(lang_abk, lang, sizeof(lang_abk));
	if (strlen(lang_abk) >= 3 && lang_abk[2] == '_')
		lang_abk[2] = '\0';

	fprintf(stderr, "lang_default = %s\n", lang_default);
	fprintf(stderr, "dialect = %s\n", dialect);
	fprintf(stderr, "lang_abk = %s\n", lang_abk);

	fp = fopen(filename, "r");
	if (fp) {
		int matches;
		while (fgets(buffer, sizeof(buffer), fp)) {
			p = buffer;
			if (p[0] == '#')
				continue;

			while (isspace(*p))
				p++;

			if (*p == '\0')
				continue;

			/* Parse input, removing newline */
			matches = sscanf(p, "%64[^:]:%64[^:]:%256[^:\n]", 
			    keym, lng, desc);
			if (matches == 3) {
				if (strcmp(keym, "FONT")
				    && strcmp(keym, "MENU")) {
					/* Check file exists & is readable */
					if (check_file(keym) == -1)
						continue;
				}
			}

			if (show) {
				/*
				 * Take note of supported languages, which
				 * might be in a comma-delimited list
				 */
				char *tmp = strdup(lng);
				char *delim = tmp;

				for (delim = tmp; ; ) {
					char ch = *delim++;
					if (ch == ',' || ch == '\0') {
						delim[-1] = '\0';
						if (!sl_find(lang_list, tmp))
							sl_add(lang_list, tmp);
						if (ch == '\0')
							break;
						tmp = delim;
					}
				}
			}
			/* Set empty language to default language */
			if (lng[0] == '\0')
				lg = lang_default;
			else
				lg = lng;


			/* 4) Your choice if it exists
			 * 3) Long match eg. en_GB.ISO8859-1 is equal to
			 *      en_..\.ISO8859-1
			 * 2) short match 'de'
			 * 1) default langlist 'en'
			 * 0) any language
			 *
			 * Language may be a comma separated list
			 * A higher match overwrites a lower
			 * A later entry overwrites a previous if it exists
			 *     twice in the database
			 */

			/* Check for favoured language */
			km = get_keymap(keym);
			mark = (km) ? km->mark : 0;

			if (find_token(lg, lang))
				add_keymap(desc, 4, keym);
			else if (mark <= 3 && find_token(lg, dialect))
				add_keymap(desc, 3, keym);
			else if (mark <= 2 && find_token(lg, lang_abk))
				add_keymap(desc, 2, keym);
			else if (mark <= 1 && find_token(lg, lang_default))
				add_keymap(desc, 1, keym);
			else if (mark <= 0)
				add_keymap(desc, 0, keym);
		}
		fclose(fp);

	} else
		fprintf(stderr, "Could not open %s for reading\n", filename);

	if (show) {
		qsort(lang_list->sl_str, lang_list->sl_cur, sizeof(char*),
		    compare_lang);
		printf("Currently supported languages: ");
		for (i=0; i< (int) lang_list->sl_cur; i++)
			printf("%s ", lang_list->sl_str[i]);
		puts("");
		exit(0);
	}

	km = get_keymap("MENU");
	if (km)
		/* Take note of menu title */
		menu = strdup(km->desc);
	km = get_keymap("FONT");
	if (km)
		/* Take note of language font */
		font = strdup(km->desc);

	/* Remove unwanted items from list */
	remove_keymap("MENU");
	remove_keymap("FONT");

	/* Look for keymaps not in database */
	dirp = opendir(dir);
	if (dirp) {
		while ((dp = readdir(dirp)) != NULL) {
			const char *ext = get_extension(dp->d_name);
			if (ext) {
				if ((!strcmp(ext, ".fnt") ||
				    !strcmp(ext, ".kbd")) &&
				    !get_keymap(dp->d_name)) {
					char *q;

					/* Remove any .fnt or .kbd extension */
					q = strdup(dp->d_name);
					*(get_extension(q)) = '\0';
					add_keymap(q, 0, dp->d_name);
					free(q);

					if (verbose)
						fprintf(stderr,
						    "'%s' not in database\n",
						    dp->d_name);
				}
			}
		}
		closedir(dirp);
	} else
		fprintf(stderr, "Could not open directory '%s'\n", dir);

	/* Sort items in keymap */
	num_keymaps = get_num_keymaps();

	km_sorted = (struct keymap **)
	    malloc(num_keymaps*sizeof(struct keymap *));

	/* Make array of pointers to items in hash */
	items = 0;
	SLIST_FOREACH(km, &head, entries)
		km_sorted[items++] = km;

	/* Change '8x8' to '8x08' so sort works as we might expect... */
	kludge_desc(km_sorted, num_keymaps);

	qsort(km_sorted, num_keymaps, sizeof(struct keymap *), compare_keymap);

	/* ...change back again */
	unkludge_desc(km_sorted, num_keymaps);

	if (print) {
		for (i=0; i<num_keymaps; i++)
			printf("%s\n", km_sorted[i]->desc);
		exit(0);
	}

	show_dialog(km_sorted, num_keymaps);

	free(km_sorted);
}

/*
 * Display usage information and exit
 */
static void
usage(void)
{

	fprintf(stderr, "usage: %s\t[-K] [-V] [-d|-default] [-h|-help] "
	    "[-l|-lang language]\n\t\t[-p|-print] [-r|-restore] [-s|-show] "
	    "[-v|-verbose]\n", program);
	exit(1);
}

static void
parse_args(int argc, char **argv)
{
	int i;

	for (i=1; i<argc; i++) {
		if (argv[i][0] != '-')
			usage();
		else if (!strcmp(argv[i], "-help") || !strcmp(argv[i], "-h"))
			usage();
		else if (!strcmp(argv[i], "-verbose") || !strcmp(argv[i], "-v"))
			verbose = 1;
		else if (!strcmp(argv[i], "-lang") || !strcmp(argv[i], "-l"))
			if (i + 1 == argc)
				usage();
			else
				lang = argv[++i];
		else if (!strcmp(argv[i], "-default") || !strcmp(argv[i], "-d"))
			lang = lang_default;
		else if (!strcmp(argv[i], "-show") || !strcmp(argv[i], "-s"))
			show = 1;
		else if (!strcmp(argv[i], "-print") || !strcmp(argv[i], "-p"))
			print = 1;
		else if (!strcmp(argv[i], "-restore") ||
		    !strcmp(argv[i], "-r")) {
			vidcontrol(font_current);
			exit(0);
		} else if (!strcmp(argv[i], "-K"))
			dir = keymapdir;
		else if (!strcmp(argv[i], "-V"))
			dir = fontdir;
		else
			usage();
	}
}

/*
 * A front-end for the 'vidfont' and 'kbdmap' programs.
 */
int
main(int argc, char **argv)
{

	x11 = system("kbdcontrol -d >/dev/null");

	if (x11) {
		fprintf(stderr, "You are not on a virtual console - "
				"expect certain strange side-effects\n");
		sleep(2);
	}

	using_vt = check_vt();
	if (using_vt == 0) {
		keymapdir = DEFAULT_SC_KEYMAP_DIR;
		fontdir = DEFAULT_SC_FONT_DIR;
		font_default = DEFAULT_SC_FONT;
	}

	SLIST_INIT(&head);

	lang = get_locale();

	program = extract_name(argv[0]);

	font_current = get_font();
	if (font_current == NULL)
		font_current = font_default;

	if (strcmp(program, "kbdmap"))
		dir = fontdir;
	else
		dir = keymapdir;

	/* Parse command line arguments */
	parse_args(argc, argv);

	/* Read and display options */
	menu_read();

	return 0;
}
