#ifndef _LINUX_GETCPU_H
#define _LINUX_GETCPU_H 1

/* Cache for getcpu() to speed it up. Results might be upto a jiffie
   out of date, but will be faster.
   User programs should not refer to the contents of this structure.
   It is only a cache for vgetcpu(). It might change in future kernels.
   The user program must store this information per thread (__thread)
   If you want 100% accurate information pass NULL instead. */
struct getcpu_cache {
	unsigned long t0;
	unsigned long t1;
	unsigned long res[4];
};

#endif
