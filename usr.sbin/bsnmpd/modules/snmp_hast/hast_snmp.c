/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Mikolaj Golub <trociny@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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
#include <sys/queue.h>

#include <bsnmp/snmpmod.h>

#include <string.h>

#include "hast.h"
#include "hast_oid.h"
#include "hast_proto.h"
#include "hast_tree.h"
#include "nv.h"
#include "pjdlog.h"
#include "proto.h"

#define UPDATE_INTERVAL	500	/* update interval in ticks */

static struct lmodule *module;

static const struct asn_oid oid_hast = OIDX_begemotHast;

/* the Object Resource registration index */
static u_int hast_index = 0;

/*
 * Structure that describes single resource.
 */
struct hast_snmp_resource {
	TAILQ_ENTRY(hast_snmp_resource) link;
	int32_t		index;
	char		name[NAME_MAX];
	int		error;
	int		role;
	char		provname[NAME_MAX];
	char		localpath[PATH_MAX];
	int32_t		extentsize;
	int32_t		keepdirty;
	char		remoteaddr[HAST_ADDRSIZE];
	char		sourceaddr[HAST_ADDRSIZE];
	int		replication;
	int		status;
	uint64_t	dirty;
	uint64_t	reads;
	uint64_t	writes;
	uint64_t	deletes;
	uint64_t	flushes;
	uint64_t	activemap_updates;
	uint64_t	read_errors;
	uint64_t	write_errors;
	uint64_t	delete_errors;
	uint64_t	flush_errors;
	pid_t		workerpid;
	uint32_t	local_queue;
	uint32_t	send_queue;
	uint32_t	recv_queue;
	uint32_t	done_queue;
	uint32_t	idle_queue;
};

static TAILQ_HEAD(, hast_snmp_resource) resources =
    TAILQ_HEAD_INITIALIZER(resources);

/* Path to configuration file. */
static u_char *cfgpath;
/* Ticks of the last hast resources update. */
static uint64_t last_resources_update;

static void free_resources(void);
static int hastctl(struct nv *nvin, struct nv **nvout);
static int hast_fini(void);
static int hast_init(struct lmodule *mod, int argc, char *argv[]);
static void hast_start(void);
static int set_role(const char *resource, int role);
static int str2role(const char *str);
static int str2replication(const char *str);
static int str2status(const char *str);
static int update_resources(void);

const struct snmp_module config = {
    .comment   = "This module implements the BEGEMOT MIB for HAST.",
    .init      = hast_init,
    .start     = hast_start,
    .fini      = hast_fini,
    .tree      = hast_ctree,
    .tree_size = hast_CTREE_SIZE,
};

static int
hast_init(struct lmodule *mod, int argc __unused, char *argv[] __unused)
{

	module = mod;

	pjdlog_init(PJDLOG_MODE_SYSLOG);
	pjdlog_debug_set(0);

	cfgpath = malloc(sizeof(HAST_CONFIG));
	if (cfgpath == NULL) {
		pjdlog_error("Unable to allocate %zu bytes for cfgpath",
		    sizeof(HAST_CONFIG));
		return (-1);
	}
	strcpy(cfgpath, HAST_CONFIG);
	return(0);
}

static void
hast_start(void)
{
	hast_index = or_register(&oid_hast,
	    "The MIB module for BEGEMOT-HAST-MIB.", module);
}

static int
hast_fini(void)
{

	or_unregister(hast_index);
	free_resources();
	free(cfgpath);
	return (0);
}

static void
free_resources(void)
{
	struct hast_snmp_resource *res;

	while ((res = TAILQ_FIRST(&resources)) != NULL) {
		TAILQ_REMOVE(&resources, res, link);
		free(res);
	}
}

static int
str2role(const char *str)
{

	if (strcmp(str, "init") == 0)
		return (HAST_ROLE_INIT);
	if (strcmp(str, "primary") == 0)
		return (HAST_ROLE_PRIMARY);
	if (strcmp(str, "secondary") == 0)
		return (HAST_ROLE_SECONDARY);
	return (HAST_ROLE_UNDEF);
}

static int
str2replication(const char *str)
{

	if (strcmp(str, "fullsync") == 0)
		return (HAST_REPLICATION_FULLSYNC);
	if (strcmp(str, "memsync") == 0)
		return (HAST_REPLICATION_MEMSYNC);
	if (strcmp(str, "async") == 0)
		return (HAST_REPLICATION_ASYNC);
	return (-1);
}

static int
str2status(const char *str)
{

	if (strcmp(str, "complete") == 0)
		return (0);
	if (strcmp(str, "degraded") == 0)
		return (1);
	return (-1);
}

static int
hastctl(struct nv *nvin, struct nv **nvout)
{
	struct hastd_config *cfg;
	struct proto_conn *conn;
	struct nv *nv;
	int error;

	cfg = yy_config_parse(cfgpath, true);
	if (cfg == NULL)
		return (-1);

	/* Setup control connection... */
	if (proto_client(NULL, cfg->hc_controladdr, &conn) == -1) {
		pjdlog_error("Unable to setup control connection to %s",
		    cfg->hc_controladdr);
		return (-1);
	}
	/* ...and connect to hastd. */
	if (proto_connect(conn, HAST_TIMEOUT) == -1) {
		pjdlog_error("Unable to connect to hastd via %s",
		    cfg->hc_controladdr);
		proto_close(conn);
		return (-1);
	}
	/* Send the command to the server... */
	if (hast_proto_send(NULL, conn, nvin, NULL, 0) == -1) {
		pjdlog_error("Unable to send command to hastd via %s",
		    cfg->hc_controladdr);
		proto_close(conn);
		return (-1);
	}
	/* ...and receive reply. */
	if (hast_proto_recv_hdr(conn, &nv) == -1) {
		pjdlog_error("cannot receive reply from hastd via %s",
		    cfg->hc_controladdr);
		proto_close(conn);
		return (-1);
	}
	proto_close(conn);
	error = nv_get_int16(nv, "error");
	if (error != 0) {
		pjdlog_error("Error %d received from hastd.", error);
		nv_free(nv);
		return (-1);
	}
	nv_set_error(nv, 0);
	*nvout = nv;
	return (0);
}

static int
set_role(const char *resource, int role)
{
	struct nv *nvin, *nvout;
	int error;

	nvin = nv_alloc();
	nv_add_string(nvin, resource, "resource%d", 0);
	nv_add_uint8(nvin, HASTCTL_CMD_SETROLE, "cmd");
	nv_add_uint8(nvin, role, "role");
	error = hastctl(nvin, &nvout);
	nv_free(nvin);
	if (error != 0)
		return (-1);
	nv_free(nvout);
	return (SNMP_ERR_NOERROR);
}

static int
update_resources(void)
{
	struct hast_snmp_resource *res;
	struct nv *nvin, *nvout;
	static uint64_t now;
	unsigned int i;
	const char *str;
	int error;

	now = get_ticks();
	if (now - last_resources_update < UPDATE_INTERVAL)
		return (0);

	last_resources_update = now;

	free_resources();

	nvin = nv_alloc();
	nv_add_uint8(nvin, HASTCTL_CMD_STATUS, "cmd");
	nv_add_string(nvin, "all", "resource%d", 0);
	error = hastctl(nvin, &nvout);
	nv_free(nvin);
	if (error != 0)
		return (-1);

	for (i = 0; ; i++) {
		str = nv_get_string(nvout, "resource%u", i);
		if (str == NULL)
			break;
		res = calloc(1, sizeof(*res));
		if (res == NULL) {
			pjdlog_error("Unable to allocate %zu bytes for "
			    "resource", sizeof(*res));
			return (-1);
		}
		res->index = i + 1;
		strncpy(res->name, str, sizeof(res->name) - 1);
		error = nv_get_int16(nvout, "error%u", i);
		if (error != 0)
			continue;
		str = nv_get_string(nvout, "role%u", i);
		res->role = str != NULL ? str2role(str) : HAST_ROLE_UNDEF;
		str = nv_get_string(nvout, "provname%u", i);
		if (str != NULL)
			strncpy(res->provname, str, sizeof(res->provname) - 1);
		str = nv_get_string(nvout, "localpath%u", i);
		if (str != NULL) {
			strncpy(res->localpath, str,
			    sizeof(res->localpath) - 1);
		}
		res->extentsize = nv_get_uint32(nvout, "extentsize%u", i);
		res->keepdirty = nv_get_uint32(nvout, "keepdirty%u", i);
		str = nv_get_string(nvout, "remoteaddr%u", i);
		if (str != NULL) {
			strncpy(res->remoteaddr, str,
			    sizeof(res->remoteaddr) - 1);
		}
		str = nv_get_string(nvout, "sourceaddr%u", i);
		if (str != NULL) {
			strncpy(res->sourceaddr, str,
			    sizeof(res->sourceaddr) - 1);
		}
		str = nv_get_string(nvout, "replication%u", i);
		res->replication = str != NULL ? str2replication(str) : -1;
		str = nv_get_string(nvout, "status%u", i);
		res->status = str != NULL ? str2status(str) : -1;
		res->dirty = nv_get_uint64(nvout, "dirty%u", i);
		res->reads = nv_get_uint64(nvout, "stat_read%u", i);
		res->writes = nv_get_uint64(nvout, "stat_write%u", i);
		res->deletes = nv_get_uint64(nvout, "stat_delete%u", i);
		res->flushes = nv_get_uint64(nvout, "stat_flush%u", i);
		res->activemap_updates =
		    nv_get_uint64(nvout, "stat_activemap_update%u", i);
		res->read_errors =
		    nv_get_uint64(nvout, "stat_read_error%u", i);
		res->write_errors =
		    nv_get_uint64(nvout, "stat_write_error%u", i);
		res->delete_errors =
		    nv_get_uint64(nvout, "stat_delete_error%u", i);
		res->flush_errors =
		    nv_get_uint64(nvout, "stat_flush_error%u", i);
		res->workerpid = nv_get_int32(nvout, "workerpid%u", i);
		res->local_queue =
		    nv_get_uint64(nvout, "local_queue_size%u", i);
		res->send_queue =
		    nv_get_uint64(nvout, "send_queue_size%u", i);
		res->recv_queue =
		    nv_get_uint64(nvout, "recv_queue_size%u", i);
		res->done_queue =
		    nv_get_uint64(nvout, "done_queue_size%u", i);
		res->idle_queue =
		    nv_get_uint64(nvout, "idle_queue_size%u", i);
		TAILQ_INSERT_TAIL(&resources, res, link);
	}
	nv_free(nvout);
	return (0);
}

int
op_hastConfig(struct snmp_context *context, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which;

	which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GET:
		switch (which) {
		case LEAF_hastConfigFile:
			return (string_get(value, cfgpath, -1));
		default:
			return (SNMP_ERR_RES_UNAVAIL);
		}
	case SNMP_OP_SET:
		switch (which) {
		case LEAF_hastConfigFile:
			return (string_save(value, context, -1,
			    (u_char **)&cfgpath));
		default:
			return (SNMP_ERR_RES_UNAVAIL);
		}
	case SNMP_OP_GETNEXT:
	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	default:
		return (SNMP_ERR_RES_UNAVAIL);
	}
}

int
op_hastResourceTable(struct snmp_context *context __unused,
    struct snmp_value *value, u_int sub, u_int iidx __unused, enum snmp_op op)
{
	struct hast_snmp_resource *res;
	asn_subid_t which;
	int ret;

	if (update_resources() == -1)
		return (SNMP_ERR_RES_UNAVAIL);

	which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GETNEXT:
		res = NEXT_OBJECT_INT(&resources, &value->var, sub);
		if (res == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = res->index;
		break;
	case SNMP_OP_GET:
		if (value->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		res = FIND_OBJECT_INT(&resources, &value->var, sub);
		if (res == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;
	case SNMP_OP_SET:
		res = FIND_OBJECT_INT(&resources, &value->var, sub);
		if (res == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (which) {
		case LEAF_hastResourceRole:
			ret = set_role(res->name, value->v.integer);
			/* force update on next run */
			last_resources_update = 0;
			break;
		default:
			ret = SNMP_ERR_NOT_WRITEABLE;
			break;
		}
		return ret;
	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	default:
		return (SNMP_ERR_RES_UNAVAIL);
	}

	ret = SNMP_ERR_NOERROR;

	switch (which) {
	case LEAF_hastResourceIndex:
		value->v.integer = res->index;
		break;
	case LEAF_hastResourceName:
		ret = string_get(value, res->name, -1);
		break;
	case LEAF_hastResourceRole:
		value->v.integer = res->role;
		break;
	case LEAF_hastResourceProvName:
		ret = string_get(value, res->provname, -1);
		break;
	case LEAF_hastResourceLocalPath:
		ret = string_get(value, res->localpath, -1);
		break;
	case LEAF_hastResourceExtentSize:
		value->v.integer = res->extentsize;
		break;
	case LEAF_hastResourceKeepDirty:
		value->v.integer = res->keepdirty;
		break;
	case LEAF_hastResourceRemoteAddr:
		ret = string_get(value, res->remoteaddr, -1);
		break;
	case LEAF_hastResourceSourceAddr:
		ret = string_get(value, res->sourceaddr, -1);
		break;
	case LEAF_hastResourceReplication:
		value->v.integer = res->replication;
		break;
	case LEAF_hastResourceStatus:
		value->v.integer = res->status;
		break;
	case LEAF_hastResourceDirty:
		value->v.counter64 = res->dirty;
		break;
	case LEAF_hastResourceReads:
		value->v.counter64 = res->reads;
		break;
	case LEAF_hastResourceWrites:
		value->v.counter64 = res->writes;
		break;
	case LEAF_hastResourceDeletes:
		value->v.counter64 = res->deletes;
		break;
	case LEAF_hastResourceFlushes:
		value->v.counter64 = res->flushes;
		break;
	case LEAF_hastResourceActivemapUpdates:
		value->v.counter64 = res->activemap_updates;
		break;
	case LEAF_hastResourceReadErrors:
		value->v.counter64 = res->read_errors;
		break;
	case LEAF_hastResourceWriteErrors:
		value->v.counter64 = res->write_errors;
		break;
	case LEAF_hastResourceDeleteErrors:
		value->v.counter64 = res->delete_errors;
		break;
	case LEAF_hastResourceFlushErrors:
		value->v.counter64 = res->flush_errors;
		break;
	case LEAF_hastResourceWorkerPid:
		value->v.integer = res->workerpid;
		break;
	case LEAF_hastResourceLocalQueue:
		value->v.uint32 = res->local_queue;
		break;
	case LEAF_hastResourceSendQueue:
		value->v.uint32 = res->send_queue;
		break;
	case LEAF_hastResourceRecvQueue:
		value->v.uint32 = res->recv_queue;
		break;
	case LEAF_hastResourceDoneQueue:
		value->v.uint32 = res->done_queue;
		break;
	case LEAF_hastResourceIdleQueue:
		value->v.uint32 = res->idle_queue;
		break;
	default:
		ret = SNMP_ERR_RES_UNAVAIL;
		break;
	}
	return (ret);
}
