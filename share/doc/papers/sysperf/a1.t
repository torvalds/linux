.\" Copyright (c) 1985 The Regents of the University of California.
.\" All rights reserved.
.\"
.\" Redistribution and use in source and binary forms, with or without
.\" modification, are permitted provided that the following conditions
.\" are met:
.\" 1. Redistributions of source code must retain the above copyright
.\"    notice, this list of conditions and the following disclaimer.
.\" 2. Redistributions in binary form must reproduce the above copyright
.\"    notice, this list of conditions and the following disclaimer in the
.\"    documentation and/or other materials provided with the distribution.
.\" 3. Neither the name of the University nor the names of its contributors
.\"    may be used to endorse or promote products derived from this software
.\"    without specific prior written permission.
.\"
.\" THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
.\" ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
.\" IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
.\" ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
.\" FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
.\" DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
.\" OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
.\" HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
.\" LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
.\" OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
.\" SUCH DAMAGE.
.\"
.\"	@(#)a1.t	5.1 (Berkeley) 4/17/91
.\"
.ds RH Appendix A \- Benchmark sources
.nr H2 1
.sp 2
.de vS
.nf
..
.de vE
.fi
..
.bp
.SH
\s+2Appendix A \- Benchmark sources\s-2
.LP
The programs shown here run under 4.2 with only routines
from the standard libraries.  When run under 4.1 they were augmented
with a \fIgetpagesize\fP routine and a copy of the \fIrandom\fP
function from the C library.  The \fIvforks\fP and \fIvexecs\fP
programs are constructed from the \fIforks\fP and \fIexecs\fP programs,
respectively, by substituting calls to \fIfork\fP with calls to
\fIvfork\fP.
.SH
syscall
.LP
.vS
/*
 * System call overhead benchmark.
 */
main(argc, argv)
	char *argv[];
{
	register int ncalls;

	if (argc < 2) {
		printf("usage: %s #syscalls\n", argv[0]);
		exit(1);
	}
	ncalls = atoi(argv[1]);
	while (ncalls-- > 0)
		(void) getpid();
}
.vE
.SH
csw
.LP
.vS
/*
 * Context switching benchmark.
 *
 * Force system to context switch 2*nsigs
 * times by forking and exchanging signals.
 * To calculate system overhead for a context
 * switch, the signocsw program must be run
 * with nsigs.  Overhead is then estimated by
 *	t1 = time csw <n>
 *	t2 = time signocsw <n>
 *	overhead = t1 - 2 * t2;
 */
#include <signal.h>

int	sigsub();
int	otherpid;
int	nsigs;

main(argc, argv)
	char *argv[];
{
	int pid;

	if (argc < 2) {
		printf("usage: %s nsignals\n", argv[0]);
		exit(1);
	}
	nsigs = atoi(argv[1]);
	signal(SIGALRM, sigsub);
	otherpid = getpid();
	pid = fork();
	if (pid != 0) {
		otherpid = pid;
		kill(otherpid, SIGALRM);
	}
	for (;;)
		sigpause(0);
}

sigsub()
{

	signal(SIGALRM, sigsub);
	kill(otherpid, SIGALRM);
	if (--nsigs <= 0)
		exit(0);
}
.vE
.SH
signocsw
.LP
.vS
/*
 * Signal without context switch benchmark.
 */
#include <signal.h>

int	pid;
int	nsigs;
int	sigsub();

main(argc, argv)
	char *argv[];
{
	register int i;

	if (argc < 2) {
		printf("usage: %s nsignals\n", argv[0]);
		exit(1);
	}
	nsigs = atoi(argv[1]);
	signal(SIGALRM, sigsub);
	pid = getpid();
	for (i = 0; i < nsigs; i++)
		kill(pid, SIGALRM);
}

sigsub()
{

	signal(SIGALRM, sigsub);
}
.vE
.SH
pipeself
.LP
.vS
/*
 * IPC benchmark,
 * write to self using pipes.
 */

main(argc, argv)
	char *argv[];
{
	char buf[512];
	int fd[2], msgsize;
	register int i, iter;

	if (argc < 3) {
		printf("usage: %s iterations message-size\n", argv[0]);
		exit(1);
	}
	argc--, argv++;
	iter = atoi(*argv);
	argc--, argv++;
	msgsize = atoi(*argv);
	if (msgsize > sizeof (buf) || msgsize <= 0) {
		printf("%s: Bad message size.\n", *argv);
		exit(2);
	}
	if (pipe(fd) < 0) {
		perror("pipe");
		exit(3);
	}
	for (i = 0; i < iter; i++) {
		write(fd[1], buf, msgsize);
		read(fd[0], buf, msgsize);
	}
}
.vE
.SH
pipediscard
.LP
.vS
/*
 * IPC benchmarkl,
 * write and discard using pipes.
 */

main(argc, argv)
	char *argv[];
{
	char buf[512];
	int fd[2], msgsize;
	register int i, iter;

	if (argc < 3) {
		printf("usage: %s iterations message-size\n", argv[0]);
		exit(1);
	}
	argc--, argv++;
	iter = atoi(*argv);
	argc--, argv++;
	msgsize = atoi(*argv);
	if (msgsize > sizeof (buf) || msgsize <= 0) {
		printf("%s: Bad message size.\n", *argv);
		exit(2);
	}
	if (pipe(fd) < 0) {
		perror("pipe");
		exit(3);
	}
	if (fork() == 0)
		for (i = 0; i < iter; i++)
			read(fd[0], buf, msgsize);
	else
		for (i = 0; i < iter; i++)
			write(fd[1], buf, msgsize);
}
.vE
.SH
pipeback
.LP
.vS
/*
 * IPC benchmark,
 * read and reply using pipes.
 *
 * Process forks and exchanges messages
 * over a pipe in a request-response fashion.
 */

main(argc, argv)
	char *argv[];
{
	char buf[512];
	int fd[2], fd2[2], msgsize;
	register int i, iter;

	if (argc < 3) {
		printf("usage: %s iterations message-size\n", argv[0]);
		exit(1);
	}
	argc--, argv++;
	iter = atoi(*argv);
	argc--, argv++;
	msgsize = atoi(*argv);
	if (msgsize > sizeof (buf) || msgsize <= 0) {
		printf("%s: Bad message size.\n", *argv);
		exit(2);
	}
	if (pipe(fd) < 0) {
		perror("pipe");
		exit(3);
	}
	if (pipe(fd2) < 0) {
		perror("pipe");
		exit(3);
	}
	if (fork() == 0)
		for (i = 0; i < iter; i++) {
			read(fd[0], buf, msgsize);
			write(fd2[1], buf, msgsize);
		}
	else
		for (i = 0; i < iter; i++) {
			write(fd[1], buf, msgsize);
			read(fd2[0], buf, msgsize);
		}
}
.vE
.SH
forks
.LP
.vS
/*
 * Benchmark program to calculate fork+wait
 * overhead (approximately).  Process
 * forks and exits while parent waits.
 * The time to run this program is used
 * in calculating exec overhead.
 */

main(argc, argv)
	char *argv[];
{
	register int nforks, i;
	char *cp;
	int pid, child, status, brksize;

	if (argc < 2) {
		printf("usage: %s number-of-forks sbrk-size\n", argv[0]);
		exit(1);
	}
	nforks = atoi(argv[1]);
	if (nforks < 0) {
		printf("%s: bad number of forks\n", argv[1]);
		exit(2);
	}
	brksize = atoi(argv[2]);
	if (brksize < 0) {
		printf("%s: bad size to sbrk\n", argv[2]);
		exit(3);
	}
	cp = (char *)sbrk(brksize);
	if ((int)cp == -1) {
		perror("sbrk");
		exit(4);
	}
	for (i = 0; i < brksize; i += 1024)
		cp[i] = i;
	while (nforks-- > 0) {
		child = fork();
		if (child == -1) {
			perror("fork");
			exit(-1);
		}
		if (child == 0)
			_exit(-1);
		while ((pid = wait(&status)) != -1 && pid != child)
			;
	}
	exit(0);
}
.vE
.SH
execs
.LP
.vS
/*
 * Benchmark program to calculate exec
 * overhead (approximately).  Process
 * forks and execs "null" test program.
 * The time to run the fork program should
 * then be deducted from this one to
 * estimate the overhead for the exec.
 */

main(argc, argv)
	char *argv[];
{
	register int nexecs, i;
	char *cp, *sbrk();
	int pid, child, status, brksize;

	if (argc < 3) {
		printf("usage: %s number-of-execs sbrk-size job-name\n",
		    argv[0]);
		exit(1);
	}
	nexecs = atoi(argv[1]);
	if (nexecs < 0) {
		printf("%s: bad number of execs\n", argv[1]);
		exit(2);
	}
	brksize = atoi(argv[2]);
	if (brksize < 0) {
		printf("%s: bad size to sbrk\n", argv[2]);
		exit(3);
	}
	cp = sbrk(brksize);
	if ((int)cp == -1) {
		perror("sbrk");
		exit(4);
	}
	for (i = 0; i < brksize; i += 1024)
		cp[i] = i;
	while (nexecs-- > 0) {
		child = fork();
		if (child == -1) {
			perror("fork");
			exit(-1);
		}
		if (child == 0) {
			execv(argv[3], argv);
			perror("execv");
			_exit(-1);
		}
		while ((pid = wait(&status)) != -1 && pid != child)
			;
	}
	exit(0);
}
.vE
.SH
nulljob
.LP
.vS
/*
 * Benchmark "null job" program.
 */

main(argc, argv)
	char *argv[];
{

	exit(0);
}
.vE
.SH
bigjob
.LP
.vS
/*
 * Benchmark "null big job" program.
 */
/* 250 here is intended to approximate vi's text+data size */
char	space[1024 * 250] = "force into data segment";

main(argc, argv)
	char *argv[];
{

	exit(0);
}
.vE
.bp
.SH
seqpage
.LP
.vS
/*
 * Sequential page access benchmark.
 */
#include <sys/vadvise.h>

char	*valloc();

main(argc, argv)
	char *argv[];
{
	register i, niter;
	register char *pf, *lastpage;
	int npages = 4096, pagesize, vflag = 0;
	char *pages, *name;

	name = argv[0];
	argc--, argv++;
again:
	if (argc < 1) {
usage:
		printf("usage: %s [ -v ] [ -p #pages ] niter\n", name);
		exit(1);
	}
	if (strcmp(*argv, "-p") == 0) {
		argc--, argv++;
		if (argc < 1)
			goto usage;
		npages = atoi(*argv);
		if (npages <= 0) {
			printf("%s: Bad page count.\n", *argv);
			exit(2);
		}
		argc--, argv++;
		goto again;
	}
	if (strcmp(*argv, "-v") == 0) {
		argc--, argv++;
		vflag++;
		goto again;
	}
	niter = atoi(*argv);
	pagesize = getpagesize();
	pages = valloc(npages * pagesize);
	if (pages == (char *)0) {
		printf("Can't allocate %d pages (%2.1f megabytes).\n",
		    npages, (npages * pagesize) / (1024. * 1024.));
		exit(3);
	}
	lastpage = pages + (npages * pagesize);
	if (vflag)
		vadvise(VA_SEQL);
	for (i = 0; i < niter; i++)
		for (pf = pages; pf < lastpage; pf += pagesize)
			*pf = 1;
}
.vE
.SH
randpage
.LP
.vS
/*
 * Random page access benchmark.
 */
#include <sys/vadvise.h>

char	*valloc();
int	rand();

main(argc, argv)
	char *argv[];
{
	register int npages = 4096, pagesize, pn, i, niter;
	int vflag = 0, debug = 0;
	char *pages, *name;

	name = argv[0];
	argc--, argv++;
again:
	if (argc < 1) {
usage:
		printf("usage: %s [ -d ] [ -v ] [ -p #pages ] niter\n", name);
		exit(1);
	}
	if (strcmp(*argv, "-p") == 0) {
		argc--, argv++;
		if (argc < 1)
			goto usage;
		npages = atoi(*argv);
		if (npages <= 0) {
			printf("%s: Bad page count.\n", *argv);
			exit(2);
		}
		argc--, argv++;
		goto again;
	}
	if (strcmp(*argv, "-v") == 0) {
		argc--, argv++;
		vflag++;
		goto again;
	}
	if (strcmp(*argv, "-d") == 0) {
		argc--, argv++;
		debug++;
		goto again;
	}
	niter = atoi(*argv);
	pagesize = getpagesize();
	pages = valloc(npages * pagesize);
	if (pages == (char *)0) {
		printf("Can't allocate %d pages (%2.1f megabytes).\n",
		    npages, (npages * pagesize) / (1024. * 1024.));
		exit(3);
	}
	if (vflag)
		vadvise(VA_ANOM);
	for (i = 0; i < niter; i++) {
		pn = random() % npages;
		if (debug)
			printf("touch page %d\n", pn);
		pages[pagesize * pn] = 1;
	}
}
.vE
.SH
gausspage
.LP
.vS
/*
 * Random page access with
 * a gaussian distribution.
 *
 * Allocate a large (zero fill on demand) address
 * space and fault the pages in a random gaussian
 * order.
 */

float	sqrt(), log(), rnd(), cos(), gauss();
char	*valloc();
int	rand();

main(argc, argv)
	char *argv[];
{
	register int pn, i, niter, delta;
	register char *pages;
	float sd = 10.0;
	int npages = 4096, pagesize, debug = 0;
	char *name;

	name = argv[0];
	argc--, argv++;
again:
	if (argc < 1) {
usage:
		printf(
"usage: %s [ -d ] [ -p #pages ] [ -s standard-deviation ] iterations\n", name);
		exit(1);
	}
	if (strcmp(*argv, "-s") == 0) {
		argc--, argv++;
		if (argc < 1)
			goto usage;
		sscanf(*argv, "%f", &sd);
		if (sd <= 0) {
			printf("%s: Bad standard deviation.\n", *argv);
			exit(2);
		}
		argc--, argv++;
		goto again;
	}
	if (strcmp(*argv, "-p") == 0) {
		argc--, argv++;
		if (argc < 1)
			goto usage;
		npages = atoi(*argv);
		if (npages <= 0) {
			printf("%s: Bad page count.\n", *argv);
			exit(2);
		}
		argc--, argv++;
		goto again;
	}
	if (strcmp(*argv, "-d") == 0) {
		argc--, argv++;
		debug++;
		goto again;
	}
	niter = atoi(*argv);
	pagesize = getpagesize();
	pages = valloc(npages*pagesize);
	if (pages == (char *)0) {
		printf("Can't allocate %d pages (%2.1f megabytes).\n",
		    npages, (npages*pagesize) / (1024. * 1024.));
		exit(3);
	}
	pn = 0;
	for (i = 0; i < niter; i++) {
		delta = gauss(sd, 0.0);
		while (pn + delta < 0 || pn + delta > npages)
			delta = gauss(sd, 0.0);
		pn += delta;
		if (debug)
			printf("touch page %d\n", pn);
		else
			pages[pn * pagesize] = 1;
	}
}

float
gauss(sd, mean)
	float sd, mean;
{
	register float qa, qb;

	qa = sqrt(log(rnd()) * -2.0);
	qb = 3.14159 * rnd();
	return (qa * cos(qb) * sd + mean);
}

float
rnd()
{
	static int seed = 1;
	static int biggest = 0x7fffffff;

	return ((float)rand(seed) / (float)biggest);
}
.vE
