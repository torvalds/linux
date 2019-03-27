/* $FreeBSD$ */
/*	$NetBSD: sem.h,v 1.5 1994/06/29 06:45:15 cgd Exp $	*/

/*
 * SVID compatible sem.h file
 *
 * Author:  Daniel Boulet
 */

#ifndef _SYS_SEM_H_
#define _SYS_SEM_H_

#ifdef _WANT_SYSVSEM_INTERNALS
#define	_WANT_SYSVIPC_INTERNALS
#endif
#include <sys/ipc.h>

#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;
#define	_PID_T_DECLARED
#endif

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#ifndef _TIME_T_DECLARED
typedef	__time_t	time_t;
#define	_TIME_T_DECLARED
#endif

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
struct semid_ds_old {
	struct ipc_perm_old sem_perm;	/* operation permission struct */
	struct sem	*__sem_base;	/* pointer to first semaphore in set */
	unsigned short	sem_nsems;	/* number of sems in set */
	time_t		sem_otime;	/* last operation time */
	long		sem_pad1;	/* SVABI/386 says I need this here */
	time_t		sem_ctime;	/* last change time */
    					/* Times measured in secs since */
    					/* 00:00:00 UTC, Jan. 1, 1970, without leap seconds */
	long		sem_pad2;	/* SVABI/386 says I need this here */
	long		sem_pad3[4];	/* SVABI/386 says I need this here */
};
#endif

struct semid_ds {
	struct ipc_perm	sem_perm;	/* operation permission struct */
	struct sem	*__sem_base;	/* pointer to first semaphore in set */
	unsigned short	sem_nsems;	/* number of sems in set */
	time_t		sem_otime;	/* last operation time */
	time_t		sem_ctime;	/* last change time */
    					/* Times measured in secs since */
    					/* 00:00:00 UTC, Jan. 1, 1970, without leap seconds */
};

/*
 * semop's sops parameter structure
 */
struct sembuf {
	unsigned short	sem_num;	/* semaphore # */
	short		sem_op;		/* semaphore operation */
	short		sem_flg;	/* operation flags */
};
#define SEM_UNDO	010000

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7) || \
    defined(_WANT_SEMUN_OLD)
union semun_old {
	int		val;		/* value for SETVAL */
	struct		semid_ds_old *buf; /* buffer for IPC_STAT & IPC_SET */
	unsigned short	*array;		/* array for GETALL & SETALL */
};
#endif

#if defined(_KERNEL) || defined(_WANT_SEMUN)
/*
 * semctl's arg parameter structure
 */
union semun {
	int		val;		/* value for SETVAL */
	struct		semid_ds *buf;	/* buffer for IPC_STAT & IPC_SET */
	unsigned short	*array;		/* array for GETALL & SETALL */
};
#endif

/*
 * commands for semctl
 */
#define GETNCNT	3	/* Return the value of semncnt {READ} */
#define GETPID	4	/* Return the value of sempid {READ} */
#define GETVAL	5	/* Return the value of semval {READ} */
#define GETALL	6	/* Return semvals into arg.array {READ} */
#define GETZCNT	7	/* Return the value of semzcnt {READ} */
#define SETVAL	8	/* Set the value of semval to arg.val {ALTER} */
#define SETALL	9	/* Set semvals from arg.array {ALTER} */
#define SEM_STAT 10	/* Like IPC_STAT but treats semid as sema-index */
#define SEM_INFO 11	/* Like IPC_INFO but treats semid as sema-index */

/*
 * Permissions
 */
#define SEM_A		IPC_W	/* alter permission */
#define SEM_R		IPC_R	/* read permission */

#if defined(_KERNEL) || defined(_WANT_SYSVSEM_INTERNALS)
/*
 * semaphore info struct
 */
struct seminfo {
	int	semmni;		/* # of semaphore identifiers */
	int	semmns;		/* # of semaphores in system */
	int	semmnu;		/* # of undo structures in system */
	int	semmsl;		/* max # of semaphores per id */
	int	semopm;		/* max # of operations per semop call */
	int	semume;		/* max # of undo entries per process */
	int	semusz;		/* size in bytes of undo structure */
	int	semvmx;		/* semaphore maximum value */
	int	semaem;		/* adjust on exit max value */
};

/*
 * Kernel wrapper for the user-level structure
 */
struct semid_kernel {
	struct	semid_ds u;
	struct	label *label;	/* MAC framework label */
	struct	ucred *cred;	/* creator's credentials */
};

/* internal "mode" bits */
#define	SEM_ALLOC	01000	/* semaphore is allocated */
#define	SEM_DEST	02000	/* semaphore will be destroyed on last detach */
#endif

#ifdef _KERNEL
extern struct seminfo	seminfo;
/*
 * Process sem_undo vectors at proc exit.
 */
void	semexit(struct proc *p);

#else /* !_KERNEL */

__BEGIN_DECLS
#if __BSD_VISIBLE
int semsys(int, ...);
#endif
int semctl(int, int, int, ...);
int semget(key_t, int, int);
int semop(int, struct sembuf *, size_t);
__END_DECLS

#endif /* !_KERNEL */

#endif /* !_SYS_SEM_H_ */
