/* atm_zatm.h - Driver-specific declarations of the ZATM driver (for use by
		driver-specific utilities) */

/* Written 1995-1999 by Werner Almesberger, EPFL LRC/ICA */


#ifndef LINUX_ATM_ZATM_H
#define LINUX_ATM_ZATM_H

/*
 * Note: non-kernel programs including this file must also include
 * sys/types.h for struct timeval
 */

#include <linux/atmapi.h>
#include <linux/atmioc.h>

#define ZATM_GETPOOL	_IOW('a',ATMIOC_SARPRV+1,struct atmif_sioc)
						/* get pool statistics */
#define ZATM_GETPOOLZ	_IOW('a',ATMIOC_SARPRV+2,struct atmif_sioc)
						/* get statistics and zero */
#define ZATM_SETPOOL	_IOW('a',ATMIOC_SARPRV+3,struct atmif_sioc)
						/* set pool parameters */

struct zatm_pool_info {
	int ref_count;			/* free buffer pool usage counters */
	int low_water,high_water;	/* refill parameters */
	int rqa_count,rqu_count;	/* queue condition counters */
	int offset,next_off;		/* alignment optimizations: offset */
	int next_cnt,next_thres;	/* repetition counter and threshold */
};

struct zatm_pool_req {
	int pool_num;			/* pool number */
	struct zatm_pool_info info;	/* actual information */
};

struct zatm_t_hist {
	struct timeval real;		/* real (wall-clock) time */
	struct timeval expected;	/* expected real time */
};


#define ZATM_OAM_POOL		0	/* free buffer pool for OAM cells */
#define ZATM_AAL0_POOL		1	/* free buffer pool for AAL0 cells */
#define ZATM_AAL5_POOL_BASE	2	/* first AAL5 free buffer pool */
#define ZATM_LAST_POOL	ZATM_AAL5_POOL_BASE+10 /* max. 64 kB */

#define ZATM_TIMER_HISTORY_SIZE	16	/* number of timer adjustments to
					   record; must be 2^n */

#endif
