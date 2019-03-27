/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <sys/queue.h>

#include "inf.h"

extern FILE *yyin;
int yyparse (void);

const char *words[W_MAX];	/* More than we'll need. */
int idx;

static struct section_head sh;
static struct reg_head rh;
static struct assign_head ah;

static char	*sstrdup	(const char *);
static struct assign
		*find_assign	(const char *, const char *);
static struct assign
		*find_next_assign
				(struct assign *);
static struct section
		*find_section	(const char *);
static int	dump_deviceids_pci	(void);
static int	dump_deviceids_pcmcia	(void);
static int	dump_deviceids_usb	(void);
static void	dump_pci_id	(const char *);
static void	dump_pcmcia_id	(const char *);
static void	dump_usb_id	(const char *);
static void	dump_regvals	(void);
static void	dump_paramreg	(const struct section *,
				const struct reg *, int);

static FILE	*ofp;

int
inf_parse (FILE *fp, FILE *outfp)
{
	TAILQ_INIT(&sh);
	TAILQ_INIT(&rh);
	TAILQ_INIT(&ah);

	ofp = outfp;
	yyin = fp;
	yyparse();

	if (dump_deviceids_pci() == 0 &&
	    dump_deviceids_pcmcia() == 0 &&
	    dump_deviceids_usb() == 0)
		return (-1);

	fprintf(outfp, "#ifdef NDIS_REGVALS\n");
	dump_regvals();
	fprintf(outfp, "#endif /* NDIS_REGVALS */\n");

	return (0);
}

void
section_add (const char *s)
{
	struct section *sec;

	sec = malloc(sizeof(struct section));
	bzero(sec, sizeof(struct section));
	sec->name = s;
	TAILQ_INSERT_TAIL(&sh, sec, link);

	return;
}

static struct assign *
find_assign (const char *s, const char *k)
{
	struct assign *assign;
	char newkey[256];

	/* Deal with string section lookups. */

	if (k != NULL && k[0] == '%') {
		bzero(newkey, sizeof(newkey));
		strncpy(newkey, k + 1, strlen(k) - 2);
		k = newkey;
	}

	TAILQ_FOREACH(assign, &ah, link) {
		if (strcasecmp(assign->section->name, s) == 0) {
			if (k == NULL)
				return(assign);
			else
				if (strcasecmp(assign->key, k) == 0)
					return(assign);
		}
	}
	return(NULL);
}

static struct assign *
find_next_assign (struct assign *a)
{
	struct assign *assign;

	TAILQ_FOREACH(assign, &ah, link) {
		if (assign == a)
			break;
	}

	assign = assign->link.tqe_next;

	if (assign == NULL || assign->section != a->section)
		return(NULL);

	return (assign);
}

static const char *
stringcvt(const char *s)
{
	struct assign *manf;

	manf = find_assign("strings", s);
	if (manf == NULL)
		return(s);
	return(manf->vals[0]);
}

struct section *
find_section (const char *s)
{
	struct section *section;

	TAILQ_FOREACH(section, &sh, link) {
		if (strcasecmp(section->name, s) == 0)
			return(section);
	}
	return(NULL);
}

static void
dump_pcmcia_id(const char *s)
{
	char *manstr, *devstr;
	char *p0, *p;

	p0 = __DECONST(char *, s);

	p = strchr(p0, '\\');
	if (p == NULL)
		return;
	p0 = p + 1;

	p = strchr(p0, '-');
	if (p == NULL)
		return;
	*p = '\0';

	manstr = p0;

	/* Convert any underscores to spaces. */

	while (*p0 != '\0') {
		if (*p0 == '_')
			*p0 = ' ';
		p0++;
	}

	p0 = p + 1;
	p = strchr(p0, '-');
	if (p == NULL)
		return;
	*p = '\0';

	devstr = p0;

	/* Convert any underscores to spaces. */

	while (*p0 != '\0') {
		if (*p0 == '_')
			*p0 = ' ';
		p0++;
	}

	fprintf(ofp, "\t\\\n\t{ \"%s\", \"%s\", ", manstr, devstr);
	return;
}

static void
dump_pci_id(const char *s)
{
	char *p;
	char vidstr[7], didstr[7], subsysstr[14];

	p = strcasestr(s, "VEN_");
	if (p == NULL)
		return;
	p += 4;
	strcpy(vidstr, "0x");
	strncat(vidstr, p, 4);
	p = strcasestr(s, "DEV_");
	if (p == NULL)
		return;
	p += 4;
	strcpy(didstr, "0x");
	strncat(didstr, p, 4);
	if (p == NULL)
		return;
	p = strcasestr(s, "SUBSYS_");
	if (p == NULL)
		strcpy(subsysstr, "0x00000000");
	else {
		p += 7;
		strcpy(subsysstr, "0x");
		strncat(subsysstr, p, 8);
	}

	fprintf(ofp, "\t\\\n\t{ %s, %s, %s, ", vidstr, didstr, subsysstr);
	return;
}

static void
dump_usb_id(const char *s)
{
	char *p;
	char vidstr[7], pidstr[7];

	p = strcasestr(s, "VID_");
	if (p == NULL)
		return;
	p += 4;
	strcpy(vidstr, "0x");
	strncat(vidstr, p, 4);
	p = strcasestr(s, "PID_");
	if (p == NULL)
		return;
	p += 4;
	strcpy(pidstr, "0x");
	strncat(pidstr, p, 4);
	if (p == NULL)
		return;

	fprintf(ofp, "\t\\\n\t{ %s, %s, ", vidstr, pidstr);
}

static int
dump_deviceids_pci()
{
	struct assign *manf, *dev;
	struct section *sec;
	struct assign *assign;
	char xpsec[256];
	int first = 1, found = 0;

	/* Find manufacturer name */
	manf = find_assign("Manufacturer", NULL);

nextmanf:

	/* Find manufacturer section */
	if (manf->vals[1] != NULL &&
	    (strcasecmp(manf->vals[1], "NT.5.1") == 0 ||
	    strcasecmp(manf->vals[1], "NTx86") == 0 ||
	    strcasecmp(manf->vals[1], "NTx86.5.1") == 0 ||
	    strcasecmp(manf->vals[1], "NTamd64") == 0)) {
		/* Handle Windows XP INF files. */
		snprintf(xpsec, sizeof(xpsec), "%s.%s",
		    manf->vals[0], manf->vals[1]);
		sec = find_section(xpsec);
	} else
		sec = find_section(manf->vals[0]);

	/* See if there are any PCI device definitions. */

	TAILQ_FOREACH(assign, &ah, link) {
		if (assign->section == sec) {
			dev = find_assign("strings", assign->key);
			if (strcasestr(assign->vals[1], "PCI") != NULL) {
				found++;
				break;
			}
		}
	}

	if (found == 0)
		goto done;

	found = 0;

	if (first == 1) {
		/* Emit start of PCI device table */
		fprintf (ofp, "#define NDIS_PCI_DEV_TABLE");
		first = 0;
	}

retry:

	/*
	 * Now run through all the device names listed
	 * in the manufacturer section and dump out the
	 * device descriptions and vendor/device IDs.
	 */

	TAILQ_FOREACH(assign, &ah, link) {
		if (assign->section == sec) {
			dev = find_assign("strings", assign->key);
			/* Emit device IDs. */
			if (strcasestr(assign->vals[1], "PCI") != NULL)
				dump_pci_id(assign->vals[1]);
			else
				continue;
			/* Emit device description */
			fprintf (ofp, "\t\\\n\t\"%s\" },", dev->vals[0]);
			found++;
		}
	}

	/* Someone tried to fool us. Shame on them. */
	if (!found) {
		found++;
		sec = find_section(manf->vals[0]);
		goto retry;
	}

	/* Handle Manufacturer sections with multiple entries. */
	manf = find_next_assign(manf);

	if (manf != NULL)
		goto nextmanf;

done:
	/* Emit end of table */

	fprintf(ofp, "\n\n");

	return (found);
}

static int
dump_deviceids_pcmcia()
{
	struct assign *manf, *dev;
	struct section *sec;
	struct assign *assign;
	char xpsec[256];
	int first = 1, found = 0;

	/* Find manufacturer name */
	manf = find_assign("Manufacturer", NULL);

nextmanf:

	/* Find manufacturer section */
	if (manf->vals[1] != NULL &&
	    (strcasecmp(manf->vals[1], "NT.5.1") == 0 ||
	    strcasecmp(manf->vals[1], "NTx86") == 0 ||
	    strcasecmp(manf->vals[1], "NTx86.5.1") == 0 ||
	    strcasecmp(manf->vals[1], "NTamd64") == 0)) {
		/* Handle Windows XP INF files. */
		snprintf(xpsec, sizeof(xpsec), "%s.%s",
		    manf->vals[0], manf->vals[1]);
		sec = find_section(xpsec);
	} else
		sec = find_section(manf->vals[0]);

	/* See if there are any PCMCIA device definitions. */

	TAILQ_FOREACH(assign, &ah, link) {
		if (assign->section == sec) {
			dev = find_assign("strings", assign->key);
			if (strcasestr(assign->vals[1], "PCMCIA") != NULL) {
				found++;
				break;
			}
		}
	}

	if (found == 0)
		goto done;

	found = 0;

	if (first == 1) {
		/* Emit start of PCMCIA device table */
		fprintf (ofp, "#define NDIS_PCMCIA_DEV_TABLE");
		first = 0;
	}

retry:

	/*
	 * Now run through all the device names listed
	 * in the manufacturer section and dump out the
	 * device descriptions and vendor/device IDs.
	 */

	TAILQ_FOREACH(assign, &ah, link) {
		if (assign->section == sec) {
			dev = find_assign("strings", assign->key);
			/* Emit device IDs. */
			if (strcasestr(assign->vals[1], "PCMCIA") != NULL)
				dump_pcmcia_id(assign->vals[1]);
			else
				continue;
			/* Emit device description */
			fprintf (ofp, "\t\\\n\t\"%s\" },", dev->vals[0]);
			found++;
		}
	}

	/* Someone tried to fool us. Shame on them. */
	if (!found) {
		found++;
		sec = find_section(manf->vals[0]);
		goto retry;
	}

	/* Handle Manufacturer sections with multiple entries. */
	manf = find_next_assign(manf);

	if (manf != NULL)
		goto nextmanf;

done:
	/* Emit end of table */

	fprintf(ofp, "\n\n");

	return (found);
}

static int
dump_deviceids_usb()
{
	struct assign *manf, *dev;
	struct section *sec;
	struct assign *assign;
	char xpsec[256];
	int first = 1, found = 0;

	/* Find manufacturer name */
	manf = find_assign("Manufacturer", NULL);

nextmanf:

	/* Find manufacturer section */
	if (manf->vals[1] != NULL &&
	    (strcasecmp(manf->vals[1], "NT.5.1") == 0 ||
	    strcasecmp(manf->vals[1], "NTx86") == 0 ||
	    strcasecmp(manf->vals[1], "NTx86.5.1") == 0 ||
	    strcasecmp(manf->vals[1], "NTamd64") == 0)) {
		/* Handle Windows XP INF files. */
		snprintf(xpsec, sizeof(xpsec), "%s.%s",
		    manf->vals[0], manf->vals[1]);
		sec = find_section(xpsec);
	} else
		sec = find_section(manf->vals[0]);

	/* See if there are any USB device definitions. */

	TAILQ_FOREACH(assign, &ah, link) {
		if (assign->section == sec) {
			dev = find_assign("strings", assign->key);
			if (strcasestr(assign->vals[1], "USB") != NULL) {
				found++;
				break;
			}
		}
	}

	if (found == 0)
		goto done;

	found = 0;

	if (first == 1) {
		/* Emit start of USB device table */
		fprintf (ofp, "#define NDIS_USB_DEV_TABLE");
		first = 0;
	}

retry:

	/*
	 * Now run through all the device names listed
	 * in the manufacturer section and dump out the
	 * device descriptions and vendor/device IDs.
	 */

	TAILQ_FOREACH(assign, &ah, link) {
		if (assign->section == sec) {
			dev = find_assign("strings", assign->key);
			/* Emit device IDs. */
			if (strcasestr(assign->vals[1], "USB") != NULL)
				dump_usb_id(assign->vals[1]);
			else
				continue;
			/* Emit device description */
			fprintf (ofp, "\t\\\n\t\"%s\" },", dev->vals[0]);
			found++;
		}
	}

	/* Someone tried to fool us. Shame on them. */
	if (!found) {
		found++;
		sec = find_section(manf->vals[0]);
		goto retry;
	}

	/* Handle Manufacturer sections with multiple entries. */
	manf = find_next_assign(manf);

	if (manf != NULL)
		goto nextmanf;

done:
	/* Emit end of table */

	fprintf(ofp, "\n\n");

	return (found);
}

static void
dump_addreg(const char *s, int devidx)
{
	struct section *sec;
	struct reg *reg;

	/* Find the addreg section */
	sec = find_section(s);

	/* Dump all the keys defined in it. */
	TAILQ_FOREACH(reg, &rh, link) {
		/*
		 * Keys with an empty subkey are very easy to parse,
		 * so just deal with them here. If a parameter key
		 * of the same name also exists, prefer that one and
		 * skip this one.
		 */
		if (reg->section == sec) {
			if (reg->subkey == NULL) {
				fprintf(ofp, "\n\t{ \"%s\",", reg->key);
				fprintf(ofp,"\n\t\"%s \",", reg->key);
				fprintf(ofp, "\n\t{ \"%s\" }, %d },",
				    reg->value == NULL ? "" :
				    stringcvt(reg->value), devidx);
			} else if (strncasecmp(reg->subkey,
			    "Ndi\\params", strlen("Ndi\\params")-1) == 0 &&
			    (reg->key != NULL && strcasecmp(reg->key,
			    "ParamDesc") == 0))
				dump_paramreg(sec, reg, devidx);
		}
	}

	return;
}

static void
dump_enumreg(const struct section *s, const struct reg *r)
{
	struct reg *reg;
	char enumkey[256];

	sprintf(enumkey, "%s\\enum", r->subkey);
	TAILQ_FOREACH(reg, &rh, link) {
		if (reg->section != s)
			continue;
		if (reg->subkey == NULL || strcasecmp(reg->subkey, enumkey))
			continue;
		fprintf(ofp, " [%s=%s]", reg->key,
		    stringcvt(reg->value));
	}
	return;
}

static void
dump_editreg(const struct section *s, const struct reg *r)
{
	struct reg *reg;

	TAILQ_FOREACH(reg, &rh, link) {
		if (reg->section != s)
			continue;
		if (reg->subkey == NULL || strcasecmp(reg->subkey, r->subkey))
			continue;
		if (reg->key == NULL)
			continue;
		if (strcasecmp(reg->key, "LimitText") == 0)
			fprintf(ofp, " [maxchars=%s]", reg->value);
		if (strcasecmp(reg->key, "Optional") == 0 &&
		    strcmp(reg->value, "1") == 0)
			fprintf(ofp, " [optional]");
	}
	return;
}

/* Use this for int too */
static void
dump_dwordreg(const struct section *s, const struct reg *r)
{
	struct reg *reg;

	TAILQ_FOREACH(reg, &rh, link) {
		if (reg->section != s)
			continue;
		if (reg->subkey == NULL || strcasecmp(reg->subkey, r->subkey))
			continue;
		if (reg->key == NULL)
			continue;
		if (strcasecmp(reg->key, "min") == 0)
			fprintf(ofp, " [min=%s]", reg->value);
		if (strcasecmp(reg->key, "max") == 0)
			fprintf(ofp, " [max=%s]", reg->value);
	}
	return;
}

static void
dump_defaultinfo(const struct section *s, const struct reg *r, int devidx)
{
	struct reg *reg;
	TAILQ_FOREACH(reg, &rh, link) {
		if (reg->section != s)
			continue;
		if (reg->subkey == NULL || strcasecmp(reg->subkey, r->subkey))
			continue;
		if (reg->key == NULL || strcasecmp(reg->key, "Default"))
			continue;
		fprintf(ofp, "\n\t{ \"%s\" }, %d },", reg->value == NULL ? "" :
		    stringcvt(reg->value), devidx);
		return;
	}
	/* Default registry entry missing */
	fprintf(ofp, "\n\t{ \"\" }, %d },", devidx);
	return;
}

static void
dump_paramdesc(const struct section *s, const struct reg *r)
{
	struct reg *reg;
	TAILQ_FOREACH(reg, &rh, link) {
		if (reg->section != s)
			continue;
		if (reg->subkey == NULL || strcasecmp(reg->subkey, r->subkey))
			continue;
		if (reg->key == NULL || strcasecmp(reg->key, "ParamDesc"))
			continue;
		fprintf(ofp, "\n\t\"%s", stringcvt(r->value));
			break;
	}
	return;
}

static void
dump_typeinfo(const struct section *s, const struct reg *r)
{
	struct reg *reg;
	TAILQ_FOREACH(reg, &rh, link) {
		if (reg->section != s)
			continue;
		if (reg->subkey == NULL || strcasecmp(reg->subkey, r->subkey))
			continue;
		if (reg->key == NULL)
			continue;
		if (strcasecmp(reg->key, "type"))
			continue;
		if (strcasecmp(reg->value, "dword") == 0 ||
		    strcasecmp(reg->value, "int") == 0)
			dump_dwordreg(s, r);
		if (strcasecmp(reg->value, "enum") == 0)
			dump_enumreg(s, r);
		if (strcasecmp(reg->value, "edit") == 0)
			dump_editreg(s, r);
	}
	return;
}

static void
dump_paramreg(const struct section *s, const struct reg *r, int devidx)
{
	const char *keyname;

	keyname = r->subkey + strlen("Ndi\\params\\");
	fprintf(ofp, "\n\t{ \"%s\",", keyname);
	dump_paramdesc(s, r);
	dump_typeinfo(s, r);
	fprintf(ofp, "\",");
	dump_defaultinfo(s, r, devidx);

	return;
}

static void
dump_regvals(void)
{
	struct assign *manf, *dev;
	struct section *sec;
	struct assign *assign;
	char sname[256];
	int found = 0, i, is_winxp = 0, is_winnt = 0, devidx = 0;

	/* Find signature to check for special case of WinNT. */
	assign = find_assign("version", "signature");
	if (strcasecmp(assign->vals[0], "$windows nt$") == 0)
		is_winnt++;

	/* Emit start of block */
	fprintf (ofp, "ndis_cfg ndis_regvals[] = {");

	/* Find manufacturer name */
	manf = find_assign("Manufacturer", NULL);

nextmanf:

	/* Find manufacturer section */
	if (manf->vals[1] != NULL &&
	    (strcasecmp(manf->vals[1], "NT.5.1") == 0 ||
	    strcasecmp(manf->vals[1], "NTx86") == 0 ||
	    strcasecmp(manf->vals[1], "NTx86.5.1") == 0 ||
	    strcasecmp(manf->vals[1], "NTamd64") == 0)) {
		is_winxp++;
		/* Handle Windows XP INF files. */
		snprintf(sname, sizeof(sname), "%s.%s",
		    manf->vals[0], manf->vals[1]);
		sec = find_section(sname);
	} else
		sec = find_section(manf->vals[0]);

retry:

	TAILQ_FOREACH(assign, &ah, link) {
		if (assign->section == sec) {
			found++;
			/*
			 * Find all the AddReg sections.
			 * Look for section names with .NT, unless
			 * this is a WinXP .INF file.
			 */

			if (is_winxp) {
				sprintf(sname, "%s.NTx86", assign->vals[0]);
				dev = find_assign(sname, "AddReg");
				if (dev == NULL) {
					sprintf(sname, "%s.NT",
					    assign->vals[0]);
					dev = find_assign(sname, "AddReg");
				}
				if (dev == NULL)
					dev = find_assign(assign->vals[0],
					    "AddReg");
			} else {
				sprintf(sname, "%s.NT", assign->vals[0]);
				dev = find_assign(sname, "AddReg");
				if (dev == NULL && is_winnt)
					dev = find_assign(assign->vals[0],
					    "AddReg");
			}
			/* Section not found. */
			if (dev == NULL)
				continue;
			for (i = 0; i < W_MAX; i++) {
				if (dev->vals[i] != NULL)
					dump_addreg(dev->vals[i], devidx);
			}
			devidx++;
		}
	}

	if (!found) {
		sec = find_section(manf->vals[0]);
		is_winxp = 0;
		found++;
		goto retry;
	}

	manf = find_next_assign(manf);

	if (manf != NULL)
		goto nextmanf;

	fprintf(ofp, "\n\t{ NULL, NULL, { 0 }, 0 }\n};\n\n");

	return;
}

void
assign_add (const char *a)
{
	struct assign *assign;
	int i;

	assign = malloc(sizeof(struct assign));
	bzero(assign, sizeof(struct assign));
	assign->section = TAILQ_LAST(&sh, section_head);
	assign->key = sstrdup(a);
	for (i = 0; i < idx; i++)
		assign->vals[(idx - 1) - i] = sstrdup(words[i]);
	TAILQ_INSERT_TAIL(&ah, assign, link);

	clear_words();
	return;
}

void
define_add (const char *d __unused)
{
#ifdef notdef
	fprintf(stderr, "define \"%s\"\n", d);
#endif
	return;
}

static char *
sstrdup(const char *str)
{
	if (str != NULL && strlen(str))
		return (strdup(str));
	return (NULL);
}

static int
satoi (const char *nptr)
{
	if (nptr != NULL && strlen(nptr))
		return (atoi(nptr));
	return (0);
}

void
regkey_add (const char *r)
{
	struct reg *reg;

	reg = malloc(sizeof(struct reg));
	bzero(reg, sizeof(struct reg));
	reg->section = TAILQ_LAST(&sh, section_head);
	reg->root = sstrdup(r);
	reg->subkey = sstrdup(words[3]);
	reg->key = sstrdup(words[2]);
	reg->flags = satoi(words[1]);
	reg->value = sstrdup(words[0]);
	TAILQ_INSERT_TAIL(&rh, reg, link);

	free(__DECONST(char *, r));
	clear_words();
	return;
}

void
push_word (const char *w)
{

	if (idx == W_MAX) {
		fprintf(stderr, "too many words; try bumping W_MAX in inf.h\n");
		exit(1);
	}

	if (w && strlen(w))
		words[idx++] = w;
	else
		words[idx++] = NULL;
	return;
}

void
clear_words (void)
{
	int i;

	for (i = 0; i < idx; i++) {
		if (words[i]) {
			free(__DECONST(char *, words[i]));
		}
	}
	idx = 0;
	bzero(words, sizeof(words));
	return;
}
