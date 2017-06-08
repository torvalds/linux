#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/wait.h>

#define pr_err(fmt, ...) \
		({ \
			fprintf(stderr, "%s:%d:" fmt ": %m\n", \
				__func__, __LINE__, ##__VA_ARGS__); \
			1; \
		})

#define NSIO	0xb7
#define NS_GET_USERNS   _IO(NSIO, 0x1)
#define NS_GET_PARENT   _IO(NSIO, 0x2)

#define __stack_aligned__	__attribute__((aligned(16)))
struct cr_clone_arg {
	char stack[128] __stack_aligned__;
	char stack_ptr[0];
};

static int child(void *args)
{
	prctl(PR_SET_PDEATHSIG, SIGKILL);
	while (1)
		sleep(1);
	exit(0);
}

int main(int argc, char *argv[])
{
	char *ns_strs[] = {"pid", "user"};
	char path[] = "/proc/0123456789/ns/pid";
	struct cr_clone_arg ca;
	struct stat st1, st2;
	int ns, pns, i;
	pid_t pid;

	pid = clone(child, ca.stack_ptr, CLONE_NEWUSER | CLONE_NEWPID | SIGCHLD, NULL);
	if (pid < 0)
		return pr_err("clone");

	for (i = 0; i < 2; i++) {
		snprintf(path, sizeof(path), "/proc/%d/ns/%s", pid, ns_strs[i]);
		ns = open(path, O_RDONLY);
		if (ns < 0)
			return pr_err("Unable to open %s", path);

		pns = ioctl(ns, NS_GET_PARENT);
		if (pns < 0)
			return pr_err("Unable to get a parent pidns");

		snprintf(path, sizeof(path), "/proc/self/ns/%s", ns_strs[i]);
		if (stat(path, &st2))
			return pr_err("Unable to stat %s", path);
		if (fstat(pns, &st1))
			return pr_err("Unable to stat the parent pidns");
		if (st1.st_ino != st2.st_ino)
			return pr_err("NS_GET_PARENT returned a wrong namespace");

		if (ioctl(pns, NS_GET_PARENT) >= 0 || errno != EPERM)
			return pr_err("Don't get EPERM");;
	}

	kill(pid, SIGKILL);
	wait(NULL);
	return 0;
}
