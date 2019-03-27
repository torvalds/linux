/*-
 * Copyright (c) 2011, 2012 Konstantin Belousov <kib@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TRACE	">>>> "

static const char *
decode_wait_status(int status)
{
	static char c[128];
	char b[32];
	int first;

	c[0] = '\0';
	first = 1;
	if (WIFCONTINUED(status)) {
		first = 0;
		strlcat(c, "CONT", sizeof(c));
	}
	if (WIFEXITED(status)) {
		if (first)
			first = 0;
		else
			strlcat(c, ",", sizeof(c));
		snprintf(b, sizeof(b), "EXIT(%d)", WEXITSTATUS(status));
		strlcat(c, b, sizeof(c));
	}
	if (WIFSIGNALED(status)) {
		if (first)
			first = 0;
		else
			strlcat(c, ",", sizeof(c));
		snprintf(b, sizeof(b), "SIG(%s)", strsignal(WTERMSIG(status)));
		strlcat(c, b, sizeof(c));
		if (WCOREDUMP(status))
			strlcat(c, ",CORE", sizeof(c));
	}
	if (WIFSTOPPED(status)) {
		if (first)
			first = 0;
		else
			strlcat(c, ",", sizeof(c));
		snprintf(b, sizeof(b), "SIG(%s)", strsignal(WSTOPSIG(status)));
		strlcat(c, b, sizeof(c));
	}
	return (c);
}

static const char *
decode_pl_flags(struct ptrace_lwpinfo *lwpinfo)
{
	static char c[128];
	static struct decode_tag {
		int flag;
		const char *desc;
	} decode[] = {
		{ PL_FLAG_SA, "SA" },
		{ PL_FLAG_BOUND, "BOUND" },
		{ PL_FLAG_SCE, "SCE" },
		{ PL_FLAG_SCX, "SCX" },
		{ PL_FLAG_EXEC, "EXEC" },
		{ PL_FLAG_SI, "SI" },
		{ PL_FLAG_FORKED, "FORKED" },
		{ PL_FLAG_CHILD, "CHILD" },
		{ PL_FLAG_BORN, "LWPBORN" },
		{ PL_FLAG_EXITED, "LWPEXITED" },
		{ PL_FLAG_VFORKED, "VFORKED" },
		{ PL_FLAG_VFORK_DONE, "VFORKDONE" },
	};
	char de[32];
	unsigned first, flags, i;

	c[0] = '\0';
	first = 1;
	flags = lwpinfo->pl_flags;
	for (i = 0; i < sizeof(decode) / sizeof(decode[0]); i++) {
		if ((flags & decode[i].flag) != 0) {
			if (first)
				first = 0;
			else
				strlcat(c, ",", sizeof(c));
			strlcat(c, decode[i].desc, sizeof(c));
			flags &= ~decode[i].flag;
		}
	}
	for (i = 0; i < sizeof(flags) * NBBY; i++) {
		if ((flags & (1 << i)) != 0) {
			if (first)
				first = 0;
			else
				strlcat(c, ",", sizeof(c));
			snprintf(de, sizeof(de), "<%d>", i);
			strlcat(c, de, sizeof(c));
		}
	}
	return (c);
}

static const char *
decode_pl_event(struct ptrace_lwpinfo *lwpinfo)
{

	switch (lwpinfo->pl_event) {
	case PL_EVENT_NONE:
		return ("NONE");

	case PL_EVENT_SIGNAL:
		return ("SIG");

	default:
		return ("UNKNOWN");
	}
}

static void
get_pathname(pid_t pid)
{
	char pathname[PATH_MAX];
	int error, name[4];
	size_t len;

	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_PATHNAME;
	name[3] = pid;

	len = sizeof(pathname);
	error = sysctl(name, 4, pathname, &len, NULL, 0);
	if (error < 0) {
		if (errno != ESRCH) {
			fprintf(stderr, "sysctl kern.proc.pathname.%d: %s\n",
			    pid, strerror(errno));
			return;
		}
		fprintf(stderr, "pid %d exited\n", pid);
		return;
	}
	if (len == 0 || strlen(pathname) == 0) {
		fprintf(stderr, "No cached pathname for process %d\n", pid);
		return;
	}
	printf(TRACE "pid %d path %s\n", pid, pathname);
}

static void
wait_info(int pid, int status, struct ptrace_lwpinfo *lwpinfo)
{
	long *args;
	int error, i;

	printf(TRACE "pid %d wait %s", pid,
	    decode_wait_status(status));
	if (lwpinfo != NULL) {
		printf(" event %s flags %s",
		    decode_pl_event(lwpinfo), decode_pl_flags(lwpinfo));
		if ((lwpinfo->pl_flags & (PL_FLAG_SCE | PL_FLAG_SCX)) != 0) {
			printf(" sc%d", lwpinfo->pl_syscall_code);
			args = calloc(lwpinfo->pl_syscall_narg, sizeof(long));
			error = ptrace(PT_GET_SC_ARGS, lwpinfo->pl_lwpid,
			    (caddr_t)args, lwpinfo->pl_syscall_narg *
			    sizeof(long));
			if (error == 0) {
				for (i = 0; i < (int)lwpinfo->pl_syscall_narg;
				    i++) {
					printf("%c%#lx", i == 0 ? '(' : ',',
					    args[i]);
				}
			} else {
				fprintf(stderr, "PT_GET_SC_ARGS failed: %s",
				    strerror(errno));
			}
			printf(")");
			free(args);
		}
	}
	printf("\n");
}

static int
trace_sc(int pid)
{
	struct ptrace_lwpinfo lwpinfo;
	int status;

	if (ptrace(PT_TO_SCE, pid, (caddr_t)1, 0) < 0) {
		perror("PT_TO_SCE");
		ptrace(PT_KILL, pid, NULL, 0);
		return (-1);
	}

	if (waitpid(pid, &status, 0) == -1) {
		perror("waitpid");
		return (-1);
	}
	if (WIFEXITED(status) || WIFSIGNALED(status)) {
		wait_info(pid, status, NULL);
		return (-1);
	}
	assert(WIFSTOPPED(status));
	assert(WSTOPSIG(status) == SIGTRAP);

	if (ptrace(PT_LWPINFO, pid, (caddr_t)&lwpinfo, sizeof(lwpinfo)) < 0) {
		perror("PT_LWPINFO");
		ptrace(PT_KILL, pid, NULL, 0);
		return (-1);
	}
	wait_info(pid, status, &lwpinfo);
	assert(lwpinfo.pl_flags & PL_FLAG_SCE);

	if (ptrace(PT_TO_SCX, pid, (caddr_t)1, 0) < 0) {
		perror("PT_TO_SCX");
		ptrace(PT_KILL, pid, NULL, 0);
		return (-1);
	}

	if (waitpid(pid, &status, 0) == -1) {
		perror("waitpid");
		return (-1);
	}
	if (WIFEXITED(status) || WIFSIGNALED(status)) {
		wait_info(pid, status, NULL);
		return (-1);
	}
	assert(WIFSTOPPED(status));
	assert(WSTOPSIG(status) == SIGTRAP);

	if (ptrace(PT_LWPINFO, pid, (caddr_t)&lwpinfo, sizeof(lwpinfo)) < 0) {
		perror("PT_LWPINFO");
		ptrace(PT_KILL, pid, NULL, 0);
		return (-1);
	}
	wait_info(pid, status, &lwpinfo);
	assert(lwpinfo.pl_flags & PL_FLAG_SCX);

	if (lwpinfo.pl_flags & PL_FLAG_EXEC)
		get_pathname(pid);

	if (lwpinfo.pl_flags & PL_FLAG_FORKED) {
		printf(TRACE "forked child %d\n", lwpinfo.pl_child_pid);
		return (lwpinfo.pl_child_pid);
	}
	return (0);
}

static int
trace_cont(int pid)
{
	struct ptrace_lwpinfo lwpinfo;
	int status;

	if (ptrace(PT_CONTINUE, pid, (caddr_t)1, 0) < 0) {
		perror("PT_CONTINUE");
		ptrace(PT_KILL, pid, NULL, 0);
		return (-1);
	}

	if (waitpid(pid, &status, 0) == -1) {
		perror("waitpid");
		return (-1);
	}
	if (WIFEXITED(status) || WIFSIGNALED(status)) {
		wait_info(pid, status, NULL);
		return (-1);
	}
	assert(WIFSTOPPED(status));
	assert(WSTOPSIG(status) == SIGTRAP);

	if (ptrace(PT_LWPINFO, pid, (caddr_t)&lwpinfo, sizeof(lwpinfo)) < 0) {
		perror("PT_LWPINFO");
		ptrace(PT_KILL, pid, NULL, 0);
		return (-1);
	}
	wait_info(pid, status, &lwpinfo);

	if ((lwpinfo.pl_flags & (PL_FLAG_EXEC | PL_FLAG_SCX)) ==
	    (PL_FLAG_EXEC | PL_FLAG_SCX))
		get_pathname(pid);

	if ((lwpinfo.pl_flags & (PL_FLAG_FORKED | PL_FLAG_SCX)) ==
	    (PL_FLAG_FORKED | PL_FLAG_SCX)) {
		printf(TRACE "forked child %d\n", lwpinfo.pl_child_pid);
		return (lwpinfo.pl_child_pid);
	}

	return (0);
}

static int trace_syscalls = 1;

static int
trace(pid_t pid)
{

	return (trace_syscalls ? trace_sc(pid) : trace_cont(pid));
}


int
main(int argc, char *argv[])
{
	struct ptrace_lwpinfo lwpinfo;
	int c, status, use_vfork;
	pid_t pid, pid1;

	trace_syscalls = 1;
	use_vfork = 0;
	while ((c = getopt(argc, argv, "csv")) != -1) {
		switch (c) {
		case 'c':
			trace_syscalls = 0;
			break;
		case 's':
			trace_syscalls = 1;
			break;
		case 'v':
			use_vfork = 1;
			break;
		default:
		case '?':
			fprintf(stderr, "Usage: %s [-c] [-s] [-v]\n", argv[0]);
			return (2);
		}
	}

	if ((pid = fork()) < 0) {
		perror("fork");
		return 1;
	}
	else if (pid == 0) {
		if (ptrace(PT_TRACE_ME, 0, NULL, 0) < 0) {
			perror("PT_TRACE_ME");
			_exit(1);
		}
		kill(getpid(), SIGSTOP);
		getpid();
		if ((pid1 = use_vfork ? vfork() : fork()) < 0) {
			perror("fork1");
			return (1);
		} else if (pid1 == 0) {
			printf("Hi from child %d\n", getpid());
			execl("/bin/ls", "ls", "/", (char *)NULL);
		}
	}
	else { /* parent */
		if (waitpid(pid, &status, 0) == -1) {
			perror("waitpid");
			return (-1);
		}
		assert(WIFSTOPPED(status));
		assert(WSTOPSIG(status) == SIGSTOP);

		if (ptrace(PT_LWPINFO, pid, (caddr_t)&lwpinfo,
		    sizeof(lwpinfo)) < 0) {
			perror("PT_LWPINFO");
			ptrace(PT_KILL, pid, NULL, 0);
			return (-1);
		}
		wait_info(pid, status, &lwpinfo);

		if (ptrace(PT_FOLLOW_FORK, pid, 0, 1) < 0) {
			perror("PT_FOLLOW_FORK");
			ptrace(PT_KILL, pid, NULL, 0);
			return (2);
		}

		while ((pid1 = trace(pid)) >= 0) {
			if (pid1 != 0) {
				printf(TRACE "attached to pid %d\n", pid1);
#if 0
				kill(pid1, SIGCONT);
#endif
				if (waitpid(pid1, &status, 0) == -1) {
					perror("waitpid");
					return (-1);
				}
				printf(TRACE "nested loop, pid %d status %s\n",
				    pid1, decode_wait_status(status));
				assert(WIFSTOPPED(status));
				assert(WSTOPSIG(status) == SIGSTOP);
				if (ptrace(PT_LWPINFO, pid1, (caddr_t)&lwpinfo,
				    sizeof(lwpinfo)) < 0) {
					perror("PT_LWPINFO");
					ptrace(PT_KILL, pid1, NULL, 0);
					return (-1);
				}
				wait_info(pid1, status, &lwpinfo);

				while (trace(pid1) >= 0)
					;
			}
		}

		ptrace(PT_CONTINUE, pid, (caddr_t)1, 0);
	}
	return (0);
}
