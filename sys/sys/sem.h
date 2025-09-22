/*	$OpenBSD: sem.h,v 1.26 2024/10/26 05:39:03 jsg Exp $	*/
/*	$NetBSD: sem.h,v 1.8 1996/02/09 18:25:29 christos Exp $	*/

/*
 * SVID compatible sem.h file
 *
 * Author:  Daniel Boulet
 */

#ifndef _SYS_SEM_H_
#define _SYS_SEM_H_

#ifndef _SYS_IPC_H_
#include <sys/ipc.h>
#endif

#if __BSD_VISIBLE

/* sem-specific sysctl variables corresponding to members of struct seminfo */
#define	KERN_SEMINFO_SEMMNI	1	/* int: # of semaphore identifiers */
#define	KERN_SEMINFO_SEMMNS	2	/* int: # of semaphores in system */
#define	KERN_SEMINFO_SEMMNU	3	/* int: # of undo structures in system */
#define	KERN_SEMINFO_SEMMSL	4	/* int: max semaphores per id */
#define	KERN_SEMINFO_SEMOPM	5	/* int: max operations per semop call */
#define	KERN_SEMINFO_SEMUME	6	/* int: max undo entries per process */
#define	KERN_SEMINFO_SEMUSZ	7	/* int: size in bytes of struct undo */
#define	KERN_SEMINFO_SEMVMX	8	/* int: semaphore maximum value */
#define	KERN_SEMINFO_SEMAEM	9	/* int: adjust on exit max value */
#define	KERN_SEMINFO_MAXID	10	/* number of valid semaphore sysctls */

#define	CTL_KERN_SEMINFO_NAMES { \
	{ 0, 0 }, \
	{ "semmni", CTLTYPE_INT }, \
	{ "semmns", CTLTYPE_INT }, \
	{ "semmnu", CTLTYPE_INT }, \
	{ "semmsl", CTLTYPE_INT }, \
	{ "semopm", CTLTYPE_INT }, \
	{ "semume", CTLTYPE_INT }, \
	{ "semusz", CTLTYPE_INT }, \
	{ "semvmx", CTLTYPE_INT }, \
	{ "semaem", CTLTYPE_INT }, \
}

#endif /* __BSD_VISIBLE */

struct sem {
	unsigned short	semval;		/* semaphore value */
	pid_t		sempid;		/* pid of last operation */
	unsigned short	semncnt;	/* # awaiting semval > cval */
	unsigned short	semzcnt;	/* # awaiting semval = 0 */
};

struct semid_ds {
	struct ipc_perm	sem_perm;	/* operation permission struct */
	struct sem	*sem_base;	/* pointer to first semaphore in set */
	unsigned short	sem_nsems;	/* number of sems in set */
	time_t		sem_otime;	/* last operation time */
	long		sem_pad1;	/* SVABI/386 says I need this here */
	time_t		sem_ctime;	/* last change time */
	    				/* Times measured in secs since */
	    				/* 00:00:00 GMT, Jan. 1, 1970 */
	long		sem_pad2;	/* SVABI/386 says I need this here */
	long		sem_pad3[4];	/* SVABI/386 says I need this here */
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

/*
 * semctl's arg parameter structure
 */
union semun {
	int		val;		/* value for SETVAL */
	struct semid_ds	*buf;		/* buffer for IPC_STAT & IPC_SET */
	unsigned short	*array;		/* array for GETALL & SETALL */
};

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


/*
 * Permissions
 */
#define SEM_A		0200	/* alter permission */
#define SEM_R		0400	/* read permission */


#ifdef _KERNEL
#include <sys/queue.h>

/*
 * Kernel implementation stuff
 */
#define SEMVMX	32767		/* semaphore maximum value */
#define SEMAEM	16384		/* adjust on exit max value */

/*
 * Undo structure (one per process)
 */
struct sem_undo {
	SLIST_ENTRY(sem_undo) un_next;	/* ptr to next active undo structure */
	struct	process *un_proc;	/* owner of this structure */
	short	un_cnt;			/* # of active entries */
	struct undo {
		short	un_adjval;	/* adjust on exit values */
		short	un_num;		/* semaphore # */
		int	un_id;		/* semid */
	} un_ent[1];			/* undo entries */
};

/*
 * semaphore info struct
 */
struct seminfo {
	int	semmni,		/* # of semaphore identifiers */
		semmns,		/* # of semaphores in system */
		semmnu,		/* # of undo structures in system */
		semmsl,		/* max # of semaphores per id */
		semopm,		/* max # of operations per semop call */
		semume,		/* max # of undo entries per process */
		semusz,		/* size in bytes of undo structure */
		semvmx,		/* semaphore maximum value */
		semaem;		/* adjust on exit max value */
};

struct sem_sysctl_info {
	struct	seminfo seminfo;
	struct	semid_ds semids[1];
};

extern struct seminfo	seminfo;

/*
 * Configuration parameters
 */
#ifndef SEMMNI
#define SEMMNI	10		/* # of semaphore identifiers */
#endif
#ifndef SEMMNS
#define SEMMNS	60		/* # of semaphores in system */
#endif
#ifndef SEMUME
#define SEMUME	10		/* max # of undo entries per process */
#endif
#ifndef SEMMNU
#define SEMMNU	30		/* # of undo structures in system */
#endif

/* shouldn't need tuning */
#ifndef SEMMSL
#define SEMMSL	SEMMNS		/* max # of semaphores per id */
#endif
#ifndef SEMOPM
#define SEMOPM	100		/* max # of operations per semop call */
#endif

/* actual size of an undo structure */
#define SEMUSZ	(sizeof(struct sem_undo)+sizeof(struct undo)*SEMUME)

extern struct	semid_ds **sema;	/* semaphore id list */

void	seminit(void);
void	semexit(struct process *);
int	sysctl_sysvsem(int *, u_int, void *, size_t *, void *, size_t);
#endif /* _KERNEL */

#ifndef _KERNEL
__BEGIN_DECLS
int	semctl(int, int, int, ...);
int	__semctl(int, int, int, union semun *);
int	semget(key_t, int, int);
int	semop(int, struct sembuf *, size_t);
__END_DECLS
#endif /* !_KERNEL */

#endif /* !_SYS_SEM_H_ */
