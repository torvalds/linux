/*-
 * Copyright (c) 2001 Robert N. M. Watson
 * All rights reserved.
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/uio.h>
#include <sys/ptrace.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/ktrace.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Relevant parts of a process credential.
 */
struct cred {
	uid_t	cr_euid, cr_ruid, cr_svuid;
	int	cr_issetugid;
};

/*
 * Description of a scenario.
 */
struct scenario {
	struct cred	*sc_cred1, *sc_cred2;	/* credentials of p1 and p2 */
	int		sc_canptrace_errno;	/* desired ptrace failure */
	int		sc_canktrace_errno;	/* desired ktrace failure */
	int		sc_cansighup_errno;	/* desired SIGHUP failure */
	int		sc_cansigsegv_errno;	/* desired SIGSEGV failure */
	int		sc_cansee_errno;	/* desired getprio failure */
	int		sc_cansched_errno;	/* desired setprio failure */
	char		*sc_name;		/* test name */
};

/*
 * Table of relevant credential combinations.
 */
static struct cred creds[] = {
/*		euid	ruid	svuid	issetugid	*/
/* 0 */ {	0,	0,	0,	0 },	/* privileged */
/* 1 */ {	0,	0,	0,	1 },	/* privileged + issetugid */
/* 2 */ {	1000,	1000,	1000,	0 },	/* unprivileged1 */
/* 3 */ {	1000,	1000,	1000,	1 },	/* unprivileged1 + issetugid */
/* 4 */ {	1001,	1001,	1001,	0 },	/* unprivileged2 */
/* 5 */ {	1001,	1001,	1001,	1 },	/* unprivileged2 + issetugid */
/* 6 */ {	1000,	0,	0,	0 },	/* daemon1 */
/* 7 */ {	1000,	0,	0,	1 },	/* daemon1 + issetugid */
/* 8 */ {	1001,	0,	0,	0 },	/* daemon2 */
/* 9 */ {	1001,	0,	0,	1 },	/* daemon2 + issetugid */
/* 10 */{	0,	1000,	1000,	0 },	/* setuid1 */
/* 11 */{	0, 	1000,	1000,	1 },	/* setuid1 + issetugid */
/* 12 */{	0,	1001,	1001,	0 },	/* setuid2 */
/* 13 */{	0,	1001,	1001,	1 },	/* setuid2 + issetugid */
};

/*
 * Table of scenarios.
 */
static const struct scenario scenarios[] = {
/*	cred1		cred2		ptrace	ktrace, sighup	sigsegv	see	sched	name */
/* privileged on privileged */
{	&creds[0],	&creds[0],	0,	0,	0,	0,	0,	0,	"0. priv on priv"},
{	&creds[0],	&creds[1],	0,	0,	0,	0,	0,	0,	"1. priv on priv"},
{	&creds[1],	&creds[0],	0,	0,	0,	0,	0,	0,	"2. priv on priv"},
{	&creds[1],	&creds[1],	0,	0,	0,	0,	0,	0,	"3. priv on priv"},
/* privileged on unprivileged */
{	&creds[0],	&creds[2],	0,	0,	0,	0,	0,	0,	"4. priv on unpriv1"},
{	&creds[0],	&creds[3],	0,	0,	0,	0,	0,	0,	"5. priv on unpriv1"},
{	&creds[1],	&creds[2],	0,	0,	0,	0,	0,	0,	"6. priv on unpriv1"},
{	&creds[1],	&creds[3],	0,	0,	0,	0,	0,	0,	"7. priv on unpriv1"},
/* unprivileged on privileged */
{	&creds[2],	&creds[0],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"8. unpriv1 on priv"},
{	&creds[2],	&creds[1],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"9. unpriv1 on priv"},
{	&creds[3],	&creds[0],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"10. unpriv1 on priv"},
{	&creds[3],	&creds[1],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"11. unpriv1 on priv"},
/* unprivileged on same unprivileged */
{	&creds[2],	&creds[2],	0,	0,	0,	0,	0,	0,	"12. unpriv1 on unpriv1"},
{	&creds[2],	&creds[3],	EPERM,	EPERM,	0,	EPERM,	0,	0,	"13. unpriv1 on unpriv1"},
{	&creds[3],	&creds[2],	0,	0,	0,	0,	0,	0,	"14. unpriv1 on unpriv1"},
{	&creds[3],	&creds[3],	EPERM,	EPERM,	0,	EPERM,	0,	0,	"15. unpriv1 on unpriv1"},
/* unprivileged on different unprivileged */
{	&creds[2],	&creds[4],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"16. unpriv1 on unpriv2"},
{	&creds[2],	&creds[5],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"17. unpriv1 on unpriv2"},
{	&creds[3],	&creds[4],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"18. unpriv1 on unpriv2"},
{	&creds[3],	&creds[5],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"19. unpriv1 on unpriv2"},
/* unprivileged on daemon, same */
{	&creds[2],	&creds[6],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"20. unpriv1 on daemon1"},
{	&creds[2],	&creds[7],	EPERM,	EPERM,	EPERM,	EPERM,	0, 	EPERM,	"21. unpriv1 on daemon1"},
{	&creds[3],	&creds[6],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"22. unpriv1 on daemon1"},
{	&creds[3],	&creds[7],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"23. unpriv1 on daemon1"},
/* unprivileged on daemon, different */
{	&creds[2],	&creds[8],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"24. unpriv1 on daemon2"},
{	&creds[2],	&creds[9],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"25. unpriv1 on daemon2"},
{	&creds[3],	&creds[8],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"26. unpriv1 on daemon2"},
{	&creds[3],	&creds[9],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"27. unpriv1 on daemon2"},
/* unprivileged on setuid, same */
{	&creds[2],	&creds[10],	EPERM,	EPERM,	0,	0,	0,	0,	"28. unpriv1 on setuid1"},
{	&creds[2],	&creds[11],	EPERM,	EPERM,	0,	EPERM,	0,	0,	"29. unpriv1 on setuid1"},
{	&creds[3],	&creds[10],	EPERM,	EPERM,	0,	0,	0,	0,	"30. unpriv1 on setuid1"},
{	&creds[3],	&creds[11],	EPERM,	EPERM,	0,	EPERM,	0,	0,	"31. unpriv1 on setuid1"},
/* unprivileged on setuid, different */
{	&creds[2],	&creds[12],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"32. unpriv1 on setuid2"},
{	&creds[2],	&creds[13],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"33. unpriv1 on setuid2"},
{	&creds[3],	&creds[12],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"34. unpriv1 on setuid2"},
{	&creds[3],	&creds[13],	EPERM,	EPERM,	EPERM,	EPERM,	0,	EPERM,	"35. unpriv1 on setuid2"},
};
int scenarios_count = sizeof(scenarios) / sizeof(struct scenario);

/*
 * Convert an error number to a compact string representation.  For now,
 * implement only the error numbers we are likely to see.
 */
static char *
errno_to_string(int error)
{

	switch (error) {
	case EPERM:
		return ("EPERM");
	case EACCES:
		return ("EACCES");
	case EINVAL:
		return ("EINVAL");
	case ENOSYS:
		return ("ENOSYS");
	case ESRCH:
		return ("ESRCH");
	case EOPNOTSUPP:
		return ("EOPNOTSUPP");
	case 0:
		return ("0");
	default:
		printf("%d\n", error);
		return ("unknown");
	}
}

/*
 * Return a process credential describing the current process.
 */
static int
cred_get(struct cred *cred)
{
	int error;

	error = getresuid(&cred->cr_ruid, &cred->cr_euid, &cred->cr_svuid);
	if (error)
		return (error);

	cred->cr_issetugid = issetugid();

	return (0);
}

/*
 * Userland stub for __setsugid() to take into account possible presence
 * in C library, kernel, et al.
 */
int
setugid(int flag)
{

#ifdef SETSUGID_SUPPORTED
	return (__setugid(flag));
#else
#ifdef SETSUGID_SUPPORTED_BUT_NO_LIBC_STUB
	return (syscall(374, flag));
#else
	return (ENOSYS);
#endif
#endif
}

/*
 * Set the current process's credentials to match the passed credential.
 */
static int
cred_set(struct cred *cred)
{
	int error;

	error = setresuid(cred->cr_ruid, cred->cr_euid, cred->cr_svuid);
	if (error)
		return (error);

	error = setugid(cred->cr_issetugid);
	if (error) {
		perror("__setugid");
		return (error);
	}

#ifdef CHECK_CRED_SET
	{
		uid_t ruid, euid, svuid;
		error = getresuid(&ruid, &euid, &svuid);
		if (error) {
			perror("getresuid");
			return (-1);
		}
		assert(ruid == cred->cr_ruid);
		assert(euid == cred->cr_euid);
		assert(svuid == cred->cr_svuid);
		assert(cred->cr_issetugid == issetugid());
	}
#endif /* !CHECK_CRED_SET */

	return (0);
}

/*
 * Print the passed process credential to the passed I/O stream.
 */
static void
cred_print(FILE *output, struct cred *cred)
{

	fprintf(output, "(e:%d r:%d s:%d P_SUGID:%d)", cred->cr_euid,
	    cred->cr_ruid, cred->cr_svuid, cred->cr_issetugid);
}

#define	LOOP_PTRACE	0
#define	LOOP_KTRACE	1
#define	LOOP_SIGHUP	2
#define	LOOP_SIGSEGV	3
#define	LOOP_SEE	4
#define	LOOP_SCHED	5
#define	LOOP_MAX	LOOP_SCHED

/*
 * Enact a scenario by looping through the four test cases for the scenario,
 * spawning off pairs of processes with the desired credentials, and
 * reporting results to stdout.
 */
static int
enact_scenario(int scenario)
{
	pid_t pid1, pid2;
	char *name, *tracefile;
	int error, desirederror, loop;

	for (loop = 0; loop < LOOP_MAX+1; loop++) {
		/*
		 * Spawn the first child, target of the operation.
		 */
		pid1 = fork();
		switch (pid1) {
		case -1:
			return (-1);
		case 0:
			/* child */
			error = cred_set(scenarios[scenario].sc_cred2);
			if (error) {
				perror("cred_set");
				return (error);
			}
			/* 200 seconds should be plenty of time. */
			sleep(200);
			exit(0);
		default:
			/* parent */
			break;
		}

		/*
		 * XXX
		 * This really isn't ideal -- give proc 1 a chance to set
		 * its credentials, or we may get spurious errors.  Really,
		 * some for of IPC should be used to allow the parent to
		 * wait for the first child to be ready before spawning
		 * the second child.
		 */
		sleep(1);

		/*
		 * Spawn the second child, source of the operation.
		 */
		pid2 = fork();
		switch (pid2) {
		case -1:
			return (-1);
	
		case 0:
			/* child */
			error = cred_set(scenarios[scenario].sc_cred1);
			if (error) {
				perror("cred_set");
				return (error);
			}
	
			/*
			 * Initialize errno to zero so as to catch any
			 * generated errors.  In each case, perform the
			 * operation.  Preserve the error number for later
			 * use so it doesn't get stomped on by any I/O.
			 * Determine the desired error for the given case
			 * by extracting it from the scenario table.
			 * Initialize a function name string for output
			 * prettiness.
			 */
			errno = 0;
			switch (loop) {
			case LOOP_PTRACE:
				error = ptrace(PT_ATTACH, pid1, NULL, 0);
				error = errno;
				name = "ptrace";
				desirederror =
				    scenarios[scenario].sc_canptrace_errno;
				break;
			case LOOP_KTRACE:
				tracefile = mktemp("/tmp/testuid_ktrace.XXXXXX");
				if (tracefile == NULL) {
					error = errno;
					perror("mktemp");
					break;
				}
				error = ktrace(tracefile, KTROP_SET,
				    KTRFAC_SYSCALL, pid1);
				error = errno;
				name = "ktrace";
				desirederror =
				    scenarios[scenario].sc_canktrace_errno;
				unlink(tracefile);
				break;
			case LOOP_SIGHUP:
				error = kill(pid1, SIGHUP);
				error = errno;
				name = "sighup";
				desirederror =
				    scenarios[scenario].sc_cansighup_errno;
				break;
			case LOOP_SIGSEGV:
				error = kill(pid1, SIGSEGV);
				error = errno;
				name = "sigsegv";
				desirederror =
				    scenarios[scenario].sc_cansigsegv_errno;
				break;
			case LOOP_SEE:
				getpriority(PRIO_PROCESS, pid1);
				error = errno;
				name = "see";
				desirederror =
				    scenarios[scenario].sc_cansee_errno;
				break;
			case LOOP_SCHED:
				error = setpriority(PRIO_PROCESS, pid1,
				   0);
				error = errno;
				name = "sched";
				desirederror =
				    scenarios[scenario].sc_cansched_errno;
				break;
			default:
				name = "broken";
			}

			if (error != desirederror) {
				fprintf(stdout,
				    "[%s].%s: expected %s, got %s\n  ",
				    scenarios[scenario].sc_name, name,
				    errno_to_string(desirederror),
				    errno_to_string(error));
				cred_print(stdout,
				    scenarios[scenario].sc_cred1);
				cred_print(stdout,
				    scenarios[scenario].sc_cred2);
				fprintf(stdout, "\n");
			}

			exit(0);

		default:
			/* parent */
			break;
		}

		error = waitpid(pid2, NULL, 0);
		/*
		 * Once pid2 has died, it's safe to kill pid1, if it's still
		 * alive.  Mask signal failure in case the test actually
		 * killed pid1 (not unlikely: can occur in both signal and
		 * ptrace cases).
		 */
		kill(pid1, SIGKILL);
		error = waitpid(pid2, NULL, 0);
	}
	
	return (0);
}

void
enact_scenarios(void)
{
	int i, error;

	for (i = 0; i < scenarios_count; i++) {
		error = enact_scenario(i);
		if (error)
			perror("enact_scenario");
	}
}
