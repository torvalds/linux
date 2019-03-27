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
#! /bin/csh -fx
# Script to run benchmark programs.
#
date
make clean; time make
time syscall 100000
time seqpage -p 7500 10
time seqpage -v -p 7500 10
time randpage -p 7500 30000
time randpage -v -p 7500 30000
time gausspage -p 7500 -s 1 30000
time gausspage -p 7500 -s 10 30000
time gausspage -p 7500 -s 30 30000
time gausspage -p 7500 -s 40 30000
time gausspage -p 7500 -s 50 30000
time gausspage -p 7500 -s 60 30000
time gausspage -p 7500 -s 80 30000
time gausspage -p 7500 -s 10000 30000
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
# fill environment with ~1024 bytes
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
