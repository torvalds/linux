/*
 * Copyright 1996 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Second attempt at a `tzmenu' program, using the separate description
 * files provided in newer tzdata releases.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#ifdef HAVE_DIALOG
#include <dialog.h>
#endif

#define	_PATH_ZONETAB		"/usr/share/zoneinfo/zone.tab"
#define	_PATH_ISO3166		"/usr/share/misc/iso3166"
#define	_PATH_ZONEINFO		"/usr/share/zoneinfo"
#define	_PATH_LOCALTIME		"/etc/localtime"
#define	_PATH_DB		"/var/db/zoneinfo"
#define	_PATH_WALL_CMOS_CLOCK	"/etc/wall_cmos_clock"

#ifdef PATH_MAX
#define	SILLY_BUFFER_SIZE	2*PATH_MAX
#else
#warning "Somebody needs to fix this to dynamically size this buffer."
#define	SILLY_BUFFER_SIZE	2048
#endif

/* special return codes for `fire' actions */
#define DITEM_FAILURE           1

/* flags - returned in upper 16 bits of return status */
#define DITEM_LEAVE_MENU        (1 << 16)
#define DITEM_RECREATE          (1 << 18)

static char	path_zonetab[MAXPATHLEN], path_iso3166[MAXPATHLEN],
		path_zoneinfo[MAXPATHLEN], path_localtime[MAXPATHLEN],
		path_db[MAXPATHLEN], path_wall_cmos_clock[MAXPATHLEN];

static int reallydoit = 1;
static int reinstall = 0;
static char *chrootenv = NULL;

static void	usage(void);
static int	install_zoneinfo(const char *zoneinfo);
static int	install_zoneinfo_file(const char *zoneinfo_file);

#ifdef HAVE_DIALOG
/* for use in describing more exotic behaviors */
typedef struct dialogMenuItem {
	char *prompt;
	char *title;
	int (*fire)(struct dialogMenuItem *self);
	void *data;
} dialogMenuItem;

static int
xdialog_count_rows(const char *p)
{
	int rows = 0;

	while ((p = strchr(p, '\n')) != NULL) {
		p++;
		if (*p == '\0')
			break;
		rows++;
	}

	return (rows ? rows : 1);
}

static int
xdialog_count_columns(const char *p)
{
	int len;
	int max_len = 0;
	const char *q;

	for (; (q = strchr(p, '\n')) != NULL; p = q + 1) {
		len = q - p;
		max_len = MAX(max_len, len);
	}

	len = strlen(p);
	max_len = MAX(max_len, len);
	return (max_len);
}

static int
xdialog_menu(const char *title, const char *cprompt, int height, int width,
	     int menu_height, int item_no, dialogMenuItem *ditems)
{
	int i, result, choice = 0;
	DIALOG_LISTITEM *listitems;
	DIALOG_VARS save_vars;

	dlg_save_vars(&save_vars);

	/* initialize list items */
	listitems = dlg_calloc(DIALOG_LISTITEM, item_no + 1);
	assert_ptr(listitems, "xdialog_menu");
	for (i = 0; i < item_no; i++) {
		listitems[i].name = ditems[i].prompt;
		listitems[i].text = ditems[i].title;
	}

	/* calculate height */
	if (height < 0)
		height = xdialog_count_rows(cprompt) + menu_height + 4 + 2;
	if (height > LINES)
		height = LINES;

	/* calculate width */
	if (width < 0) {
		int tag_x = 0;

		for (i = 0; i < item_no; i++) {
			int j, l;

			l = strlen(listitems[i].name);
			for (j = 0; j < item_no; j++) {
				int k = strlen(listitems[j].text);
				tag_x = MAX(tag_x, l + k + 2);
			}
		}
		width = MAX(xdialog_count_columns(cprompt), title != NULL ?
		    xdialog_count_columns(title) : 0);
		width = MAX(width, tag_x + 4) + 4;
	}
	width = MAX(width, 24);
	if (width > COLS)
		width = COLS;

again:
	dialog_vars.default_item = listitems[choice].name;
	result = dlg_menu(title, cprompt, height, width,
	    menu_height, item_no, listitems, &choice, NULL);
	switch (result) {
	case DLG_EXIT_ESC:
		result = -1;
		break;
	case DLG_EXIT_OK:
		if (ditems[choice].fire != NULL) {
			int status;

			status = ditems[choice].fire(ditems + choice);
			if (status & DITEM_RECREATE) {
				dlg_clear();
				goto again;
			}
		}
		result = 0;
		break;
	case DLG_EXIT_CANCEL:
	default:
		result = 1;
		break;
	}

	free(listitems);
	dlg_restore_vars(&save_vars);
	return (result);
}

static int usedialog = 1;

static int	confirm_zone(const char *filename);
static int	continent_country_menu(dialogMenuItem *);
static int	set_zone_multi(dialogMenuItem *);
static int	set_zone_whole_country(dialogMenuItem *);
static int	set_zone_menu(dialogMenuItem *);
static int	set_zone_utc(void);

struct continent {
	dialogMenuItem *menu;
	int		nitems;
};

static struct continent	africa, america, antarctica, arctic, asia, atlantic;
static struct continent	australia, europe, indian, pacific, utc;

static struct continent_names {
	const char	*name;
	struct continent *continent;
} continent_names[] = {
	{ "Africa",	&africa },
	{ "America",	&america },
	{ "Antarctica",	&antarctica },
	{ "Arctic",	&arctic },
	{ "Asia",	&asia },
	{ "Atlantic",	&atlantic },
	{ "Australia",	&australia },
	{ "Europe",	&europe },
	{ "Indian",	&indian },
	{ "Pacific",	&pacific },
	{ "UTC",	&utc }
};

static struct continent_items {
	char		prompt[2];
	char		title[30];
} continent_items[] = {
	{ "1",	"Africa" },
	{ "2",	"America -- North and South" },
	{ "3",	"Antarctica" },
	{ "4",	"Arctic Ocean" },
	{ "5",	"Asia" },
	{ "6",	"Atlantic Ocean" },
	{ "7",	"Australia" },
	{ "8",	"Europe" },
	{ "9",	"Indian Ocean" },
	{ "0",	"Pacific Ocean" },
	{ "a",	"UTC" }
};

#define	NCONTINENTS	\
    (int)((sizeof(continent_items)) / (sizeof(continent_items[0])))
static dialogMenuItem continents[NCONTINENTS];

#define	OCEANP(x)	((x) == 3 || (x) == 5 || (x) == 8 || (x) == 9)

static int
continent_country_menu(dialogMenuItem *continent)
{
	char		title[64], prompt[64];
	struct continent *contp = continent->data;
	int		isocean = OCEANP(continent - continents);
	int		menulen;
	int		rv;

	if (strcmp(continent->title, "UTC") == 0)
		return (set_zone_utc());

	/* Short cut -- if there's only one country, don't post a menu. */
	if (contp->nitems == 1)
		return (contp->menu[0].fire(&contp->menu[0]));

	/* It's amazing how much good grammar really matters... */
	if (!isocean) {
		snprintf(title, sizeof(title), "Countries in %s",
		    continent->title);
		snprintf(prompt, sizeof(prompt), "Select a country or region");
	} else {
		snprintf(title, sizeof(title), "Islands and groups in the %s",
		    continent->title);
		snprintf(prompt, sizeof(prompt), "Select an island or group");
	}

	menulen = contp->nitems < 16 ? contp->nitems : 16;
	rv = xdialog_menu(title, prompt, -1, -1, menulen, contp->nitems,
	    contp->menu);
	if (rv == 0)
		return (DITEM_LEAVE_MENU);
	return (DITEM_RECREATE);
}

static struct continent *
find_continent(const char *name)
{
	int		i;

	for (i = 0; i < NCONTINENTS; i++)
		if (strcmp(name, continent_names[i].name) == 0)
			return (continent_names[i].continent);
	return (0);
}

struct country {
	char		*name;
	char		*tlc;
	int		nzones;
	char		*filename;	/* use iff nzones < 0 */
	struct continent *continent;	/* use iff nzones < 0 */
	TAILQ_HEAD(, zone) zones;	/* use iff nzones > 0 */
	dialogMenuItem	*submenu;	/* use iff nzones > 0 */
};

struct zone {
	TAILQ_ENTRY(zone) link;
	char		*descr;
	char		*filename;
	struct continent *continent;
};

/*
 * This is the easiest organization... we use ISO 3166 country codes,
 * of the two-letter variety, so we just size this array to suit.
 * Beats worrying about dynamic allocation.
 */
#define	NCOUNTRIES	(26 * 26)
static struct country countries[NCOUNTRIES];

#define	CODE2INT(s)	((s[0] - 'A') * 26 + (s[1] - 'A'))

/*
 * Read the ISO 3166 country code database in _PATH_ISO3166
 * (/usr/share/misc/iso3166).  On error, exit via err(3).
 */
static void
read_iso3166_table(void)
{
	FILE		*fp;
	struct country	*cp;
	size_t		len;
	char		*s, *t, *name;
	int		lineno;

	fp = fopen(path_iso3166, "r");
	if (!fp)
		err(1, "%s", path_iso3166);
	lineno = 0;

	while ((s = fgetln(fp, &len)) != NULL) {
		lineno++;
		if (s[len - 1] != '\n')
			errx(1, "%s:%d: invalid format", path_iso3166, lineno);
		s[len - 1] = '\0';
		if (s[0] == '#' || strspn(s, " \t") == len - 1)
			continue;

		/* Isolate the two-letter code. */
		t = strsep(&s, "\t");
		if (t == NULL || strlen(t) != 2)
			errx(1, "%s:%d: invalid format", path_iso3166, lineno);
		if (t[0] < 'A' || t[0] > 'Z' || t[1] < 'A' || t[1] > 'Z')
			errx(1, "%s:%d: invalid code `%s'", path_iso3166,
			    lineno, t);

		/* Now skip past the three-letter and numeric codes. */
		name = strsep(&s, "\t");	/* 3-let */
		if (name == NULL || strlen(name) != 3)
			errx(1, "%s:%d: invalid format", path_iso3166, lineno);
		name = strsep(&s, "\t");	/* numeric */
		if (name == NULL || strlen(name) != 3)
			errx(1, "%s:%d: invalid format", path_iso3166, lineno);

		name = s;

		cp = &countries[CODE2INT(t)];
		if (cp->name)
			errx(1, "%s:%d: country code `%s' multiply defined: %s",
			    path_iso3166, lineno, t, cp->name);
		cp->name = strdup(name);
		if (cp->name == NULL)
			errx(1, "malloc failed");
		cp->tlc = strdup(t);
		if (cp->tlc == NULL)
			errx(1, "malloc failed");
	}

	fclose(fp);
}

static void
add_zone_to_country(int lineno, const char *tlc, const char *descr,
    const char *file, struct continent *cont)
{
	struct zone	*zp;
	struct country	*cp;

	if (tlc[0] < 'A' || tlc[0] > 'Z' || tlc[1] < 'A' || tlc[1] > 'Z')
		errx(1, "%s:%d: country code `%s' invalid", path_zonetab,
		    lineno, tlc);

	cp = &countries[CODE2INT(tlc)];
	if (cp->name == 0)
		errx(1, "%s:%d: country code `%s' unknown", path_zonetab,
		    lineno, tlc);

	if (descr) {
		if (cp->nzones < 0)
			errx(1, "%s:%d: conflicting zone definition",
			    path_zonetab, lineno);

		zp = malloc(sizeof(*zp));
		if (zp == NULL)
			errx(1, "malloc(%zu)", sizeof(*zp));

		if (cp->nzones == 0)
			TAILQ_INIT(&cp->zones);

		zp->descr = strdup(descr);
		if (zp->descr == NULL)
			errx(1, "malloc failed");
		zp->filename = strdup(file);
		if (zp->filename == NULL)
			errx(1, "malloc failed");
		zp->continent = cont;
		TAILQ_INSERT_TAIL(&cp->zones, zp, link);
		cp->nzones++;
	} else {
		if (cp->nzones > 0)
			errx(1, "%s:%d: zone must have description",
			    path_zonetab, lineno);
		if (cp->nzones < 0)
			errx(1, "%s:%d: zone multiply defined",
			    path_zonetab, lineno);
		cp->nzones = -1;
		cp->filename = strdup(file);
		if (cp->filename == NULL)
			errx(1, "malloc failed");
		cp->continent = cont;
	}
}

/*
 * This comparison function intentionally sorts all of the null-named
 * ``countries''---i.e., the codes that don't correspond to a real
 * country---to the end.  Everything else is lexical by country name.
 */
static int
compare_countries(const void *xa, const void *xb)
{
	const struct country *a = xa, *b = xb;

	if (a->name == 0 && b->name == 0)
		return (0);
	if (a->name == 0 && b->name != 0)
		return (1);
	if (b->name == 0)
		return (-1);

	return (strcmp(a->name, b->name));
}

/*
 * This must be done AFTER all zone descriptions are read, since it breaks
 * CODE2INT().
 */
static void
sort_countries(void)
{

	qsort(countries, NCOUNTRIES, sizeof(countries[0]), compare_countries);
}

static void
read_zones(void)
{
	char		contbuf[16];
	FILE		*fp;
	struct continent *cont;
	size_t		len, contlen;
	char		*line, *tlc, *file, *descr, *p;
	int		lineno;

	fp = fopen(path_zonetab, "r");
	if (!fp)
		err(1, "%s", path_zonetab);
	lineno = 0;

	while ((line = fgetln(fp, &len)) != NULL) {
		lineno++;
		if (line[len - 1] != '\n')
			errx(1, "%s:%d: invalid format", path_zonetab, lineno);
		line[len - 1] = '\0';
		if (line[0] == '#')
			continue;

		tlc = strsep(&line, "\t");
		if (strlen(tlc) != 2)
			errx(1, "%s:%d: invalid country code `%s'",
			    path_zonetab, lineno, tlc);
		/* coord = */ strsep(&line, "\t");	 /* Unused */
		file = strsep(&line, "\t");
		/* get continent portion from continent/country */
		p = strchr(file, '/');
		if (p == NULL)
			errx(1, "%s:%d: invalid zone name `%s'", path_zonetab,
			    lineno, file);
		contlen = p - file + 1;		/* trailing nul */
		if (contlen > sizeof(contbuf))
			errx(1, "%s:%d: continent name in zone name `%s' too long",
			    path_zonetab, lineno, file);
		strlcpy(contbuf, file, contlen);
		cont = find_continent(contbuf);
		if (!cont)
			errx(1, "%s:%d: invalid region `%s'", path_zonetab,
			    lineno, contbuf);

		descr = (line != NULL && *line != '\0') ? line : NULL;

		add_zone_to_country(lineno, tlc, descr, file, cont);
	}
	fclose(fp);
}

static void
make_menus(void)
{
	struct country	*cp;
	struct zone	*zp, *zp2;
	struct continent *cont;
	dialogMenuItem	*dmi;
	int		i;

	/*
	 * First, count up all the countries in each continent/ocean.
	 * Be careful to count those countries which have multiple zones
	 * only once for each.  NB: some countries are in multiple
	 * continents/oceans.
	 */
	for (cp = countries; cp->name; cp++) {
		if (cp->nzones == 0)
			continue;
		if (cp->nzones < 0) {
			cp->continent->nitems++;
		} else {
			TAILQ_FOREACH(zp, &cp->zones, link) {
				cont = zp->continent;
				for (zp2 = TAILQ_FIRST(&cp->zones);
				    zp2->continent != cont;
				    zp2 = TAILQ_NEXT(zp2, link))
					;
				if (zp2 == zp)
					zp->continent->nitems++;
			}
		}
	}

	/*
	 * Now allocate memory for the country menus and initialize
	 * continent menus.  We set nitems back to zero so that we can
	 * use it for counting again when we actually build the menus.
	 */
	memset(continents, 0, sizeof(continents));
	for (i = 0; i < NCONTINENTS; i++) {
		continent_names[i].continent->menu =
		    malloc(sizeof(dialogMenuItem) *
		    continent_names[i].continent->nitems);
		if (continent_names[i].continent->menu == NULL)
			errx(1, "malloc for continent menu");
		continent_names[i].continent->nitems = 0;
		continents[i].prompt = continent_items[i].prompt;
		continents[i].title = continent_items[i].title;
		continents[i].fire = continent_country_menu;
		continents[i].data = continent_names[i].continent;
	}

	/*
	 * Now that memory is allocated, create the menu items for
	 * each continent.  For multiple-zone countries, also create
	 * the country's zone submenu.
	 */
	for (cp = countries; cp->name; cp++) {
		if (cp->nzones == 0)
			continue;
		if (cp->nzones < 0) {
			dmi = &cp->continent->menu[cp->continent->nitems];
			memset(dmi, 0, sizeof(*dmi));
			asprintf(&dmi->prompt, "%d", ++cp->continent->nitems);
			dmi->title = cp->name;
			dmi->fire = set_zone_whole_country;
			dmi->data = cp;
		} else {
			cp->submenu = malloc(cp->nzones * sizeof(*dmi));
			if (cp->submenu == 0)
				errx(1, "malloc for submenu");
			cp->nzones = 0;
			TAILQ_FOREACH(zp, &cp->zones, link) {
				cont = zp->continent;
				dmi = &cp->submenu[cp->nzones];
				memset(dmi, 0, sizeof(*dmi));
				asprintf(&dmi->prompt, "%d", ++cp->nzones);
				dmi->title = zp->descr;
				dmi->fire = set_zone_multi;
				dmi->data = zp;

				for (zp2 = TAILQ_FIRST(&cp->zones);
				    zp2->continent != cont;
				    zp2 = TAILQ_NEXT(zp2, link))
					;
				if (zp2 != zp)
					continue;

				dmi = &cont->menu[cont->nitems];
				memset(dmi, 0, sizeof(*dmi));
				asprintf(&dmi->prompt, "%d", ++cont->nitems);
				dmi->title = cp->name;
				dmi->fire = set_zone_menu;
				dmi->data = cp;
			}
		}
	}
}

static int
set_zone_menu(dialogMenuItem *dmi)
{
	char		title[64], prompt[64];
	struct country	*cp = dmi->data;
	int		menulen;
	int		rv;

	snprintf(title, sizeof(title), "%s Time Zones", cp->name);
	snprintf(prompt, sizeof(prompt),
	    "Select a zone which observes the same time as your locality.");
	menulen = cp->nzones < 16 ? cp->nzones : 16;
	rv = xdialog_menu(title, prompt, -1, -1, menulen, cp->nzones,
	    cp->submenu);
	if (rv != 0)
		return (DITEM_RECREATE);
	return (DITEM_LEAVE_MENU);
}

static int
set_zone_utc(void)
{
	if (!confirm_zone("UTC"))
		return (DITEM_FAILURE | DITEM_RECREATE);

	return (install_zoneinfo("UTC"));
}

static int
confirm_zone(const char *filename)
{
	char		title[64], prompt[64];
	time_t		t = time(0);
	struct tm	*tm;
	int		rv;

	setenv("TZ", filename, 1);
	tzset();
	tm = localtime(&t);

	snprintf(title, sizeof(title), "Confirmation");
	snprintf(prompt, sizeof(prompt),
	    "Does the abbreviation `%s' look reasonable?", tm->tm_zone);
	rv = !dialog_yesno(title, prompt, 5, 72);
	return (rv);
}

static int
set_zone_multi(dialogMenuItem *dmi)
{
	struct zone	*zp = dmi->data;
	int		rv;

	if (!confirm_zone(zp->filename))
		return (DITEM_FAILURE | DITEM_RECREATE);

	rv = install_zoneinfo(zp->filename);
	return (rv);
}

static int
set_zone_whole_country(dialogMenuItem *dmi)
{
	struct country	*cp = dmi->data;
	int		rv;

	if (!confirm_zone(cp->filename))
		return (DITEM_FAILURE | DITEM_RECREATE);

	rv = install_zoneinfo(cp->filename);
	return (rv);
}

#endif

static int
install_zoneinfo_file(const char *zoneinfo_file)
{
	char		buf[1024];
	char		title[64], prompt[SILLY_BUFFER_SIZE];
	struct stat	sb;
	ssize_t		len;
	int		fd1, fd2, copymode;

	if (lstat(path_localtime, &sb) < 0) {
		/* Nothing there yet... */
		copymode = 1;
	} else if (S_ISLNK(sb.st_mode))
		copymode = 0;
	else
		copymode = 1;

#ifdef VERBOSE
	snprintf(title, sizeof(title), "Info");
	if (copymode)
		snprintf(prompt, sizeof(prompt),
		    "Copying %s to %s", zoneinfo_file, path_localtime);
	else
		snprintf(prompt, sizeof(prompt),
		    "Creating symbolic link %s to %s",
		    path_localtime, zoneinfo_file);
#ifdef HAVE_DIALOG
	if (usedialog)
		dialog_msgbox(title, prompt, 8, 72, 1);
	else
#endif
		fprintf(stderr, "%s\n", prompt);
#endif

	if (reallydoit) {
		if (copymode) {
			fd1 = open(zoneinfo_file, O_RDONLY, 0);
			if (fd1 < 0) {
				snprintf(title, sizeof(title), "Error");
				snprintf(prompt, sizeof(prompt),
				    "Could not open %s: %s", zoneinfo_file,
				    strerror(errno));
#ifdef HAVE_DIALOG
				if (usedialog)
					dialog_msgbox(title, prompt, 8, 72, 1);
				else
#endif
					fprintf(stderr, "%s\n", prompt);
				return (DITEM_FAILURE | DITEM_RECREATE);
			}

			if (unlink(path_localtime) < 0 && errno != ENOENT) {
				snprintf(prompt, sizeof(prompt),
				    "Could not delete %s: %s",
				    path_localtime, strerror(errno));
#ifdef HAVE_DIALOG
				if (usedialog) {
					snprintf(title, sizeof(title), "Error");
					dialog_msgbox(title, prompt, 8, 72, 1);
				} else
#endif
					fprintf(stderr, "%s\n", prompt);
				return (DITEM_FAILURE | DITEM_RECREATE);
			}

			fd2 = open(path_localtime, O_CREAT | O_EXCL | O_WRONLY,
			    S_IRUSR | S_IRGRP | S_IROTH);
			if (fd2 < 0) {
				snprintf(title, sizeof(title), "Error");
				snprintf(prompt, sizeof(prompt),
				    "Could not open %s: %s",
				    path_localtime, strerror(errno));
#ifdef HAVE_DIALOG
				if (usedialog)
					dialog_msgbox(title, prompt, 8, 72, 1);
				else
#endif
					fprintf(stderr, "%s\n", prompt);
				return (DITEM_FAILURE | DITEM_RECREATE);
			}

			while ((len = read(fd1, buf, sizeof(buf))) > 0)
				if ((len = write(fd2, buf, len)) < 0)
					break;

			if (len == -1) {
				snprintf(title, sizeof(title), "Error");
				snprintf(prompt, sizeof(prompt),
				    "Error copying %s to %s %s", zoneinfo_file,
				    path_localtime, strerror(errno));
#ifdef HAVE_DIALOG
				if (usedialog)
					dialog_msgbox(title, prompt, 8, 72, 1);
				else
#endif
					fprintf(stderr, "%s\n", prompt);
				/* Better to leave none than a corrupt one. */
				unlink(path_localtime);
				return (DITEM_FAILURE | DITEM_RECREATE);
			}
			close(fd1);
			close(fd2);
		} else {
			if (access(zoneinfo_file, R_OK) != 0) {
				snprintf(title, sizeof(title), "Error");
				snprintf(prompt, sizeof(prompt),
				    "Cannot access %s: %s", zoneinfo_file,
				    strerror(errno));
#ifdef HAVE_DIALOG
				if (usedialog)
					dialog_msgbox(title, prompt, 8, 72, 1);
				else
#endif
					fprintf(stderr, "%s\n", prompt);
				return (DITEM_FAILURE | DITEM_RECREATE);
			}
			if (unlink(path_localtime) < 0 && errno != ENOENT) {
				snprintf(prompt, sizeof(prompt),
				    "Could not delete %s: %s",
				    path_localtime, strerror(errno));
#ifdef HAVE_DIALOG
				if (usedialog) {
					snprintf(title, sizeof(title), "Error");
					dialog_msgbox(title, prompt, 8, 72, 1);
				} else
#endif
					fprintf(stderr, "%s\n", prompt);
				return (DITEM_FAILURE | DITEM_RECREATE);
			}
			if (symlink(zoneinfo_file, path_localtime) < 0) {
				snprintf(title, sizeof(title), "Error");
				snprintf(prompt, sizeof(prompt),
				    "Cannot create symbolic link %s to %s: %s",
				    path_localtime, zoneinfo_file,
				    strerror(errno));
#ifdef HAVE_DIALOG
				if (usedialog)
					dialog_msgbox(title, prompt, 8, 72, 1);
				else
#endif
					fprintf(stderr, "%s\n", prompt);
				return (DITEM_FAILURE | DITEM_RECREATE);
			}
		}

#ifdef VERBOSE
		snprintf(title, sizeof(title), "Done");
		if (copymode)
			snprintf(prompt, sizeof(prompt),
			    "Copied timezone file from %s to %s",
			    zoneinfo_file, path_localtime);
		else
			snprintf(prompt, sizeof(prompt),
			    "Created symbolic link from %s to %s",
			    zoneinfo_file, path_localtime);
#ifdef HAVE_DIALOG
		if (usedialog)
			dialog_msgbox(title, prompt, 8, 72, 1);
		else
#endif
			fprintf(stderr, "%s\n", prompt);
#endif
	} /* reallydoit */

	return (DITEM_LEAVE_MENU);
}

static int
install_zoneinfo(const char *zoneinfo)
{
	int		rv;
	FILE		*f;
	char		path_zoneinfo_file[MAXPATHLEN];

	if ((size_t)snprintf(path_zoneinfo_file, sizeof(path_zoneinfo_file),
	    "%s/%s", path_zoneinfo, zoneinfo) >= sizeof(path_zoneinfo_file))
		errx(1, "%s/%s name too long", path_zoneinfo, zoneinfo);
	rv = install_zoneinfo_file(path_zoneinfo_file);

	/* Save knowledge for later */
	if (reallydoit && (rv & DITEM_FAILURE) == 0) {
		if ((f = fopen(path_db, "w")) != NULL) {
			fprintf(f, "%s\n", zoneinfo);
			fclose(f);
		}
	}

	return (rv);
}

static void
usage(void)
{

	fprintf(stderr, "usage: tzsetup [-nrs] [-C chroot_directory]"
	    " [zoneinfo_file | zoneinfo_name]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
#ifdef HAVE_DIALOG
	char		title[64], prompt[128];
	int		fd;
#endif
	int		c, rv, skiputc;
	char		vm_guest[16] = "";
	size_t		len = sizeof(vm_guest);

	skiputc = 0;

	/* Default skiputc to 1 for VM guests */
	if (sysctlbyname("kern.vm_guest", vm_guest, &len, NULL, 0) == 0 &&
	    strcmp(vm_guest, "none") != 0)
		skiputc = 1;

	while ((c = getopt(argc, argv, "C:nrs")) != -1) {
		switch(c) {
		case 'C':
			chrootenv = optarg;
			break;
		case 'n':
			reallydoit = 0;
			break;
		case 'r':
			reinstall = 1;
#ifdef HAVE_DIALOG
			usedialog = 0;
#endif
			break;
		case 's':
			skiputc = 1;
			break;
		default:
			usage();
		}
	}

	if (argc - optind > 1)
		usage();

	if (chrootenv == NULL) {
		strcpy(path_zonetab, _PATH_ZONETAB);
		strcpy(path_iso3166, _PATH_ISO3166);
		strcpy(path_zoneinfo, _PATH_ZONEINFO);
		strcpy(path_localtime, _PATH_LOCALTIME);
		strcpy(path_db, _PATH_DB);
		strcpy(path_wall_cmos_clock, _PATH_WALL_CMOS_CLOCK);
	} else {
		sprintf(path_zonetab, "%s/%s", chrootenv, _PATH_ZONETAB);
		sprintf(path_iso3166, "%s/%s", chrootenv, _PATH_ISO3166);
		sprintf(path_zoneinfo, "%s/%s", chrootenv, _PATH_ZONEINFO);
		sprintf(path_localtime, "%s/%s", chrootenv, _PATH_LOCALTIME);
		sprintf(path_db, "%s/%s", chrootenv, _PATH_DB);
		sprintf(path_wall_cmos_clock, "%s/%s", chrootenv,
		    _PATH_WALL_CMOS_CLOCK);
	}

	/* Override the user-supplied umask. */
	(void)umask(S_IWGRP | S_IWOTH);

	if (reinstall == 1) {
		FILE *f;
		char zoneinfo[MAXPATHLEN];

		if ((f = fopen(path_db, "r")) != NULL) {
			if (fgets(zoneinfo, sizeof(zoneinfo), f) != NULL) {
				zoneinfo[sizeof(zoneinfo) - 1] = 0;
				if (strlen(zoneinfo) > 0) {
					zoneinfo[strlen(zoneinfo) - 1] = 0;
					rv = install_zoneinfo(zoneinfo);
					exit(rv & ~DITEM_LEAVE_MENU);
				}
				errx(1, "Error reading %s.\n", path_db);
			}
			fclose(f);
			errx(1,
			    "Unable to determine earlier installed zoneinfo "
			    "name. Check %s", path_db);
		}
		errx(1, "Cannot open %s for reading. Does it exist?", path_db);
	}

	/*
	 * If the arguments on the command-line do not specify a file,
	 * then interpret it as a zoneinfo name
	 */
	if (optind == argc - 1) {
		struct stat sb;

		if (stat(argv[optind], &sb) != 0) {
#ifdef HAVE_DIALOG
			usedialog = 0;
#endif
			rv = install_zoneinfo(argv[optind]);
			exit(rv & ~DITEM_LEAVE_MENU);
		}
		/* FALLTHROUGH */
	}
#ifdef HAVE_DIALOG

	read_iso3166_table();
	read_zones();
	sort_countries();
	make_menus();

	init_dialog(stdin, stdout);
	if (skiputc == 0) {
		DIALOG_VARS save_vars;
		int yesno;

		snprintf(title, sizeof(title),
		    "Select local or UTC (Greenwich Mean Time) clock");
		snprintf(prompt, sizeof(prompt),
		    "Is this machine's CMOS clock set to UTC?  "
		    "If it is set to local time,\n"
		    "or you don't know, please choose NO here!");
		dlg_save_vars(&save_vars);
#if !defined(__sparc64__)
		dialog_vars.defaultno = TRUE;
#endif
		yesno = dialog_yesno(title, prompt, 7, 73);
		dlg_restore_vars(&save_vars);
		if (!yesno) {
			if (reallydoit)
				unlink(path_wall_cmos_clock);
		} else {
			if (reallydoit) {
				fd = open(path_wall_cmos_clock,
				    O_WRONLY | O_CREAT | O_TRUNC,
				    S_IRUSR | S_IRGRP | S_IROTH);
				if (fd < 0) {
					end_dialog();
					err(1, "create %s",
					    path_wall_cmos_clock);
				}
				close(fd);
			}
		}
		dlg_clear();
	}
	if (optind == argc - 1) {
		snprintf(title, sizeof(title), "Default timezone provided");
		snprintf(prompt, sizeof(prompt),
		    "\nUse the default `%s' zone?", argv[optind]);
		if (!dialog_yesno(title, prompt, 7, 72)) {
			rv = install_zoneinfo_file(argv[optind]);
			dlg_clear();
			end_dialog();
			exit(rv & ~DITEM_LEAVE_MENU);
		}
		dlg_clear();
	}
	snprintf(title, sizeof(title), "Time Zone Selector");
	snprintf(prompt, sizeof(prompt), "Select a region");
	xdialog_menu(title, prompt, -1, -1, NCONTINENTS, NCONTINENTS,
	    continents);

	dlg_clear();
	end_dialog();
#else
	usage();
#endif
	return (0);
}
