/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2008-2009 Stacey Son <sson@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/lockstat.h>
#include <sys/sdt.h>
#include <sys/time.h>

SDT_PROVIDER_DEFINE(lockstat);

SDT_PROBE_DEFINE1(lockstat, , , adaptive__acquire, "struct mtx *");
SDT_PROBE_DEFINE1(lockstat, , , adaptive__release, "struct mtx *");
SDT_PROBE_DEFINE2(lockstat, , , adaptive__spin, "struct mtx *", "uint64_t");
SDT_PROBE_DEFINE2(lockstat, , , adaptive__block, "struct mtx *", "uint64_t");

SDT_PROBE_DEFINE1(lockstat, , , spin__acquire, "struct mtx *");
SDT_PROBE_DEFINE1(lockstat, , , spin__release, "struct mtx *");
SDT_PROBE_DEFINE2(lockstat, , , spin__spin, "struct mtx *", "uint64_t");

SDT_PROBE_DEFINE2(lockstat, , , rw__acquire, "struct rwlock *", "int");
SDT_PROBE_DEFINE2(lockstat, , , rw__release, "struct rwlock *", "int");
SDT_PROBE_DEFINE5(lockstat, , , rw__block, "struct rwlock *", "uint64_t", "int",
    "int", "int");
SDT_PROBE_DEFINE2(lockstat, , , rw__spin, "struct rwlock *", "uint64_t");
SDT_PROBE_DEFINE1(lockstat, , , rw__upgrade, "struct rwlock *");
SDT_PROBE_DEFINE1(lockstat, , , rw__downgrade, "struct rwlock *");

SDT_PROBE_DEFINE2(lockstat, , , sx__acquire, "struct sx *", "int");
SDT_PROBE_DEFINE2(lockstat, , , sx__release, "struct sx *", "int");
SDT_PROBE_DEFINE5(lockstat, , , sx__block, "struct sx *", "uint64_t", "int",
    "int", "int");
SDT_PROBE_DEFINE2(lockstat, , , sx__spin, "struct sx *", "uint64_t");
SDT_PROBE_DEFINE1(lockstat, , , sx__upgrade, "struct sx *");
SDT_PROBE_DEFINE1(lockstat, , , sx__downgrade, "struct sx *");

SDT_PROBE_DEFINE2(lockstat, , , thread__spin, "struct mtx *", "uint64_t");

volatile bool __read_frequently lockstat_enabled;

uint64_t 
lockstat_nsecs(struct lock_object *lo)
{
	struct bintime bt;
	uint64_t ns;

	if (!lockstat_enabled)
		return (0);
	if ((lo->lo_flags & LO_NOPROFILE) != 0)
		return (0);

	binuptime(&bt);
	ns = bt.sec * (uint64_t)1000000000;
	ns += ((uint64_t)1000000000 * (uint32_t)(bt.frac >> 32)) >> 32;
	return (ns);
}
