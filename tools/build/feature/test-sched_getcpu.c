#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>

int main(void)
{
	return sched_getcpu();
}
