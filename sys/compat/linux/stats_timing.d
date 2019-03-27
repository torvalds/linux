#!/usr/sbin/dtrace -qs

/*-
 * Copyright (c) 2008-2012 Alexander Leidinger <netchild@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * Some statistics (all per provider):
 *  - number of calls to a function per executable binary (not per PID!)
 *    - allows to see where an optimization would be beneficial for a given
 *      application
 *  - graph of CPU time spend in functions per executable binary
 *    - together with the number of calls to this function this allows
 *      to determine if a kernel optimization would be beneficial / is
 *      possible for a given application
 *  - graph of longest running (CPU-time!) function in total
 *    - may help finding problem cases in the kernel code
 * - graph of longest held (CPU-time!) locks
 */

#pragma D option dynvarsize=32m

linuxulator*:::entry
{
	self->time[probefunc] = vtimestamp;
	@calls[probeprov, execname, probefunc] = count();
}

linuxulator*:::return
/self->time[probefunc] != 0/
{
	this->timediff = self->time[probefunc] - vtimestamp;

	@stats[probeprov, execname, probefunc] = quantize(this->timediff);
	@longest[probeprov, probefunc] = max(this->timediff);

	self->time[probefunc] = 0;
}

linuxulator*:::locked
{
	self->lock[arg0] = vtimestamp;
}

linuxulator*:::unlock
/self->lock[arg0] != 0/
{
	this->timediff = self->lock[arg0] - vtimestamp;

	@lockstats[probefunc] = quantize(this->timediff);
	@longlock[probefunc] = max(this->timediff);

	self->lock[arg0] = 0;
}

END
{
	printf("Number of calls per provider/application/kernel function:");
	printa(@calls);
	printf("CPU-timing statistics per provider/application/kernel function (in ns):");
	printa(@stats);
	printf("Longest running (CPU-time!) functions per provider (in ns):");
	printa(@longest);
	printf("Lock CPU-timing statistics:");
	printa(@lockstats);
	printf("Longest running (CPU-time!) locks:");
	printa(@longlock);
}

