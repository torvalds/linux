/*	$OpenBSD: pfctl_osfp.c,v 1.14 2006/04/08 02:13:14 ray Exp $ */

/*
 * Copyright (c) 2003 Mike Frantzen <frantzen@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/pfvar.h>

#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pfctl_parser.h"
#include "pfctl.h"

#ifndef MIN
# define MIN(a,b)	(((a) < (b)) ? (a) : (b))
#endif /* MIN */
#ifndef MAX
# define MAX(a,b)	(((a) > (b)) ? (a) : (b))
#endif /* MAX */


#if 0
# define DEBUG(fp, str, v...) \
	fprintf(stderr, "%s:%s:%s " str "\n", (fp)->fp_os.fp_class_nm, \
	    (fp)->fp_os.fp_version_nm, (fp)->fp_os.fp_subtype_nm , ## v);
#else
# define DEBUG(fp, str, v...) ((void)0)
#endif


struct name_entry;
LIST_HEAD(name_list, name_entry);
struct name_entry {
	LIST_ENTRY(name_entry)	nm_entry;
	int			nm_num;
	char			nm_name[PF_OSFP_LEN];

	struct name_list	nm_sublist;
	int			nm_sublist_num;
};
static struct name_list classes = LIST_HEAD_INITIALIZER(&classes);
static int class_count;
static int fingerprint_count;

void			 add_fingerprint(int, int, struct pf_osfp_ioctl *);
struct name_entry	*fingerprint_name_entry(struct name_list *, char *);
void			 pfctl_flush_my_fingerprints(struct name_list *);
char			*get_field(char **, size_t *, int *);
int			 get_int(char **, size_t *, int *, int *, const char *,
			     int, int, const char *, int);
int			 get_str(char **, size_t *, char **, const char *, int,
			     const char *, int);
int			 get_tcpopts(const char *, int, const char *,
			    pf_tcpopts_t *, int *, int *, int *, int *, int *,
			    int *);
void			 import_fingerprint(struct pf_osfp_ioctl *);
const char		*print_ioctl(struct pf_osfp_ioctl *);
void			 print_name_list(int, struct name_list *, const char *);
void			 sort_name_list(int, struct name_list *);
struct name_entry	*lookup_name_list(struct name_list *, const char *);

/* Load fingerprints from a file */
int
pfctl_file_fingerprints(int dev, int opts, const char *fp_filename)
{
	FILE *in;
	char *line;
	size_t len;
	int i, lineno = 0;
	int window, w_mod, ttl, df, psize, p_mod, mss, mss_mod, wscale,
	    wscale_mod, optcnt, ts0;
	pf_tcpopts_t packed_tcpopts;
	char *class, *version, *subtype, *desc, *tcpopts;
	struct pf_osfp_ioctl fp;

	pfctl_flush_my_fingerprints(&classes);

	if ((in = pfctl_fopen(fp_filename, "r")) == NULL) {
		warn("%s", fp_filename);
		return (1);
	}
	class = version = subtype = desc = tcpopts = NULL;

	if ((opts & PF_OPT_NOACTION) == 0)
		pfctl_clear_fingerprints(dev, opts);

	while ((line = fgetln(in, &len)) != NULL) {
		lineno++;
		if (class)
			free(class);
		if (version)
			free(version);
		if (subtype)
			free(subtype);
		if (desc)
			free(desc);
		if (tcpopts)
			free(tcpopts);
		class = version = subtype = desc = tcpopts = NULL;
		memset(&fp, 0, sizeof(fp));

		/* Chop off comment */
		for (i = 0; i < len; i++)
			if (line[i] == '#') {
				len = i;
				break;
			}
		/* Chop off whitespace */
		while (len > 0 && isspace(line[len - 1]))
			len--;
		while (len > 0 && isspace(line[0])) {
			len--;
			line++;
		}
		if (len == 0)
			continue;

#define T_DC	0x01	/* Allow don't care */
#define T_MSS	0x02	/* Allow MSS multiple */
#define T_MTU	0x04	/* Allow MTU multiple */
#define T_MOD	0x08	/* Allow modulus */

#define GET_INT(v, mod, n, ty, mx) \
	get_int(&line, &len, &v, mod, n, ty, mx, fp_filename, lineno)
#define GET_STR(v, n, mn) \
	get_str(&line, &len, &v, n, mn, fp_filename, lineno)

		if (GET_INT(window, &w_mod, "window size", T_DC|T_MSS|T_MTU|
		    T_MOD, 0xffff) ||
		    GET_INT(ttl, NULL, "ttl", 0, 0xff) ||
		    GET_INT(df, NULL, "don't fragment frag", 0, 1) ||
		    GET_INT(psize, &p_mod, "overall packet size", T_MOD|T_DC,
		    8192) ||
		    GET_STR(tcpopts, "TCP Options", 1) ||
		    GET_STR(class, "OS class", 1) ||
		    GET_STR(version, "OS version", 0) ||
		    GET_STR(subtype, "OS subtype", 0) ||
		    GET_STR(desc, "OS description", 2))
			continue;
		if (get_tcpopts(fp_filename, lineno, tcpopts, &packed_tcpopts,
		    &optcnt, &mss, &mss_mod, &wscale, &wscale_mod, &ts0))
			continue;
		if (len != 0) {
			fprintf(stderr, "%s:%d excess field\n", fp_filename,
			    lineno);
			continue;
		}

		fp.fp_ttl = ttl;
		if (df)
			fp.fp_flags |= PF_OSFP_DF;
		switch (w_mod) {
		case 0:
			break;
		case T_DC:
			fp.fp_flags |= PF_OSFP_WSIZE_DC;
			break;
		case T_MSS:
			fp.fp_flags |= PF_OSFP_WSIZE_MSS;
			break;
		case T_MTU:
			fp.fp_flags |= PF_OSFP_WSIZE_MTU;
			break;
		case T_MOD:
			fp.fp_flags |= PF_OSFP_WSIZE_MOD;
			break;
		}
		fp.fp_wsize = window;

		switch (p_mod) {
		case T_DC:
			fp.fp_flags |= PF_OSFP_PSIZE_DC;
			break;
		case T_MOD:
			fp.fp_flags |= PF_OSFP_PSIZE_MOD;
		}
		fp.fp_psize = psize;


		switch (wscale_mod) {
		case T_DC:
			fp.fp_flags |= PF_OSFP_WSCALE_DC;
			break;
		case T_MOD:
			fp.fp_flags |= PF_OSFP_WSCALE_MOD;
		}
		fp.fp_wscale = wscale;

		switch (mss_mod) {
		case T_DC:
			fp.fp_flags |= PF_OSFP_MSS_DC;
			break;
		case T_MOD:
			fp.fp_flags |= PF_OSFP_MSS_MOD;
			break;
		}
		fp.fp_mss = mss;

		fp.fp_tcpopts = packed_tcpopts;
		fp.fp_optcnt = optcnt;
		if (ts0)
			fp.fp_flags |= PF_OSFP_TS0;

		if (class[0] == '@')
			fp.fp_os.fp_enflags |= PF_OSFP_GENERIC;
		if (class[0] == '*')
			fp.fp_os.fp_enflags |= PF_OSFP_NODETAIL;

		if (class[0] == '@' || class[0] == '*')
			strlcpy(fp.fp_os.fp_class_nm, class + 1,
			    sizeof(fp.fp_os.fp_class_nm));
		else
			strlcpy(fp.fp_os.fp_class_nm, class,
			    sizeof(fp.fp_os.fp_class_nm));
		strlcpy(fp.fp_os.fp_version_nm, version,
		    sizeof(fp.fp_os.fp_version_nm));
		strlcpy(fp.fp_os.fp_subtype_nm, subtype,
		    sizeof(fp.fp_os.fp_subtype_nm));

		add_fingerprint(dev, opts, &fp);

		fp.fp_flags |= (PF_OSFP_DF | PF_OSFP_INET6);
		fp.fp_psize += sizeof(struct ip6_hdr) - sizeof(struct ip);
		add_fingerprint(dev, opts, &fp);
	}

	if (class)
		free(class);
	if (version)
		free(version);
	if (subtype)
		free(subtype);
	if (desc)
		free(desc);
	if (tcpopts)
		free(tcpopts);

	fclose(in);

	if (opts & PF_OPT_VERBOSE2)
		printf("Loaded %d passive OS fingerprints\n",
		    fingerprint_count);
	return (0);
}

/* flush the kernel's fingerprints */
void
pfctl_clear_fingerprints(int dev, int opts)
{
	if (ioctl(dev, DIOCOSFPFLUSH))
		err(1, "DIOCOSFPFLUSH");
}

/* flush pfctl's view of the fingerprints */
void
pfctl_flush_my_fingerprints(struct name_list *list)
{
	struct name_entry *nm;

	while ((nm = LIST_FIRST(list)) != NULL) {
		LIST_REMOVE(nm, nm_entry);
		pfctl_flush_my_fingerprints(&nm->nm_sublist);
		free(nm);
	}
	fingerprint_count = 0;
	class_count = 0;
}

/* Fetch the active fingerprints from the kernel */
int
pfctl_load_fingerprints(int dev, int opts)
{
	struct pf_osfp_ioctl io;
	int i;

	pfctl_flush_my_fingerprints(&classes);

	for (i = 0; i >= 0; i++) {
		memset(&io, 0, sizeof(io));
		io.fp_getnum = i;
		if (ioctl(dev, DIOCOSFPGET, &io)) {
			if (errno == EBUSY)
				break;
			warn("DIOCOSFPGET");
			return (1);
		}
		import_fingerprint(&io);
	}
	return (0);
}

/* List the fingerprints */
void
pfctl_show_fingerprints(int opts)
{
	if (LIST_FIRST(&classes) != NULL) {
		if (opts & PF_OPT_SHOWALL) {
			pfctl_print_title("OS FINGERPRINTS:");
			printf("%u fingerprints loaded\n", fingerprint_count);
		} else {
			printf("Class\tVersion\tSubtype(subversion)\n");
			printf("-----\t-------\t-------------------\n");
			sort_name_list(opts, &classes);
			print_name_list(opts, &classes, "");
		}
	}
}

/* Lookup a fingerprint */
pf_osfp_t
pfctl_get_fingerprint(const char *name)
{
	struct name_entry *nm, *class_nm, *version_nm, *subtype_nm;
	pf_osfp_t ret = PF_OSFP_NOMATCH;
	int class, version, subtype;
	int unp_class, unp_version, unp_subtype;
	int wr_len, version_len, subtype_len;
	char *ptr, *wr_name;

	if (strcasecmp(name, "unknown") == 0)
		return (PF_OSFP_UNKNOWN);

	/* Try most likely no version and no subtype */
	if ((nm = lookup_name_list(&classes, name))) {
		class = nm->nm_num;
		version = PF_OSFP_ANY;
		subtype = PF_OSFP_ANY;
		goto found;
	} else {

		/* Chop it up into class/version/subtype */

		if ((wr_name = strdup(name)) == NULL)
			err(1, "malloc");
		if ((ptr = strchr(wr_name, ' ')) == NULL) {
			free(wr_name);
			return (PF_OSFP_NOMATCH);
		}
		*ptr++ = '\0';

		/* The class is easy to find since it is delimited by a space */
		if ((class_nm = lookup_name_list(&classes, wr_name)) == NULL) {
			free(wr_name);
			return (PF_OSFP_NOMATCH);
		}
		class = class_nm->nm_num;

		/* Try no subtype */
		if ((version_nm = lookup_name_list(&class_nm->nm_sublist, ptr)))
		{
			version = version_nm->nm_num;
			subtype = PF_OSFP_ANY;
			free(wr_name);
			goto found;
		}


		/*
		 * There must be a version and a subtype.
		 * We'll do some fuzzy matching to pick up things like:
		 *   Linux 2.2.14 (version=2.2 subtype=14)
		 *   FreeBSD 4.0-STABLE (version=4.0 subtype=STABLE)
		 *   Windows 2000 SP2	(version=2000 subtype=SP2)
		 */
#define CONNECTOR(x)	((x) == '.' || (x) == ' ' || (x) == '\t' || (x) == '-')
		wr_len = strlen(ptr);
		LIST_FOREACH(version_nm, &class_nm->nm_sublist, nm_entry) {
			version_len = strlen(version_nm->nm_name);
			if (wr_len < version_len + 2 ||
			    !CONNECTOR(ptr[version_len]))
				continue;
			/* first part of the string must be version */
			if (strncasecmp(ptr, version_nm->nm_name,
			    version_len))
				continue;

			LIST_FOREACH(subtype_nm, &version_nm->nm_sublist,
			    nm_entry) {
				subtype_len = strlen(subtype_nm->nm_name);
				if (wr_len != version_len + subtype_len + 1)
					continue;

				/* last part of the string must be subtype */
				if (strcasecmp(&ptr[version_len+1],
				    subtype_nm->nm_name) != 0)
					continue;

				/* Found it!! */
				version = version_nm->nm_num;
				subtype = subtype_nm->nm_num;
				free(wr_name);
				goto found;
			}
		}

		free(wr_name);
		return (PF_OSFP_NOMATCH);
	}

found:
	PF_OSFP_PACK(ret, class, version, subtype);
	if (ret != PF_OSFP_NOMATCH) {
		PF_OSFP_UNPACK(ret, unp_class, unp_version, unp_subtype);
		if (class != unp_class) {
			fprintf(stderr, "warning: fingerprint table overflowed "
			    "classes\n");
			return (PF_OSFP_NOMATCH);
		}
		if (version != unp_version) {
			fprintf(stderr, "warning: fingerprint table overflowed "
			    "versions\n");
			return (PF_OSFP_NOMATCH);
		}
		if (subtype != unp_subtype) {
			fprintf(stderr, "warning: fingerprint table overflowed "
			    "subtypes\n");
			return (PF_OSFP_NOMATCH);
		}
	}
	if (ret == PF_OSFP_ANY) {
		/* should never happen */
		fprintf(stderr, "warning: fingerprint packed to 'any'\n");
		return (PF_OSFP_NOMATCH);
	}

	return (ret);
}

/* Lookup a fingerprint name by ID */
char *
pfctl_lookup_fingerprint(pf_osfp_t fp, char *buf, size_t len)
{
	int class, version, subtype;
	struct name_list *list;
	struct name_entry *nm;

	char *class_name, *version_name, *subtype_name;
	class_name = version_name = subtype_name = NULL;

	if (fp == PF_OSFP_UNKNOWN) {
		strlcpy(buf, "unknown", len);
		return (buf);
	}
	if (fp == PF_OSFP_ANY) {
		strlcpy(buf, "any", len);
		return (buf);
	}

	PF_OSFP_UNPACK(fp, class, version, subtype);
	if (class >= (1 << _FP_CLASS_BITS) ||
	    version >= (1 << _FP_VERSION_BITS) ||
	    subtype >= (1 << _FP_SUBTYPE_BITS)) {
		warnx("PF_OSFP_UNPACK(0x%x) failed!!", fp);
		strlcpy(buf, "nomatch", len);
		return (buf);
	}

	LIST_FOREACH(nm, &classes, nm_entry) {
		if (nm->nm_num == class) {
			class_name = nm->nm_name;
			if (version == PF_OSFP_ANY)
				goto found;
			list = &nm->nm_sublist;
			LIST_FOREACH(nm, list, nm_entry) {
				if (nm->nm_num == version) {
					version_name = nm->nm_name;
					if (subtype == PF_OSFP_ANY)
						goto found;
					list = &nm->nm_sublist;
					LIST_FOREACH(nm, list, nm_entry) {
						if (nm->nm_num == subtype) {
							subtype_name =
							    nm->nm_name;
							goto found;
						}
					} /* foreach subtype */
					strlcpy(buf, "nomatch", len);
					return (buf);
				}
			} /* foreach version */
			strlcpy(buf, "nomatch", len);
			return (buf);
		}
	} /* foreach class */

	strlcpy(buf, "nomatch", len);
	return (buf);

found:
	snprintf(buf, len, "%s", class_name);
	if (version_name) {
		strlcat(buf, " ", len);
		strlcat(buf, version_name, len);
		if (subtype_name) {
			if (strchr(version_name, ' '))
				strlcat(buf, " ", len);
			else if (strchr(version_name, '.') &&
			    isdigit(*subtype_name))
				strlcat(buf, ".", len);
			else
				strlcat(buf, " ", len);
			strlcat(buf, subtype_name, len);
		}
	}
	return (buf);
}

/* lookup a name in a list */
struct name_entry *
lookup_name_list(struct name_list *list, const char *name)
{
	struct name_entry *nm;
	LIST_FOREACH(nm, list, nm_entry)
		if (strcasecmp(name, nm->nm_name) == 0)
			return (nm);

	return (NULL);
}


void
add_fingerprint(int dev, int opts, struct pf_osfp_ioctl *fp)
{
	struct pf_osfp_ioctl fptmp;
	struct name_entry *nm_class, *nm_version, *nm_subtype;
	int class, version, subtype;

/* We expand #-# or #.#-#.# version/subtypes into multiple fingerprints */
#define EXPAND(field) do {						\
	int _dot = -1, _start = -1, _end = -1, _i = 0;			\
	/* pick major version out of #.# */				\
	if (isdigit(fp->field[_i]) && fp->field[_i+1] == '.') {		\
		_dot = fp->field[_i] - '0';				\
		_i += 2;						\
	}								\
	if (isdigit(fp->field[_i]))					\
		_start = fp->field[_i++] - '0';				\
	else								\
		break;							\
	if (isdigit(fp->field[_i]))					\
		_start = (_start * 10) + fp->field[_i++] - '0';		\
	if (fp->field[_i++] != '-')					\
		break;							\
	if (isdigit(fp->field[_i]) && fp->field[_i+1] == '.' &&		\
	    fp->field[_i] - '0' == _dot)				\
		_i += 2;						\
	else if (_dot != -1)						\
		break;							\
	if (isdigit(fp->field[_i]))					\
		_end = fp->field[_i++] - '0';				\
	else								\
		break;							\
	if (isdigit(fp->field[_i]))					\
		_end = (_end * 10) + fp->field[_i++] - '0';		\
	if (isdigit(fp->field[_i]))					\
		_end = (_end * 10) + fp->field[_i++] - '0';		\
	if (fp->field[_i] != '\0')					\
		break;							\
	memcpy(&fptmp, fp, sizeof(fptmp));				\
	for (;_start <= _end; _start++) {				\
		memset(fptmp.field, 0, sizeof(fptmp.field));		\
		fptmp.fp_os.fp_enflags |= PF_OSFP_EXPANDED;		\
		if (_dot == -1)						\
			snprintf(fptmp.field, sizeof(fptmp.field),	\
			    "%d", _start);				\
		    else						\
			snprintf(fptmp.field, sizeof(fptmp.field),	\
			    "%d.%d", _dot, _start);			\
		add_fingerprint(dev, opts, &fptmp);			\
	}								\
} while(0)

	/* We allow "#-#" as a version or subtype and we'll expand it */
	EXPAND(fp_os.fp_version_nm);
	EXPAND(fp_os.fp_subtype_nm);

	if (strcasecmp(fp->fp_os.fp_class_nm, "nomatch") == 0)
		errx(1, "fingerprint class \"nomatch\" is reserved");

	version = PF_OSFP_ANY;
	subtype = PF_OSFP_ANY;

	nm_class = fingerprint_name_entry(&classes, fp->fp_os.fp_class_nm);
	if (nm_class->nm_num == 0)
		nm_class->nm_num = ++class_count;
	class = nm_class->nm_num;

	nm_version = fingerprint_name_entry(&nm_class->nm_sublist,
	    fp->fp_os.fp_version_nm);
	if (nm_version) {
		if (nm_version->nm_num == 0)
			nm_version->nm_num = ++nm_class->nm_sublist_num;
		version = nm_version->nm_num;
		nm_subtype = fingerprint_name_entry(&nm_version->nm_sublist,
		    fp->fp_os.fp_subtype_nm);
		if (nm_subtype) {
			if (nm_subtype->nm_num == 0)
				nm_subtype->nm_num =
				    ++nm_version->nm_sublist_num;
			subtype = nm_subtype->nm_num;
		}
	}


	DEBUG(fp, "\tsignature %d:%d:%d %s", class, version, subtype,
	    print_ioctl(fp));

	PF_OSFP_PACK(fp->fp_os.fp_os, class, version, subtype);
	fingerprint_count++;

#ifdef FAKE_PF_KERNEL
	/* Linked to the sys/net/pf_osfp.c.  Call pf_osfp_add() */
	if ((errno = pf_osfp_add(fp)))
#else
	if ((opts & PF_OPT_NOACTION) == 0 && ioctl(dev, DIOCOSFPADD, fp))
#endif /* FAKE_PF_KERNEL */
	{
		if (errno == EEXIST) {
			warn("Duplicate signature for %s %s %s",
				fp->fp_os.fp_class_nm,
				fp->fp_os.fp_version_nm,
				fp->fp_os.fp_subtype_nm);

		} else {
			err(1, "DIOCOSFPADD");
		}
	}
}

/* import a fingerprint from the kernel */
void
import_fingerprint(struct pf_osfp_ioctl *fp)
{
	struct name_entry *nm_class, *nm_version, *nm_subtype;
	int class, version, subtype;

	PF_OSFP_UNPACK(fp->fp_os.fp_os, class, version, subtype);

	nm_class = fingerprint_name_entry(&classes, fp->fp_os.fp_class_nm);
	if (nm_class->nm_num == 0) {
		nm_class->nm_num = class;
		class_count = MAX(class_count, class);
	}

	nm_version = fingerprint_name_entry(&nm_class->nm_sublist,
	    fp->fp_os.fp_version_nm);
	if (nm_version) {
		if (nm_version->nm_num == 0) {
			nm_version->nm_num = version;
			nm_class->nm_sublist_num = MAX(nm_class->nm_sublist_num,
			    version);
		}
		nm_subtype = fingerprint_name_entry(&nm_version->nm_sublist,
		    fp->fp_os.fp_subtype_nm);
		if (nm_subtype) {
			if (nm_subtype->nm_num == 0) {
				nm_subtype->nm_num = subtype;
				nm_version->nm_sublist_num =
				    MAX(nm_version->nm_sublist_num, subtype);
			}
		}
	}


	fingerprint_count++;
	DEBUG(fp, "import signature %d:%d:%d", class, version, subtype);
}

/* Find an entry for a fingerprints class/version/subtype */
struct name_entry *
fingerprint_name_entry(struct name_list *list, char *name)
{
	struct name_entry *nm_entry;

	if (name == NULL || strlen(name) == 0)
		return (NULL);

	LIST_FOREACH(nm_entry, list, nm_entry) {
		if (strcasecmp(nm_entry->nm_name, name) == 0) {
			/* We'll move this to the front of the list later */
			LIST_REMOVE(nm_entry, nm_entry);
			break;
		}
	}
	if (nm_entry == NULL) {
		nm_entry = calloc(1, sizeof(*nm_entry));
		if (nm_entry == NULL)
			err(1, "calloc");
		LIST_INIT(&nm_entry->nm_sublist);
		strlcpy(nm_entry->nm_name, name, sizeof(nm_entry->nm_name));
	}
	LIST_INSERT_HEAD(list, nm_entry, nm_entry);
	return (nm_entry);
}


void
print_name_list(int opts, struct name_list *nml, const char *prefix)
{
	char newprefix[32];
	struct name_entry *nm;

	LIST_FOREACH(nm, nml, nm_entry) {
		snprintf(newprefix, sizeof(newprefix), "%s%s\t", prefix,
		    nm->nm_name);
		printf("%s\n", newprefix);
		print_name_list(opts, &nm->nm_sublist, newprefix);
	}
}

void
sort_name_list(int opts, struct name_list *nml)
{
	struct name_list new;
	struct name_entry *nm, *nmsearch, *nmlast;

	/* yes yes, it's a very slow sort.  so sue me */

	LIST_INIT(&new);

	while ((nm = LIST_FIRST(nml)) != NULL) {
		LIST_REMOVE(nm, nm_entry);
		nmlast = NULL;
		LIST_FOREACH(nmsearch, &new, nm_entry) {
			if (strcasecmp(nmsearch->nm_name, nm->nm_name) > 0) {
				LIST_INSERT_BEFORE(nmsearch, nm, nm_entry);
				break;
			}
			nmlast = nmsearch;
		}
		if (nmsearch == NULL) {
			if (nmlast)
				LIST_INSERT_AFTER(nmlast, nm, nm_entry);
			else
				LIST_INSERT_HEAD(&new, nm, nm_entry);
		}

		sort_name_list(opts, &nm->nm_sublist);
	}
	nmlast = NULL;
	while ((nm = LIST_FIRST(&new)) != NULL) {
		LIST_REMOVE(nm, nm_entry);
		if (nmlast == NULL)
			LIST_INSERT_HEAD(nml, nm, nm_entry);
		else
			LIST_INSERT_AFTER(nmlast, nm, nm_entry);
		nmlast = nm;
	}
}

/* parse the next integer in a formatted config file line */
int
get_int(char **line, size_t *len, int *var, int *mod,
    const char *name, int flags, int max, const char *filename, int lineno)
{
	int fieldlen, i;
	char *field;
	long val = 0;

	if (mod)
		*mod = 0;
	*var = 0;

	field = get_field(line, len, &fieldlen);
	if (field == NULL)
		return (1);
	if (fieldlen == 0) {
		fprintf(stderr, "%s:%d empty %s\n", filename, lineno, name);
		return (1);
	}

	i = 0;
	if ((*field == '%' || *field == 'S' || *field == 'T' || *field == '*')
	    && fieldlen >= 1) {
		switch (*field) {
		case 'S':
			if (mod && (flags & T_MSS))
				*mod = T_MSS;
			if (fieldlen == 1)
				return (0);
			break;
		case 'T':
			if (mod && (flags & T_MTU))
				*mod = T_MTU;
			if (fieldlen == 1)
				return (0);
			break;
		case '*':
			if (fieldlen != 1) {
				fprintf(stderr, "%s:%d long '%c' %s\n",
				    filename, lineno, *field, name);
				return (1);
			}
			if (mod && (flags & T_DC)) {
				*mod = T_DC;
				return (0);
			}
		case '%':
			if (mod && (flags & T_MOD))
				*mod = T_MOD;
			if (fieldlen == 1) {
				fprintf(stderr, "%s:%d modulus %s must have a "
				    "value\n", filename, lineno, name);
				return (1);
			}
			break;
		}
		if (mod == NULL || *mod == 0) {
			fprintf(stderr, "%s:%d does not allow %c' %s\n",
			    filename, lineno, *field, name);
			return (1);
		}
		i++;
	}

	for (; i < fieldlen; i++) {
		if (field[i] < '0' || field[i] > '9') {
			fprintf(stderr, "%s:%d non-digit character in %s\n",
			    filename, lineno, name);
			return (1);
		}
		val = val * 10 + field[i] - '0';
		if (val < 0) {
			fprintf(stderr, "%s:%d %s overflowed\n", filename,
			    lineno, name);
			return (1);
		}
	}

	if (val > max) {
		fprintf(stderr, "%s:%d %s value %ld > %d\n", filename, lineno,
		    name, val, max);
		return (1);
	}
	*var = (int)val;

	return (0);
}

/* parse the next string in a formatted config file line */
int
get_str(char **line, size_t *len, char **v, const char *name, int minlen,
    const char *filename, int lineno)
{
	int fieldlen;
	char *ptr;

	ptr = get_field(line, len, &fieldlen);
	if (ptr == NULL)
		return (1);
	if (fieldlen < minlen) {
		fprintf(stderr, "%s:%d too short %s\n", filename, lineno, name);
		return (1);
	}
	if ((*v = malloc(fieldlen + 1)) == NULL) {
		perror("malloc()");
		return (1);
	}
	memcpy(*v, ptr, fieldlen);
	(*v)[fieldlen] = '\0';

	return (0);
}

/* Parse out the TCP opts */
int
get_tcpopts(const char *filename, int lineno, const char *tcpopts,
    pf_tcpopts_t *packed, int *optcnt, int *mss, int *mss_mod, int *wscale,
    int *wscale_mod, int *ts0)
{
	int i, opt;

	*packed = 0;
	*optcnt = 0;
	*wscale = 0;
	*wscale_mod = T_DC;
	*mss = 0;
	*mss_mod = T_DC;
	*ts0 = 0;
	if (strcmp(tcpopts, ".") == 0)
		return (0);

	for (i = 0; tcpopts[i] && *optcnt < PF_OSFP_MAX_OPTS;) {
		switch ((opt = toupper(tcpopts[i++]))) {
		case 'N':	/* FALLTHROUGH */
		case 'S':
			*packed = (*packed << PF_OSFP_TCPOPT_BITS) |
			    (opt == 'N' ? PF_OSFP_TCPOPT_NOP :
			    PF_OSFP_TCPOPT_SACK);
			break;
		case 'W':	/* FALLTHROUGH */
		case 'M': {
			int *this_mod, *this;

			if (opt == 'W') {
				this = wscale;
				this_mod = wscale_mod;
			} else {
				this = mss;
				this_mod = mss_mod;
			}
			*this = 0;
			*this_mod = 0;

			*packed = (*packed << PF_OSFP_TCPOPT_BITS) |
			    (opt == 'W' ? PF_OSFP_TCPOPT_WSCALE :
			    PF_OSFP_TCPOPT_MSS);
			if (tcpopts[i] == '*' && (tcpopts[i + 1] == '\0' ||
			    tcpopts[i + 1] == ',')) {
				*this_mod = T_DC;
				i++;
				break;
			}

			if (tcpopts[i] == '%') {
				*this_mod = T_MOD;
				i++;
			}
			do {
				if (!isdigit(tcpopts[i])) {
					fprintf(stderr, "%s:%d unknown "
					    "character '%c' in %c TCP opt\n",
					    filename, lineno, tcpopts[i], opt);
					return (1);
				}
				*this = (*this * 10) + tcpopts[i++] - '0';
			} while(tcpopts[i] != ',' && tcpopts[i] != '\0');
			break;
		}
		case 'T':
			if (tcpopts[i] == '0') {
				*ts0 = 1;
				i++;
			}
			*packed = (*packed << PF_OSFP_TCPOPT_BITS) |
			    PF_OSFP_TCPOPT_TS;
			break;
		}
		(*optcnt) ++;
		if (tcpopts[i] == '\0')
			break;
		if (tcpopts[i] != ',') {
			fprintf(stderr, "%s:%d unknown option to %c TCP opt\n",
			    filename, lineno, opt);
			return (1);
		}
		i++;
	}

	return (0);
}

/* rip the next field ouf of a formatted config file line */
char *
get_field(char **line, size_t *len, int *fieldlen)
{
	char *ret, *ptr = *line;
	size_t plen = *len;


	while (plen && isspace(*ptr)) {
		plen--;
		ptr++;
	}
	ret = ptr;
	*fieldlen = 0;

	for (; plen > 0 && *ptr != ':'; plen--, ptr++)
		(*fieldlen)++;
	if (plen) {
		*line = ptr + 1;
		*len = plen - 1;
	} else {
		*len = 0;
	}
	while (*fieldlen && isspace(ret[*fieldlen - 1]))
		(*fieldlen)--;
	return (ret);
}


const char *
print_ioctl(struct pf_osfp_ioctl *fp)
{
	static char buf[1024];
	char tmp[32];
	int i, opt;

	*buf = '\0';
	if (fp->fp_flags & PF_OSFP_WSIZE_DC)
		strlcat(buf, "*", sizeof(buf));
	else if (fp->fp_flags & PF_OSFP_WSIZE_MSS)
		strlcat(buf, "S", sizeof(buf));
	else if (fp->fp_flags & PF_OSFP_WSIZE_MTU)
		strlcat(buf, "T", sizeof(buf));
	else {
		if (fp->fp_flags & PF_OSFP_WSIZE_MOD)
			strlcat(buf, "%", sizeof(buf));
		snprintf(tmp, sizeof(tmp), "%d", fp->fp_wsize);
		strlcat(buf, tmp, sizeof(buf));
	}
	strlcat(buf, ":", sizeof(buf));

	snprintf(tmp, sizeof(tmp), "%d", fp->fp_ttl);
	strlcat(buf, tmp, sizeof(buf));
	strlcat(buf, ":", sizeof(buf));

	if (fp->fp_flags & PF_OSFP_DF)
		strlcat(buf, "1", sizeof(buf));
	else
		strlcat(buf, "0", sizeof(buf));
	strlcat(buf, ":", sizeof(buf));

	if (fp->fp_flags & PF_OSFP_PSIZE_DC)
		strlcat(buf, "*", sizeof(buf));
	else {
		if (fp->fp_flags & PF_OSFP_PSIZE_MOD)
			strlcat(buf, "%", sizeof(buf));
		snprintf(tmp, sizeof(tmp), "%d", fp->fp_psize);
		strlcat(buf, tmp, sizeof(buf));
	}
	strlcat(buf, ":", sizeof(buf));

	if (fp->fp_optcnt == 0)
		strlcat(buf, ".", sizeof(buf));
	for (i = fp->fp_optcnt - 1; i >= 0; i--) {
		opt = fp->fp_tcpopts >> (i * PF_OSFP_TCPOPT_BITS);
		opt &= (1 << PF_OSFP_TCPOPT_BITS) - 1;
		switch (opt) {
		case PF_OSFP_TCPOPT_NOP:
			strlcat(buf, "N", sizeof(buf));
			break;
		case PF_OSFP_TCPOPT_SACK:
			strlcat(buf, "S", sizeof(buf));
			break;
		case PF_OSFP_TCPOPT_TS:
			strlcat(buf, "T", sizeof(buf));
			if (fp->fp_flags & PF_OSFP_TS0)
				strlcat(buf, "0", sizeof(buf));
			break;
		case PF_OSFP_TCPOPT_MSS:
			strlcat(buf, "M", sizeof(buf));
			if (fp->fp_flags & PF_OSFP_MSS_DC)
				strlcat(buf, "*", sizeof(buf));
			else {
				if (fp->fp_flags & PF_OSFP_MSS_MOD)
					strlcat(buf, "%", sizeof(buf));
				snprintf(tmp, sizeof(tmp), "%d", fp->fp_mss);
				strlcat(buf, tmp, sizeof(buf));
			}
			break;
		case PF_OSFP_TCPOPT_WSCALE:
			strlcat(buf, "W", sizeof(buf));
			if (fp->fp_flags & PF_OSFP_WSCALE_DC)
				strlcat(buf, "*", sizeof(buf));
			else {
				if (fp->fp_flags & PF_OSFP_WSCALE_MOD)
					strlcat(buf, "%", sizeof(buf));
				snprintf(tmp, sizeof(tmp), "%d", fp->fp_wscale);
				strlcat(buf, tmp, sizeof(buf));
			}
			break;
		}

		if (i != 0)
			strlcat(buf, ",", sizeof(buf));
	}
	strlcat(buf, ":", sizeof(buf));

	strlcat(buf, fp->fp_os.fp_class_nm, sizeof(buf));
	strlcat(buf, ":", sizeof(buf));
	strlcat(buf, fp->fp_os.fp_version_nm, sizeof(buf));
	strlcat(buf, ":", sizeof(buf));
	strlcat(buf, fp->fp_os.fp_subtype_nm, sizeof(buf));
	strlcat(buf, ":", sizeof(buf));

	snprintf(tmp, sizeof(tmp), "TcpOpts %d 0x%llx", fp->fp_optcnt,
	    (long long int)fp->fp_tcpopts);
	strlcat(buf, tmp, sizeof(buf));

	return (buf);
}
