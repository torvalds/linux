#ifndef _XT_HASHLIMIT_H
#define _XT_HASHLIMIT_H

/* timings are in milliseconds. */
#define XT_HASHLIMIT_SCALE 10000
/* 1/10,000 sec period => max of 10,000/sec.  Min rate is then 429490
   seconds, or one every 59 hours. */

/* details of this structure hidden by the implementation */
struct xt_hashlimit_htable;

#define XT_HASHLIMIT_HASH_DIP	0x0001
#define XT_HASHLIMIT_HASH_DPT	0x0002
#define XT_HASHLIMIT_HASH_SIP	0x0004
#define XT_HASHLIMIT_HASH_SPT	0x0008

struct hashlimit_cfg {
	u_int32_t mode;	  /* bitmask of IPT_HASHLIMIT_HASH_* */
	u_int32_t avg;    /* Average secs between packets * scale */
	u_int32_t burst;  /* Period multiplier for upper limit. */

	/* user specified */
	u_int32_t size;		/* how many buckets */
	u_int32_t max;		/* max number of entries */
	u_int32_t gc_interval;	/* gc interval */
	u_int32_t expire;	/* when do entries expire? */
};

struct xt_hashlimit_info {
	char name [IFNAMSIZ];		/* name */
	struct hashlimit_cfg cfg;

	/* Used internally by the kernel */
	struct xt_hashlimit_htable *hinfo;
	union {
		void *ptr;
		struct xt_hashlimit_info *master;
	} u;
};
#endif /*_XT_HASHLIMIT_H*/
