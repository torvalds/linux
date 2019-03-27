/*-
 * Copyright (c) 2005-2006 The FreeBSD Project
 * All rights reserved.
 *
 * Author: Victor Cruceru <soc-victor@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
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
 *
 * $FreeBSD$
 */

/*
 * Host Resources MIB scalars implementation for SNMPd.
 */

#include <sys/param.h>
#include <sys/sysctl.h>

#include <pwd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <utmpx.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

/* boot timestamp in centi-seconds */
static uint64_t kernel_boot;

/* physical memory size in Kb */
static uint64_t phys_mem_size;

/* boot line (malloced) */
static u_char *boot_line;

/* maximum number of processes */
static uint32_t max_proc;

/**
 * Free all static data
 */
void
fini_scalars(void)
{

	free(boot_line);
}

/**
 * Get system uptime in hundredths of seconds since the epoch
 * Returns 0 in case of an error
 */
static int
OS_getSystemUptime(uint32_t *ut)
{
	struct timeval right_now;
	uint64_t now;

	if (kernel_boot == 0) {
		/* first time, do the sysctl */
		struct timeval kernel_boot_timestamp;
		int mib[2] = { CTL_KERN, KERN_BOOTTIME };
		size_t len = sizeof(kernel_boot_timestamp);

		if (sysctl(mib, nitems(mib), &kernel_boot_timestamp,
		    &len, NULL, 0) == -1) {
			syslog(LOG_ERR, "sysctl KERN_BOOTTIME failed: %m");
			return (SNMP_ERR_GENERR);
		}

		HRDBG("boot timestamp from kernel: {%lld, %ld}",
		    (long long)kernel_boot_timestamp.tv_sec,
		    (long)kernel_boot_timestamp.tv_usec);

		kernel_boot = ((uint64_t)kernel_boot_timestamp.tv_sec * 100) +
		    (kernel_boot_timestamp.tv_usec / 10000);
	}

	if (gettimeofday(&right_now, NULL) < 0) {
		syslog(LOG_ERR, "gettimeofday failed: %m");
		return (SNMP_ERR_GENERR);
	}
	now = ((uint64_t)right_now.tv_sec * 100) + (right_now.tv_usec / 10000);

	if (now - kernel_boot > UINT32_MAX)
		*ut = UINT32_MAX;
	else
		*ut = now - kernel_boot;

	return (SNMP_ERR_NOERROR);
}

/**
 * Get system local date and time in a foramt suitable for DateAndTime TC:
 *           field  octets  contents                  range
 *           -----  ------  --------                  -----
 *             1      1-2   year*                     0..65536
 *             2       3    month                     1..12
 *             3       4    day                       1..31
 *             4       5    hour                      0..23
 *             5       6    minutes                   0..59
 *             6       7    seconds                   0..60
 *                          (use 60 for leap-second)
 *             7       8    deci-seconds              0..9
 *             8       9    direction from UTC        '+' / '-'
 *             9      10    hours from UTC*           0..13
 *            10      11    minutes from UTC          0..59
 *
 *           * Notes:
 *           - the value of year is in network-byte order
 *           - daylight saving time in New Zealand is +13
 *
 *           For example, Tuesday May 26, 1992 at 1:30:15 PM EDT would be
 *           displayed as:
 *
 *                            1992-5-26,13:30:15.0,-4:0
 *
 * Returns -1 in case of an error or the length of the string (8 or 11)
 * Actually returns always 11 on freebsd
 */
static int
OS_getSystemDate(struct snmp_value *value)
{
	u_char s_date_time[11];
	struct tm tloc_tm;
	time_t tloc_time_t;
	struct timeval right_now;
	int string_len;

	if (gettimeofday(&right_now, NULL) < 0) {
		syslog(LOG_ERR, "gettimeofday failed: %m");
		return (SNMP_ERR_GENERR);
	}

	tloc_time_t = right_now.tv_sec;

	if (localtime_r(&tloc_time_t, &tloc_tm) == NULL) {
		syslog(LOG_ERR, "localtime_r() failed: %m ");
		return (SNMP_ERR_GENERR);
	}

	string_len = make_date_time(s_date_time, &tloc_tm,
	    right_now.tv_usec / 100000);

	return (string_get(value, s_date_time, string_len));
}

/**
 * Get kernel boot path. For FreeBSD it seems that no arguments are
 * present. Returns NULL if an error occurred. The returned data is a
 * pointer to a global storage.
 */
int
OS_getSystemInitialLoadParameters(u_char **params)
{

	if (boot_line == NULL) {
		int mib[2] = { CTL_KERN, KERN_BOOTFILE };
		char *buf;
		size_t buf_len = 0;

		/* get the needed buffer len */
		if (sysctl(mib, 2, NULL, &buf_len, NULL, 0) != 0) {
			syslog(LOG_ERR,
			    "sysctl({CTL_KERN,KERN_BOOTFILE}) failed: %m");
			return (SNMP_ERR_GENERR);
		}

		if ((buf = malloc(buf_len)) == NULL) {
			syslog(LOG_ERR, "malloc failed");
			return (SNMP_ERR_GENERR);
		}
		if (sysctl(mib, 2, buf, &buf_len, NULL, 0)) {
			syslog(LOG_ERR,
			    "sysctl({CTL_KERN,KERN_BOOTFILE}) failed: %m");
			free(buf);
			return (SNMP_ERR_GENERR);
		}

		boot_line = buf;
		HRDBG("kernel boot file: %s", boot_line);
	}

	*params = boot_line;
	return (SNMP_ERR_NOERROR);
}

/**
 * Get number of current users which are logged in
 */
static int
OS_getSystemNumUsers(uint32_t *nu)
{
	struct utmpx *utmp;

	setutxent();
	*nu = 0;
	while ((utmp = getutxent()) != NULL) {
		if (utmp->ut_type == USER_PROCESS)
			(*nu)++;
	}
	endutxent();

	return (SNMP_ERR_NOERROR);
}

/**
 * Get number of current processes existing into the system
 */
static int
OS_getSystemProcesses(uint32_t *proc_count)
{
	int pc;

	if (hr_kd == NULL)
		return (SNMP_ERR_GENERR);

	if (kvm_getprocs(hr_kd, KERN_PROC_PROC, 0, &pc) == NULL) {
		syslog(LOG_ERR, "kvm_getprocs failed: %m");
		return (SNMP_ERR_GENERR);
	}

	*proc_count = pc;
	return (SNMP_ERR_NOERROR);
}

/**
 * Get maximum number of processes allowed on this system
 */
static int
OS_getSystemMaxProcesses(uint32_t *mproc)
{

	if (max_proc == 0) {
		int mib[2] = { CTL_KERN, KERN_MAXPROC };
		int mp;
		size_t len = sizeof(mp);

		if (sysctl(mib, 2, &mp, &len, NULL, 0) == -1) {
			syslog(LOG_ERR, "sysctl KERN_MAXPROC failed: %m");
			return (SNMP_ERR_GENERR);
		}
		max_proc = mp;
	}

	*mproc = max_proc;
	return (SNMP_ERR_NOERROR);
}

/*
 * Get the physical memeory size in Kbytes.
 * Returns SNMP error code.
 */
static int
OS_getMemorySize(uint32_t *ms)
{

	if (phys_mem_size == 0) {
		int mib[2] = { CTL_HW, HW_PHYSMEM };
		u_long physmem;
		size_t len = sizeof(physmem);

		if (sysctl(mib, 2, &physmem, &len, NULL, 0) == -1) {
			syslog(LOG_ERR,
			    "sysctl({ CTL_HW, HW_PHYSMEM }) failed: %m");
			return (SNMP_ERR_GENERR);
		}

		phys_mem_size = physmem / 1024;
	}

	if (phys_mem_size > UINT32_MAX)
		*ms = UINT32_MAX;
	else
		*ms = phys_mem_size;
	return (SNMP_ERR_NOERROR);
}

/*
 * Try to use the s_date_time parameter as a DateAndTime TC to fill in
 * the second parameter.
 * Returns 0 on succes and -1 for an error.
 * Bug: time zone info is not used
 */
static struct timeval *
OS_checkSystemDateInput(const u_char *str, u_int len)
{
	struct tm tm_to_set;
	time_t t;
	struct timeval *tv;

	if (len != 8 && len != 11)
		return (NULL);

	if (str[2] == 0 || str[2] > 12 ||
	    str[3] == 0 || str[3] > 31 ||
	    str[4] > 23 || str[5] > 59 || str[6] > 60 || str[7] > 9)
		return (NULL);

	tm_to_set.tm_year = ((str[0] << 8) + str[1]) - 1900;
	tm_to_set.tm_mon = str[2] - 1;
	tm_to_set.tm_mday = str[3];
	tm_to_set.tm_hour = str[4];
	tm_to_set.tm_min = str[5];
	tm_to_set.tm_sec = str[6];
	tm_to_set.tm_isdst = 0;

	/* now make UTC from it */
	if ((t = timegm(&tm_to_set)) == (time_t)-1)
		return (NULL);

	/* now apply timezone if specified */
	if (len == 11) {
		if (str[9] > 13 || str[10] > 59)
			return (NULL);
		if (str[8] == '+')
			t += 3600 * str[9] + 60 * str[10];
		else
			t -= 3600 * str[9] + 60 * str[10];
	}

	if ((tv = malloc(sizeof(*tv))) == NULL)
		return (NULL);

	tv->tv_sec = t;
	tv->tv_usec = (int32_t)str[7] * 100000;

	return (tv);
}

/*
 * Set system date and time. Timezone is not changed
 */
static int
OS_setSystemDate(const struct timeval *timeval_to_set)
{
	if (settimeofday(timeval_to_set, NULL) == -1) {
		syslog(LOG_ERR, "settimeofday failed: %m");
		return (SNMP_ERR_GENERR);
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * prototype of this function was genrated by gensnmptree tool in header file
 * hostres_tree.h
 * Returns SNMP_ERR_NOERROR on success
 */
int
op_hrSystem(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op curr_op)
{
	int err;
	u_char *str;

	switch (curr_op) {

	  case SNMP_OP_GET:
		switch (value->var.subs[sub - 1]) {

		case LEAF_hrSystemUptime:
			return (OS_getSystemUptime(&value->v.uint32));

		case LEAF_hrSystemDate:
			return (OS_getSystemDate(value));

		case LEAF_hrSystemInitialLoadDevice:
			value->v.uint32 = 0; /* FIXME */
			return (SNMP_ERR_NOERROR);

		case LEAF_hrSystemInitialLoadParameters:
			if ((err = OS_getSystemInitialLoadParameters(&str)) !=
			    SNMP_ERR_NOERROR)
				return (err);
			return (string_get(value, str, -1));

		case LEAF_hrSystemNumUsers:
			return (OS_getSystemNumUsers(&value->v.uint32));

		case LEAF_hrSystemProcesses:
			return (OS_getSystemProcesses(&value->v.uint32));

		case LEAF_hrSystemMaxProcesses:
			return (OS_getSystemMaxProcesses(&value->v.uint32));
		}
		abort();

	  case SNMP_OP_SET:
		switch (value->var.subs[sub - 1]) {

		case LEAF_hrSystemDate:
			if ((ctx->scratch->ptr1 =
			    OS_checkSystemDateInput(value->v.octetstring.octets,
			    value->v.octetstring.len)) == NULL)
				return (SNMP_ERR_WRONG_VALUE);

			return (SNMP_ERR_NOERROR);

		case LEAF_hrSystemInitialLoadDevice:
		case LEAF_hrSystemInitialLoadParameters:
			return (SNMP_ERR_NOT_WRITEABLE);

		}
		abort();

	  case SNMP_OP_ROLLBACK:
		switch (value->var.subs[sub - 1]) {

		case LEAF_hrSystemDate:
			free(ctx->scratch->ptr1);
			return (SNMP_ERR_NOERROR);

		case LEAF_hrSystemInitialLoadDevice:
		case LEAF_hrSystemInitialLoadParameters:
			abort();
		}
		abort();

	  case SNMP_OP_COMMIT:
		switch (value->var.subs[sub - 1]) {

		case LEAF_hrSystemDate:
			(void)OS_setSystemDate(ctx->scratch->ptr1);
			free(ctx->scratch->ptr1);
			return (SNMP_ERR_NOERROR);

		case LEAF_hrSystemInitialLoadDevice:
		case LEAF_hrSystemInitialLoadParameters:
			abort();
		}
		abort();

	  case SNMP_OP_GETNEXT:
		abort();
	}
	abort();
}

/*
 * prototype of this function was genrated by gensnmptree tool
 * in the header file hostres_tree.h
 * Returns SNMP_ERR_NOERROR on success
 */
int
op_hrStorage(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op curr_op)
{

	/* only GET is possible */
	switch (curr_op) {

	case SNMP_OP_GET:
		switch (value->var.subs[sub - 1]) {

		case LEAF_hrMemorySize:
			return (OS_getMemorySize(&value->v.uint32));
		}
		abort();

	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);

	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
	case SNMP_OP_GETNEXT:
		abort();
	}
	abort();
}
