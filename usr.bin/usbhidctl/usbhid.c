/*	$NetBSD: usbhid.c,v 1.14 2000/07/03 02:51:37 matt Exp $	*/
/*	$FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (augustss@netbsd.org).
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <ctype.h>
#include <errno.h>
#include <usbhid.h>
#include <dev/usb/usbhid.h>

static struct variable {
	char *name;
	int instance;
	int val;
	struct hid_item h;
	struct variable *next;
} *vars;

static int verbose = 0;
static int noname = 0;
static int hexdump = 0;
static int wflag = 0;
static int zflag = 0;

static void usage(void);
static void dumpitem(const char *label, struct hid_item *h);
static void dumpitems(report_desc_t r);
static void prdata(u_char *buf, struct hid_item *h);
static void dumpdata(int f, report_desc_t r, int loop);
static void writedata(int f, report_desc_t r);

static void
parceargs(report_desc_t r, int all, int nnames, char **names)
{
	struct hid_data *d;
	struct hid_item h;
	char colls[1000];
	char hname[1000], *tmp1, *tmp2;
	struct variable *var, **pnext;
	int i, instance, cp, t;

	pnext = &vars;
	if (all) {
		if (wflag)
			errx(1, "Must not specify -w to read variables");
		cp = 0;
		for (d = hid_start_parse(r,
		    1<<hid_input | 1<<hid_output | 1<<hid_feature, -1);
		    hid_get_item(d, &h); ) {
			if (h.kind == hid_collection) {
				cp += sprintf(&colls[cp], "%s%s:%s",
				    cp != 0 ? "." : "",
				    hid_usage_page(HID_PAGE(h.usage)),
				    hid_usage_in_page(h.usage));
			} else if (h.kind == hid_endcollection) {
				tmp1 = strrchr(colls, '.');
				if (tmp1 != NULL) {
					cp -= strlen(tmp1);
					tmp1[0] = 0;
				} else {
					cp = 0;
					colls[0] = 0;
				}
			}
			if ((h.kind != hid_input && h.kind != hid_output &&
			    h.kind != hid_feature) || (h.flags & HIO_CONST))
				continue;
			var = malloc(sizeof(*var));
			memset(var, 0, sizeof(*var));
			asprintf(&var->name, "%s%s%s:%s",
			    colls, colls[0] != 0 ? "." : "",
			    hid_usage_page(HID_PAGE(h.usage)),
			    hid_usage_in_page(h.usage));
			var->h = h;
			*pnext = var;
			pnext = &var->next;
		}
		hid_end_parse(d);
		return;
	}
	for (i = 0; i < nnames; i++) {
		var = malloc(sizeof(*var));
		memset(var, 0, sizeof(*var));
		tmp1 = tmp2 = strdup(names[i]);
		strsep(&tmp2, "=");
		var->name = strsep(&tmp1, "#");
		if (tmp1 != NULL)
			var->instance = atoi(tmp1);
		if (tmp2 != NULL) {
			if (!wflag)
				errx(1, "Must specify -w to write variables");
			var->val = atoi(tmp2);
		} else
			if (wflag)
				errx(1, "Must not specify -w to read variables");
		*pnext = var;
		pnext = &var->next;

		instance = 0;
		cp = 0;
		for (d = hid_start_parse(r,
		    1<<hid_input | 1<<hid_output | 1<<hid_feature, -1);
		    hid_get_item(d, &h); ) {
			if (h.kind == hid_collection) {
				cp += sprintf(&colls[cp], "%s%s:%s",
				    cp != 0 ? "." : "",
				    hid_usage_page(HID_PAGE(h.usage)),
				    hid_usage_in_page(h.usage));
			} else if (h.kind == hid_endcollection) {
				tmp1 = strrchr(colls, '.');
				if (tmp1 != NULL) {
					cp -= strlen(tmp1);
					tmp1[0] = 0;
				} else {
					cp = 0;
					colls[0] = 0;
				}
			}
			if ((h.kind != hid_input && h.kind != hid_output &&
			    h.kind != hid_feature) || (h.flags & HIO_CONST))
				continue;
			snprintf(hname, sizeof(hname), "%s%s%s:%s",
			    colls, colls[0] != 0 ? "." : "",
			    hid_usage_page(HID_PAGE(h.usage)),
			    hid_usage_in_page(h.usage));
			t = strlen(hname) - strlen(var->name);
			if (t > 0) {
				if (strcmp(hname + t, var->name) != 0)
					continue;
				if (hname[t - 1] != '.')
					continue;
			} else if (strcmp(hname, var->name) != 0)
				continue;
			if (var->instance != instance++)
				continue;
			var->h = h;
			break;
		}
		hid_end_parse(d);
		if (var->h.usage == 0)
			errx(1, "Unknown item '%s'", var->name);
	}
}

static void
usage(void)
{

	fprintf(stderr,
                "usage: %s -f device "
                "[-l] [-n] [-r] [-t tablefile] [-v] [-x] [-z] name ...\n",
                getprogname());
	fprintf(stderr,
                "       %s -f device "
                "[-l] [-n] [-r] [-t tablefile] [-v] [-x] [-z] -a\n",
                getprogname());
	fprintf(stderr,
                "       %s -f device "
                "[-t tablefile] [-v] [-z] -w name=value\n",
                getprogname());
	exit(1);
}

static void
dumpitem(const char *label, struct hid_item *h)
{
	if ((h->flags & HIO_CONST) && !verbose)
		return;
	printf("%s rid=%d size=%d count=%d page=%s usage=%s%s%s", label,
	       h->report_ID, h->report_size, h->report_count,
	       hid_usage_page(HID_PAGE(h->usage)),
	       hid_usage_in_page(h->usage),
	       h->flags & HIO_CONST ? " Const" : "",
	       h->flags & HIO_VARIABLE ? "" : " Array");
	printf(", logical range %d..%d",
	       h->logical_minimum, h->logical_maximum);
	if (h->physical_minimum != h->physical_maximum)
		printf(", physical range %d..%d",
		       h->physical_minimum, h->physical_maximum);
	if (h->unit)
		printf(", unit=0x%02x exp=%d", h->unit, h->unit_exponent);
	printf("\n");
}

static const char *
hid_collection_type(int32_t type)
{
	static char num[8];

	switch (type) {
	case 0: return ("Physical");
	case 1: return ("Application");
	case 2: return ("Logical");
	case 3: return ("Report");
	case 4: return ("Named_Array");
	case 5: return ("Usage_Switch");
	case 6: return ("Usage_Modifier");
	}
	snprintf(num, sizeof(num), "0x%02x", type);
	return (num);
}

static void
dumpitems(report_desc_t r)
{
	struct hid_data *d;
	struct hid_item h;
	int size;

	for (d = hid_start_parse(r, ~0, -1); hid_get_item(d, &h); ) {
		switch (h.kind) {
		case hid_collection:
			printf("Collection type=%s page=%s usage=%s\n",
			       hid_collection_type(h.collection),
			       hid_usage_page(HID_PAGE(h.usage)),
			       hid_usage_in_page(h.usage));
			break;
		case hid_endcollection:
			printf("End collection\n");
			break;
		case hid_input:
			dumpitem("Input  ", &h);
			break;
		case hid_output:
			dumpitem("Output ", &h);
			break;
		case hid_feature:
			dumpitem("Feature", &h);
			break;
		}
	}
	hid_end_parse(d);
	size = hid_report_size(r, hid_input, -1);
	printf("Total   input size %d bytes\n", size);

	size = hid_report_size(r, hid_output, -1);
	printf("Total  output size %d bytes\n", size);

	size = hid_report_size(r, hid_feature, -1);
	printf("Total feature size %d bytes\n", size);
}

static void
prdata(u_char *buf, struct hid_item *h)
{
	u_int data;
	int i, pos;

	pos = h->pos;
	for (i = 0; i < h->report_count; i++) {
		data = hid_get_data(buf, h);
		if (i > 0)
			printf(" ");
		if (h->logical_minimum < 0)
			printf("%d", (int)data);
		else
			printf("%u", data);
                if (hexdump)
			printf(" [0x%x]", data);
		h->pos += h->report_size;
	}
	h->pos = pos;
}

static void
dumpdata(int f, report_desc_t rd, int loop)
{
	struct variable *var;
	int dlen, havedata, i, match, r, rid, use_rid;
	u_char *dbuf;
	enum hid_kind kind;

	kind = zflag ? 3 : 0;
	rid = -1;
	use_rid = !!hid_get_report_id(f);
	do {
		if (kind < 3) {
			if (++rid >= 256) {
				rid = 0;
				kind++;
			}
			if (kind >= 3)
				rid = -1;
			for (var = vars; var; var = var->next) {
				if (rid == var->h.report_ID &&
				    kind == var->h.kind)
					break;
			}
			if (var == NULL)
				continue;
		}
		dlen = hid_report_size(rd, kind < 3 ? kind : hid_input, rid);
		if (dlen <= 0)
			continue;
		dbuf = malloc(dlen);
		memset(dbuf, 0, dlen);
		if (kind < 3) {
			dbuf[0] = rid;
			r = hid_get_report(f, kind, dbuf, dlen);
			if (r < 0)
				warn("hid_get_report(rid %d)", rid);
			havedata = !r && (rid == 0 || dbuf[0] == rid);
			if (rid != 0)
				dbuf[0] = rid;
		} else {
			r = read(f, dbuf, dlen);
			if (r < 1)
				err(1, "read error");
			havedata = 1;
		}
		if (verbose) {
			printf("Got %s report %d (%d bytes):",
			    kind == hid_output ? "output" :
			    kind == hid_feature ? "feature" : "input",
			    use_rid ? dbuf[0] : 0, dlen);
			if (havedata) {
				for (i = 0; i < dlen; i++)
					printf(" %02x", dbuf[i]);
			}
			printf("\n");
		}
		match = 0;
		for (var = vars; var; var = var->next) {
			if ((kind < 3 ? kind : hid_input) != var->h.kind)
				continue;
			if (var->h.report_ID != 0 &&
			    dbuf[0] != var->h.report_ID)
				continue;
			match = 1;
			if (!noname)
				printf("%s=", var->name);
			if (havedata)
				prdata(dbuf, &var->h);
			printf("\n");
		}
		if (match)
			printf("\n");
		free(dbuf);
	} while (loop || kind < 3);
}

static void
writedata(int f, report_desc_t rd)
{
	struct variable *var;
	int dlen, i, r, rid;
	u_char *dbuf;
	enum hid_kind kind;

	kind = 0;
	rid = 0;
	for (kind = 0; kind < 3; kind ++) {
	    for (rid = 0; rid < 256; rid ++) {
		for (var = vars; var; var = var->next) {
			if (rid == var->h.report_ID && kind == var->h.kind)
				break;
		}
		if (var == NULL)
			continue;
		dlen = hid_report_size(rd, kind, rid);
		if (dlen <= 0)
			continue;
		dbuf = malloc(dlen);
		memset(dbuf, 0, dlen);
		dbuf[0] = rid;
		if (!zflag && hid_get_report(f, kind, dbuf, dlen) == 0) {
			if (verbose) {
				printf("Got %s report %d (%d bytes):",
				    kind == hid_input ? "input" :
				    kind == hid_output ? "output" : "feature",
				    rid, dlen);
				for (i = 0; i < dlen; i++)
					printf(" %02x", dbuf[i]);
				printf("\n");
			}
		} else if (!zflag) {
			warn("hid_get_report(rid %d)", rid);
			if (verbose) {
				printf("Can't get %s report %d (%d bytes). "
				    "Will be initialized with zeros.\n",
				    kind == hid_input ? "input" :
				    kind == hid_output ? "output" : "feature",
				    rid, dlen);
			}
		}
		for (var = vars; var; var = var->next) {
			if (rid != var->h.report_ID || kind != var->h.kind)
				continue;
			hid_set_data(dbuf, &var->h, var->val);
		}
		if (verbose) {
			printf("Setting %s report %d (%d bytes):",
			    kind == hid_output ? "output" :
			    kind == hid_feature ? "feature" : "input",
			    rid, dlen);
			for (i = 0; i < dlen; i++)
				printf(" %02x", dbuf[i]);
			printf("\n");
		}
		r = hid_set_report(f, kind, dbuf, dlen);
		if (r != 0)
			warn("hid_set_report(rid %d)", rid);
		free(dbuf);
	    }
	}
}

int
main(int argc, char **argv)
{
	report_desc_t r;
	char *table = 0;
	char devnam[100], *dev = NULL;
	int f;
	int all = 0;
	int ch;
	int repdump = 0;
	int loop = 0;

	while ((ch = getopt(argc, argv, "af:lnrt:vwxz")) != -1) {
		switch(ch) {
		case 'a':
			all++;
			break;
		case 'f':
			dev = optarg;
			break;
		case 'l':
			loop ^= 1;
			break;
		case 'n':
			noname++;
			break;
		case 'r':
			repdump++;
			break;
		case 't':
			table = optarg;
			break;
		case 'v':
			verbose++;
			break;
		case 'w':
			wflag = 1;
			break;
		case 'x':
			hexdump = 1;
			break;
		case 'z':
			zflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (dev == NULL)
		usage();

	if (argc == 0 && !all && !repdump)
		usage();

	if (dev[0] != '/') {
		if (isdigit(dev[0]))
			snprintf(devnam, sizeof(devnam), "/dev/uhid%s", dev);
		else
			snprintf(devnam, sizeof(devnam), "/dev/%s", dev);
		dev = devnam;
	}

	hid_init(table);

	f = open(dev, O_RDWR);
	if (f < 0)
		err(1, "%s", dev);

	r = hid_get_report_desc(f);
	if (r == 0)
		errx(1, "USB_GET_REPORT_DESC");

	if (repdump) {
		printf("Report descriptor:\n");
		dumpitems(r);
	}
	if (argc != 0 || all) {
		parceargs(r, all, argc, argv);
		if (wflag)
			writedata(f, r);
		else
			dumpdata(f, r, loop);
	}

	hid_dispose_report_desc(r);
	exit(0);
}
