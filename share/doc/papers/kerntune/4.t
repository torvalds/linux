.\" Copyright (c) 1984 M. K. McKusick
.\" Copyright (c) 1984 The Regents of the University of California.
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
.\"	@(#)4.t	1.2 (Berkeley) 11/8/90
.\"
.ds RH Conclusions
.NH 1
Conclusions
.PP
We have created a profiler that aids in the evaluation
of the kernel.
For each routine in the kernel,
the profile shows the extent to which that routine
helps support various abstractions,
and how that routine uses other abstractions.
The profile assesses the cost of routines
at all levels of the kernel decomposition.
The profiler is easily used,
and can be compiled into the kernel.
It adds only five to thirty percent execution overhead to the kernel
being profiled,
produces no additional output while the kernel is running
and allows the kernel to be measured in its real environment.
Kernel profiles can be used to identify bottlenecks in performance.
We have shown how to improve performance 
by caching recently calculated name translations.
The combined caches added to the name translation process
reduce the average cost of translating a pathname to an inode by 35%.
These changes reduce the percentage of time spent running
in the system by nearly 9%.
.nr H2 1
.ds RH Acknowledgements
.NH
\s+2Acknowledgements\s0
.PP
I would like to thank Robert Elz for sharing his ideas and 
his code for cacheing system wide names.
Thanks also to all the users at Berkeley who provided all the
input to generate the kernel profiles.
This work was supported by
the Defense Advance Research Projects Agency (DoD) under
Arpa Order No. 4031 monitored by Naval Electronic System Command under
Contract No. N00039-82-C-0235.
.ds RH References
.nr H2 1
.sp 2
.NH
\s+2References\s-2
.LP
.IP [Bentley81] 20
Bentley, J. L.,
``Writing Efficient Code'',
Department of Computer Science,
Carnegie-Mellon University,
Pittsburgh, Pennsylvania,
CMU-CS-81-116, 1981.
.IP [Graham82] 20
Graham, S., Kessler, P., McKusick, M.,
``gprof: A Call Graph Execution Profiler'',
Proceedings of the SIGPLAN '82 Symposium on Compiler Construction,
Volume 17, Number 6, June 1982. pp 120-126
.IP [Graham83] 20
Graham, S., Kessler, P., McKusick, M.,
``An Execution Profiler for Modular Programs''
Software - Practice and Experience,
Volume 13, 1983. pp 671-685
.IP [Ritchie74] 20
Ritchie, D. M. and Thompson, K.,
``The UNIX Time-Sharing System'',
CACM 17, 7. July 1974. pp 365-375
