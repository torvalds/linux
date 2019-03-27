/*-
 * Copyright (c) 2005-2006 The FreeBSD Project
 * All rights reserved.
 *
 * Author: Victor Cruceru <soc-victor@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
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
 *
 * $FreeBSD$
 */

/*
 * Host Resources MIB implementation for SNMPd: instrumentation for
 * hrPrinterTable
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

#include <sys/dirent.h>
#include "lp.h"

/* Constants */
static const struct asn_oid OIDX_hrDevicePrinter_c = OIDX_hrDevicePrinter;

enum PrinterStatus {
	PS_OTHER	= 1,
	PS_UNKNOWN	= 2,
	PS_IDLE		= 3,
	PS_PRINTING	= 4,
	PS_WARMUP	= 5
};

/*
 * This structure is used to hold a SNMP table entry
 * for HOST-RESOURCES-MIB's hrPrinterTable.
 */
struct printer_entry {
	int32_t		index;
	int32_t		status;  /* values from PrinterStatus enum above */
	u_char		detectedErrorState[2];
	TAILQ_ENTRY(printer_entry) link;
#define	HR_PRINTER_FOUND		0x001
	uint32_t	flags;

};
TAILQ_HEAD(printer_tbl, printer_entry);

/* the hrPrinterTable */
static struct printer_tbl printer_tbl = TAILQ_HEAD_INITIALIZER(printer_tbl);

/* last (agent) tick when hrPrinterTable was updated */
static uint64_t printer_tick;

/**
 * Create entry into the printer table.
 */
static struct printer_entry *
printer_entry_create(const struct device_entry *devEntry)
{
	struct printer_entry *entry = NULL;

	assert(devEntry != NULL);
	if (devEntry == NULL)
		return (NULL);

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		syslog(LOG_WARNING, "hrPrinterTable: %s: %m", __func__);
		return (NULL);
	}
	memset(entry, 0, sizeof(*entry));
	entry->index = devEntry->index;
	INSERT_OBJECT_INT(entry, &printer_tbl);
	return (entry);
}

/**
 * Delete entry from the printer table.
 */
static void
printer_entry_delete(struct printer_entry *entry)
{

	assert(entry != NULL);
	if (entry == NULL)
		return;

	TAILQ_REMOVE(&printer_tbl, entry, link);
	free(entry);
}

/**
 * Find a printer by its index
 */
static struct printer_entry *
printer_find_by_index(int32_t idx)
{
	struct printer_entry *entry;

	TAILQ_FOREACH(entry, &printer_tbl, link)
		if (entry->index == idx)
			return (entry);

	return (NULL);
}

/**
 * Get the status of a printer
 */
static enum PrinterStatus
get_printer_status(const struct printer *pp)
{
	char statfile[MAXPATHLEN];
	char lockfile[MAXPATHLEN];
	char fline[128];
	int fd;
	FILE *f = NULL;
	enum PrinterStatus ps = PS_UNKNOWN;

	if (pp->lock_file[0] == '/')
		strlcpy(lockfile, pp->lock_file, sizeof(lockfile));
	else
		snprintf(lockfile, sizeof(lockfile), "%s/%s",
		    pp->spool_dir, pp->lock_file);

	fd = open(lockfile, O_RDONLY);
	if (fd < 0 || flock(fd, LOCK_SH | LOCK_NB) == 0) {
		ps = PS_IDLE;
		goto LABEL_DONE;
	}

	if (pp->status_file[0] == '/')
		strlcpy(statfile, pp->status_file, sizeof(statfile));
	else
		snprintf(statfile, sizeof(statfile), "%s/%s",
		    pp->spool_dir, pp->status_file);

	f = fopen(statfile, "r");
	if (f == NULL) {
		syslog(LOG_ERR, "cannot open status file: %s", strerror(errno));
		ps = PS_UNKNOWN;
		goto LABEL_DONE;
	}

	memset(&fline[0], '\0', sizeof(fline));
	if (fgets(fline, sizeof(fline) -1, f) == NULL) {
		ps = PS_UNKNOWN;
		goto LABEL_DONE;
	}

	if (strstr(fline, "is ready and printing") != NULL) {
		ps = PS_PRINTING;
		goto LABEL_DONE;
	}

	if (strstr(fline, "to become ready (offline?)") != NULL) {
		ps = PS_OTHER;
		goto LABEL_DONE;
	}

LABEL_DONE:
	if (fd >= 0)
		(void)close(fd);	/* unlocks as well */

	if (f != NULL)
		(void)fclose(f);

	return (ps);
}

/**
 * Called for each printer found in /etc/printcap.
 */
static void
handle_printer(struct printer *pp)
{
	struct device_entry *dev_entry;
	struct printer_entry *printer_entry;
	char dev_only[128];
	struct stat sb;

	if (pp->remote_host != NULL) {
		HRDBG("skipped %s -- remote", pp->printer);
		return;
	}

	if (strncmp(pp->lp, _PATH_DEV, strlen(_PATH_DEV)) != 0) {
		HRDBG("skipped %s [device %s] -- remote", pp->printer, pp->lp);
		return;
	}

	memset(dev_only, '\0', sizeof(dev_only));
	snprintf(dev_only, sizeof(dev_only), "%s", pp->lp + strlen(_PATH_DEV));

	HRDBG("printer %s has device %s", pp->printer, dev_only);

	if (stat(pp->lp, &sb) < 0) {
		if (errno == ENOENT) {
			HRDBG("skipped %s -- device %s missing",
			    pp->printer, pp->lp);
			return;
		}
	}

	if ((dev_entry = device_find_by_name(dev_only)) == NULL) {
		HRDBG("%s not in hrDeviceTable", pp->lp);
		return;
	}
	HRDBG("%s found in hrDeviceTable", pp->lp);
	dev_entry->type = &OIDX_hrDevicePrinter_c;

	dev_entry->flags |= HR_DEVICE_IMMUTABLE;

	/* Then check hrPrinterTable for this device */
	if ((printer_entry = printer_find_by_index(dev_entry->index)) == NULL &&
	    (printer_entry = printer_entry_create(dev_entry)) == NULL)
		return;

	printer_entry->flags |= HR_PRINTER_FOUND;
	printer_entry->status = get_printer_status(pp);
	memset(printer_entry->detectedErrorState, 0,
	    sizeof(printer_entry->detectedErrorState));
}

static void
hrPrinter_get_OS_entries(void)
{
	int  status, more;
	struct printer myprinter, *pp = &myprinter;

	init_printer(pp);
	HRDBG("---->Getting printers .....");
	more = firstprinter(pp, &status);
	if (status)
		goto errloop;

	while (more) {
		do {
			HRDBG("---->Got printer %s", pp->printer);

			handle_printer(pp);
			more = nextprinter(pp, &status);
errloop:
			if (status)
				syslog(LOG_WARNING,
				    "hrPrinterTable: printcap entry for %s "
				    "has errors, skipping",
				    pp->printer ? pp->printer : "<noname?>");
		} while (more && status);
	}

	lastprinter();
	printer_tick = this_tick;
}

/**
 * Init the things for hrPrinterTable
 */
void
init_printer_tbl(void)
{

	hrPrinter_get_OS_entries();
}

/**
 * Finalization routine for hrPrinterTable
 * It destroys the lists and frees any allocated heap memory
 */
void
fini_printer_tbl(void)
{
	struct printer_entry *n1;

	while ((n1 = TAILQ_FIRST(&printer_tbl)) != NULL) {
		TAILQ_REMOVE(&printer_tbl, n1, link);
		free(n1);
	}
}

/**
 * Refresh the printer table if needed.
 */
void
refresh_printer_tbl(void)
{
	struct printer_entry *entry;
	struct printer_entry *entry_tmp;

	if (this_tick <= printer_tick) {
		HRDBG("no refresh needed");
		return;
	}

	/* mark each entry as missing */
	TAILQ_FOREACH(entry, &printer_tbl, link)
		entry->flags &= ~HR_PRINTER_FOUND;

	hrPrinter_get_OS_entries();

	/*
	 * Purge items that disappeared
	 */
	entry = TAILQ_FIRST(&printer_tbl);
	while (entry != NULL) {
		entry_tmp = TAILQ_NEXT(entry, link);
		if (!(entry->flags & HR_PRINTER_FOUND))
			printer_entry_delete(entry);
		entry = entry_tmp;
	}

	printer_tick = this_tick;

	HRDBG("refresh DONE ");
}

int
op_hrPrinterTable(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op curr_op)
{
	struct printer_entry *entry;

	refresh_printer_tbl();

	switch (curr_op) {

	case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_INT(&printer_tbl, &value->var,
		    sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = entry->index;
		goto get;

	case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_INT(&printer_tbl, &value->var,
		    sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_INT(&printer_tbl, &value->var,
		    sub)) == NULL)
			return (SNMP_ERR_NO_CREATION);
		return (SNMP_ERR_NOT_WRITEABLE);

	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		abort();
	}
	abort();

  get:
	switch (value->var.subs[sub - 1]) {

	case LEAF_hrPrinterStatus:
		value->v.integer = entry->status;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrPrinterDetectedErrorState:
		return (string_get(value, entry->detectedErrorState,
		    sizeof(entry->detectedErrorState)));
	}
	abort();
}
