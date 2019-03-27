.\" Copyright (c) 1986, 1993
.\"	The Regents of the University of California.  All rights reserved.
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
.\"	@(#)6.t	8.1 (Berkeley) 6/8/93
.\"
.nr H2 1
.ds RH Acknowledgements
.SH
\s+2Acknowledgements\s0
.PP
We thank Robert Elz for his ongoing interest in the new file system,
and for adding disk quotas in a rational and efficient manner.
We also acknowledge Dennis Ritchie for his suggestions
on the appropriate modifications to the user interface.
We appreciate Michael Powell's explanations on how
the DEMOS file system worked;
many of his ideas were used in this implementation.
Special commendation goes to Peter Kessler and Robert Henry for acting
like real users during the early debugging stage when file systems were
less stable than they should have been.
The criticisms and suggestions by the reviews contributed significantly
to the coherence of the paper.
Finally we thank our sponsors,
the National Science Foundation under grant MCS80-05144,
and the Defense Advance Research Projects Agency (DoD) under
ARPA Order No. 4031 monitored by Naval Electronic System Command under
Contract No. N00039-82-C-0235.
.ds RH References
.nr H2 1
.sp 2
.SH
\s+2References\s0
.LP
.IP [Almes78] 20
Almes, G., and Robertson, G.
"An Extensible File System for Hydra"
Proceedings of the Third International Conference on Software Engineering,
IEEE, May 1978.
.IP [Bass81] 20
Bass, J.
"Implementation Description for File Locking",
Onyx Systems Inc, 73 E. Trimble Rd, San Jose, CA 95131
Jan 1981.
.IP [Feiertag71] 20
Feiertag, R. J. and Organick, E. I., 
"The Multics Input-Output System",
Proceedings of the Third Symposium on Operating Systems Principles,
ACM, Oct 1971. pp 35-41
.IP [Ferrin82a] 20
Ferrin, T.E.,
"Performance and Robustness Improvements in Version 7 UNIX",
Computer Graphics Laboratory Technical Report 2,
School of Pharmacy, University of California,
San Francisco, January 1982.
Presented at the 1982 Winter Usenix Conference, Santa Monica, California.
.IP [Ferrin82b] 20
Ferrin, T.E.,
"Performance Issuses of VMUNIX Revisited",
;login: (The Usenix Association Newsletter), Vol 7, #5, November 1982. pp 3-6
.IP [Kridle83] 20
Kridle, R., and McKusick, M.,
"Performance Effects of Disk Subsystem Choices for
VAX Systems Running 4.2BSD UNIX",
Computer Systems Research Group, Dept of EECS, Berkeley, CA 94720,
Technical Report #8.
.IP [Kowalski78] 20
Kowalski, T.
"FSCK - The UNIX System Check Program",
Bell Laboratory, Murray Hill, NJ 07974. March 1978
.IP [Knuth75] 20
Kunth, D.
"The Art of Computer Programming",
Volume 3 - Sorting and Searching,
Addison-Wesley Publishing Company Inc, Reading, Mass, 1975. pp 506-549
.IP [Maruyama76]
Maruyama, K., and Smith, S.
"Optimal reorganization of Distributed Space Disk Files",
CACM, 19, 11. Nov 1976. pp 634-642
.IP [Nevalainen77] 20
Nevalainen, O., Vesterinen, M.
"Determining Blocking Factors for Sequential Files by Heuristic Methods",
The Computer Journal, 20, 3. Aug 1977. pp 245-247
.IP [Pechura83] 20
Pechura, M., and Schoeffler, J.
"Estimating File Access Time of Floppy Disks",
CACM, 26, 10. Oct 1983. pp 754-763
.IP [Peterson83] 20
Peterson, G.
"Concurrent Reading While Writing",
ACM Transactions on Programming Languages and Systems,
ACM, 5, 1. Jan 1983. pp 46-55
.IP [Powell79] 20
Powell, M.
"The DEMOS File System",
Proceedings of the Sixth Symposium on Operating Systems Principles,
ACM, Nov 1977. pp 33-42
.IP [Ritchie74] 20
Ritchie, D. M. and Thompson, K.,
"The UNIX Time-Sharing System",
CACM 17, 7. July 1974. pp 365-375
.IP [Smith81a] 20
Smith, A.
"Input/Output Optimization and Disk Architectures: A Survey",
Performance and Evaluation 1. Jan 1981. pp 104-117
.IP [Smith81b] 20
Smith, A.
"Bibliography on File and I/O System Optimization and Related Topics",
Operating Systems Review, 15, 4. Oct 1981. pp 39-54
.IP [Symbolics81] 20
"Symbolics File System",
Symbolics Inc, 9600 DeSoto Ave, Chatsworth, CA 91311
Aug 1981.
.IP [Thompson78] 20
Thompson, K.
"UNIX Implementation",
Bell System Technical Journal, 57, 6, part 2. pp 1931-1946
July-August 1978.
.IP [Thompson80] 20
Thompson, M.
"Spice File System",
Carnegie-Mellon University,
Department of Computer Science, Pittsburg, PA 15213
#CMU-CS-80, Sept 1980.
.IP [Trivedi80] 20
Trivedi, K.
"Optimal Selection of CPU Speed, Device Capabilities, and File Assignments",
Journal of the ACM, 27, 3. July 1980. pp 457-473
.IP [White80] 20
White, R. M.
"Disk Storage Technology",
Scientific American, 243(2), August 1980.
