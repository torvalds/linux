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
 * Host Resources MIB for SNMPd. Implementation for the hrDiskStorageTable
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ata.h>
#include <sys/disk.h>
#include <sys/linker.h>
#include <sys/mdioctl.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <ctype.h>
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

enum hrDiskStrorageAccess {
	DS_READ_WRITE = 1,
	DS_READ_ONLY  = 2
};

enum hrDiskStrorageMedia {
	DSM_OTHER	=	1,
	DSM_UNKNOWN	=	2,
	DSM_HARDDISK	=	3,
	DSM_FLOPPYDISK	=	4,
	DSM_OPTICALDISKROM=	5,
	DSM_OPTICALDISKWORM=	6,
	DSM_OPTICALDISKRW=	7,
	DSM_RAMDISK	=	8
};

/*
 * This structure is used to hold a SNMP table entry for HOST-RESOURCES-MIB's
 * hrDiskStorageTable. Note that index is external being allocated and
 * maintained by the hrDeviceTable code.
 *
 * NOTE: according to MIB removable means removable media, not the
 * device itself (like a USB card reader)
 */
struct disk_entry {
	int32_t		index;
	int32_t		access;		/* enum hrDiskStrorageAccess */
	int32_t		media;		/* enum hrDiskStrorageMedia*/
	int32_t		removable; 	/* enum snmpTCTruthValue*/
	int32_t		capacity;
	TAILQ_ENTRY(disk_entry) link;
	/*
	 * next items are not from the SNMP mib table, only to be used
	 * internally
	 */
#define HR_DISKSTORAGE_FOUND	0x001
#define HR_DISKSTORAGE_ATA	0x002 /* belongs to the ATA subsystem */
#define HR_DISKSTORAGE_MD	0x004 /* it is a MD (memory disk) */
	uint32_t	flags;
	uint64_t	r_tick;
	u_char		dev_name[32];	/* device name, i.e. "ad4" or "acd0" */
};
TAILQ_HEAD(disk_tbl, disk_entry);

/* the head of the list with hrDiskStorageTable's entries */
static struct disk_tbl disk_tbl =
    TAILQ_HEAD_INITIALIZER(disk_tbl);

/* last tick when hrFSTable was updated */
static uint64_t disk_storage_tick;

/* minimum number of ticks between refreshs */
uint32_t disk_storage_tbl_refresh = HR_DISK_TBL_REFRESH * 100;

/* fd for "/dev/mdctl"*/
static int md_fd = -1;

/* buffer for sysctl("kern.disks") */
static char *disk_list;
static size_t disk_list_len;

/* some constants */
static const struct asn_oid OIDX_hrDeviceDiskStorage_c =
    OIDX_hrDeviceDiskStorage;

/**
 * Load the MD driver if it isn't loaded already.
 */
static void
mdmaybeload(void)
{
	char name1[64], name2[64];

	snprintf(name1, sizeof(name1), "g_%s", MD_NAME);
	snprintf(name2, sizeof(name2), "geom_%s", MD_NAME);
	if (modfind(name1) == -1) {
		/* Not present in kernel, try loading it. */
		if (kldload(name2) == -1 || modfind(name1) == -1) {
			if (errno != EEXIST) {
				errx(EXIT_FAILURE,
				    "%s module not available!", name2);
			}
		}
	}
}

/**
 * Create a new entry into the DiskStorageTable.
 */
static struct disk_entry *
disk_entry_create(const struct device_entry *devEntry)
{
	struct disk_entry *entry;

	assert(devEntry != NULL);
	if (devEntry == NULL)
		return NULL;

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		syslog(LOG_WARNING, "hrDiskStorageTable: %s: %m", __func__);
		return (NULL);
	}

	memset(entry, 0, sizeof(*entry));
	entry->index = devEntry->index;
	INSERT_OBJECT_INT(entry, &disk_tbl);

	return (entry);
}

/**
 * Delete a disk table entry.
 */
static void
disk_entry_delete(struct disk_entry *entry)
{
	struct device_entry *devEntry;

	assert(entry != NULL);
	TAILQ_REMOVE(&disk_tbl, entry, link);

	devEntry = device_find_by_index(entry->index);

	free(entry);

	/*
	 * Also delete the respective device entry -
	 * this is needed for disk devices that are not
	 * detected by libdevinfo
	 */
	if (devEntry != NULL &&
	    (devEntry->flags & HR_DEVICE_IMMUTABLE) == HR_DEVICE_IMMUTABLE)
		device_entry_delete(devEntry);
}

/**
 * Find a disk storage entry given its index.
 */
static struct disk_entry *
disk_find_by_index(int32_t idx)
{
	struct disk_entry *entry;

	TAILQ_FOREACH(entry, &disk_tbl, link)
		if (entry->index == idx)
			return (entry);

	return (NULL);
}

/**
 * Get the disk parameters
 */
static void
disk_query_disk(struct disk_entry *entry)
{
	char dev_path[128];
	int fd;
	off_t mediasize;

	if (entry == NULL || entry->dev_name[0] == '\0')
		return;

	snprintf(dev_path, sizeof(dev_path),
	    "%s%s", _PATH_DEV, entry->dev_name);
	entry->capacity = 0;

	HRDBG("OPENING device %s", dev_path);
	if ((fd = open(dev_path, O_RDONLY|O_NONBLOCK)) == -1) {
		HRDBG("OPEN device %s failed: %s", dev_path, strerror(errno));
		return;
	}

	if (ioctl(fd, DIOCGMEDIASIZE, &mediasize) < 0) {
		HRDBG("DIOCGMEDIASIZE for device %s failed: %s",
		    dev_path, strerror(errno));
		(void)close(fd);
		return;
	}

	mediasize = mediasize / 1024;
	entry->capacity = (mediasize > INT_MAX ? INT_MAX : mediasize);
	partition_tbl_handle_disk(entry->index, entry->dev_name);

	(void)close(fd);
}

/**
 * Find all ATA disks in the device table.
 */
static void
disk_OS_get_ATA_disks(void)
{
	struct device_map_entry *map;
	struct device_entry *entry;
	struct disk_entry *disk_entry;
	const struct disk_entry *found;

	/* Things we know are ata disks */
	static const struct disk_entry lookup[] = {
		{
		    .dev_name = "ad",
		    .media = DSM_HARDDISK,
		    .removable = SNMP_FALSE
		},
		{
		    .dev_name = "ar",
		    .media = DSM_OTHER,
		    .removable = SNMP_FALSE
		},
		{
		    .dev_name = "acd",
		    .media = DSM_OPTICALDISKROM,
		    .removable = SNMP_TRUE
		},
		{
		    .dev_name = "afd",
		    .media = DSM_FLOPPYDISK,
		    .removable = SNMP_TRUE
		},
		{
		    .dev_name = "ast",
		    .media = DSM_OTHER,
		    .removable = SNMP_TRUE
		},

		{ .media = DSM_UNKNOWN }
	};

	/* Walk over the device table looking for ata disks */
	STAILQ_FOREACH(map, &device_map, link) {
		/* Skip deleted entries. */
		if (map->entry_p == NULL)
			continue;
		for (found = lookup; found->media != DSM_UNKNOWN; found++) {
			if (strncmp(map->name_key, found->dev_name,
			    strlen(found->dev_name)) != 0)
				continue;

			/*
			 * Avoid false disk devices. For example adw(4) and
			 * adv(4) - they are not disks!
			 */
			if (strlen(map->name_key) > strlen(found->dev_name) &&
			    !isdigit(map->name_key[strlen(found->dev_name)]))
				continue;

			/* First get the entry from the hrDeviceTbl */
			entry = map->entry_p;
			entry->type = &OIDX_hrDeviceDiskStorage_c;

			/* Then check hrDiskStorage table for this device */
			disk_entry = disk_find_by_index(entry->index);
			if (disk_entry == NULL) {
				disk_entry = disk_entry_create(entry);
				if (disk_entry == NULL)
					continue;

				disk_entry->access = DS_READ_WRITE;
				strlcpy(disk_entry->dev_name, entry->name,
				    sizeof(disk_entry->dev_name));

				disk_entry->media = found->media;
				disk_entry->removable = found->removable;
			}

			disk_entry->flags |= HR_DISKSTORAGE_FOUND;
			disk_entry->flags |= HR_DISKSTORAGE_ATA;

			disk_query_disk(disk_entry);
			disk_entry->r_tick = this_tick;
		}
	}
}

/**
 * Find MD disks in the device table.
 */
static void
disk_OS_get_MD_disks(void)
{
	struct device_map_entry *map;
	struct device_entry *entry;
	struct disk_entry *disk_entry;
	struct md_ioctl mdio;
	int unit;

	if (md_fd <= 0)
		return;

	/* Look for md devices */
	STAILQ_FOREACH(map, &device_map, link) {
		/* Skip deleted entries. */
		if (map->entry_p == NULL)
			continue;
		if (sscanf(map->name_key, "md%d", &unit) != 1)
			continue;

		/* First get the entry from the hrDeviceTbl */
		entry = device_find_by_index(map->hrIndex);
		entry->type = &OIDX_hrDeviceDiskStorage_c;

		/* Then check hrDiskStorage table for this device */
		disk_entry = disk_find_by_index(entry->index);
		if (disk_entry == NULL) {
			disk_entry = disk_entry_create(entry);
			if (disk_entry == NULL)
				continue;

			memset(&mdio, 0, sizeof(mdio));
			mdio.md_version = MDIOVERSION;
			mdio.md_unit = unit;

			if (ioctl(md_fd, MDIOCQUERY, &mdio) < 0) {
				syslog(LOG_ERR,
				    "hrDiskStorageTable: Couldnt ioctl");
				continue;
			}

			if ((mdio.md_options & MD_READONLY) == MD_READONLY)
				disk_entry->access = DS_READ_ONLY;
			else
				disk_entry->access = DS_READ_WRITE;

			strlcpy(disk_entry->dev_name, entry->name,
			    sizeof(disk_entry->dev_name));

			disk_entry->media = DSM_RAMDISK;
			disk_entry->removable = SNMP_FALSE;
		}

		disk_entry->flags |= HR_DISKSTORAGE_FOUND;
		disk_entry->flags |= HR_DISKSTORAGE_MD;
		disk_entry->r_tick = this_tick;
	}
}

/**
 * Find rest of disks
 */
static void
disk_OS_get_disks(void)
{
	size_t disk_cnt = 0;
	struct device_entry *entry;
	struct disk_entry *disk_entry;

	size_t need = 0;

	if (sysctlbyname("kern.disks", NULL, &need, NULL, 0) == -1) {
		syslog(LOG_ERR, "%s: sysctl_1 kern.disks failed: %m", __func__);
		return;
	}

	if (need == 0)
		return;

	if (disk_list_len != need + 1 || disk_list == NULL) {
		disk_list_len = need + 1;
		disk_list = reallocf(disk_list, disk_list_len);
	}

	if (disk_list == NULL) {
		syslog(LOG_ERR, "%s: reallocf failed", __func__);
		disk_list_len = 0;
		return;
	}

	memset(disk_list, 0, disk_list_len);

	if (sysctlbyname("kern.disks", disk_list, &need, NULL, 0) == -1 ||
	    disk_list[0] == 0) {
		syslog(LOG_ERR, "%s: sysctl_2 kern.disks failed: %m", __func__);
		return;
	}

	for (disk_cnt = 0; disk_cnt < need; disk_cnt++) {
		char *disk = NULL;
		char disk_device[128] = "";

		disk = strsep(&disk_list, " ");
		if (disk == NULL)
			break;

		snprintf(disk_device, sizeof(disk_device),
		    "%s%s", _PATH_DEV, disk);

		/* First check if the disk is in the hrDeviceTable. */
		if ((entry = device_find_by_name(disk)) == NULL) {
			/*
			 * not found there - insert it as immutable
			 */
			syslog(LOG_WARNING, "%s: adding device '%s' to "
			    "device list", __func__, disk);

			if ((entry = device_entry_create(disk, "", "")) == NULL)
				continue;

			entry->flags |= HR_DEVICE_IMMUTABLE;
		}

		entry->type = &OIDX_hrDeviceDiskStorage_c;

		/* Then check hrDiskStorage table for this device */
		disk_entry = disk_find_by_index(entry->index);
		if (disk_entry == NULL) {
			disk_entry = disk_entry_create(entry);
			if (disk_entry == NULL)
				continue;
		}

		disk_entry->flags |= HR_DISKSTORAGE_FOUND;

		if ((disk_entry->flags & HR_DISKSTORAGE_ATA) ||
		    (disk_entry->flags & HR_DISKSTORAGE_MD)) {
			/*
			 * ATA/MD detection is running before this one,
			 * so don't waste the time here
			 */
			continue;
		}

		disk_entry->access = DS_READ_WRITE;
		disk_entry->media = DSM_UNKNOWN;
		disk_entry->removable = SNMP_FALSE;

		if (strncmp(disk_entry->dev_name, "da", 2) == 0 ||
		    strncmp(disk_entry->dev_name, "ada", 3) == 0) {
			disk_entry->media = DSM_HARDDISK;
			disk_entry->removable = SNMP_FALSE;
		} else if (strncmp(disk_entry->dev_name, "cd", 2) == 0) {
			disk_entry->media = DSM_OPTICALDISKROM;
			disk_entry->removable = SNMP_TRUE;
	 	} else {
			disk_entry->media = DSM_UNKNOWN;
			disk_entry->removable = SNMP_FALSE;
		}

		strlcpy((char *)disk_entry->dev_name, disk,
		    sizeof(disk_entry->dev_name));

		disk_query_disk(disk_entry);
		disk_entry->r_tick = this_tick;
	}
}

/**
 * Refresh routine for hrDiskStorageTable
 * Usable for polling the system for any changes.
 */
void
refresh_disk_storage_tbl(int force)
{
	struct disk_entry *entry, *entry_tmp;

	if (disk_storage_tick != 0 && !force &&
	    this_tick - disk_storage_tick < disk_storage_tbl_refresh) {
		HRDBG("no refresh needed");
		return;
	}

	partition_tbl_pre_refresh();

	/* mark each entry as missing */
	TAILQ_FOREACH(entry, &disk_tbl, link)
		entry->flags &= ~HR_DISKSTORAGE_FOUND;

	disk_OS_get_ATA_disks();	/* this must be called first ! */
	disk_OS_get_MD_disks();
	disk_OS_get_disks();

	/*
	 * Purge items that disappeared
	 */
	TAILQ_FOREACH_SAFE(entry, &disk_tbl, link, entry_tmp)
		if (!(entry->flags & HR_DISKSTORAGE_FOUND))
			/* XXX remove IMMUTABLE entries that have disappeared */
			disk_entry_delete(entry);

	disk_storage_tick = this_tick;

	partition_tbl_post_refresh();

	HRDBG("refresh DONE");
}

/*
 * Init the things for both of hrDiskStorageTable
 */
int
init_disk_storage_tbl(void)
{
	char mddev[32] = "";

	/* Try to load md.ko if not loaded already */
	mdmaybeload();

	md_fd = -1;
	snprintf(mddev, sizeof(mddev) - 1, "%s%s", _PATH_DEV, MDCTL_NAME);
	if ((md_fd = open(mddev, O_RDWR)) == -1) {
		syslog(LOG_ERR, "open %s failed - will not include md(4) "
		    "info: %m", mddev);
	}

	refresh_disk_storage_tbl(1);

	return (0);
}

/*
 * Finalization routine for hrDiskStorageTable
 * It destroys the lists and frees any allocated heap memory
 */
void
fini_disk_storage_tbl(void)
{
	struct disk_entry *n1;

	while ((n1 = TAILQ_FIRST(&disk_tbl)) != NULL) {
		TAILQ_REMOVE(&disk_tbl, n1, link);
		free(n1);
	}

	free(disk_list);

	if (md_fd > 0) {
		if (close(md_fd) == -1)
			syslog(LOG_ERR,"close (/dev/mdctl) failed: %m");
		md_fd = -1;
	}
}

/*
 * This is the implementation for a generated (by our SNMP "compiler" tool)
 * function prototype, see hostres_tree.h
 * It handles the SNMP operations for hrDiskStorageTable
 */
int
op_hrDiskStorageTable(struct snmp_context *ctx __unused,
    struct snmp_value *value, u_int sub, u_int iidx __unused,
    enum snmp_op curr_op)
{
	struct disk_entry *entry;

	refresh_disk_storage_tbl(0);

	switch (curr_op) {

	case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_INT(&disk_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = entry->index;
		goto get;

	case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_INT(&disk_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_INT(&disk_tbl,
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

	case LEAF_hrDiskStorageAccess:
	  	value->v.integer = entry->access;
	  	return (SNMP_ERR_NOERROR);

	case LEAF_hrDiskStorageMedia:
	  	value->v.integer = entry->media;
	  	return (SNMP_ERR_NOERROR);

	case LEAF_hrDiskStorageRemovable:
	  	value->v.integer = entry->removable;
	  	return (SNMP_ERR_NOERROR);

	case LEAF_hrDiskStorageCapacity:
	  	value->v.integer = entry->capacity;
	  	return (SNMP_ERR_NOERROR);
	}
	abort();
}
