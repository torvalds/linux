.\" $FreeBSD$
.am vS
..
.am vE
..
'ss 23
'ds _ \d\(mi\u
'ps 9z
'vs 10p
'ds - \(mi
'ds / \\h'\\w' 'u-\\w'/'u'/
'ds /* \\h'\\w' 'u-\\w'/'u'/*
'bd B 3
'bd S B 3
'nr cm 0
'nf
'de vH
'ev 2
'ft 1
'sp .35i
'tl '\s14\f3\\*(=F\fP\s0'\\*(=H'\f3\s14\\*(=F\fP\s0'
'sp .25i
'ft 1
\f2\s12\h'\\n(.lu-\w'\\*(=f'u'\\*(=f\fP\s0\h'|0u'
.sp .05i
'ev
'ds =G \\*(=F
..
'de vF
'ev 2
'sp .35i
'ie o 'tl '\f2\\*(=M''Page % of \\*(=G\fP'
'el 'tl '\f2Page % of \\*(=G''\\*(=M\fP'
'bp
'ev
'ft 1
'if \\n(cm=1 'ft 2
..
'de ()
'pn 1
..
'de +C
'nr cm 1
'ft 2
'ds +K
'ds -K
..
'de -C
'nr cm 0
'ft 1
'ds +K \f3
'ds -K \fP
..
'+C
'-C
'am +C
'ne 3
..
'de FN
\f2\s14\h'\\n(.lu-\w'\\$1'u'\\$1\fP\s0\h'|0u'\c
.if r x .if \\nx .if d =F .tm \\$1 \\*(=F \\n%
'ds =f \&...\\$1
..
'de FC
.if r x .if \\nx .if d =F .tm \\$1 \\*(=F \\n%
'ds =f \&...\\$1
..
'de -F
'rm =f
..
'ft 1
'lg 0
'-F
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
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP System call overhead benchmark\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kregister\*(-K \*(+Kint\*(-K ncalls;

\h'|11n'\*(+Kif\*(-K (argc < 2) \*(+K{\*(-K
\h'|21n'printf("usage: %s #syscalls\en", argv[0]);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'ncalls = atoi(argv[1]);
\h'|11n'\*(+Kwhile\*(-K (ncalls\*-\*- > 0)
\h'|21n'(\*(+Kvoid\*(-K) getpid();
\*(+K}\*(-K\c\c
'-F

.vE
.SH
csw
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Context switching benchmark\&.
 \fI*\fP
 \fI*\fP Force system to context switch 2\fI*\fPnsigs
 \fI*\fP times by forking and exchanging signals\&.
 \fI*\fP To calculate system overhead for a context
 \fI*\fP switch, the signocsw program must be run
 \fI*\fP with nsigs\&.  Overhead is then estimated by
 \fI*\fP\h'|11n't1 = time csw <n>
 \fI*\fP\h'|11n't2 = time signocsw <n>
 \fI*\fP\h'|11n'overhead = t1 \*- 2 \fI*\fP t2;
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+K#include\*(-K <signal\&.h>

\*(+Kint\*(-K\h'|11n'sigsub();
\*(+Kint\*(-K\h'|11n'otherpid;
\*(+Kint\*(-K\h'|11n'nsigs;

'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kint\*(-K pid;

\h'|11n'\*(+Kif\*(-K (argc < 2) \*(+K{\*(-K
\h'|21n'printf("usage: %s nsignals\en", argv[0]);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'nsigs = atoi(argv[1]);
\h'|11n'signal(SIGALRM, sigsub);
\h'|11n'otherpid = getpid();
\h'|11n'pid = fork();
\h'|11n'\*(+Kif\*(-K (pid != 0) \*(+K{\*(-K
\h'|21n'otherpid = pid;
\h'|21n'kill(otherpid, SIGALRM);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kfor\*(-K (;;)
\h'|21n'sigpause(0);
\*(+K}\*(-K\c\c
'-F


'FN sigsub
sigsub()
\*(+K{\*(-K

\h'|11n'signal(SIGALRM, sigsub);
\h'|11n'kill(otherpid, SIGALRM);
\h'|11n'\*(+Kif\*(-K (\*-\*-nsigs <= 0)
\h'|21n'exit(0);
\*(+K}\*(-K\c\c
'-F

.vE
.SH
signocsw
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Signal without context switch benchmark\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+K#include\*(-K <signal\&.h>

\*(+Kint\*(-K\h'|11n'pid;
\*(+Kint\*(-K\h'|11n'nsigs;
\*(+Kint\*(-K\h'|11n'sigsub();

'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kregister\*(-K \*(+Kint\*(-K i;

\h'|11n'\*(+Kif\*(-K (argc < 2) \*(+K{\*(-K
\h'|21n'printf("usage: %s nsignals\en", argv[0]);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'nsigs = atoi(argv[1]);
\h'|11n'signal(SIGALRM, sigsub);
\h'|11n'pid = getpid();
\h'|11n'\*(+Kfor\*(-K (i = 0; i < nsigs; i++)
\h'|21n'kill(pid, SIGALRM);
\*(+K}\*(-K\c\c
'-F


'FN sigsub
sigsub()
\*(+K{\*(-K

\h'|11n'signal(SIGALRM, sigsub);
\*(+K}\*(-K\c\c
'-F

.vE
.SH
pipeself
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP IPC benchmark,
 \fI*\fP write to self using pipes\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C


'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kchar\*(-K buf[512];
\h'|11n'\*(+Kint\*(-K fd[2], msgsize;
\h'|11n'\*(+Kregister\*(-K \*(+Kint\*(-K i, iter;

\h'|11n'\*(+Kif\*(-K (argc < 3) \*(+K{\*(-K
\h'|21n'printf("usage: %s iterations message\*-size\en", argv[0]);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'argc\*-\*-, argv++;
\h'|11n'iter = atoi(\fI*\fPargv);
\h'|11n'argc\*-\*-, argv++;
\h'|11n'msgsize = atoi(\fI*\fPargv);
\h'|11n'\*(+Kif\*(-K (msgsize > \*(+Ksizeof\*(-K (buf) || msgsize <= 0) \*(+K{\*(-K
\h'|21n'printf("%s: Bad message size\&.\en", \fI*\fPargv);
\h'|21n'exit(2);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (pipe(fd) < 0) \*(+K{\*(-K
\h'|21n'perror("pipe");
\h'|21n'exit(3);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kfor\*(-K (i = 0; i < iter; i++) \*(+K{\*(-K
\h'|21n'write(fd[1], buf, msgsize);
\h'|21n'read(fd[0], buf, msgsize);
\h'|11n'\*(+K}\*(-K
\*(+K}\*(-K\c\c
'-F

.vE
.SH
pipediscard
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP IPC benchmarkl,
 \fI*\fP write and discard using pipes\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C


'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kchar\*(-K buf[512];
\h'|11n'\*(+Kint\*(-K fd[2], msgsize;
\h'|11n'\*(+Kregister\*(-K \*(+Kint\*(-K i, iter;

\h'|11n'\*(+Kif\*(-K (argc < 3) \*(+K{\*(-K
\h'|21n'printf("usage: %s iterations message\*-size\en", argv[0]);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'argc\*-\*-, argv++;
\h'|11n'iter = atoi(\fI*\fPargv);
\h'|11n'argc\*-\*-, argv++;
\h'|11n'msgsize = atoi(\fI*\fPargv);
\h'|11n'\*(+Kif\*(-K (msgsize > \*(+Ksizeof\*(-K (buf) || msgsize <= 0) \*(+K{\*(-K
\h'|21n'printf("%s: Bad message size\&.\en", \fI*\fPargv);
\h'|21n'exit(2);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (pipe(fd) < 0) \*(+K{\*(-K
\h'|21n'perror("pipe");
\h'|21n'exit(3);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (fork() == 0)
\h'|21n'\*(+Kfor\*(-K (i = 0; i < iter; i++)
\h'|31n'read(fd[0], buf, msgsize);
\h'|11n'\*(+Kelse\*(-K
\h'|21n'\*(+Kfor\*(-K (i = 0; i < iter; i++)
\h'|31n'write(fd[1], buf, msgsize);
\*(+K}\*(-K\c\c
'-F

.vE
.SH
pipeback
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP IPC benchmark,
 \fI*\fP read and reply using pipes\&.
 \fI*\fP
 \fI*\fP Process forks and exchanges messages
 \fI*\fP over a pipe in a request\*-response fashion\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C


'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kchar\*(-K buf[512];
\h'|11n'\*(+Kint\*(-K fd[2], fd2[2], msgsize;
\h'|11n'\*(+Kregister\*(-K \*(+Kint\*(-K i, iter;

\h'|11n'\*(+Kif\*(-K (argc < 3) \*(+K{\*(-K
\h'|21n'printf("usage: %s iterations message\*-size\en", argv[0]);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'argc\*-\*-, argv++;
\h'|11n'iter = atoi(\fI*\fPargv);
\h'|11n'argc\*-\*-, argv++;
\h'|11n'msgsize = atoi(\fI*\fPargv);
\h'|11n'\*(+Kif\*(-K (msgsize > \*(+Ksizeof\*(-K (buf) || msgsize <= 0) \*(+K{\*(-K
\h'|21n'printf("%s: Bad message size\&.\en", \fI*\fPargv);
\h'|21n'exit(2);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (pipe(fd) < 0) \*(+K{\*(-K
\h'|21n'perror("pipe");
\h'|21n'exit(3);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (pipe(fd2) < 0) \*(+K{\*(-K
\h'|21n'perror("pipe");
\h'|21n'exit(3);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (fork() == 0)
\h'|21n'\*(+Kfor\*(-K (i = 0; i < iter; i++) \*(+K{\*(-K
\h'|31n'read(fd[0], buf, msgsize);
\h'|31n'write(fd2[1], buf, msgsize);
\h'|21n'\*(+K}\*(-K
\h'|11n'\*(+Kelse\*(-K
\h'|21n'\*(+Kfor\*(-K (i = 0; i < iter; i++) \*(+K{\*(-K
\h'|31n'write(fd[1], buf, msgsize);
\h'|31n'read(fd2[0], buf, msgsize);
\h'|21n'\*(+K}\*(-K
\*(+K}\*(-K\c\c
'-F

.vE
.SH
forks
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Benchmark program to calculate fork+wait
 \fI*\fP overhead (approximately)\&.  Process
 \fI*\fP forks and exits while parent waits\&.
 \fI*\fP The time to run this program is used
 \fI*\fP in calculating exec overhead\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C


'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kregister\*(-K \*(+Kint\*(-K nforks, i;
\h'|11n'\*(+Kchar\*(-K \fI*\fPcp;
\h'|11n'\*(+Kint\*(-K pid, child, status, brksize;

\h'|11n'\*(+Kif\*(-K (argc < 2) \*(+K{\*(-K
\h'|21n'printf("usage: %s number\*-of\*-forks sbrk\*-size\en", argv[0]);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'nforks = atoi(argv[1]);
\h'|11n'\*(+Kif\*(-K (nforks < 0) \*(+K{\*(-K
\h'|21n'printf("%s: bad number of forks\en", argv[1]);
\h'|21n'exit(2);
\h'|11n'\*(+K}\*(-K
\h'|11n'brksize = atoi(argv[2]);
\h'|11n'\*(+Kif\*(-K (brksize < 0) \*(+K{\*(-K
\h'|21n'printf("%s: bad size to sbrk\en", argv[2]);
\h'|21n'exit(3);
\h'|11n'\*(+K}\*(-K
\h'|11n'cp = (\*(+Kchar\*(-K \fI*\fP)sbrk(brksize);
\h'|11n'\*(+Kif\*(-K ((\*(+Kint\*(-K)cp == \*-1) \*(+K{\*(-K
\h'|21n'perror("sbrk");
\h'|21n'exit(4);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kfor\*(-K (i = 0; i < brksize; i += 1024)
\h'|21n'cp[i] = i;
\h'|11n'\*(+Kwhile\*(-K (nforks\*-\*- > 0) \*(+K{\*(-K
\h'|21n'child = fork();
\h'|21n'\*(+Kif\*(-K (child == \*-1) \*(+K{\*(-K
\h'|31n'perror("fork");
\h'|31n'exit(\*-1);
\h'|21n'\*(+K}\*(-K
\h'|21n'\*(+Kif\*(-K (child == 0)
\h'|31n'\*_exit(\*-1);
\h'|21n'\*(+Kwhile\*(-K ((pid = wait(&status)) != \*-1 && pid != child)
\h'|31n';
\h'|11n'\*(+K}\*(-K
\h'|11n'exit(0);
\*(+K}\*(-K\c\c
'-F

.vE
.SH
execs
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Benchmark program to calculate exec
 \fI*\fP overhead (approximately)\&.  Process
 \fI*\fP forks and execs "null" test program\&.
 \fI*\fP The time to run the fork program should
 \fI*\fP then be deducted from this one to
 \fI*\fP estimate the overhead for the exec\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C


'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kregister\*(-K \*(+Kint\*(-K nexecs, i;
\h'|11n'\*(+Kchar\*(-K \fI*\fPcp, \fI*\fPsbrk();
\h'|11n'\*(+Kint\*(-K pid, child, status, brksize;

\h'|11n'\*(+Kif\*(-K (argc < 3) \*(+K{\*(-K
\h'|21n'printf("usage: %s number\*-of\*-execs sbrk\*-size job\*-name\en",
\h'|21n'    argv[0]);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'nexecs = atoi(argv[1]);
\h'|11n'\*(+Kif\*(-K (nexecs < 0) \*(+K{\*(-K
\h'|21n'printf("%s: bad number of execs\en", argv[1]);
\h'|21n'exit(2);
\h'|11n'\*(+K}\*(-K
\h'|11n'brksize = atoi(argv[2]);
\h'|11n'\*(+Kif\*(-K (brksize < 0) \*(+K{\*(-K
\h'|21n'printf("%s: bad size to sbrk\en", argv[2]);
\h'|21n'exit(3);
\h'|11n'\*(+K}\*(-K
\h'|11n'cp = sbrk(brksize);
\h'|11n'\*(+Kif\*(-K ((\*(+Kint\*(-K)cp == \*-1) \*(+K{\*(-K
\h'|21n'perror("sbrk");
\h'|21n'exit(4);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kfor\*(-K (i = 0; i < brksize; i += 1024)
\h'|21n'cp[i] = i;
\h'|11n'\*(+Kwhile\*(-K (nexecs\*-\*- > 0) \*(+K{\*(-K
\h'|21n'child = fork();
\h'|21n'\*(+Kif\*(-K (child == \*-1) \*(+K{\*(-K
\h'|31n'perror("fork");
\h'|31n'exit(\*-1);
\h'|21n'\*(+K}\*(-K
\h'|21n'\*(+Kif\*(-K (child == 0) \*(+K{\*(-K
\h'|31n'execv(argv[3], argv);
\h'|31n'perror("execv");
\h'|31n'\*_exit(\*-1);
\h'|21n'\*(+K}\*(-K
\h'|21n'\*(+Kwhile\*(-K ((pid = wait(&status)) != \*-1 && pid != child)
\h'|31n';
\h'|11n'\*(+K}\*(-K
\h'|11n'exit(0);
\*(+K}\*(-K\c\c
'-F

.vE
.SH
nulljob
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Benchmark "null job" program\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C


'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K

\h'|11n'exit(0);
\*(+K}\*(-K\c\c
'-F

.vE
.SH
bigjob
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Benchmark "null big job" program\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C
 250 here is intended to approximate vi\'s text+data size \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+Kchar\*(-K\h'|11n'space[1024 \fI*\fP 250] = "force into data segment";

'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K

\h'|11n'exit(0);
\*(+K}\*(-K\c\c
'-F

.vE
.bp
.SH
seqpage
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Sequential page access benchmark\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+K#include\*(-K <sys\fI\h'\w' 'u-\w'/'u'/\fPvadvise\&.h>

\*(+Kchar\*(-K\h'|11n'\fI*\fPvalloc();

'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kregister\*(-K i, niter;
\h'|11n'\*(+Kregister\*(-K \*(+Kchar\*(-K \fI*\fPpf, \fI*\fPlastpage;
\h'|11n'\*(+Kint\*(-K npages = 4096, pagesize, vflag = 0;
\h'|11n'\*(+Kchar\*(-K \fI*\fPpages, \fI*\fPname;

\h'|11n'name = argv[0];
\h'|11n'argc\*-\*-, argv++;
again:
\h'|11n'\*(+Kif\*(-K (argc < 1) \*(+K{\*(-K
usage:
\h'|21n'printf("usage: %s [ \*-v ] [ \*-p #pages ] niter\en", name);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (strcmp(\fI*\fPargv, "\*-p") == 0) \*(+K{\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'\*(+Kif\*(-K (argc < 1)
\h'|31n'\*(+Kgoto\*(-K usage;
\h'|21n'npages = atoi(\fI*\fPargv);
\h'|21n'\*(+Kif\*(-K (npages <= 0) \*(+K{\*(-K
\h'|31n'printf("%s: Bad page count\&.\en", \fI*\fPargv);
\h'|31n'exit(2);
\h'|21n'\*(+K}\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'\*(+Kgoto\*(-K again;
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (strcmp(\fI*\fPargv, "\*-v") == 0) \*(+K{\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'vflag++;
\h'|21n'\*(+Kgoto\*(-K again;
\h'|11n'\*(+K}\*(-K
\h'|11n'niter = atoi(\fI*\fPargv);
\h'|11n'pagesize = getpagesize();
\h'|11n'pages = valloc(npages \fI*\fP pagesize);
\h'|11n'\*(+Kif\*(-K (pages == (\*(+Kchar\*(-K \fI*\fP)0) \*(+K{\*(-K
\h'|21n'printf("Can\'t allocate %d pages (%2\&.1f megabytes)\&.\en",
\h'|21n'    npages, (npages \fI*\fP pagesize) \fI\h'\w' 'u-\w'/'u'/\fP (1024\&. \fI*\fP 1024\&.));
\h'|21n'exit(3);
\h'|11n'\*(+K}\*(-K
\h'|11n'lastpage = pages + (npages \fI*\fP pagesize);
\h'|11n'\*(+Kif\*(-K (vflag)
\h'|21n'vadvise(VA\*_SEQL);
\h'|11n'\*(+Kfor\*(-K (i = 0; i < niter; i++)
\h'|21n'\*(+Kfor\*(-K (pf = pages; pf < lastpage; pf += pagesize)
\h'|31n'\fI*\fPpf = 1;
\*(+K}\*(-K\c\c
'-F

.vE
.SH
randpage
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Random page access benchmark\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+K#include\*(-K <sys\fI\h'\w' 'u-\w'/'u'/\fPvadvise\&.h>

\*(+Kchar\*(-K\h'|11n'\fI*\fPvalloc();
\*(+Kint\*(-K\h'|11n'rand();

'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kregister\*(-K \*(+Kint\*(-K npages = 4096, pagesize, pn, i, niter;
\h'|11n'\*(+Kint\*(-K vflag = 0, debug = 0;
\h'|11n'\*(+Kchar\*(-K \fI*\fPpages, \fI*\fPname;

\h'|11n'name = argv[0];
\h'|11n'argc\*-\*-, argv++;
again:
\h'|11n'\*(+Kif\*(-K (argc < 1) \*(+K{\*(-K
usage:
\h'|21n'printf("usage: %s [ \*-d ] [ \*-v ] [ \*-p #pages ] niter\en", name);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (strcmp(\fI*\fPargv, "\*-p") == 0) \*(+K{\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'\*(+Kif\*(-K (argc < 1)
\h'|31n'\*(+Kgoto\*(-K usage;
\h'|21n'npages = atoi(\fI*\fPargv);
\h'|21n'\*(+Kif\*(-K (npages <= 0) \*(+K{\*(-K
\h'|31n'printf("%s: Bad page count\&.\en", \fI*\fPargv);
\h'|31n'exit(2);
\h'|21n'\*(+K}\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'\*(+Kgoto\*(-K again;
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (strcmp(\fI*\fPargv, "\*-v") == 0) \*(+K{\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'vflag++;
\h'|21n'\*(+Kgoto\*(-K again;
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (strcmp(\fI*\fPargv, "\*-d") == 0) \*(+K{\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'debug++;
\h'|21n'\*(+Kgoto\*(-K again;
\h'|11n'\*(+K}\*(-K
\h'|11n'niter = atoi(\fI*\fPargv);
\h'|11n'pagesize = getpagesize();
\h'|11n'pages = valloc(npages \fI*\fP pagesize);
\h'|11n'\*(+Kif\*(-K (pages == (\*(+Kchar\*(-K \fI*\fP)0) \*(+K{\*(-K
\h'|21n'printf("Can\'t allocate %d pages (%2\&.1f megabytes)\&.\en",
\h'|21n'    npages, (npages \fI*\fP pagesize) \fI\h'\w' 'u-\w'/'u'/\fP (1024\&. \fI*\fP 1024\&.));
\h'|21n'exit(3);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (vflag)
\h'|21n'vadvise(VA\*_ANOM);
\h'|11n'\*(+Kfor\*(-K (i = 0; i < niter; i++) \*(+K{\*(-K
\h'|21n'pn = random() % npages;
\h'|21n'\*(+Kif\*(-K (debug)
\h'|31n'printf("touch page %d\en", pn);
\h'|21n'pages[pagesize \fI*\fP pn] = 1;
\h'|11n'\*(+K}\*(-K
\*(+K}\*(-K\c\c
'-F

.vE
.SH
gausspage
.LP
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Random page access with
 \fI*\fP a gaussian distribution\&.
 \fI*\fP
 \fI*\fP Allocate a large (zero fill on demand) address
 \fI*\fP space and fault the pages in a random gaussian
 \fI*\fP order\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C


\*(+Kfloat\*(-K\h'|11n'sqrt(), log(), rnd(), cos(), gauss();
\*(+Kchar\*(-K\h'|11n'\fI*\fPvalloc();
\*(+Kint\*(-K\h'|11n'rand();

'FN main
main(argc, argv)
\h'|11n'\*(+Kchar\*(-K \fI*\fPargv[];
\*(+K{\*(-K
\h'|11n'\*(+Kregister\*(-K \*(+Kint\*(-K pn, i, niter, delta;
\h'|11n'\*(+Kregister\*(-K \*(+Kchar\*(-K \fI*\fPpages;
\h'|11n'\*(+Kfloat\*(-K sd = 10\&.0;
\h'|11n'\*(+Kint\*(-K npages = 4096, pagesize, debug = 0;
\h'|11n'\*(+Kchar\*(-K \fI*\fPname;

\h'|11n'name = argv[0];
\h'|11n'argc\*-\*-, argv++;
again:
\h'|11n'\*(+Kif\*(-K (argc < 1) \*(+K{\*(-K
usage:
\h'|21n'printf(
"usage: %s [ \*-d ] [ \*-p #pages ] [ \*-s standard\*-deviation ] iterations\en", name);
\h'|21n'exit(1);
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (strcmp(\fI*\fPargv, "\*-s") == 0) \*(+K{\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'\*(+Kif\*(-K (argc < 1)
\h'|31n'\*(+Kgoto\*(-K usage;
\h'|21n'sscanf(\fI*\fPargv, "%f", &sd);
\h'|21n'\*(+Kif\*(-K (sd <= 0) \*(+K{\*(-K
\h'|31n'printf("%s: Bad standard deviation\&.\en", \fI*\fPargv);
\h'|31n'exit(2);
\h'|21n'\*(+K}\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'\*(+Kgoto\*(-K again;
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (strcmp(\fI*\fPargv, "\*-p") == 0) \*(+K{\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'\*(+Kif\*(-K (argc < 1)
\h'|31n'\*(+Kgoto\*(-K usage;
\h'|21n'npages = atoi(\fI*\fPargv);
\h'|21n'\*(+Kif\*(-K (npages <= 0) \*(+K{\*(-K
\h'|31n'printf("%s: Bad page count\&.\en", \fI*\fPargv);
\h'|31n'exit(2);
\h'|21n'\*(+K}\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'\*(+Kgoto\*(-K again;
\h'|11n'\*(+K}\*(-K
\h'|11n'\*(+Kif\*(-K (strcmp(\fI*\fPargv, "\*-d") == 0) \*(+K{\*(-K
\h'|21n'argc\*-\*-, argv++;
\h'|21n'debug++;
\h'|21n'\*(+Kgoto\*(-K again;
\h'|11n'\*(+K}\*(-K
\h'|11n'niter = atoi(\fI*\fPargv);
\h'|11n'pagesize = getpagesize();
\h'|11n'pages = valloc(npages\fI*\fPpagesize);
\h'|11n'\*(+Kif\*(-K (pages == (\*(+Kchar\*(-K \fI*\fP)0) \*(+K{\*(-K
\h'|21n'printf("Can\'t allocate %d pages (%2\&.1f megabytes)\&.\en",
\h'|21n'    npages, (npages\fI*\fPpagesize) \fI\h'\w' 'u-\w'/'u'/\fP (1024\&. \fI*\fP 1024\&.));
\h'|21n'exit(3);
\h'|11n'\*(+K}\*(-K
\h'|11n'pn = 0;
\h'|11n'\*(+Kfor\*(-K (i = 0; i < niter; i++) \*(+K{\*(-K
\h'|21n'delta = gauss(sd, 0\&.0);
\h'|21n'\*(+Kwhile\*(-K (pn + delta < 0 || pn + delta > npages)
\h'|31n'delta = gauss(sd, 0\&.0);
\h'|21n'pn += delta;
\h'|21n'\*(+Kif\*(-K (debug)
\h'|31n'printf("touch page %d\en", pn);
\h'|21n'\*(+Kelse\*(-K
\h'|31n'pages[pn \fI*\fP pagesize] = 1;
\h'|11n'\*(+K}\*(-K
\*(+K}\*(-K\c\c
'-F


\*(+Kfloat\*(-K
'FN gauss
gauss(sd, mean)
\h'|11n'\*(+Kfloat\*(-K sd, mean;
\*(+K{\*(-K
\h'|11n'\*(+Kregister\*(-K \*(+Kfloat\*(-K qa, qb;

\h'|11n'qa = sqrt(log(rnd()) \fI*\fP \*-2\&.0);
\h'|11n'qb = 3\&.14159 \fI*\fP rnd();
\h'|11n'\*(+Kreturn\*(-K (qa \fI*\fP cos(qb) \fI*\fP sd + mean);
\*(+K}\*(-K\c\c
'-F


\*(+Kfloat\*(-K
'FN rnd
rnd()
\*(+K{\*(-K
\h'|11n'\*(+Kstatic\*(-K \*(+Kint\*(-K seed = 1;
\h'|11n'\*(+Kstatic\*(-K \*(+Kint\*(-K biggest = 0x7fffffff;

\h'|11n'\*(+Kreturn\*(-K ((\*(+Kfloat\*(-K)rand(seed) \fI\h'\w' 'u-\w'/'u'/\fP (\*(+Kfloat\*(-K)biggest);
\*(+K}\*(-K\c\c
'-F

.vE
'-F
.\" $FreeBSD$
.am vS
..
.am vE
..
'ss 23
'ds _ \d\(mi\u
'ps 9z
'vs 10p
'ds - \(mi
'ds / \\h'\\w' 'u-\\w'/'u'/
'ds /* \\h'\\w' 'u-\\w'/'u'/*
'bd B 3
'bd S B 3
'nr cm 0
'nf
'de vH
'ev 2
'ft 1
'sp .35i
'tl '\s14\f3\\*(=F\fP\s0'\\*(=H'\f3\s14\\*(=F\fP\s0'
'sp .25i
'ft 1
\f2\s12\h'\\n(.lu-\w'\\*(=f'u'\\*(=f\fP\s0\h'|0u'
.sp .05i
'ev
'ds =G \\*(=F
..
'de vF
'ev 2
'sp .35i
'ie o 'tl '\f2\\*(=M''Page % of \\*(=G\fP'
'el 'tl '\f2Page % of \\*(=G''\\*(=M\fP'
'bp
'ev
'ft 1
'if \\n(cm=1 'ft 2
..
'de ()
'pn 1
..
'de +C
'nr cm 1
'ft 2
'ds +K
'ds -K
..
'de -C
'nr cm 0
'ft 1
'ds +K \f3
'ds -K \fP
..
'+C
'-C
'am +C
'ne 3
..
'de FN
\f2\s14\h'\\n(.lu-\w'\\$1'u'\\$1\fP\s0\h'|0u'\c
.if r x .if \\nx .if d =F .tm \\$1 \\*(=F \\n%
'ds =f \&...\\$1
..
'de FC
.if r x .if \\nx .if d =F .tm \\$1 \\*(=F \\n%
'ds =f \&...\\$1
..
'de -F
'rm =f
..
'ft 1
'lg 0
'-F
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
.\"	@(#)a2.t	5.1 (Berkeley) 4/17/91
.\"
.SH
run (shell script)
.LP
.vS
\*(+K#\*(-K! \fI\h'\w' 'u-\w'/'u'/\fPbin\fI\h'\w' 'u-\w'/'u'/\fPcsh \*-fx
\*(+K#\*(-K Script to run benchmark programs\&.
\*(+K#\*(-K
date
make clean; time make
time syscall 100000
time seqpage \*-p 7500 10
time seqpage \*-v \*-p 7500 10
time randpage \*-p 7500 30000
time randpage \*-v \*-p 7500 30000
time gausspage \*-p 7500 \*-s 1 30000
time gausspage \*-p 7500 \*-s 10 30000
time gausspage \*-p 7500 \*-s 30 30000
time gausspage \*-p 7500 \*-s 40 30000
time gausspage \*-p 7500 \*-s 50 30000
time gausspage \*-p 7500 \*-s 60 30000
time gausspage \*-p 7500 \*-s 80 30000
time gausspage \*-p 7500 \*-s 10000 30000
time csw 10000
time signocsw 10000
time pipeself 10000 512
time pipeself 10000 4
time udgself 10000 512
time udgself 10000 4
time pipediscard 10000 512
time pipediscard 10000 4
time udgdiscard 10000 512
time udgdiscard 10000 4
time pipeback 10000 512
time pipeback 10000 4
time udgback 10000 512
time udgback 10000 4
size forks
time forks 1000 0
time forks 1000 1024
time forks 1000 102400
size vforks
time vforks 1000 0
time vforks 1000 1024
time vforks 1000 102400
countenv
size nulljob
time execs 1000 0 nulljob
time execs 1000 1024 nulljob
time execs 1000 102400 nulljob
time vexecs 1000 0 nulljob
time vexecs 1000 1024 nulljob
time vexecs 1000 102400 nulljob
size bigjob
time execs 1000 0 bigjob
time execs 1000 1024 bigjob
time execs 1000 102400 bigjob
time vexecs 1000 0 bigjob
time vexecs 1000 1024 bigjob
time vexecs 1000 102400 bigjob
\*(+K#\*(-K fill environment with ~1024 bytes
setenv a 012345678901234567890123456789012345678901234567890123456780123456789
setenv b 012345678901234567890123456789012345678901234567890123456780123456789
setenv c 012345678901234567890123456789012345678901234567890123456780123456789
setenv d 012345678901234567890123456789012345678901234567890123456780123456789
setenv e 012345678901234567890123456789012345678901234567890123456780123456789
setenv f 012345678901234567890123456789012345678901234567890123456780123456789
setenv g 012345678901234567890123456789012345678901234567890123456780123456789
setenv h 012345678901234567890123456789012345678901234567890123456780123456789
setenv i 012345678901234567890123456789012345678901234567890123456780123456789
setenv j 012345678901234567890123456789012345678901234567890123456780123456789
setenv k 012345678901234567890123456789012345678901234567890123456780123456789
setenv l 012345678901234567890123456789012345678901234567890123456780123456789
setenv m 012345678901234567890123456789012345678901234567890123456780123456789
setenv n 012345678901234567890123456789012345678901234567890123456780123456789
setenv o 012345678901234567890123456789012345678901234567890123456780123456789
countenv
time execs 1000 0 nulljob
time execs 1000 1024 nulljob
time execs 1000 102400 nulljob
time execs 1000 0 bigjob
time execs 1000 1024 bigjob
time execs 1000 102400 bigjob
.vE
.bp
'-F
