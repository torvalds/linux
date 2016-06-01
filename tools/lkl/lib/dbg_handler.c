#include <stdio.h>
#include <lkl_host.h>

extern void dbg_entrance();
static int dbg_running = 0;

static void dbg_thread(void* arg) {
	lkl_host_ops.thread_detach();
	printf("======Enter Debug======\n");
	dbg_entrance();
	printf("======Exit Debug======\n");
	dbg_running = 0;
}

void dbg_handler(int signum) {
	/* We don't care about the possible race on dbg_running. */
	if (dbg_running) {
		fprintf(stderr, "A debug lib is running\n");
		return;
	}
	dbg_running = 1;
	lkl_host_ops.thread_create(&dbg_thread, NULL);
}

#ifndef __MINGW32__
#include <signal.h>
void lkl_register_dbg_handler() {
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = dbg_handler;
	if (sigaction(SIGTSTP, &sa, NULL) == -1) {
		perror("sigaction");
	}
}
#else
void lkl_register_dbg_handler() {
	fprintf(stderr, "lkl_register_dbg_handler is not implemented.\n");
}
#endif
