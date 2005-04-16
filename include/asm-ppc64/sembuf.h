#ifndef _PPC64_SEMBUF_H
#define _PPC64_SEMBUF_H

/* 
 * The semid64_ds structure for PPC architecture.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Pad space is left for:
 * - 2 miscellaneous 64-bit values
 */

struct semid64_ds {
	struct ipc64_perm sem_perm;	/* permissions .. see ipc.h */
	__kernel_time_t	sem_otime;	/* last semop time */
	__kernel_time_t	sem_ctime;	/* last change time */
	unsigned long	sem_nsems;	/* no. of semaphores in array */

	unsigned long	__unused1;
	unsigned long	__unused2;
};

#endif /* _PPC64_SEMBUF_H */
