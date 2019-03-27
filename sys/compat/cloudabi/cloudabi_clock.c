/*-
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/proc.h>
#include <sys/stdint.h>
#include <sys/systm.h>
#include <sys/syscallsubr.h>

#include <contrib/cloudabi/cloudabi_types_common.h>

#include <compat/cloudabi/cloudabi_proto.h>
#include <compat/cloudabi/cloudabi_util.h>

/* Converts a CloudABI clock ID to a FreeBSD clock ID. */
static int
cloudabi_convert_clockid(cloudabi_clockid_t in, clockid_t *out)
{
	switch (in) {
	case CLOUDABI_CLOCK_MONOTONIC:
		*out = CLOCK_MONOTONIC;
		return (0);
	case CLOUDABI_CLOCK_PROCESS_CPUTIME_ID:
		*out = CLOCK_PROCESS_CPUTIME_ID;
		return (0);
	case CLOUDABI_CLOCK_REALTIME:
		*out = CLOCK_REALTIME;
		return (0);
	case CLOUDABI_CLOCK_THREAD_CPUTIME_ID:
		*out = CLOCK_THREAD_CPUTIME_ID;
		return (0);
	default:
		return (EINVAL);
	}
}

/* Converts a struct timespec to a CloudABI timestamp. */
int
cloudabi_convert_timespec(const struct timespec *in, cloudabi_timestamp_t *out)
{
	cloudabi_timestamp_t s, ns;

	if (in->tv_sec < 0) {
		/* Timestamps from before the Epoch cannot be expressed. */
		*out = 0;
		return (EOVERFLOW);
	}
	s = in->tv_sec;
	ns = in->tv_nsec;
	if (s > UINT64_MAX / 1000000000 ||
	    (s == UINT64_MAX / 1000000000 && ns > UINT64_MAX % 1000000000)) {
		/* Addition of seconds and nanoseconds would overflow. */
		*out = UINT64_MAX;
		return (EOVERFLOW);
	}
	*out = s * 1000000000 + ns;
	return (0);
}

/* Fetches the time value of a clock. */
int
cloudabi_clock_time_get(struct thread *td, cloudabi_clockid_t clock_id,
    cloudabi_timestamp_t *ret)
{
	struct timespec ts;
	int error;
	clockid_t clockid;

	error = cloudabi_convert_clockid(clock_id, &clockid);
	if (error != 0)
		return (error);
	error = kern_clock_gettime(td, clockid, &ts);
	if (error != 0)
		return (error);
	return (cloudabi_convert_timespec(&ts, ret));
}

int
cloudabi_sys_clock_res_get(struct thread *td,
    struct cloudabi_sys_clock_res_get_args *uap)
{
	struct timespec ts;
	cloudabi_timestamp_t cts;
	int error;
	clockid_t clockid;

	error = cloudabi_convert_clockid(uap->clock_id, &clockid);
	if (error != 0)
		return (error);
	error = kern_clock_getres(td, clockid, &ts);
	if (error != 0)
		return (error);
	error = cloudabi_convert_timespec(&ts, &cts);
	if (error != 0)
		return (error);
	memcpy(td->td_retval, &cts, sizeof(cts));
	return (0);
}

int
cloudabi_sys_clock_time_get(struct thread *td,
    struct cloudabi_sys_clock_time_get_args *uap)
{
	cloudabi_timestamp_t ts;
	int error;

	error = cloudabi_clock_time_get(td, uap->clock_id, &ts);
	memcpy(td->td_retval, &ts, sizeof(ts));
	return (error);
}
