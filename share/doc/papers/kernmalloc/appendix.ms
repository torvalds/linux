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
.\" Copyright (c) 1988 The Regents of the University of California.
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
.\"	@(#)appendix.t	5.1 (Berkeley) 4/16/91
.\"
.bp
.H 1 "Appendix A - Implementation Details"
.LP
.nf
.vS
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Constants for setting the parameters of the kernel memory allocator\&.
 \fI*\fP
 \fI*\fP 2 \fI*\fP\fI*\fP MINBUCKET is the smallest unit of memory that will be
 \fI*\fP allocated\&. It must be at least large enough to hold a pointer\&.
 \fI*\fP
 \fI*\fP Units of memory less or equal to MAXALLOCSAVE will permanently
 \fI*\fP allocate physical memory; requests for these size pieces of memory
 \fI*\fP are quite fast\&. Allocations greater than MAXALLOCSAVE must
 \fI*\fP always allocate and free physical memory; requests for these size
 \fI*\fP allocations should be done infrequently as they will be slow\&.
 \fI*\fP Constraints: CLBYTES <= MAXALLOCSAVE <= 2 \fI*\fP\fI*\fP (MINBUCKET + 14)
 \fI*\fP and MAXALLOCSIZE must be a power of two\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+K#define\*(-K MINBUCKET\h'|31n'4\h'|51n'\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C
 4 => min allocation of 16 bytes \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

'FN MAXALLOCSAVE
\*(+K#define\*(-K MAXALLOCSAVE\h'|31n'(2 \fI*\fP CLBYTES)

\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Maximum amount of kernel dynamic memory\&.
 \fI*\fP Constraints: must be a multiple of the pagesize\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

'FN MAXKMEM
\*(+K#define\*(-K MAXKMEM\h'|31n'(1024 \fI*\fP PAGESIZE)

\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Arena for all kernel dynamic memory allocation\&.
 \fI*\fP This arena is known to start on a page boundary\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+Kextern\*(-K \*(+Kchar\*(-K kmembase[MAXKMEM];

\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Array of descriptors that describe the contents of each page
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+Kstruct\*(-K kmemsizes \*(+K{\*(-K
\h'|11n'\*(+Kshort\*(-K\h'|21n'ks\*_indx;\h'|41n'\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C
 bucket index, size of small allocations \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\h'|11n'u\*_short\h'|21n'ks\*_pagecnt;\h'|41n'\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C
 for large allocations, pages allocated \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+K}\*(-K\c\c
'-F
 kmemsizes[MAXKMEM \fI\h'\w' 'u-\w'/'u'/\fP PAGESIZE];
'FC MAXALLOCSAVE

\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Set of buckets for each size of memory block that is retained
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+Kstruct\*(-K kmembuckets \*(+K{\*(-K
\h'|11n'caddr\*_t kb\*_next;\h'|41n'\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C
 list of free blocks \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

\*(+K}\*(-K\c\c
'-F
 bucket[MINBUCKET + 16];
.bp
\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Macro to convert a size to a bucket index\&. If the size is constant,
 \fI*\fP this macro reduces to a compile time constant\&.
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

'FN MINALLOCSIZE
\*(+K#define\*(-K MINALLOCSIZE\h'|31n'(1 << MINBUCKET)
'FN BUCKETINDX
\*(+K#define\*(-K BUCKETINDX(size) \e
\h'|11n'(size) <= (MINALLOCSIZE \fI*\fP 128) \e
\h'|21n'? (size) <= (MINALLOCSIZE \fI*\fP 8) \e
\h'|31n'? (size) <= (MINALLOCSIZE \fI*\fP 2) \e
\h'|41n'? (size) <= (MINALLOCSIZE \fI*\fP 1) \e
\h'|51n'? (MINBUCKET + 0) \e
\h'|51n': (MINBUCKET + 1) \e
\h'|41n': (size) <= (MINALLOCSIZE \fI*\fP 4) \e
\h'|51n'? (MINBUCKET + 2) \e
\h'|51n': (MINBUCKET + 3) \e
\h'|31n': (size) <= (MINALLOCSIZE\fI*\fP 32) \e
\h'|41n'? (size) <= (MINALLOCSIZE \fI*\fP 16) \e
\h'|51n'? (MINBUCKET + 4) \e
\h'|51n': (MINBUCKET + 5) \e
\h'|41n': (size) <= (MINALLOCSIZE \fI*\fP 64) \e
\h'|51n'? (MINBUCKET + 6) \e
\h'|51n': (MINBUCKET + 7) \e
\h'|21n': (size) <= (MINALLOCSIZE \fI*\fP 2048) \e
\h'|31n'\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C
 etc \&.\&.\&. \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C


\fI\h'\w' 'u-\w'/'u'/\fP\fI*\fP\c\c
'+C

 \fI*\fP Macro versions for the usual cases of malloc\fI\h'\w' 'u-\w'/'u'/\fPfree
 \fI*\fP\fI\h'\w' 'u-\w'/'u'/\fP\c
'-C

'FN MALLOC
\*(+K#define\*(-K MALLOC(space, cast, size, flags) \*(+K{\*(-K \e
\h'|11n'\*(+Kregister\*(-K \*(+Kstruct\*(-K kmembuckets \fI*\fPkbp = &bucket[BUCKETINDX(size)]; \e
\h'|11n'\*(+Klong\*(-K s = splimp(); \e
\h'|11n'\*(+Kif\*(-K (kbp\*->kb\*_next == NULL) \*(+K{\*(-K \e
\h'|21n'(space) = (cast)malloc(size, flags); \e
\h'|11n'\*(+K}\*(-K \*(+Kelse\*(-K \*(+K{\*(-K \e
\h'|21n'(space) = (cast)kbp\*->kb\*_next; \e
\h'|21n'kbp\*->kb\*_next = \fI*\fP(caddr\*_t \fI*\fP)(space); \e
\h'|11n'\*(+K}\*(-K \e
\h'|11n'splx(s); \e
\*(+K}\*(-K\c\c
'-F

'FC BUCKETINDX

'FN FREE
\*(+K#define\*(-K FREE(addr) \*(+K{\*(-K \e
\h'|11n'\*(+Kregister\*(-K \*(+Kstruct\*(-K kmembuckets \fI*\fPkbp; \e
\h'|11n'\*(+Kregister\*(-K \*(+Kstruct\*(-K kmemsizes \fI*\fPksp = \e
\h'|21n'&kmemsizes[((addr) \*- kmembase) \fI\h'\w' 'u-\w'/'u'/\fP PAGESIZE]; \e
\h'|11n'\*(+Klong\*(-K s = splimp(); \e
\h'|11n'\*(+Kif\*(-K (1 << ksp\*->ks\*_indx > MAXALLOCSAVE) \*(+K{\*(-K \e
\h'|21n'free(addr); \e
\h'|11n'\*(+K}\*(-K \*(+Kelse\*(-K \*(+K{\*(-K \e
\h'|21n'kbp = &bucket[ksp\*->ks\*_indx]; \e
\h'|21n'\fI*\fP(caddr\*_t \fI*\fP)(addr) = kbp\*->kb\*_next; \e
\h'|21n'kbp\*->kb\*_next = (caddr\*_t)(addr); \e
\h'|11n'\*(+K}\*(-K \e
\h'|11n'splx(s); \e
\*(+K}\*(-K\c\c
'-F

'FC BUCKETINDX
.vE
