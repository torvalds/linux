#ifndef _PPC_SEMBUF_H
#define _PPC_SEMBUF_H

/*
 * The semid64_ds structure for PPC architecture.
 */

struct semid64_ds {
	struct ipc64_perm sem_perm;		/* permissions .. see ipc.h */
	unsigned int	__unused1;
	__kernel_time_t	sem_otime;		/* last semop time */
	unsigned int	__unused2;
	__kernel_time_t	sem_ctime;		/* last change time */
	unsigned long	sem_nsems;		/* no. of semaphores in array */
	unsigned long	__unused3;
	unsigned long	__unused4;
};

#endif /* _PPC_SEMBUF_H */
