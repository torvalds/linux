#include <stddef.h>
#include <stdbool.h>
#include <linux/compiler.h>
#include <linux/lockdep.h>
#include <unistd.h>
#include <sys/syscall.h>

static __thread struct task_struct current_obj;

/* lockdep wants these */
bool debug_locks = true;
bool debug_locks_silent;

__attribute__((constructor)) static void liblockdep_init(void)
{
	lockdep_init();
}

__attribute__((destructor)) static void liblockdep_exit(void)
{
	debug_check_no_locks_held();
}

struct task_struct *__curr(void)
{
	if (current_obj.pid == 0) {
		/* Makes lockdep output pretty */
		prctl(PR_GET_NAME, current_obj.comm);
		current_obj.pid = syscall(__NR_gettid);
	}

	return &current_obj;
}
