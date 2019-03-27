#!/usr/sbin/dtrace -qs

/*-
 * Copyright (c) 2011-2012 Alexander Leidinger <netchild@FreeBSD.org>
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
 * Trace futex operations:
 *  - internal locks
 *  - size of the futex list
 *  - report error conditions (emulation errors, kernel errors,
 *    programming errors)
 *  - execution time (wallclock) of futex related functions
 */

#pragma D option specsize=32m

/* Error conditions */
linuxulator*:futex:futex_get:error,
linuxulator*:futex:futex_sleep:requeue_error,
linuxulator*:futex:futex_sleep:sleep_error,
linuxulator*:futex:futex_wait:copyin_error,
linuxulator*:futex:futex_wait:itimerfix_error,
linuxulator*:futex:futex_wait:sleep_error,
linuxulator*:futex:futex_atomic_op:missing_access_check,
linuxulator*:futex:futex_atomic_op:unimplemented_op,
linuxulator*:futex:futex_atomic_op:unimplemented_cmp,
linuxulator*:futex:linux_sys_futex:unimplemented_clockswitch,
linuxulator*:futex:linux_sys_futex:copyin_error,
linuxulator*:futex:linux_sys_futex:unhandled_efault,
linuxulator*:futex:linux_sys_futex:unimplemented_lock_pi,
linuxulator*:futex:linux_sys_futex:unimplemented_unlock_pi,
linuxulator*:futex:linux_sys_futex:unimplemented_trylock_pi,
linuxulator*:futex:linux_sys_futex:unimplemented_wait_requeue_pi,
linuxulator*:futex:linux_sys_futex:unimplemented_cmp_requeue_pi,
linuxulator*:futex:linux_sys_futex:unknown_operation,
linuxulator*:futex:linux_get_robust_list:copyout_error,
linuxulator*:futex:handle_futex_death:copyin_error,
linuxulator*:futex:fetch_robust_entry:copyin_error,
linuxulator*:futex:release_futexes:copyin_error
{
	printf("ERROR: %s in %s:%s:%s\n", probename, probeprov, probemod,
	    probefunc);
	stack();
	ustack();
}

linuxulator*:futex:linux_sys_futex:invalid_cmp_requeue_use,
linuxulator*:futex:linux_sys_futex:deprecated_requeue,
linuxulator*:futex:linux_set_robust_list:size_error
{
	printf("WARNING: %s:%s:%s:%s in application %s, maybe an application error?\n",
	    probename, probeprov, probemod, probefunc, execname);
	stack();
	ustack();
}


/* Per futex checks/statistics */

linuxulator*:futex:futex:create
{
	++futex_count;
	@max_futexes = max(futex_count);
}

linuxulator*:futex:futex:destroy
/futex_count == 0/
{
	printf("ERROR: Request to destroy a futex which was not created,\n");
	printf("       or this script was started after some futexes where\n");
	printf("       created. Stack trace:\n");
	stack();
	ustack();
}

linuxulator*:futex:futex:destroy
{
	--futex_count;
}


/* Internal locks */

linuxulator*:locks:futex_mtx:locked
{
	++check[probefunc, arg0];
	@stats[probefunc] = count();

	ts[probefunc] = timestamp;
	spec[probefunc] = speculation();
	printf("Stacktrace of last lock operation of the %s:\n", probefunc);
	stack();
}

linuxulator*:locks:futex_mtx:unlock
/check[probefunc, arg0] == 0/
{
	printf("ERROR: unlock attempt of unlocked %s (%p),", probefunc, arg0);
	printf("       missing SDT probe in kernel, or dtrace program started");
	printf("       while the %s was already held (race condition).", probefunc);
	printf("       Stack trace follows:");
	stack();
}

linuxulator*:locks:futex_mtx:unlock
{
	discard(spec[probefunc]);
	spec[probefunc] = 0;
	--check[probefunc, arg0];
}

/* Timeout handling for internal locks */

tick-10s
/spec["futex_mtx"] != 0 && timestamp - ts["futex_mtx"] >= 9999999000/
{
	commit(spec["futex_mtx"]);
	spec["futex_mtx"] = 0;
}


/* Timing statistings */

linuxulator*:futex::entry
{
	self->time[probefunc] = timestamp;
	@calls[probeprov, execname, probefunc] = count();
}

linuxulator*:futex::return
/self->time[probefunc] != 0/
{
	this->timediff = self->time[probefunc] - timestamp;

	@timestats[probeprov, execname, probefunc] = quantize(this->timediff);
	@longest[probeprov, probefunc] = max(this->timediff);

	self->time[probefunc] = 0;
}


/* Statistics */

END
{
	printf("Number of locks per type:");
	printa(@stats);
	printf("Number of maximum number of futexes in the futex list:");
	printa(@max_futexes);
	printf("Number of futexes still existing: %d", futex_count);
	printf("Number of calls per provider/application/kernel function:");
	printa(@calls);
	printf("Wallclock-timing statistics per provider/application/kernel function (in ns):");
	printa(@timestats);
	printf("Longest running (wallclock!) functions per provider (in ns):");
	printa(@longest);
}
