/*
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
 *
 * Host Resources MIB implementation for SNMPd: instrumentation for
 * hrSWInstalledTable
 */

#include <sys/limits.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sysexits.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

#define	CONTENTS_FNAME	"+CONTENTS"

enum SWInstalledType {
	SWI_UNKNOWN		= 1,
	SWI_OPERATING_SYSTEM	= 2,
	SWI_DEVICE_DRIVER	= 3,
	SWI_APPLICATION		= 4
};

#define	SW_NAME_MLEN	(64 + 1)

/*
 * This structure is used to hold a SNMP table entry
 * for HOST-RESOURCES-MIB's hrSWInstalledTable
 */
struct swins_entry {
	int32_t		index;
	u_char		*name;	/* max len for this is SW_NAME_MLEN */
	const struct asn_oid *id;
	int32_t		type;	/* from enum SWInstalledType */
	u_char		date[11];
	u_int		date_len;

#define	HR_SWINSTALLED_FOUND		0x001
#define	HR_SWINSTALLED_IMMUTABLE	0x002
	uint32_t	flags;

	TAILQ_ENTRY(swins_entry) link;
};
TAILQ_HEAD(swins_tbl, swins_entry);

/*
 * Table to keep a conistent mapping between software and indexes.
 */
struct swins_map_entry {
	int32_t	index;	/* swins_entry::index */
	u_char	*name;	/* map key,a copy of swins_entry::name*/

	/*
	 * next may be NULL if the respective hrSWInstalledTblEntry
	 * is (temporally) gone
	 */
	struct swins_entry *entry;

	STAILQ_ENTRY(swins_map_entry) link;
};
STAILQ_HEAD(swins_map, swins_map_entry);

/* map for consistent indexing */
static struct swins_map swins_map = STAILQ_HEAD_INITIALIZER(swins_map);

/* the head of the list with hrSWInstalledTable's entries */
static struct swins_tbl swins_tbl = TAILQ_HEAD_INITIALIZER(swins_tbl);

/* next int available for indexing the hrSWInstalledTable */
static uint32_t next_swins_index = 1;

/* last (agent) tick when hrSWInstalledTable was updated */
static uint64_t swins_tick;

/* maximum number of ticks between updates of network table */
uint32_t swins_tbl_refresh = HR_SWINS_TBL_REFRESH * 100;

/* package directory */
u_char *pkg_dir;

/* last change of package list */
static time_t os_pkg_last_change;

/**
 * Create a new entry into the hrSWInstalledTable
 */
static struct swins_entry *
swins_entry_create(const char *name)
{
	struct swins_entry *entry;
	struct swins_map_entry *map;

	STAILQ_FOREACH(map, &swins_map, link)
		if (strcmp((const char *)map->name, name) == 0)
			break;

	if (map == NULL) {
		size_t name_len;
		/* new object - get a new index */
		if (next_swins_index > INT_MAX) {
			syslog(LOG_ERR, "%s: hrSWInstalledTable index wrap",
			    __func__ );
			/* There isn't much we can do here.
			 * If the next_swins_index is consumed
			 * then we can't add entries to this table
			 * So it is better to exit - if the table is sparsed
			 * at the next agent run we can fill it fully.
			 */
			errx(EX_SOFTWARE, "hrSWInstalledTable index wrap");
		}

		if ((map = malloc(sizeof(*map))) == NULL) {
			syslog(LOG_ERR, "%s: %m", __func__ );
			return (NULL);
		}

		name_len = strlen(name) + 1;
		if (name_len > SW_NAME_MLEN)
			 name_len = SW_NAME_MLEN;

		if ((map->name = malloc(name_len)) == NULL) {
			syslog(LOG_WARNING, "%s: %m", __func__);
			free(map);
			return (NULL);
		}

		map->index = next_swins_index++;
		strlcpy((char *)map->name, name, name_len);

		STAILQ_INSERT_TAIL(&swins_map, map, link);

		HRDBG("%s added into hrSWInstalled at %d", name, map->index);
	}

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		syslog(LOG_WARNING, "%s: %m", __func__);
		return (NULL);
	}
	memset(entry, 0, sizeof(*entry));

	if ((entry->name = strdup(map->name)) == NULL) {
		syslog(LOG_WARNING, "%s: %m", __func__);
		free(entry);
		return (NULL);
	}

	entry->index = map->index;
	map->entry = entry;

	INSERT_OBJECT_INT(entry, &swins_tbl);

	return (entry);
}

/**
 * Delete an entry in the hrSWInstalledTable
 */
static void
swins_entry_delete(struct swins_entry *entry)
{
	struct swins_map_entry *map;

	assert(entry != NULL);

	TAILQ_REMOVE(&swins_tbl, entry, link);

	STAILQ_FOREACH(map, &swins_map, link)
		if (map->entry == entry) {
			map->entry = NULL;
			break;
		}

	free(entry->name);
	free(entry);
}

/**
 * Find an entry given it's name
 */
static struct swins_entry *
swins_find_by_name(const char *name)
{
	struct swins_entry *entry;

	TAILQ_FOREACH(entry, &swins_tbl, link)
		if (strcmp((const char*)entry->name, name) == 0)
			return (entry);
	return (NULL);
}

/**
 * Finalize this table
 */
void
fini_swins_tbl(void)
{
	struct swins_map_entry  *n1;

	while ((n1 = STAILQ_FIRST(&swins_map)) != NULL) {
		STAILQ_REMOVE_HEAD(&swins_map, link);
		if (n1->entry != NULL) {
			TAILQ_REMOVE(&swins_tbl, n1->entry, link);
			free(n1->entry->name);
			free(n1->entry);
		}
		free(n1->name);
		free(n1);
	}
	assert(TAILQ_EMPTY(&swins_tbl));
}

/**
 * Get the *running* O/S identification
 */
static void
swins_get_OS_ident(void)
{
	struct utsname os_id;
	char os_string[SW_NAME_MLEN] = "";
	struct swins_entry *entry;
	u_char *boot;
	struct stat sb;
	struct tm k_ts;

	if (uname(&os_id) == -1) {
		syslog(LOG_WARNING, "%s: %m", __func__);
		return;
	}

	snprintf(os_string, sizeof(os_string), "%s: %s",
	    os_id.sysname, os_id.version);

	if ((entry = swins_find_by_name(os_string)) != NULL ||
	    (entry = swins_entry_create(os_string)) == NULL)
		return;

	entry->flags |= (HR_SWINSTALLED_FOUND | HR_SWINSTALLED_IMMUTABLE);
	entry->id = &oid_zeroDotZero;
	entry->type = (int32_t)SWI_OPERATING_SYSTEM;
	memset(entry->date, 0, sizeof(entry->date));

	if (OS_getSystemInitialLoadParameters(&boot) == SNMP_ERR_NOERROR &&
	    strlen(boot) > 0 && stat(boot, &sb) == 0 &&
	    localtime_r(&sb.st_ctime, &k_ts) != NULL)
		entry->date_len = make_date_time(entry->date, &k_ts, 0);
}

/**
 * Read the installed packages
 */
static int
swins_get_packages(void)
{
	struct stat sb;
	DIR *p_dir;
	struct dirent *ent;
	struct tm k_ts;
	char *pkg_file;
	struct swins_entry *entry;
	int ret = 0;

	if (pkg_dir == NULL)
		/* initialisation may have failed */
		return (-1);

	if (stat(pkg_dir, &sb) != 0) {
		syslog(LOG_ERR, "hrSWInstalledTable: stat(\"%s\") failed: %m",
		    pkg_dir);
		return (-1);
	}
	if (!S_ISDIR(sb.st_mode)) {
		syslog(LOG_ERR, "hrSWInstalledTable: \"%s\" is not a directory",
		    pkg_dir);
		return (-1);
	}
	if (sb.st_ctime <= os_pkg_last_change) {
		HRDBG("no need to rescan installed packages -- "
		    "directory time-stamp unmodified");

		TAILQ_FOREACH(entry, &swins_tbl, link)
			entry->flags |= HR_SWINSTALLED_FOUND;

		return (0);
	}

	if ((p_dir = opendir(pkg_dir)) == NULL) {
		syslog(LOG_ERR, "hrSWInstalledTable: opendir(\"%s\") failed: "
		    "%m", pkg_dir);
		return (-1);
	}

	while (errno = 0, (ent = readdir(p_dir)) != NULL) {
		HRDBG("  pkg file: %s", ent->d_name);

		/* check that the contents file is a regular file */
		if (asprintf(&pkg_file, "%s/%s/%s", pkg_dir, ent->d_name,
		    CONTENTS_FNAME) == -1)
			continue;

		if (stat(pkg_file, &sb) != 0 ) {
			free(pkg_file);
			continue;
		}

		if (!S_ISREG(sb.st_mode)) {
			syslog(LOG_ERR, "hrSWInstalledTable: \"%s\" not a "
			    "regular file -- skipped", pkg_file);
			free(pkg_file);
			continue;
		}
		free(pkg_file);

		/* read directory timestamp on package */
		if (asprintf(&pkg_file, "%s/%s", pkg_dir, ent->d_name) == -1)
			continue;

		if (stat(pkg_file, &sb) == -1 ||
		    localtime_r(&sb.st_ctime, &k_ts) == NULL) {
			free(pkg_file);
			continue;
		}
		free(pkg_file);

		/* update or create entry */
		if ((entry = swins_find_by_name(ent->d_name)) == NULL &&
		    (entry = swins_entry_create(ent->d_name)) == NULL) {
			ret = -1;
			goto PKG_LOOP_END;
		}

		entry->flags |= HR_SWINSTALLED_FOUND;
		entry->id = &oid_zeroDotZero;
		entry->type = (int32_t)SWI_APPLICATION;

		entry->date_len = make_date_time(entry->date, &k_ts, 0);
	}

	if (errno != 0) {
		syslog(LOG_ERR, "hrSWInstalledTable: readdir_r(\"%s\") failed:"
		    " %m", pkg_dir);
		ret = -1;
	} else {
		/*
		 * save the timestamp of directory
		 * to avoid any further scanning
		 */
		os_pkg_last_change = sb.st_ctime;
	}
  PKG_LOOP_END:
	(void)closedir(p_dir);
	return (ret);
}

/**
 * Refresh the installed software table.
 */
void
refresh_swins_tbl(void)
{
	int ret;
	struct swins_entry *entry, *entry_tmp;

	if (this_tick - swins_tick < swins_tbl_refresh) {
		HRDBG("no refresh needed");
		return;
	}

	/* mark each entry as missing */
	TAILQ_FOREACH(entry, &swins_tbl, link)
		entry->flags &= ~HR_SWINSTALLED_FOUND;

	ret = swins_get_packages();

	TAILQ_FOREACH_SAFE(entry, &swins_tbl, link, entry_tmp)
		if (!(entry->flags & HR_SWINSTALLED_FOUND) &&
		    !(entry->flags & HR_SWINSTALLED_IMMUTABLE))
			swins_entry_delete(entry);

	if (ret == 0)
		swins_tick = this_tick;
}

/**
 * Create and populate the package table
 */
void
init_swins_tbl(void)
{

	if ((pkg_dir = malloc(sizeof(PATH_PKGDIR))) == NULL)
		syslog(LOG_ERR, "%s: %m", __func__);
	else
		strcpy(pkg_dir, PATH_PKGDIR);

	swins_get_OS_ident();
	refresh_swins_tbl();

	HRDBG("init done");
}

/**
 * SNMP handler
 */
int
op_hrSWInstalledTable(struct snmp_context *ctx __unused,
    struct snmp_value *value, u_int sub, u_int iidx __unused,
    enum snmp_op curr_op)
{
	struct swins_entry *entry;

	refresh_swins_tbl();

	switch (curr_op) {

	  case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_INT(&swins_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = entry->index;
		goto get;

	  case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_INT(&swins_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	  case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_INT(&swins_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NO_CREATION);
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		abort();
	}
	abort();

  get:
	switch (value->var.subs[sub - 1]) {

	  case LEAF_hrSWInstalledIndex:
		value->v.integer = entry->index;
		return (SNMP_ERR_NOERROR);

	  case LEAF_hrSWInstalledName:
		return (string_get(value, entry->name, -1));
		break;

	  case LEAF_hrSWInstalledID:
		assert(entry->id != NULL);
		value->v.oid = *entry->id;
		return (SNMP_ERR_NOERROR);

	  case LEAF_hrSWInstalledType:
		value->v.integer = entry->type;
		return (SNMP_ERR_NOERROR);

	  case LEAF_hrSWInstalledDate:
		return (string_get(value, entry->date, entry->date_len));
	}
	abort();
}

/**
 * Scalars
 */
int
op_hrSWInstalled(struct snmp_context *ctx __unused,
    struct snmp_value *value __unused, u_int sub,
    u_int iidx __unused, enum snmp_op curr_op)
{

	/* only SNMP GET is possible */
	switch (curr_op) {

	case SNMP_OP_GET:
		goto get;

	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);

	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
	case SNMP_OP_GETNEXT:
		abort();
	}
	abort();

  get:
	switch (value->var.subs[sub - 1]) {

	case LEAF_hrSWInstalledLastChange:
	case LEAF_hrSWInstalledLastUpdateTime:
		/*
		 * We always update the entire table so these two tick
		 * values should be equal.
		 */
		refresh_swins_tbl();
		if (swins_tick <= start_tick)
			value->v.uint32 = 0;
		else {
			uint64_t lastChange = swins_tick - start_tick;

			/* may overflow the SNMP type */
			value->v.uint32 =
			    (lastChange > UINT_MAX ? UINT_MAX : lastChange);
		}

		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}
}
