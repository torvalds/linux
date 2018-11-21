#include <sys/resource.h>
#include <stdio.h>

static  __attribute__((constructor)) void bpf_rlimit_ctor(void)
{
	struct rlimit rlim_old, rlim_new = {
		.rlim_cur	= RLIM_INFINITY,
		.rlim_max	= RLIM_INFINITY,
	};

	getrlimit(RLIMIT_MEMLOCK, &rlim_old);
	/* For the sake of running the test cases, we temporarily
	 * set rlimit to infinity in order for kernel to focus on
	 * errors from actual test cases and not getting noise
	 * from hitting memlock limits. The limit is on per-process
	 * basis and not a global one, hence destructor not really
	 * needed here.
	 */
	if (setrlimit(RLIMIT_MEMLOCK, &rlim_new) < 0) {
		perror("Unable to lift memlock rlimit");
		/* Trying out lower limit, but expect potential test
		 * case failures from this!
		 */
		rlim_new.rlim_cur = rlim_old.rlim_cur + (1UL << 20);
		rlim_new.rlim_max = rlim_old.rlim_max + (1UL << 20);
		setrlimit(RLIMIT_MEMLOCK, &rlim_new);
	}
}
