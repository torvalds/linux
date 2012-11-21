#ifndef _UAPI_LINUX_SHM_H_
#define _UAPI_LINUX_SHM_H_

#include <linux/ipc.h>
#include <linux/errno.h>
#ifndef __KERNEL__
#include <unistd.h>
#endif

/*
 * SHMMAX, SHMMNI and SHMALL are upper limits are defaults which can
 * be increased by sysctl
 */

#define SHMMAX 0x2000000		 /* max shared seg size (bytes) */
#define SHMMIN 1			 /* min shared seg size (bytes) */
#define SHMMNI 4096			 /* max num of segs system wide */
#ifndef __KERNEL__
#define SHMALL (SHMMAX/getpagesize()*(SHMMNI/16))
#endif
#define SHMSEG SHMMNI			 /* max shared segs per process */


/* Obsolete, used only for backwards compatibility and libc5 compiles */
struct shmid_ds {
	struct ipc_perm		shm_perm;	/* operation perms */
	int			shm_segsz;	/* size of segment (bytes) */
	__kernel_time_t		shm_atime;	/* last attach time */
	__kernel_time_t		shm_dtime;	/* last detach time */
	__kernel_time_t		shm_ctime;	/* last change time */
	__kernel_ipc_pid_t	shm_cpid;	/* pid of creator */
	__kernel_ipc_pid_t	shm_lpid;	/* pid of last operator */
	unsigned short		shm_nattch;	/* no. of current attaches */
	unsigned short 		shm_unused;	/* compatibility */
	void 			*shm_unused2;	/* ditto - used by DIPC */
	void			*shm_unused3;	/* unused */
};

/* Include the definition of shmid64_ds and shminfo64 */
#include <asm/shmbuf.h>

/* permission flag for shmget */
#define SHM_R		0400	/* or S_IRUGO from <linux/stat.h> */
#define SHM_W		0200	/* or S_IWUGO from <linux/stat.h> */

/* mode for attach */
#define	SHM_RDONLY	010000	/* read-only access */
#define	SHM_RND		020000	/* round attach address to SHMLBA boundary */
#define	SHM_REMAP	040000	/* take-over region on attach */
#define	SHM_EXEC	0100000	/* execution access */

/* super user shmctl commands */
#define SHM_LOCK 	11
#define SHM_UNLOCK 	12

/* ipcs ctl commands */
#define SHM_STAT 	13
#define SHM_INFO 	14

/* Obsolete, used only for backwards compatibility */
struct	shminfo {
	int shmmax;
	int shmmin;
	int shmmni;
	int shmseg;
	int shmall;
};

struct shm_info {
	int used_ids;
	unsigned long shm_tot;	/* total allocated shm */
	unsigned long shm_rss;	/* total resident shm */
	unsigned long shm_swp;	/* total swapped shm */
	unsigned long swap_attempts;
	unsigned long swap_successes;
};


#endif /* _UAPI_LINUX_SHM_H_ */
