#ifndef _ASM_GENERIC_FCNTL_H
#define _ASM_GENERIC_FCNTL_H

#define O_ACCMODE	0003
#define O_RDONLY	0000
#define O_WRONLY	0001
#define O_RDWR		0002

#define F_DUPFD		0	/* dup */
#define F_GETFD		1	/* get close_on_exec */
#define F_SETFD		2	/* set/clear close_on_exec */
#define F_GETFL		3	/* get file->f_flags */
#define F_SETFL		4	/* set file->f_flags */

/* for F_[GET|SET]FL */
#define FD_CLOEXEC	1	/* actually anything with low bit set goes */

/* operations for bsd flock(), also used by the kernel implementation */
#define LOCK_SH		1	/* shared lock */
#define LOCK_EX		2	/* exclusive lock */
#define LOCK_NB		4	/* or'd with one of the above to prevent
				   blocking */
#define LOCK_UN		8	/* remove lock */

#define LOCK_MAND	32	/* This is a mandatory flock ... */
#define LOCK_READ	64	/* which allows concurrent read operations */
#define LOCK_WRITE	128	/* which allows concurrent write operations */
#define LOCK_RW		192	/* which allows concurrent read & write ops */

#define F_LINUX_SPECIFIC_BASE	1024

#endif /* _ASM_GENERIC_FCNTL_H */
