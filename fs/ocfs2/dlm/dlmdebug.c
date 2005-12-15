/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmdebug.c
 *
 * debug functionality for the dlm
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/utsname.h>
#include <linux/sysctl.h>
#include <linux/spinlock.h>

#include "cluster/heartbeat.h"
#include "cluster/nodemanager.h"
#include "cluster/tcp.h"

#include "dlmapi.h"
#include "dlmcommon.h"
#include "dlmdebug.h"

#include "dlmdomain.h"
#include "dlmdebug.h"

#define MLOG_MASK_PREFIX ML_DLM
#include "cluster/masklog.h"

void dlm_print_one_lock_resource(struct dlm_lock_resource *res)
{
	mlog(ML_NOTICE, "lockres: %.*s, owner=%u, state=%u\n",
	       res->lockname.len, res->lockname.name,
	       res->owner, res->state);
	spin_lock(&res->spinlock);
	__dlm_print_one_lock_resource(res);
	spin_unlock(&res->spinlock);
}

void __dlm_print_one_lock_resource(struct dlm_lock_resource *res)
{
	struct list_head *iter2;
	struct dlm_lock *lock;

	assert_spin_locked(&res->spinlock);

	mlog(ML_NOTICE, "lockres: %.*s, owner=%u, state=%u\n",
	       res->lockname.len, res->lockname.name,
	       res->owner, res->state);
	mlog(ML_NOTICE, "  last used: %lu, on purge list: %s\n",
	     res->last_used, list_empty(&res->purge) ? "no" : "yes");
	mlog(ML_NOTICE, "  granted queue: \n");
	list_for_each(iter2, &res->granted) {
		lock = list_entry(iter2, struct dlm_lock, list);
		spin_lock(&lock->spinlock);
		mlog(ML_NOTICE, "    type=%d, conv=%d, node=%u, "
		       "cookie=%"MLFu64", ast=(empty=%c,pend=%c), bast=(empty=%c,pend=%c)\n", 
		       lock->ml.type, lock->ml.convert_type, lock->ml.node, lock->ml.cookie, 
		       list_empty(&lock->ast_list) ? 'y' : 'n',
		       lock->ast_pending ? 'y' : 'n',
		       list_empty(&lock->bast_list) ? 'y' : 'n',
		       lock->bast_pending ? 'y' : 'n');
		spin_unlock(&lock->spinlock);
	}
	mlog(ML_NOTICE, "  converting queue: \n");
	list_for_each(iter2, &res->converting) {
		lock = list_entry(iter2, struct dlm_lock, list);
		spin_lock(&lock->spinlock);
		mlog(ML_NOTICE, "    type=%d, conv=%d, node=%u, "
		       "cookie=%"MLFu64", ast=(empty=%c,pend=%c), bast=(empty=%c,pend=%c)\n", 
		       lock->ml.type, lock->ml.convert_type, lock->ml.node, lock->ml.cookie, 
		       list_empty(&lock->ast_list) ? 'y' : 'n',
		       lock->ast_pending ? 'y' : 'n',
		       list_empty(&lock->bast_list) ? 'y' : 'n',
		       lock->bast_pending ? 'y' : 'n');
		spin_unlock(&lock->spinlock);
	}
	mlog(ML_NOTICE, "  blocked queue: \n");
	list_for_each(iter2, &res->blocked) {
		lock = list_entry(iter2, struct dlm_lock, list);
		spin_lock(&lock->spinlock);
		mlog(ML_NOTICE, "    type=%d, conv=%d, node=%u, "
		       "cookie=%"MLFu64", ast=(empty=%c,pend=%c), bast=(empty=%c,pend=%c)\n", 
		       lock->ml.type, lock->ml.convert_type, lock->ml.node, lock->ml.cookie, 
		       list_empty(&lock->ast_list) ? 'y' : 'n',
		       lock->ast_pending ? 'y' : 'n',
		       list_empty(&lock->bast_list) ? 'y' : 'n',
		       lock->bast_pending ? 'y' : 'n');
		spin_unlock(&lock->spinlock);
	}
}

void dlm_print_one_lock(struct dlm_lock *lockid)
{
	dlm_print_one_lock_resource(lockid->lockres);
}
EXPORT_SYMBOL_GPL(dlm_print_one_lock);

void dlm_dump_lock_resources(struct dlm_ctxt *dlm)
{
	struct dlm_lock_resource *res;
	struct list_head *iter;
	struct list_head *bucket;
	int i;

	mlog(ML_NOTICE, "struct dlm_ctxt: %s, node=%u, key=%u\n",
		  dlm->name, dlm->node_num, dlm->key);
	if (!dlm || !dlm->name) {
		mlog(ML_ERROR, "dlm=%p\n", dlm);
		return;
	}

	spin_lock(&dlm->spinlock);
	for (i=0; i<DLM_HASH_SIZE; i++) {
		bucket = &(dlm->resources[i]);
		list_for_each(iter, bucket) {
			res = list_entry(iter, struct dlm_lock_resource, list);
			dlm_print_one_lock_resource(res);
		}
	}
	spin_unlock(&dlm->spinlock);
}

static const char *dlm_errnames[] = {
	[DLM_NORMAL] =			"DLM_NORMAL",
	[DLM_GRANTED] =			"DLM_GRANTED",
	[DLM_DENIED] =			"DLM_DENIED",
	[DLM_DENIED_NOLOCKS] =		"DLM_DENIED_NOLOCKS",
	[DLM_WORKING] =			"DLM_WORKING",
	[DLM_BLOCKED] =			"DLM_BLOCKED",
	[DLM_BLOCKED_ORPHAN] =		"DLM_BLOCKED_ORPHAN",
	[DLM_DENIED_GRACE_PERIOD] =	"DLM_DENIED_GRACE_PERIOD",
	[DLM_SYSERR] =			"DLM_SYSERR",
	[DLM_NOSUPPORT] =		"DLM_NOSUPPORT",
	[DLM_CANCELGRANT] =		"DLM_CANCELGRANT",
	[DLM_IVLOCKID] =		"DLM_IVLOCKID",
	[DLM_SYNC] =			"DLM_SYNC",
	[DLM_BADTYPE] =			"DLM_BADTYPE",
	[DLM_BADRESOURCE] =		"DLM_BADRESOURCE",
	[DLM_MAXHANDLES] =		"DLM_MAXHANDLES",
	[DLM_NOCLINFO] =		"DLM_NOCLINFO",
	[DLM_NOLOCKMGR] =		"DLM_NOLOCKMGR",
	[DLM_NOPURGED] =		"DLM_NOPURGED",
	[DLM_BADARGS] =			"DLM_BADARGS",
	[DLM_VOID] =			"DLM_VOID",
	[DLM_NOTQUEUED] =		"DLM_NOTQUEUED",
	[DLM_IVBUFLEN] =		"DLM_IVBUFLEN",
	[DLM_CVTUNGRANT] =		"DLM_CVTUNGRANT",
	[DLM_BADPARAM] =		"DLM_BADPARAM",
	[DLM_VALNOTVALID] =		"DLM_VALNOTVALID",
	[DLM_REJECTED] =		"DLM_REJECTED",
	[DLM_ABORT] =			"DLM_ABORT",
	[DLM_CANCEL] =			"DLM_CANCEL",
	[DLM_IVRESHANDLE] =		"DLM_IVRESHANDLE",
	[DLM_DEADLOCK] =		"DLM_DEADLOCK",
	[DLM_DENIED_NOASTS] =		"DLM_DENIED_NOASTS",
	[DLM_FORWARD] =			"DLM_FORWARD",
	[DLM_TIMEOUT] =			"DLM_TIMEOUT",
	[DLM_IVGROUPID] =		"DLM_IVGROUPID",
	[DLM_VERS_CONFLICT] =		"DLM_VERS_CONFLICT",
	[DLM_BAD_DEVICE_PATH] =		"DLM_BAD_DEVICE_PATH",
	[DLM_NO_DEVICE_PERMISSION] =	"DLM_NO_DEVICE_PERMISSION",
	[DLM_NO_CONTROL_DEVICE ] =	"DLM_NO_CONTROL_DEVICE ",
	[DLM_RECOVERING] =		"DLM_RECOVERING",
	[DLM_MIGRATING] =		"DLM_MIGRATING",
	[DLM_MAXSTATS] =		"DLM_MAXSTATS",
};

static const char *dlm_errmsgs[] = {
	[DLM_NORMAL] = 			"request in progress",
	[DLM_GRANTED] = 		"request granted",
	[DLM_DENIED] = 			"request denied",
	[DLM_DENIED_NOLOCKS] = 		"request denied, out of system resources",
	[DLM_WORKING] = 		"async request in progress",
	[DLM_BLOCKED] = 		"lock request blocked",
	[DLM_BLOCKED_ORPHAN] = 		"lock request blocked by a orphan lock",
	[DLM_DENIED_GRACE_PERIOD] = 	"topological change in progress",
	[DLM_SYSERR] = 			"system error",
	[DLM_NOSUPPORT] = 		"unsupported",
	[DLM_CANCELGRANT] = 		"can't cancel convert: already granted",
	[DLM_IVLOCKID] = 		"bad lockid",
	[DLM_SYNC] = 			"synchronous request granted",
	[DLM_BADTYPE] = 		"bad resource type",
	[DLM_BADRESOURCE] = 		"bad resource handle",
	[DLM_MAXHANDLES] = 		"no more resource handles",
	[DLM_NOCLINFO] = 		"can't contact cluster manager",
	[DLM_NOLOCKMGR] = 		"can't contact lock manager",
	[DLM_NOPURGED] = 		"can't contact purge daemon",
	[DLM_BADARGS] = 		"bad api args",
	[DLM_VOID] = 			"no status",
	[DLM_NOTQUEUED] = 		"NOQUEUE was specified and request failed",
	[DLM_IVBUFLEN] = 		"invalid resource name length",
	[DLM_CVTUNGRANT] = 		"attempted to convert ungranted lock",
	[DLM_BADPARAM] = 		"invalid lock mode specified",
	[DLM_VALNOTVALID] = 		"value block has been invalidated",
	[DLM_REJECTED] = 		"request rejected, unrecognized client",
	[DLM_ABORT] = 			"blocked lock request cancelled",
	[DLM_CANCEL] = 			"conversion request cancelled",
	[DLM_IVRESHANDLE] = 		"invalid resource handle",
	[DLM_DEADLOCK] = 		"deadlock recovery refused this request",
	[DLM_DENIED_NOASTS] = 		"failed to allocate AST",
	[DLM_FORWARD] = 		"request must wait for primary's response",
	[DLM_TIMEOUT] = 		"timeout value for lock has expired",
	[DLM_IVGROUPID] = 		"invalid group specification",
	[DLM_VERS_CONFLICT] = 		"version conflicts prevent request handling",
	[DLM_BAD_DEVICE_PATH] = 	"Locks device does not exist or path wrong",
	[DLM_NO_DEVICE_PERMISSION] = 	"Client has insufficient perms for device",
	[DLM_NO_CONTROL_DEVICE] = 	"Cannot set options on opened device ",
	[DLM_RECOVERING] = 		"lock resource being recovered",
	[DLM_MIGRATING] = 		"lock resource being migrated",
	[DLM_MAXSTATS] = 		"invalid error number",
};

const char *dlm_errmsg(enum dlm_status err)
{
	if (err >= DLM_MAXSTATS || err < 0)
		return dlm_errmsgs[DLM_MAXSTATS];
	return dlm_errmsgs[err];
}
EXPORT_SYMBOL_GPL(dlm_errmsg);

const char *dlm_errname(enum dlm_status err)
{
	if (err >= DLM_MAXSTATS || err < 0)
		return dlm_errnames[DLM_MAXSTATS];
	return dlm_errnames[err];
}
EXPORT_SYMBOL_GPL(dlm_errname);
