.\"
.\" ----------------------------------------------------------------------------
.\" "THE BEER-WARE LICENSE" (Revision 42):
.\" <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
.\" can do whatever you want with this stuff. If we meet some day, and you think
.\" this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
.\" ----------------------------------------------------------------------------
.\"
.\" $FreeBSD$
.\"
.if n .ND
.TL
Malloc(3) in modern Virtual Memory environments.
.sp
Revised 
Fri Apr  5 12:50:07  1996
.AU
Poul-Henning Kamp
.AI
<phk@FreeBSD.org>
Den Andensidste Viking
Valbygaardsvej 8
DK-4200 Slagelse
Denmark
.AB
Malloc/free is one of the oldest parts of the C language environment
and obviously the world has changed a bit since it was first made.
The fact that most UNIX kernels have changed from swap/segment to
virtual memory/page based memory management has not been sufficiently
reflected in the implementations of the malloc/free API.
.PP
A new implementation was designed, written, tested and bench-marked
with an eye on the workings and performance characteristics of modern
Virtual Memory systems.
.AE
