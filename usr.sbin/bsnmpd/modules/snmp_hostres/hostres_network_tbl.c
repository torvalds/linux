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
 * hrNetworkTable
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_mib.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <ifaddrs.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

#include <bsnmp/snmp_mibII.h>

/*
 * This structure is used to hold a SNMP table entry
 * for HOST-RESOURCES-MIB's hrNetworkTable
 */
struct network_entry {
	int32_t		index;
	int32_t		ifIndex;
	TAILQ_ENTRY(network_entry) link;
#define	HR_NETWORK_FOUND		0x001
	uint32_t	flags;

};
TAILQ_HEAD(network_tbl, network_entry);

/* the head of the list with hrNetworkTable's entries */
static struct network_tbl network_tbl = TAILQ_HEAD_INITIALIZER(network_tbl);

/* last (agent) tick when hrNetworkTable was updated */
static uint64_t network_tick;

/* maximum number of ticks between updates of network table */
uint32_t network_tbl_refresh = HR_NETWORK_TBL_REFRESH * 100;

/* Constants */
static const struct asn_oid OIDX_hrDeviceNetwork_c = OIDX_hrDeviceNetwork;

/**
 * Create a new entry into the network table
 */
static struct network_entry *
network_entry_create(const struct device_entry *devEntry)
{
	struct network_entry *entry;

	assert(devEntry != NULL);
	if (devEntry == NULL)
		return (NULL);

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		syslog(LOG_WARNING, "%s: %m", __func__);
		return (NULL);
	}

	memset(entry, 0, sizeof(*entry));
	entry->index = devEntry->index;
	INSERT_OBJECT_INT(entry, &network_tbl);

	return (entry);
}

/**
 * Delete an entry in the network table
 */
static void
network_entry_delete(struct network_entry* entry)
{

	TAILQ_REMOVE(&network_tbl, entry, link);
	free(entry);
}

/**
 * Fetch the interfaces from the mibII module, get their real name from the
 * kernel and try to find it in the device table.
 */
static void
network_get_interfaces(void)
{
	struct device_entry *dev;
	struct network_entry *net;
	struct mibif *ifp;
	int name[6];
	size_t len;
	char *dname;

	name[0] = CTL_NET;
	name[1] = PF_LINK;
	name[2] = NETLINK_GENERIC;
	name[3] = IFMIB_IFDATA;
	name[5] = IFDATA_DRIVERNAME;

	for (ifp = mib_first_if(); ifp != NULL; ifp = mib_next_if(ifp)) {
		HRDBG("%s %s", ifp->name, ifp->descr);

		name[4] = ifp->sysindex;

		/* get the original name */
		len = 0;
		if (sysctl(name, 6, NULL, &len, 0, 0) < 0) {
			syslog(LOG_ERR, "sysctl(net.link.ifdata.%d."
			    "drivername): %m", ifp->sysindex);
			continue;
		}
		if ((dname = malloc(len)) == NULL) {
			syslog(LOG_ERR, "malloc: %m");
			continue;
		}
		if (sysctl(name, 6, dname, &len, 0, 0) < 0) {
			syslog(LOG_ERR, "sysctl(net.link.ifdata.%d."
			    "drivername): %m", ifp->sysindex);
			free(dname);
			continue;
		}

		HRDBG("got device %s (%s)", ifp->name, dname);

		if ((dev = device_find_by_name(dname)) == NULL) {
			HRDBG("%s not in hrDeviceTable", dname);
			free(dname);
			continue;
		}
		HRDBG("%s found in hrDeviceTable", dname);

		dev->type = &OIDX_hrDeviceNetwork_c;
		dev->flags |= HR_DEVICE_IMMUTABLE;

		free(dname);

		/* Then check hrNetworkTable for this device */
		TAILQ_FOREACH(net, &network_tbl, link)
			if (net->index == dev->index)
				break;

		if (net == NULL && (net = network_entry_create(dev)) == NULL)
			continue;

		net->flags |= HR_NETWORK_FOUND;
		net->ifIndex = ifp->index;
	}

	network_tick = this_tick;
}

/**
 * Finalization routine for hrNetworkTable.
 * It destroys the lists and frees any allocated heap memory.
 */
void
fini_network_tbl(void)
{
	struct network_entry *n1;

	while ((n1 = TAILQ_FIRST(&network_tbl)) != NULL) {
		TAILQ_REMOVE(&network_tbl, n1, link);
		free(n1);
	}
}

/**
 * Get the interface list from mibII only at this point to be sure that
 * it is there already.
 */
void
start_network_tbl(void)
{

	mib_refresh_iflist();
	network_get_interfaces();
}

/**
 * Refresh the table.
 */
static void
refresh_network_tbl(void)
{
	struct network_entry *entry, *entry_tmp;

	if (this_tick - network_tick < network_tbl_refresh) {
		HRDBG("no refresh needed");
		return;
	}

	/* mark each entry as missing */
	TAILQ_FOREACH(entry, &network_tbl, link)
		entry->flags &= ~HR_NETWORK_FOUND;

	network_get_interfaces();

	/*
	 * Purge items that disappeared
	 */
	TAILQ_FOREACH_SAFE(entry, &network_tbl, link, entry_tmp) {
		if (!(entry->flags & HR_NETWORK_FOUND))
			network_entry_delete(entry);
	}

	HRDBG("refresh DONE");
}

/*
 * This is the implementation for a generated (by our SNMP tool)
 * function prototype, see hostres_tree.h
 * It handles the SNMP operations for hrNetworkTable
 */
int
op_hrNetworkTable(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op curr_op)
{
	struct network_entry *entry;

	refresh_network_tbl();

	switch (curr_op) {

	case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_INT(&network_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = entry->index;
		goto get;

	case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_INT(&network_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_INT(&network_tbl,
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

	case LEAF_hrNetworkIfIndex:
		value->v.integer = entry->ifIndex;
		return (SNMP_ERR_NOERROR);

	}
	abort();
}
