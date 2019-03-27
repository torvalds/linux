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
 * Host Resources MIB for SNMPd. Implementation for hrSWRunTable
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/linker.h>

#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

/*
 * Ugly thing: PID_MAX, NO_PID defined only in kernel
 */
#define	NO_PID		100000

enum SWRunType {
	SRT_UNKNOWN		= 1,
	SRT_OPERATING_SYSTEM	= 2,
	SRT_DEVICE_DRIVER	= 3,
	SRT_APPLICATION		= 4

};

enum SWRunStatus {
	SRS_RUNNING		= 1,
	SRS_RUNNABLE		= 2,
	SRS_NOT_RUNNABLE	= 3,
	SRS_INVALID		= 4
};

/* Maximum lengths for the strings according to the MIB */
#define	SWR_NAME_MLEN	(64 + 1)
#define	SWR_PATH_MLEN	(128 + 1)
#define	SWR_PARAM_MLEN	(128 + 1)

/*
 * This structure is used to hold a SNMP table entry
 * for both hrSWRunTable and hrSWRunPerfTable because
 * hrSWRunPerfTable AUGMENTS hrSWRunTable
 */
struct swrun_entry {
	int32_t		index;
	u_char		*name;		/* it may be NULL */
	const struct asn_oid *id;
	u_char		*path;		/* it may be NULL */
	u_char		*parameters;	/* it may be NULL */
	int32_t		type;		/* enum SWRunType */
	int32_t		status;		/* enum SWRunStatus */
	int32_t		perfCPU;
	int32_t		perfMemory;
#define	HR_SWRUN_FOUND 0x001
	uint32_t	flags;
	uint64_t	r_tick;		/* tick when entry refreshed */
	TAILQ_ENTRY(swrun_entry) link;
};
TAILQ_HEAD(swrun_tbl, swrun_entry);

/* the head of the list with hrSWRunTable's entries */
static struct swrun_tbl swrun_tbl = TAILQ_HEAD_INITIALIZER(swrun_tbl);

/* last (agent) tick when hrSWRunTable and hrSWRunPerTable was updated */
static uint64_t swrun_tick;

/* maximum number of ticks between updates of SWRun and SWRunPerf table */
uint32_t swrun_tbl_refresh = HR_SWRUN_TBL_REFRESH * 100;

/* the value of the MIB object with the same name */
static int32_t SWOSIndex;

/**
 * Malloc a new entry and add it to the list
 * associated to this table. The item identified by
 * the index parameter must not exist in this list.
 */
static struct swrun_entry *
swrun_entry_create(int32_t idx)
{
	struct swrun_entry *entry;

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		syslog(LOG_WARNING, "%s: %m", __func__);
		return (NULL);
	}
	memset(entry, 0, sizeof(*entry));
	entry->index = idx;

	INSERT_OBJECT_INT(entry, &swrun_tbl);
	return (entry);
}

/**
 * Unlink the entry from the list and then free its heap memory
 */
static void
swrun_entry_delete(struct swrun_entry *entry)
{

	assert(entry != NULL);

	TAILQ_REMOVE(&swrun_tbl, entry, link);

	free(entry->name);
	free(entry->path);
	free(entry->parameters);
	free(entry);
}

/**
 * Search one item by its index, return NULL if none found
 */
static struct swrun_entry *
swrun_entry_find_by_index(int32_t idx)
{
	struct swrun_entry *entry;

	TAILQ_FOREACH(entry, &swrun_tbl, link)
		if (entry->index == idx)
			return (entry);
	return (NULL);
}

/**
 * Translate the kernel's process status to SNMP.
 */
static enum SWRunStatus
swrun_OS_get_proc_status(const struct kinfo_proc *kp)
{

	assert(kp != NULL);
	if(kp ==  NULL) {
		return (SRS_INVALID);
	}

	/*
	 * I'm using the old style flags - they look cleaner to me,
	 * at least for the purpose of this SNMP table
	 */
	switch (kp->ki_stat) {

	case SSTOP:
		return (SRS_NOT_RUNNABLE);

	case SWAIT:
	case SLOCK:
	case SSLEEP:
		return (SRS_RUNNABLE);

	case SZOMB:
		return (SRS_INVALID);

	case SIDL:
	case SRUN:
		return (SRS_RUNNING);

	default:
		syslog(LOG_ERR,"Unknown process state: %d", kp->ki_stat);
		return (SRS_INVALID);
	}
}

/**
 * Make an SNMP table entry from a kernel one.
 */
static void
kinfo_proc_to_swrun_entry(const struct kinfo_proc *kp,
    struct swrun_entry *entry)
{
	char **argv = NULL;
	uint64_t cpu_time = 0;
	size_t pname_len;

	pname_len = strlen(kp->ki_comm) + 1;
	entry->name = reallocf(entry->name, pname_len);
	if (entry->name != NULL)
		strlcpy(entry->name, kp->ki_comm, pname_len);

	entry->id = &oid_zeroDotZero; /* unknown id - FIXME */

	assert(hr_kd != NULL);

	argv = kvm_getargv(hr_kd, kp, SWR_PARAM_MLEN - 1);
	if(argv != NULL){
		u_char param[SWR_PARAM_MLEN];

		memset(param, '\0', sizeof(param));

		/*
		 * FIXME
		 * Path seems to not be available.
		 * Try to hack the info in argv[0];
		 * this argv is under control of the program so this info
		 * is not realiable
		 */
		if(*argv != NULL && (*argv)[0] == '/') {
			size_t path_len;

			path_len = strlen(*argv) + 1;
			if (path_len > SWR_PATH_MLEN)
				path_len = SWR_PATH_MLEN;

			entry->path = reallocf(entry->path, path_len);
			if (entry->path != NULL) {
				memset(entry->path, '\0', path_len);
				strlcpy((char*)entry->path, *argv, path_len);
			}
		}

		argv++; /* skip the first one which was used for path */

		while (argv != NULL && *argv != NULL ) {
			if (param[0] != 0)  {
				/*
				 * add a space between parameters,
				 * except before the first one
				 */
				strlcat((char *)param, " ", sizeof(param));
			}
			strlcat((char *)param, *argv, sizeof(param));
			argv++;
		}
		/* reuse pname_len */
		pname_len = strlen(param) + 1;
		if (pname_len > SWR_PARAM_MLEN)
			pname_len = SWR_PARAM_MLEN;

		entry->parameters = reallocf(entry->parameters, pname_len);
		strlcpy(entry->parameters, param, pname_len);
	}

	entry->type = (int32_t)(IS_KERNPROC(kp) ? SRT_OPERATING_SYSTEM :
	    SRT_APPLICATION);

	entry->status = (int32_t)swrun_OS_get_proc_status(kp);
	cpu_time = kp->ki_runtime / 100000; /* centi-seconds */

	/* may overflow the snmp type */
	entry->perfCPU = (cpu_time > (uint64_t)INT_MAX ? INT_MAX : cpu_time);
	entry->perfMemory = kp->ki_size / 1024; /* in kilo-bytes */
	entry->r_tick = get_ticks();
}

/**
 * Create a table entry for a KLD
 */
static void
kld_file_stat_to_swrun(const struct kld_file_stat *kfs,
    struct swrun_entry *entry)
{
	size_t name_len;

	assert(kfs != NULL);
	assert(entry != NULL);

	name_len = strlen(kfs->name) + 1;
	if (name_len > SWR_NAME_MLEN)
		name_len = SWR_NAME_MLEN;

	entry->name = reallocf(entry->name, name_len);
	if (entry->name != NULL)
		strlcpy((char *)entry->name, kfs->name, name_len);

	/* FIXME: can we find the location where the module was loaded from? */
	entry->path = NULL;

	/* no parameters for kernel files (.ko) of for the kernel */
	entry->parameters = NULL;

	entry->id = &oid_zeroDotZero; /* unknown id - FIXME */

	if (strcmp(kfs->name, "kernel") == 0) {
		entry->type = (int32_t)SRT_OPERATING_SYSTEM;
		SWOSIndex = entry->index;
	} else {
		entry->type = (int32_t)SRT_DEVICE_DRIVER; /* well, not really */
	}
	entry->status = (int32_t)SRS_RUNNING;
	entry->perfCPU = 0;			/* Info not available */
	entry->perfMemory = kfs->size / 1024;	/* in kilo-bytes */
	entry->r_tick = get_ticks();
}

/**
 * Get all visible processes including the kernel visible threads
 */
static void
swrun_OS_get_procs(void)
{
	struct kinfo_proc *plist, *kp;
	int i;
	int nproc;
	struct swrun_entry *entry;

	plist = kvm_getprocs(hr_kd, KERN_PROC_ALL, 0, &nproc);
	if (plist == NULL || nproc < 0) {
		syslog(LOG_ERR, "kvm_getprocs() failed: %m");
		return;
	}
	for (i = 0, kp = plist; i < nproc; i++, kp++) {
		/*
		 * The SNMP table's index must begin from 1 (as specified by
		 * this table definition), the PIDs are starting from 0
		 * so we are translating the PIDs to +1
		 */
		entry = swrun_entry_find_by_index((int32_t)kp->ki_pid + 1);
		if (entry == NULL) {
			/* new entry - get memory for it */
			entry = swrun_entry_create((int32_t)kp->ki_pid + 1);
			if (entry == NULL)
				continue;
		}
		entry->flags |= HR_SWRUN_FOUND;	/* mark it as found */

		kinfo_proc_to_swrun_entry(kp, entry);
	}
}

/*
 * Get kernel items: first the kernel itself, then the loaded modules.
 */
static void
swrun_OS_get_kinfo(void)
{
	int fileid;
	struct swrun_entry *entry;
	struct kld_file_stat stat;

	for (fileid = kldnext(0); fileid > 0; fileid = kldnext(fileid)) {
		stat.version = sizeof(struct kld_file_stat);
		if (kldstat(fileid, &stat) < 0) {
			syslog(LOG_ERR, "kldstat() failed: %m");
			continue;
		}

		/*
		 * kernel and kernel files (*.ko) will be indexed starting with
		 * NO_PID + 1; NO_PID is PID_MAX + 1 thus it will be no risk to
		 * overlap with real PIDs which are in range of 1 .. NO_PID
		 */
		entry = swrun_entry_find_by_index(NO_PID + 1 + stat.id);
		if (entry == NULL) {
			/* new entry - get memory for it */
			entry = swrun_entry_create(NO_PID + 1 + stat.id);
			if (entry == NULL)
				continue;
		}
		entry->flags |= HR_SWRUN_FOUND; /* mark it as found */

		kld_file_stat_to_swrun(&stat, entry);
	}
}

/**
 * Refresh the hrSWRun and hrSWRunPert tables.
 */
static void
refresh_swrun_tbl(void)
{

	struct swrun_entry *entry, *entry_tmp;

	if (this_tick - swrun_tick < swrun_tbl_refresh) {
		HRDBG("no refresh needed ");
		return;
	}

	/* mark each entry as missing */
	TAILQ_FOREACH(entry, &swrun_tbl, link)
		entry->flags &= ~HR_SWRUN_FOUND;

	swrun_OS_get_procs();
	swrun_OS_get_kinfo();

	/*
	 * Purge items that disappeared
	 */
	TAILQ_FOREACH_SAFE(entry, &swrun_tbl, link, entry_tmp)
		if (!(entry->flags & HR_SWRUN_FOUND))
			swrun_entry_delete(entry);

	swrun_tick = this_tick;

	HRDBG("refresh DONE");
}

/**
 * Update the information in this entry
 */
static void
fetch_swrun_entry(struct swrun_entry *entry)
{
	struct kinfo_proc *plist;
	int nproc;
	struct kld_file_stat stat;

	assert(entry !=  NULL);

	if (entry->index >= NO_PID + 1)	{
		/*
		 * kernel and kernel files (*.ko) will be indexed
		 * starting with NO_PID + 1; NO_PID is PID_MAX + 1
		 * thus it will be no risk to overlap with real PIDs
		 * which are in range of 1 .. NO_PID
		 */
		stat.version = sizeof(stat);
		if (kldstat(entry->index - NO_PID - 1, &stat) == -1) {
			/*
			 * not found, it's gone. Mark it as invalid for now, it
			 * will be removed from the list at next global refersh
			 */
			 HRDBG("missing item with kid=%d",
			     entry->index -  NO_PID - 1);
			entry->status = (int32_t)SRS_INVALID;
		} else
			kld_file_stat_to_swrun(&stat, entry);

	} else {
		/* this is a process */
		assert(hr_kd != NULL);
		plist = kvm_getprocs(hr_kd, KERN_PROC_PID,
		    entry->index - 1, &nproc);
		if (plist == NULL || nproc != 1) {
			HRDBG("missing item with PID=%d", entry->index - 1);
			entry->status = (int32_t)SRS_INVALID;
		} else
			kinfo_proc_to_swrun_entry(plist, entry);
	}
}

/**
 * Invalidate entry. For KLDs we try to unload it, for processes we SIGKILL it.
 */
static int
invalidate_swrun_entry(struct swrun_entry *entry, int commit)
{
	struct kinfo_proc *plist;
	int nproc;
	struct kld_file_stat stat;

	assert(entry !=  NULL);

	if (entry->index >= NO_PID + 1)	{
		/* this is a kernel item */
		HRDBG("atempt to unload KLD %d",
		    entry->index -  NO_PID - 1);

		if (entry->index == SWOSIndex) {
			/* can't invalidate the kernel itself */
			return (SNMP_ERR_NOT_WRITEABLE);
		}

		stat.version = sizeof(stat);
		if (kldstat(entry->index - NO_PID - 1, &stat) == -1) {
			/*
			 * not found, it's gone. Mark it as invalid for now, it
			 * will be removed from the list at next global
			 * refresh
			 */
			HRDBG("missing item with kid=%d",
			    entry->index - NO_PID - 1);
			entry->status = (int32_t)SRS_INVALID;
			return (SNMP_ERR_NOERROR);
		}
		/*
		 * There is no way to try to unload a module. There seems
		 * also no way to find out whether it is busy without unloading
		 * it. We can assume that it is busy, if the reference count
		 * is larger than 2, but if it is 1 nothing helps.
		 */
		if (!commit) {
			if (stat.refs > 1)
				return (SNMP_ERR_NOT_WRITEABLE);
			return (SNMP_ERR_NOERROR);
		}
		if (kldunload(stat.id) == -1) {
			syslog(LOG_ERR,"kldunload for %d/%s failed: %m",
			    stat.id, stat.name);
			if (errno == EBUSY)
				return (SNMP_ERR_NOT_WRITEABLE);
			else
				return (SNMP_ERR_RES_UNAVAIL);
		}
	} else {
		/* this is a process */
		assert(hr_kd != NULL);

		plist = kvm_getprocs(hr_kd, KERN_PROC_PID,
		    entry->index - 1, &nproc);
		if (plist == NULL || nproc != 1) {
			HRDBG("missing item with PID=%d", entry->index - 1);
			entry->status = (int32_t)SRS_INVALID;
			return (SNMP_ERR_NOERROR);
		}
		if (IS_KERNPROC(plist)) {
			/* you don't want to do this */
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		if (kill(entry->index - 1, commit ? SIGKILL : 0) < 0) {
			syslog(LOG_ERR,"kill (%d, SIGKILL) failed: %m",
			    entry->index - 1);
			if (errno == ESRCH) {
				/* race: just gone */
				entry->status = (int32_t)SRS_INVALID;
				return (SNMP_ERR_NOERROR);
			}
			return (SNMP_ERR_GENERR);
		}
	}
	return (SNMP_ERR_NOERROR);
}

/**
 * Popuplate the hrSWRunTable.
 */
void
init_swrun_tbl(void)
{

	refresh_swrun_tbl();
	HRDBG("done");
}

/**
 * Finalize the hrSWRunTable.
 */
void
fini_swrun_tbl(void)
{
	struct swrun_entry *n1;

	while ((n1 = TAILQ_FIRST(&swrun_tbl)) != NULL) {
		TAILQ_REMOVE(&swrun_tbl, n1, link);
		free(n1);
	}
}

/*
 * This is the implementation for a generated (by a SNMP tool)
 * function prototype, see hostres_tree.h
 * It hanldes the SNMP operations for hrSWRunTable
 */
int
op_hrSWRunTable(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op curr_op)
{
	struct swrun_entry *entry;
	int ret;

	refresh_swrun_tbl();

	switch (curr_op) {

	  case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_INT(&swrun_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = entry->index;
		goto get;

	  case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_INT(&swrun_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	  case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_INT(&swrun_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NO_CREATION);

		if (entry->r_tick < this_tick)
			fetch_swrun_entry(entry);

		switch (value->var.subs[sub - 1]) {

		case LEAF_hrSWRunStatus:
			if (value->v.integer != (int32_t)SRS_INVALID)
				return (SNMP_ERR_WRONG_VALUE);

			if (entry->status == (int32_t)SRS_INVALID)
				return (SNMP_ERR_NOERROR);

			/*
			 * Here we have a problem with the entire SNMP
			 * model: if we kill now, we cannot rollback.
			 * If we kill in the commit code, we cannot
			 * return an error. Because things may change between
			 * SET and COMMIT this is impossible to handle
			 * correctly.
			 */
			return (invalidate_swrun_entry(entry, 0));
		}
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_COMMIT:
		if ((entry = FIND_OBJECT_INT(&swrun_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOERROR);

		switch (value->var.subs[sub - 1]) {

		case LEAF_hrSWRunStatus:
			if (value->v.integer == (int32_t)SRS_INVALID &&
			    entry->status != (int32_t)SRS_INVALID)
				(void)invalidate_swrun_entry(entry, 1);
			return (SNMP_ERR_NOERROR);
		}
		abort();
	}
	abort();

  get:
	ret = SNMP_ERR_NOERROR;
	switch (value->var.subs[sub - 1]) {

	  case LEAF_hrSWRunIndex:
		value->v.integer = entry->index;
		break;

	  case LEAF_hrSWRunName:
		if (entry->name != NULL)
			ret = string_get(value, entry->name, -1);
		else
			ret = string_get(value, "", -1);
		break;

	  case LEAF_hrSWRunID:
		assert(entry->id != NULL);
		value->v.oid = *entry->id;
		break;

	  case LEAF_hrSWRunPath:
		if (entry->path != NULL)
			ret = string_get(value, entry->path, -1);
		else
			ret = string_get(value, "", -1);
		break;

	  case LEAF_hrSWRunParameters:
		if (entry->parameters != NULL)
			ret = string_get(value, entry->parameters, -1);
		else
			ret = string_get(value, "", -1);
		break;

	  case LEAF_hrSWRunType:
		value->v.integer = entry->type;
		break;

	  case LEAF_hrSWRunStatus:
		value->v.integer = entry->status;
		break;

	  default:
		abort();
	}
	return (ret);
}

/**
 * Scalar(s) in the SWRun group
 */
int
op_hrSWRun(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op curr_op)
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

	case LEAF_hrSWOSIndex:
		value->v.uint32 = SWOSIndex;
		return (SNMP_ERR_NOERROR);

	default:
		abort();
	}
}

/*
 * This is the implementation for a generated (by a SNMP tool)
 * function prototype, see hostres_tree.h
 * It handles the SNMP operations for hrSWRunPerfTable
 */
int
op_hrSWRunPerfTable(struct snmp_context *ctx __unused,
    struct snmp_value *value, u_int sub, u_int iidx __unused,
    enum snmp_op curr_op )
{
	struct swrun_entry *entry;

	refresh_swrun_tbl();

	switch (curr_op) {

	  case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_INT(&swrun_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = entry->index;
		goto get;

	  case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_INT(&swrun_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	  case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_INT(&swrun_tbl,
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

	  case LEAF_hrSWRunPerfCPU:
		value->v.integer = entry->perfCPU;
		return (SNMP_ERR_NOERROR);

	  case LEAF_hrSWRunPerfMem:
		value->v.integer = entry->perfMemory;
		return (SNMP_ERR_NOERROR);
	}
	abort();
}
