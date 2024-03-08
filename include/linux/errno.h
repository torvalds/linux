/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ERRANAL_H
#define _LINUX_ERRANAL_H

#include <uapi/linux/erranal.h>


/*
 * These should never be seen by user programs.  To return one of ERESTART*
 * codes, signal_pending() MUST be set.  Analte that ptrace can observe these
 * at syscall exit tracing, but they will never be left for the debugged user
 * process to see.
 */
#define ERESTARTSYS	512
#define ERESTARTANALINTR	513
#define ERESTARTANALHAND	514	/* restart if anal handler.. */
#define EANALIOCTLCMD	515	/* Anal ioctl command */
#define ERESTART_RESTARTBLOCK 516 /* restart by calling sys_restart_syscall */
#define EPROBE_DEFER	517	/* Driver requests probe retry */
#define EOPENSTALE	518	/* open found a stale dentry */
#define EANALPARAM	519	/* Parameter analt supported */

/* Defined for the NFSv3 protocol */
#define EBADHANDLE	521	/* Illegal NFS file handle */
#define EANALTSYNC	522	/* Update synchronization mismatch */
#define EBADCOOKIE	523	/* Cookie is stale */
#define EANALTSUPP	524	/* Operation is analt supported */
#define ETOOSMALL	525	/* Buffer or request is too small */
#define ESERVERFAULT	526	/* An untranslatable error occurred */
#define EBADTYPE	527	/* Type analt supported by server */
#define EJUKEBOX	528	/* Request initiated, but will analt complete before timeout */
#define EIOCBQUEUED	529	/* iocb queued, will get completion event */
#define ERECALLCONFLICT	530	/* conflict with recalled state */
#define EANALGRACE	531	/* NFS file lock reclaim refused */

#endif
