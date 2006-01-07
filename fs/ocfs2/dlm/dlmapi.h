/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmapi.h
 *
 * externally exported dlm interfaces
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

#ifndef DLMAPI_H
#define DLMAPI_H

struct dlm_lock;
struct dlm_ctxt;

/* NOTE: changes made to this enum should be reflected in dlmdebug.c */
enum dlm_status {
	DLM_NORMAL = 0,           /*  0: request in progress */
	DLM_GRANTED,              /*  1: request granted */
	DLM_DENIED,               /*  2: request denied */
	DLM_DENIED_NOLOCKS,       /*  3: request denied, out of system resources */
	DLM_WORKING,              /*  4: async request in progress */
	DLM_BLOCKED,              /*  5: lock request blocked */
	DLM_BLOCKED_ORPHAN,       /*  6: lock request blocked by a orphan lock*/
	DLM_DENIED_GRACE_PERIOD,  /*  7: topological change in progress */
	DLM_SYSERR,               /*  8: system error */
	DLM_NOSUPPORT,            /*  9: unsupported */
	DLM_CANCELGRANT,          /* 10: can't cancel convert: already granted */
	DLM_IVLOCKID,             /* 11: bad lockid */
	DLM_SYNC,                 /* 12: synchronous request granted */
	DLM_BADTYPE,              /* 13: bad resource type */
	DLM_BADRESOURCE,          /* 14: bad resource handle */
	DLM_MAXHANDLES,           /* 15: no more resource handles */
	DLM_NOCLINFO,             /* 16: can't contact cluster manager */
	DLM_NOLOCKMGR,            /* 17: can't contact lock manager */
	DLM_NOPURGED,             /* 18: can't contact purge daemon */
	DLM_BADARGS,              /* 19: bad api args */
	DLM_VOID,                 /* 20: no status */
	DLM_NOTQUEUED,            /* 21: NOQUEUE was specified and request failed */
	DLM_IVBUFLEN,             /* 22: invalid resource name length */
	DLM_CVTUNGRANT,           /* 23: attempted to convert ungranted lock */
	DLM_BADPARAM,             /* 24: invalid lock mode specified */
	DLM_VALNOTVALID,          /* 25: value block has been invalidated */
	DLM_REJECTED,             /* 26: request rejected, unrecognized client */
	DLM_ABORT,                /* 27: blocked lock request cancelled */
	DLM_CANCEL,               /* 28: conversion request cancelled */
	DLM_IVRESHANDLE,          /* 29: invalid resource handle */
	DLM_DEADLOCK,             /* 30: deadlock recovery refused this request */
	DLM_DENIED_NOASTS,        /* 31: failed to allocate AST */
	DLM_FORWARD,              /* 32: request must wait for primary's response */
	DLM_TIMEOUT,              /* 33: timeout value for lock has expired */
	DLM_IVGROUPID,            /* 34: invalid group specification */
	DLM_VERS_CONFLICT,        /* 35: version conflicts prevent request handling */
	DLM_BAD_DEVICE_PATH,      /* 36: Locks device does not exist or path wrong */
	DLM_NO_DEVICE_PERMISSION, /* 37: Client has insufficient pers for device */
	DLM_NO_CONTROL_DEVICE,    /* 38: Cannot set options on opened device */

	DLM_RECOVERING,           /* 39: extension, allows caller to fail a lock
				     request if it is being recovered */
	DLM_MIGRATING,            /* 40: extension, allows caller to fail a lock
				     request if it is being migrated */
	DLM_MAXSTATS,             /* 41: upper limit for return code validation */
};

/* for pretty-printing dlm_status error messages */
const char *dlm_errmsg(enum dlm_status err);
/* for pretty-printing dlm_status error names */
const char *dlm_errname(enum dlm_status err);

/* Eventually the DLM will use standard errno values, but in the
 * meantime this lets us track dlm errors as they bubble up. When we
 * bring its error reporting into line with the rest of the stack,
 * these can just be replaced with calls to mlog_errno. */
#define dlm_error(st) do {						\
	if ((st) != DLM_RECOVERING &&					\
	    (st) != DLM_MIGRATING &&					\
	    (st) != DLM_FORWARD)					\
		mlog(ML_ERROR, "dlm status = %s\n", dlm_errname((st)));	\
} while (0)

#define DLM_LKSB_UNUSED1           0x01  
#define DLM_LKSB_PUT_LVB           0x02
#define DLM_LKSB_GET_LVB           0x04
#define DLM_LKSB_UNUSED2           0x08
#define DLM_LKSB_UNUSED3           0x10
#define DLM_LKSB_UNUSED4           0x20
#define DLM_LKSB_UNUSED5           0x40
#define DLM_LKSB_UNUSED6           0x80

#define DLM_LVB_LEN  64

/* Callers are only allowed access to the lvb and status members of
 * this struct. */
struct dlm_lockstatus {
	enum dlm_status status;
	u32 flags;
	struct dlm_lock *lockid;
	char lvb[DLM_LVB_LEN];
};

/* Valid lock modes. */
#define LKM_IVMODE      (-1)            /* invalid mode */
#define LKM_NLMODE      0               /* null lock */
#define LKM_CRMODE      1               /* concurrent read    unsupported */
#define LKM_CWMODE      2               /* concurrent write   unsupported */
#define LKM_PRMODE      3               /* protected read */
#define LKM_PWMODE      4               /* protected write    unsupported */
#define LKM_EXMODE      5               /* exclusive */
#define LKM_MAXMODE     5
#define LKM_MODEMASK    0xff

/* Flags passed to dlmlock and dlmunlock:
 * reserved: flags used by the "real" dlm
 * only a few are supported by this dlm
 * (U) = unsupported by ocfs2 dlm */
#define LKM_ORPHAN       0x00000010  /* this lock is orphanable (U) */
#define LKM_PARENTABLE   0x00000020  /* this lock was orphaned (U) */
#define LKM_BLOCK        0x00000040  /* blocking lock request (U) */
#define LKM_LOCAL        0x00000080  /* local lock request */
#define LKM_VALBLK       0x00000100  /* lock value block request */
#define LKM_NOQUEUE      0x00000200  /* non blocking request */
#define LKM_CONVERT      0x00000400  /* conversion request */
#define LKM_NODLCKWT     0x00000800  /* this lock wont deadlock (U) */
#define LKM_UNLOCK       0x00001000  /* deallocate this lock */
#define LKM_CANCEL       0x00002000  /* cancel conversion request */
#define LKM_DEQALL       0x00004000  /* remove all locks held by proc (U) */
#define LKM_INVVALBLK    0x00008000  /* invalidate lock value block */
#define LKM_SYNCSTS      0x00010000  /* return synchronous status if poss (U) */
#define LKM_TIMEOUT      0x00020000  /* lock request contains timeout (U) */
#define LKM_SNGLDLCK     0x00040000  /* request can self-deadlock (U) */
#define LKM_FINDLOCAL    0x00080000  /* find local lock request (U) */
#define LKM_PROC_OWNED   0x00100000  /* owned by process, not group (U) */
#define LKM_XID          0x00200000  /* use transaction id for deadlock (U) */
#define LKM_XID_CONFLICT 0x00400000  /* do not allow lock inheritance (U) */
#define LKM_FORCE        0x00800000  /* force unlock flag */
#define LKM_REVVALBLK    0x01000000  /* temporary solution: re-validate
					lock value block (U) */
/* unused */
#define LKM_UNUSED1      0x00000001  /* unused */
#define LKM_UNUSED2      0x00000002  /* unused */
#define LKM_UNUSED3      0x00000004  /* unused */
#define LKM_UNUSED4      0x00000008  /* unused */
#define LKM_UNUSED5      0x02000000  /* unused */
#define LKM_UNUSED6      0x04000000  /* unused */
#define LKM_UNUSED7      0x08000000  /* unused */

/* ocfs2 extensions: internal only
 * should never be used by caller */
#define LKM_MIGRATION    0x10000000  /* extension: lockres is to be migrated
					to another node */
#define LKM_PUT_LVB      0x20000000  /* extension: lvb is being passed
					should be applied to lockres */
#define LKM_GET_LVB      0x40000000  /* extension: lvb should be copied
					from lockres when lock is granted */
#define LKM_RECOVERY     0x80000000  /* extension: flag for recovery lock
					used to avoid recovery rwsem */


typedef void (dlm_astlockfunc_t)(void *);
typedef void (dlm_bastlockfunc_t)(void *, int);
typedef void (dlm_astunlockfunc_t)(void *, enum dlm_status);

enum dlm_status dlmlock(struct dlm_ctxt *dlm,
			int mode,
			struct dlm_lockstatus *lksb,
			int flags,
			const char *name,
			dlm_astlockfunc_t *ast,
			void *data,
			dlm_bastlockfunc_t *bast);

enum dlm_status dlmunlock(struct dlm_ctxt *dlm,
			  struct dlm_lockstatus *lksb,
			  int flags,
			  dlm_astunlockfunc_t *unlockast,
			  void *data);

struct dlm_ctxt * dlm_register_domain(const char *domain, u32 key);

void dlm_unregister_domain(struct dlm_ctxt *dlm);

void dlm_print_one_lock(struct dlm_lock *lockid);

typedef void (dlm_eviction_func)(int, void *);
struct dlm_eviction_cb {
	struct list_head        ec_item;
	dlm_eviction_func       *ec_func;
	void                    *ec_data;
};
void dlm_setup_eviction_cb(struct dlm_eviction_cb *cb,
			   dlm_eviction_func *f,
			   void *data);
void dlm_register_eviction_cb(struct dlm_ctxt *dlm,
			      struct dlm_eviction_cb *cb);
void dlm_unregister_eviction_cb(struct dlm_eviction_cb *cb);

#endif /* DLMAPI_H */
