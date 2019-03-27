/*-
 * Copyright (c) 2017 Netflix, Inc.
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

#include <sys/param.h>
#include <ctype.h>
#include <devinfo.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

/* options descriptor */
static struct option longopts[] = {
	{ "all",		no_argument,		NULL,	'a' },
	{ "dump",		no_argument,		NULL,	'd' },
	{ "hints",		required_argument,	NULL,	'h' },
	{ "nomatch",		required_argument,	NULL,	'p' },
	{ "unbound",		no_argument,		NULL,	'u' },
	{ "verbose",		no_argument,		NULL,	'v' },
	{ NULL,			0,			NULL,	0 }
};

#define	DEVMATCH_MAX_HITS 256

static int all_flag;
static int dump_flag;
static char *linker_hints;
static char *nomatch_str;
static int unbound_flag;
static int verbose_flag;

static void *hints;
static void *hints_end;
static struct devinfo_dev *root;

static void *
read_hints(const char *fn, size_t *len)
{
	void *h;
	int fd;
	struct stat sb;

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return NULL;
		err(1, "Can't open %s for reading", fn);
	}
	if (fstat(fd, &sb) != 0)
		err(1, "Can't fstat %s\n", fn);
	h = malloc(sb.st_size);
	if (h == NULL)
		err(1, "not enough space to read hints file of %ju bytes", (uintmax_t)sb.st_size);
	if (read(fd, h, sb.st_size) != sb.st_size)
		err(1, "Can't read in %ju bytes from %s", (uintmax_t)sb.st_size, fn);
	close(fd);
	*len = sb.st_size;
	return h;
}

static void
read_linker_hints(void)
{
	char fn[MAXPATHLEN];
	char *modpath, *p, *q;
	size_t buflen, len;

	if (linker_hints == NULL) {
		if (sysctlbyname("kern.module_path", NULL, &buflen, NULL, 0) < 0)
			errx(1, "Can't find kernel module path.");
		modpath = malloc(buflen);
		if (modpath == NULL)
			err(1, "Can't get memory for modpath.");
		if (sysctlbyname("kern.module_path", modpath, &buflen, NULL, 0) < 0)
			errx(1, "Can't find kernel module path.");
		p = modpath;
		while ((q = strsep(&p, ";")) != NULL) {
			snprintf(fn, sizeof(fn), "%s/linker.hints", q);
			hints = read_hints(fn, &len);
			if (hints == NULL)
				continue;
			break;
		}
		if (q == NULL)
			errx(1, "Can't read linker hints file.");
	} else {
		hints = read_hints(linker_hints, &len);
		if (hints == NULL)
			err(1, "Can't open %s for reading", fn);
	}

	if (*(int *)(intptr_t)hints != LINKER_HINTS_VERSION) {
		warnx("Linker hints version %d doesn't match expected %d.",
		    *(int *)(intptr_t)hints, LINKER_HINTS_VERSION);
		free(hints);
		hints = NULL;
	}
	if (hints != NULL)
		hints_end = (void *)((intptr_t)hints + (intptr_t)len);
}

static int
getint(void **ptr)
{
	int *p = *ptr;
	int rv;

	p = (int *)roundup2((intptr_t)p, sizeof(int));
	rv = *p++;
	*ptr = p;
	return rv;
}

static void
getstr(void **ptr, char *val)
{
	int *p = *ptr;
	char *c = (char *)p;
	int len = *(uint8_t *)c;

	memcpy(val, c + 1, len);
	val[len] = 0;
	c += len + 1;
	*ptr = (void *)c;
}

static int
pnpval_as_int(const char *val, const char *pnpinfo)
{
	int rv;
	char key[256];
	char *cp;

	if (pnpinfo == NULL)
		return -1;

	cp = strchr(val, ';');
	key[0] = ' ';
	if (cp == NULL)
		strlcpy(key + 1, val, sizeof(key) - 1);
	else {
		memcpy(key + 1, val, cp - val);
		key[cp - val + 1] = '\0';
	}
	strlcat(key, "=", sizeof(key));
	if (strncmp(key + 1, pnpinfo, strlen(key + 1)) == 0)
		rv = strtol(pnpinfo + strlen(key + 1), NULL, 0);
	else {
		cp = strstr(pnpinfo, key);
		if (cp == NULL)
			rv = -1;
		else
			rv = strtol(cp + strlen(key), NULL, 0);
	}
	return rv;
}

static void
quoted_strcpy(char *dst, const char *src)
{
	char q = ' ';

	if (*src == '\'' || *src == '"')
		q = *src++;
	while (*src && *src != q)
		*dst++ = *src++; // XXX backtick quoting
	*dst++ = '\0';
	// XXX overflow
}

static char *
pnpval_as_str(const char *val, const char *pnpinfo)
{
	static char retval[256];
	char key[256];
	char *cp;

	if (pnpinfo == NULL) {
		*retval = '\0';
		return retval;
	}

	cp = strchr(val, ';');
	key[0] = ' ';
	if (cp == NULL)
		strlcpy(key + 1, val, sizeof(key) - 1);
	else {
		memcpy(key + 1, val, cp - val);
		key[cp - val + 1] = '\0';
	}
	strlcat(key, "=", sizeof(key));
	if (strncmp(key + 1, pnpinfo, strlen(key + 1)) == 0)
		quoted_strcpy(retval, pnpinfo + strlen(key + 1));
	else {
		cp = strstr(pnpinfo, key);
		if (cp == NULL)
			strcpy(retval, "MISSING");
		else
			quoted_strcpy(retval, cp + strlen(key));
	}
	return retval;
}

static void
search_hints(const char *bus, const char *dev, const char *pnpinfo)
{
	char val1[256], val2[256];
	int ival, len, ents, i, notme, mask, bit, v, found;
	void *ptr, *walker;
	char *lastmod = NULL, *cp, *s;

	walker = hints;
	getint(&walker);
	found = 0;
	if (verbose_flag)
		printf("Searching bus %s dev %s for pnpinfo %s\n",
		    bus, dev, pnpinfo);
	while (walker < hints_end) {
		len = getint(&walker);
		ival = getint(&walker);
		ptr = walker;
		switch (ival) {
		case MDT_VERSION:
			getstr(&ptr, val1);
			ival = getint(&ptr);
			getstr(&ptr, val2);
			if (dump_flag || verbose_flag)
				printf("Version: if %s.%d kmod %s\n", val1, ival, val2);
			break;
		case MDT_MODULE:
			getstr(&ptr, val1);
			getstr(&ptr, val2);
			if (lastmod)
				free(lastmod);
			lastmod = strdup(val2);
			if (dump_flag || verbose_flag)
				printf("Module %s in %s\n", val1, val2);
			break;
		case MDT_PNP_INFO:
			if (!dump_flag && !unbound_flag && lastmod && strcmp(lastmod, "kernel") == 0)
				break;
			getstr(&ptr, val1);
			getstr(&ptr, val2);
			ents = getint(&ptr);
			if (dump_flag || verbose_flag)
				printf("PNP info for bus %s format %s %d entries (%s)\n",
				    val1, val2, ents, lastmod);
			if (strcmp(val1, "usb") == 0) {
				if (verbose_flag)
					printf("Treating usb as uhub -- bug in source table still?\n");
				strcpy(val1, "uhub");
			}
			if (bus && strcmp(val1, bus) != 0) {
				if (verbose_flag)
					printf("Skipped because table for bus %s, looking for %s\n",
					    val1, bus);
				break;
			}
			for (i = 0; i < ents; i++) {
				if (verbose_flag)
					printf("---------- Entry %d ----------\n", i);
				if (dump_flag)
					printf("   ");
				cp = val2;
				notme = 0;
				mask = -1;
				bit = -1;
				do {
					switch (*cp) {
						/* All integer fields */
					case 'I':
					case 'J':
					case 'G':
					case 'L':
					case 'M':
						ival = getint(&ptr);
						if (dump_flag) {
							printf("%#x:", ival);
							break;
						}
						if (bit >= 0 && ((1 << bit) & mask) == 0)
							break;
						v = pnpval_as_int(cp + 2, pnpinfo);
						if (verbose_flag)
							printf("Matching %s (%c) table=%#x tomatch=%#x\n",
							    cp + 2, *cp, v, ival);
						switch (*cp) {
						case 'J':
							if (ival == -1)
								break;
							/*FALLTHROUGH*/
						case 'I':
							if (v != ival)
								notme++;
							break;
						case 'G':
							if (v < ival)
								notme++;
							break;
						case 'L':
							if (v > ival)
								notme++;
							break;
						case 'M':
							mask = ival;
							break;
						}
						break;
						/* String fields */
					case 'D':
					case 'Z':
						getstr(&ptr, val1);
						if (dump_flag) {
							printf("'%s':", val1);
							break;
						}
						if (*cp == 'D')
							break;
						s = pnpval_as_str(cp + 2, pnpinfo);
						if (strcmp(s, val1) != 0)
							notme++;
						break;
						/* Key override fields, required to be last in the string */
					case 'T':
						/*
						 * This is imperfect and only does one key and will be redone
						 * to be more general for multiple keys. Currently, nothing
						 * does that.
						 */
						if (dump_flag)				/* No per-row data stored */
							break;
						if (cp[strlen(cp) - 1] == ';')		/* Skip required ; at end */
							cp[strlen(cp) - 1] = '\0';	/* in case it's not there */
						if ((s = strstr(pnpinfo, cp + 2)) == NULL)
							notme++;
						else if (s > pnpinfo && s[-1] != ' ')
							notme++;
						break;
					default:
						fprintf(stderr, "Unknown field type %c\n:", *cp);
						break;
					}
					bit++;
					cp = strchr(cp, ';');
					if (cp)
						cp++;
				} while (cp && *cp);
				if (dump_flag)
					printf("\n");
				else if (!notme) {
					if (!unbound_flag) {
						if (all_flag)
							printf("%s: %s", *dev ? dev : "unattached", lastmod);
						else
							printf("%s\n", lastmod);
						if (verbose_flag)
							printf("Matches --- %s ---\n", lastmod);
					}
					found++;
				}
			}
			break;
		default:
			if (dump_flag)
				printf("Unknown Type %d len %d\n", ival, len);
			break;
		}
		walker = (void *)(len - sizeof(int) + (intptr_t)walker);
	}
	if (unbound_flag && found == 0 && *pnpinfo) {
		if (verbose_flag)
			printf("------------------------- ");
		printf("%s on %s pnpinfo %s", *dev ? dev : "unattached", bus, pnpinfo);
		if (verbose_flag)
			printf(" -------------------------");
		printf("\n");
	}
	free(lastmod);
}

static int
find_unmatched(struct devinfo_dev *dev, void *arg)
{
	struct devinfo_dev *parent;
	char *bus, *p;

	do {
		if (!all_flag && dev->dd_name[0] != '\0')
			break;
		if (!(dev->dd_flags & DF_ENABLED))
			break;
		if (dev->dd_flags & DF_ATTACHED_ONCE)
			break;
		parent = devinfo_handle_to_device(dev->dd_parent);
		bus = strdup(parent->dd_name);
		p = bus + strlen(bus) - 1;
		while (p >= bus && isdigit(*p))
			p--;
		*++p = '\0';
		if (verbose_flag)
			printf("Searching %s %s bus at %s for pnpinfo %s\n",
			    dev->dd_name, bus, dev->dd_location, dev->dd_pnpinfo);
		search_hints(bus, dev->dd_name, dev->dd_pnpinfo);
		free(bus);
	} while (0);

	return (devinfo_foreach_device_child(dev, find_unmatched, arg));
}

struct exact_info
{
	const char *bus;
	const char *loc;
	struct devinfo_dev *dev;
};

/*
 * Look for the exact location specified by the nomatch event.  The
 * loc and pnpinfo run together to get the string we're looking for,
 * so we have to synthesize the same thing that subr_bus.c is
 * generating in devnomatch/devaddq to do the string comparison.
 */
static int
find_exact_dev(struct devinfo_dev *dev, void *arg)
{
	struct devinfo_dev *parent;
	char *loc;
	struct exact_info *info;

	info = arg;
	do {
		if (info->dev != NULL)
			break;
		if (!(dev->dd_flags & DF_ENABLED))
			break;
		parent = devinfo_handle_to_device(dev->dd_parent);
		if (strcmp(info->bus, parent->dd_name) != 0)
			break;
		asprintf(&loc, "%s %s", parent->dd_pnpinfo,
		    parent->dd_location);
		if (strcmp(loc, info->loc) == 0)
			info->dev = dev;
		free(loc);
	} while (0);

	return (devinfo_foreach_device_child(dev, find_exact_dev, arg));
}

static void
find_nomatch(char *nomatch)
{
	char *bus, *pnpinfo, *tmp, *busnameunit;
	struct exact_info info;

	/*
	 * Find our bus name. It will include the unit number. We have to search
	 * backwards to avoid false positive for any PNP string that has ' on '
	 * in them, which would come earlier in the string. Like if there were
	 * an 'Old Bard' ethernet card made by 'Stratford on Avon Hardware' or
	 * something silly like that.
	 */
	tmp = nomatch + strlen(nomatch) - 4;
	while (tmp > nomatch && strncmp(tmp, " on ", 4) != 0)
		tmp--;
	if (tmp == nomatch)
		errx(1, "No bus found in nomatch string: '%s'", nomatch);
	bus = tmp + 4;
	*tmp = '\0';
	busnameunit = strdup(bus);
	if (busnameunit == NULL)
		errx(1, "Can't allocate memory for strings");
	tmp = bus + strlen(bus) - 1;
	while (tmp > bus && isdigit(*tmp))
		tmp--;
	*++tmp = '\0';

	/*
	 * Note: the NOMATCH events place both the bus location as well as the
	 * pnp info after the 'at' and we don't know where one stops and the
	 * other begins, so we pass the whole thing to our search routine.
	 */
	if (*nomatch == '?')
		nomatch++;
	if (strncmp(nomatch, " at ", 4) != 0)
		errx(1, "Malformed NOMATCH string: '%s'", nomatch);
	pnpinfo = nomatch + 4;

	/*
	 * See if we can find the devinfo_dev for this device. If we
	 * can, and it's been attached before, we should filter it out
	 * so that a kldunload foo doesn't cause an immediate reload.
	 */
	info.loc = pnpinfo;
	info.bus = busnameunit;
	info.dev = NULL;
	devinfo_foreach_device_child(root, find_exact_dev, (void *)&info);
	if (info.dev != NULL && info.dev->dd_flags & DF_ATTACHED_ONCE)
		exit(0);
	search_hints(bus, "", pnpinfo);

	exit(0);
}

static void
usage(void)
{

	errx(1, "devmatch [-adv] [-p nomatch] [-h linker-hints]");
}

int
main(int argc, char **argv)
{
	int ch;

	while ((ch = getopt_long(argc, argv, "adh:p:uv",
		    longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			all_flag++;
			break;
		case 'd':
			dump_flag++;
			break;
		case 'h':
			linker_hints = optarg;
			break;
		case 'p':
			nomatch_str = optarg;
			break;
		case 'u':
			unbound_flag++;
			break;
		case 'v':
			verbose_flag++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc >= 1)
		usage();

	read_linker_hints();
	if (dump_flag) {
		search_hints(NULL, NULL, NULL);
		exit(0);
	}

	if (devinfo_init())
		err(1, "devinfo_init");
	if ((root = devinfo_handle_to_device(DEVINFO_ROOT_DEVICE)) == NULL)
		errx(1, "can't find root device");
	if (nomatch_str != NULL)
		find_nomatch(nomatch_str);
	else
		devinfo_foreach_device_child(root, find_unmatched, (void *)0);
	devinfo_free();
}
