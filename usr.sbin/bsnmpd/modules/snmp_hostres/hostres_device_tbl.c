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
 * Host Resources MIB: hrDeviceTable implementation for SNMPd.
 */

#include <sys/un.h>
#include <sys/limits.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sysexits.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

#define FREE_DEV_STRUCT(entry_p) do {		\
	free(entry_p->name);			\
	free(entry_p->location);		\
	free(entry_p->descr);			\
	free(entry_p);				\
} while (0)

/*
 * Status of a device
 */
enum DeviceStatus {
	DS_UNKNOWN	= 1,
	DS_RUNNING	= 2,
	DS_WARNING	= 3,
	DS_TESTING	= 4,
	DS_DOWN		= 5
};

TAILQ_HEAD(device_tbl, device_entry);

/* the head of the list with hrDeviceTable's entries */
static struct device_tbl device_tbl = TAILQ_HEAD_INITIALIZER(device_tbl);

/* Table used for consistent device table indexing. */
struct device_map device_map = STAILQ_HEAD_INITIALIZER(device_map);

/* next int available for indexing the hrDeviceTable */
static uint32_t next_device_index = 1;

/* last (agent) tick when hrDeviceTable was updated */
static uint64_t device_tick = 0;

/* maximum number of ticks between updates of device table */
uint32_t device_tbl_refresh = 10 * 100;

/* socket for /var/run/devd.pipe */
static int devd_sock = -1;

/* used to wait notifications from /var/run/devd.pipe */
static void *devd_fd;

/* some constants */
static const struct asn_oid OIDX_hrDeviceProcessor_c = OIDX_hrDeviceProcessor;
static const struct asn_oid OIDX_hrDeviceOther_c = OIDX_hrDeviceOther;

/**
 * Create a new entry out of thin air.
 */
struct device_entry *
device_entry_create(const char *name, const char *location, const char *descr)
{
	struct device_entry *entry = NULL;
	struct device_map_entry *map = NULL;
	size_t name_len;
	size_t location_len;

	assert((name[0] != 0) || (location[0] != 0));

	if (name[0] == 0 && location[0] == 0)
		return (NULL);

	STAILQ_FOREACH(map, &device_map, link) {
		assert(map->name_key != NULL);
		assert(map->location_key != NULL);

		if (strcmp(map->name_key, name) == 0 &&
		    strcmp(map->location_key, location) == 0) {
			break;
		}
	}

	if (map == NULL) {
		/* new object - get a new index */
		if (next_device_index > INT_MAX) {
			syslog(LOG_ERR,
			    "%s: hrDeviceTable index wrap", __func__);
			/* There isn't much we can do here.
			 * If the next_swins_index is consumed
			 * then we can't add entries to this table
			 * So it is better to exit - if the table is sparsed
			 * at the next agent run we can fill it fully.
			 */
			errx(EX_SOFTWARE, "hrDeviceTable index wrap");
			/* not reachable */
		}

		if ((map = malloc(sizeof(*map))) == NULL) {
			syslog(LOG_ERR, "hrDeviceTable: %s: %m", __func__ );
			return (NULL);
		}

		map->entry_p = NULL;

		name_len = strlen(name) + 1;
		if (name_len > DEV_NAME_MLEN)
			name_len = DEV_NAME_MLEN;

		if ((map->name_key = malloc(name_len)) == NULL) {
			syslog(LOG_ERR, "hrDeviceTable: %s: %m", __func__ );
			free(map);
			return (NULL);
		}

		location_len = strlen(location) + 1;
		if (location_len > DEV_LOC_MLEN)
			location_len = DEV_LOC_MLEN;

		if ((map->location_key = malloc(location_len )) == NULL) {
			syslog(LOG_ERR, "hrDeviceTable: %s: %m", __func__ );
			free(map->name_key);
			free(map);
			return (NULL);
		}

		map->hrIndex = next_device_index++;

		strlcpy(map->name_key, name, name_len);
		strlcpy(map->location_key, location, location_len);

		STAILQ_INSERT_TAIL(&device_map, map, link);
		HRDBG("%s at %s added into hrDeviceMap at index=%d",
		    name, location, map->hrIndex);
	} else {
		HRDBG("%s at %s exists in hrDeviceMap index=%d",
		    name, location, map->hrIndex);
	}

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		syslog(LOG_WARNING, "hrDeviceTable: %s: %m", __func__);
		return (NULL);
	}
	memset(entry, 0, sizeof(*entry));

	entry->index = map->hrIndex;
	map->entry_p = entry;

	if ((entry->name = strdup(map->name_key)) == NULL) {
		syslog(LOG_ERR, "hrDeviceTable: %s: %m", __func__ );
		free(entry);
		return (NULL);
	}

	if ((entry->location = strdup(map->location_key)) == NULL) {
		syslog(LOG_ERR, "hrDeviceTable: %s: %m", __func__ );
		free(entry->name);
		free(entry);
		return (NULL);
	}

	/*
	 * From here till the end of this function we reuse name_len
	 * for a different purpose - for device_entry::descr
	 */
	if (name[0] != '\0')
		name_len = strlen(name) + strlen(descr) +
		    strlen(": ") + 1;
	else
		name_len = strlen(location) + strlen(descr) +
		    strlen("unknown at : ") + 1;

	if (name_len > DEV_DESCR_MLEN)
		name_len = DEV_DESCR_MLEN;

	if ((entry->descr = malloc(name_len )) == NULL) {
		syslog(LOG_ERR, "hrDeviceTable: %s: %m", __func__ );
		free(entry->name);
		free(entry->location);
		free(entry);
		return (NULL);
	}

	memset(&entry->descr[0], '\0', name_len);

	if (name[0] != '\0')
		snprintf(entry->descr, name_len,
		    "%s: %s", name, descr);
	else
		snprintf(entry->descr, name_len,
		    "unknown at %s: %s", location, descr);

	entry->id = &oid_zeroDotZero;	/* unknown id - FIXME */
	entry->status = (u_int)DS_UNKNOWN;
	entry->errors = 0;
	entry->type = &OIDX_hrDeviceOther_c;

	INSERT_OBJECT_INT(entry, &device_tbl);

	return (entry);
}

/**
 * Create a new entry into the device table.
 */
static struct device_entry *
device_entry_create_devinfo(const struct devinfo_dev *dev_p)
{

	assert(dev_p->dd_name != NULL);
	assert(dev_p->dd_location != NULL);

	return (device_entry_create(dev_p->dd_name, dev_p->dd_location,
	    dev_p->dd_desc));
}

/**
 * Delete an entry from the device table.
 */
void
device_entry_delete(struct device_entry *entry)
{
	struct device_map_entry *map;

	assert(entry != NULL);

	TAILQ_REMOVE(&device_tbl, entry, link);

	STAILQ_FOREACH(map, &device_map, link)
		if (map->entry_p == entry) {
			map->entry_p = NULL;
			break;
		}

	FREE_DEV_STRUCT(entry);
}

/**
 * Find an entry given its name and location
 */
static struct device_entry *
device_find_by_dev(const struct devinfo_dev *dev_p)
{
	struct device_map_entry  *map;

	assert(dev_p != NULL);

	STAILQ_FOREACH(map, &device_map, link)
		if (strcmp(map->name_key, dev_p->dd_name) == 0 &&
		    strcmp(map->location_key, dev_p->dd_location) == 0)
		    	return (map->entry_p);
	return (NULL);
}

/**
 * Find an entry given its index.
 */
struct device_entry *
device_find_by_index(int32_t idx)
{
	struct device_entry *entry;

	TAILQ_FOREACH(entry, &device_tbl, link)
		if (entry->index == idx)
			return (entry);
	return (NULL);
}

/**
 * Find an device entry given its name.
 */
struct device_entry *
device_find_by_name(const char *dev_name)
{
	struct device_map_entry *map;

	assert(dev_name != NULL);

	STAILQ_FOREACH(map, &device_map, link)
		if (strcmp(map->name_key, dev_name) == 0)
			return (map->entry_p);

	return (NULL);
}

/**
 * Find out the type of device. CPU only currently.
 */
static void
device_get_type(struct devinfo_dev *dev_p, const struct asn_oid **out_type_p)
{

	assert(dev_p != NULL);
	assert(out_type_p != NULL);

	if (dev_p == NULL)
		return;

	if (strncmp(dev_p->dd_name, "cpu", strlen("cpu")) == 0 &&
	    strstr(dev_p->dd_location, ".CPU") != NULL) {
		*out_type_p = &OIDX_hrDeviceProcessor_c;
		return;
	}
}

/**
 * Get the status of a device
 */
static enum DeviceStatus
device_get_status(struct devinfo_dev *dev)
{

	assert(dev != NULL);

	switch (dev->dd_state) {
	case DS_ALIVE:			/* probe succeeded */
	case DS_NOTPRESENT:		/* not probed or probe failed */
		return (DS_DOWN);
	case DS_ATTACHED:		/* attach method called */
	case DS_BUSY:			/* device is open */
		return (DS_RUNNING);
	default:
		return (DS_UNKNOWN);
	}
}

/**
 * Get the info for the given device and then recursively process all
 * child devices.
 */
static int
device_collector(struct devinfo_dev *dev, void *arg)
{
	struct device_entry *entry;

	HRDBG("%llu/%llu name='%s' desc='%s' drivername='%s' location='%s'",
	    (unsigned long long)dev->dd_handle,
	    (unsigned long long)dev->dd_parent, dev->dd_name, dev->dd_desc,
	    dev->dd_drivername, dev->dd_location);

	if (dev->dd_name[0] != '\0' || dev->dd_location[0] != '\0') {
		HRDBG("ANALYZING dev %s at %s",
		    dev->dd_name, dev->dd_location);

		if ((entry = device_find_by_dev(dev)) != NULL) {
			entry->flags |= HR_DEVICE_FOUND;
			entry->status = (u_int)device_get_status(dev);
		} else if ((entry = device_entry_create_devinfo(dev)) != NULL) {
			device_get_type(dev, &entry->type);

			entry->flags |= HR_DEVICE_FOUND;
			entry->status = (u_int)device_get_status(dev);
		}
	} else {
		HRDBG("SKIPPED unknown device at location '%s'",
		    dev->dd_location );
	}

	return (devinfo_foreach_device_child(dev, device_collector, arg));
}

/**
 * Create the socket to the device daemon.
 */
static int
create_devd_socket(void)
{
	int d_sock;
 	struct sockaddr_un devd_addr;

 	bzero(&devd_addr, sizeof(struct sockaddr_un));

 	if ((d_sock = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
 		syslog(LOG_ERR, "Failed to create the socket for %s: %m",
		    PATH_DEVD_PIPE);
 		return (-1);
 	}

 	devd_addr.sun_family = PF_LOCAL;
	devd_addr.sun_len = sizeof(devd_addr);
 	strlcpy(devd_addr.sun_path, PATH_DEVD_PIPE,
	    sizeof(devd_addr.sun_path) - 1);

 	if (connect(d_sock, (struct sockaddr *)&devd_addr,
	    sizeof(devd_addr)) == -1) {
 		syslog(LOG_ERR,"Failed to connect socket for %s: %m",
		    PATH_DEVD_PIPE);
 		if (close(d_sock) < 0 )
 			syslog(LOG_ERR,"Failed to close socket for %s: %m",
			    PATH_DEVD_PIPE);
		return (-1);
 	}

 	return (d_sock);
}

/*
 * Event on the devd socket.
 *
 * We should probably directly process entries here. For simplicity just
 * call the refresh routine with the force flag for now.
 */
static void
devd_socket_callback(int fd, void *arg __unused)
{
	char buf[512];
	int read_len = -1;

	assert(fd == devd_sock);

	HRDBG("called");

again:
	read_len = read(fd, buf, sizeof(buf));
	if (read_len < 0) {
		if (errno == EBADF) {
			devd_sock = -1;
			if (devd_fd != NULL) {
				fd_deselect(devd_fd);
				devd_fd = NULL;
			}
			syslog(LOG_ERR, "Closing devd_fd, revert to "
			    "devinfo polling");
		}

	} else if (read_len == 0) {
		syslog(LOG_ERR, "zero bytes read from devd pipe... "
		    "closing socket!");

		if (close(devd_sock) < 0 )
 			syslog(LOG_ERR, "Failed to close devd socket: %m");

		devd_sock = -1;
		if (devd_fd != NULL) {
			fd_deselect(devd_fd);
			devd_fd = NULL;
		}
		syslog(LOG_ERR, "Closing devd_fd, revert to devinfo polling");

	} else {
		if (read_len == sizeof(buf))
			goto again;
		/* Only refresh device table on a device add or remove event. */
		if (buf[0] == '+' || buf[0] == '-')
			refresh_device_tbl(1);
	}
}

/**
 * Initialize and populate the device table.
 */
void
init_device_tbl(void)
{

	/* initially populate table for the other tables */
	refresh_device_tbl(1);

	/* no problem if that fails - just use polling mode */
	devd_sock = create_devd_socket();
}

/**
 * Start devd(8) monitoring.
 */
void
start_device_tbl(struct lmodule *mod)
{

	if (devd_sock > 0) {
		devd_fd = fd_select(devd_sock, devd_socket_callback, NULL, mod);
		if (devd_fd == NULL)
			syslog(LOG_ERR, "fd_select failed on devd socket: %m");
	}
}

/**
 * Finalization routine for hrDeviceTable
 * It destroys the lists and frees any allocated heap memory
 */
void
fini_device_tbl(void)
{
	struct device_map_entry *n1;

	if (devd_fd != NULL)
		fd_deselect(devd_fd);

	if (devd_sock != -1)
		(void)close(devd_sock);

	devinfo_free();

     	while ((n1 = STAILQ_FIRST(&device_map)) != NULL) {
		STAILQ_REMOVE_HEAD(&device_map, link);
		if (n1->entry_p != NULL) {
			TAILQ_REMOVE(&device_tbl, n1->entry_p, link);
			FREE_DEV_STRUCT(n1->entry_p);
		}
		free(n1->name_key);
		free(n1->location_key);
		free(n1);
     	}
	assert(TAILQ_EMPTY(&device_tbl));
}

/**
 * Refresh routine for hrDeviceTable. We don't refresh here if the devd socket
 * is open, because in this case we have the actual information always. We
 * also don't refresh when the table is new enough (if we don't have a devd
 * socket). In either case a refresh can be forced by passing a non-zero value.
 */
void
refresh_device_tbl(int force)
{
	struct device_entry *entry, *entry_tmp;
	struct devinfo_dev *dev_root;
	static int act = 0;

	if (!force && (devd_sock >= 0 ||
	   (device_tick != 0 && this_tick - device_tick < device_tbl_refresh))){
		HRDBG("no refresh needed");
		return;
	}

	if (act) {
		syslog(LOG_ERR, "%s: recursive call", __func__);
		return;
	}

	if (devinfo_init() != 0) {
		syslog(LOG_ERR,"%s: devinfo_init failed: %m", __func__);
		return;
	}

	act = 1;
	if ((dev_root = devinfo_handle_to_device(DEVINFO_ROOT_DEVICE)) == NULL){
		syslog(LOG_ERR, "%s: can't get the root device: %m", __func__);
		goto out;
	}

	/* mark each entry as missing */
	TAILQ_FOREACH(entry, &device_tbl, link)
		entry->flags &= ~HR_DEVICE_FOUND;

	if (devinfo_foreach_device_child(dev_root, device_collector, NULL))
		syslog(LOG_ERR, "%s: devinfo_foreach_device_child failed",
		    __func__);

	/*
	 * Purge items that disappeared
	 */
	TAILQ_FOREACH_SAFE(entry, &device_tbl, link, entry_tmp) {
		/*
		 * If HR_DEVICE_IMMUTABLE bit is set then this means that
		 * this entry was not detected by the above
		 * devinfo_foreach_device() call. So we are not deleting
		 * it there.
		 */
		if (!(entry->flags & HR_DEVICE_FOUND) &&
		    !(entry->flags & HR_DEVICE_IMMUTABLE))
			device_entry_delete(entry);
	}

	device_tick = this_tick;

	/*
	 * Force a refresh for the hrDiskStorageTable
	 * XXX Why not the other dependen tables?
	 */
	refresh_disk_storage_tbl(1);

  out:
	devinfo_free();
	act = 0;
}

/**
 * This is the implementation for a generated (by a SNMP tool)
 * function prototype, see hostres_tree.h
 * It handles the SNMP operations for hrDeviceTable
 */
int
op_hrDeviceTable(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op curr_op)
{
	struct device_entry *entry;

	refresh_device_tbl(0);

	switch (curr_op) {

	case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_INT(&device_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = entry->index;
		goto get;

	case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_INT(&device_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_INT(&device_tbl,
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

	case LEAF_hrDeviceIndex:
		value->v.integer = entry->index;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrDeviceType:
		assert(entry->type != NULL);
	  	value->v.oid = *(entry->type);
	  	return (SNMP_ERR_NOERROR);

	case LEAF_hrDeviceDescr:
	  	return (string_get(value, entry->descr, -1));

	case LEAF_hrDeviceID:
		value->v.oid = *(entry->id);
	  	return (SNMP_ERR_NOERROR);

	case LEAF_hrDeviceStatus:
	  	value->v.integer = entry->status;
	  	return (SNMP_ERR_NOERROR);

	case LEAF_hrDeviceErrors:
	  	value->v.uint32 = entry->errors;
	  	return (SNMP_ERR_NOERROR);
	}
	abort();
}
