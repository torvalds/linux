#ifndef __LINUX_BRIDGE_EBT_LIMIT_H
#define __LINUX_BRIDGE_EBT_LIMIT_H

#define EBT_LIMIT_MATCH "limit"

/* timings are in milliseconds. */
#define EBT_LIMIT_SCALE 10000

/* 1/10,000 sec period => max of 10,000/sec.  Min rate is then 429490
   seconds, or one every 59 hours. */

struct ebt_limit_info
{
	u_int32_t avg;    /* Average secs between packets * scale */
	u_int32_t burst;  /* Period multiplier for upper limit. */

	/* Used internally by the kernel */
	unsigned long prev;
	u_int32_t credit;
	u_int32_t credit_cap, cost;
};

#endif
