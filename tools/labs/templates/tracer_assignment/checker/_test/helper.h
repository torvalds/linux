#ifndef _HELPER__
#define _HELPER__

#include <asm/ioctl.h>

#define NAMESIZE 64
#define MCOUNT	 128

#define PREPARE_TEST	_IOW('t', 19, unsigned int)
#define START_TEST	_IOW('t', 20, unsigned int)
#define STOP_TEST	_IOW('t', 21, unsigned int)

/*XXX match test_params with tracers_stats
 * perhaps use the same struct
 */
struct test_params {
	pid_t pid;
	char thread_name[NAMESIZE];
	int idx; /* index for multi-kthreaded test */
	/*
	 * kcalls: 5
	 * alloc : [1024] [8] [128] [10] [128]
	 * free  : [0]    [0] [1]   [0]  [1]
	 */
	int kcalls; /* number of kmalloc calls */
	int alloc[MCOUNT]; /* sizes of kmalloc allocations */
	int free[MCOUNT];  /* intmap for which allocations to free */
	int sched;
	int up;
	int down;
	int lock;
	int unlock;
};

#endif
