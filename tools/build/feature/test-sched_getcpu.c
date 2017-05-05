#define _GNU_SOURCE
#include <sched.h>

int main(void)
{
	return sched_getcpu();
}
