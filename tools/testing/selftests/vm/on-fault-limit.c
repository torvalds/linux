#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>

#ifndef MCL_ONFAULT
#define MCL_ONFAULT (MCL_FUTURE << 1)
#endif

static int test_limit(void)
{
	int ret = 1;
	struct rlimit lims;
	void *map;

	if (getrlimit(RLIMIT_MEMLOCK, &lims)) {
		perror("getrlimit");
		return ret;
	}

	if (mlockall(MCL_ONFAULT | MCL_FUTURE)) {
		perror("mlockall");
		return ret;
	}

	map = mmap(NULL, 2 * lims.rlim_max, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);
	if (map != MAP_FAILED)
		printf("mmap should have failed, but didn't\n");
	else {
		ret = 0;
		munmap(map, 2 * lims.rlim_max);
	}

	munlockall();
	return ret;
}

int main(int argc, char **argv)
{
	int ret = 0;

	ret += test_limit();
	return ret;
}
