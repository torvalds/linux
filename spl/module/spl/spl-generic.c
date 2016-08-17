/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Generic Implementation.
\*****************************************************************************/

#include <sys/sysmacros.h>
#include <sys/systeminfo.h>
#include <sys/vmsystm.h>
#include <sys/kobj.h>
#include <sys/kmem.h>
#include <sys/kmem_cache.h>
#include <sys/vmem.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/taskq.h>
#include <sys/tsd.h>
#include <sys/zmod.h>
#include <sys/debug.h>
#include <sys/proc.h>
#include <sys/kstat.h>
#include <sys/file.h>
#include <linux/ctype.h>
#include <linux/kmod.h>
#include <linux/math64_compat.h>
#include <linux/proc_compat.h>

char spl_version[32] = "SPL v" SPL_META_VERSION "-" SPL_META_RELEASE;
EXPORT_SYMBOL(spl_version);

unsigned long spl_hostid = 0;
EXPORT_SYMBOL(spl_hostid);
module_param(spl_hostid, ulong, 0644);
MODULE_PARM_DESC(spl_hostid, "The system hostid.");

proc_t p0;
EXPORT_SYMBOL(p0);

#if BITS_PER_LONG == 32
/*
 * Support 64/64 => 64 division on a 32-bit platform.  While the kernel
 * provides a div64_u64() function for this we do not use it because the
 * implementation is flawed.  There are cases which return incorrect
 * results as late as linux-2.6.35.  Until this is fixed upstream the
 * spl must provide its own implementation.
 *
 * This implementation is a slightly modified version of the algorithm
 * proposed by the book 'Hacker's Delight'.  The original source can be
 * found here and is available for use without restriction.
 *
 * http://www.hackersdelight.org/HDcode/newCode/divDouble.c
 */

/*
 * Calculate number of leading of zeros for a 64-bit value.
 */
static int
nlz64(uint64_t x) {
	register int n = 0;

	if (x == 0)
		return 64;

	if (x <= 0x00000000FFFFFFFFULL) {n = n + 32; x = x << 32;}
	if (x <= 0x0000FFFFFFFFFFFFULL) {n = n + 16; x = x << 16;}
	if (x <= 0x00FFFFFFFFFFFFFFULL) {n = n +  8; x = x <<  8;}
	if (x <= 0x0FFFFFFFFFFFFFFFULL) {n = n +  4; x = x <<  4;}
	if (x <= 0x3FFFFFFFFFFFFFFFULL) {n = n +  2; x = x <<  2;}
	if (x <= 0x7FFFFFFFFFFFFFFFULL) {n = n +  1;}

	return n;
}

/*
 * Newer kernels have a div_u64() function but we define our own
 * to simplify portibility between kernel versions.
 */
static inline uint64_t
__div_u64(uint64_t u, uint32_t v)
{
	(void) do_div(u, v);
	return u;
}

/*
 * Implementation of 64-bit unsigned division for 32-bit machines.
 *
 * First the procedure takes care of the case in which the divisor is a
 * 32-bit quantity. There are two subcases: (1) If the left half of the
 * dividend is less than the divisor, one execution of do_div() is all that
 * is required (overflow is not possible). (2) Otherwise it does two
 * divisions, using the grade school method.
 */
uint64_t
__udivdi3(uint64_t u, uint64_t v)
{
	uint64_t u0, u1, v1, q0, q1, k;
	int n;

	if (v >> 32 == 0) {			// If v < 2**32:
		if (u >> 32 < v) {		// If u/v cannot overflow,
			return __div_u64(u, v);	// just do one division.
		} else {			// If u/v would overflow:
			u1 = u >> 32;		// Break u into two halves.
			u0 = u & 0xFFFFFFFF;
			q1 = __div_u64(u1, v);	// First quotient digit.
			k  = u1 - q1 * v;	// First remainder, < v.
			u0 += (k << 32);
			q0 = __div_u64(u0, v);	// Seconds quotient digit.
			return (q1 << 32) + q0;
		}
	} else {				// If v >= 2**32:
		n = nlz64(v);			// 0 <= n <= 31.
		v1 = (v << n) >> 32;		// Normalize divisor, MSB is 1.
		u1 = u >> 1;			// To ensure no overflow.
		q1 = __div_u64(u1, v1);		// Get quotient from
		q0 = (q1 << n) >> 31;		// Undo normalization and
						// division of u by 2.
		if (q0 != 0)			// Make q0 correct or
			q0 = q0 - 1;		// too small by 1.
		if ((u - q0 * v) >= v)
			q0 = q0 + 1;		// Now q0 is correct.

		return q0;
	}
}
EXPORT_SYMBOL(__udivdi3);

/*
 * Implementation of 64-bit signed division for 32-bit machines.
 */
int64_t
__divdi3(int64_t u, int64_t v)
{
	int64_t q, t;
	q = __udivdi3(abs64(u), abs64(v));
	t = (u ^ v) >> 63;	// If u, v have different
	return (q ^ t) - t;	// signs, negate q.
}
EXPORT_SYMBOL(__divdi3);

/*
 * Implementation of 64-bit unsigned modulo for 32-bit machines.
 */
uint64_t
__umoddi3(uint64_t dividend, uint64_t divisor)
{
	return (dividend - (divisor * __udivdi3(dividend, divisor)));
}
EXPORT_SYMBOL(__umoddi3);

#if defined(__arm) || defined(__arm__)
/*
 * Implementation of 64-bit (un)signed division for 32-bit arm machines.
 *
 * Run-time ABI for the ARM Architecture (page 20).  A pair of (unsigned)
 * long longs is returned in {{r0, r1}, {r2,r3}}, the quotient in {r0, r1},
 * and the remainder in {r2, r3}.  The return type is specifically left
 * set to 'void' to ensure the compiler does not overwrite these registers
 * during the return.  All results are in registers as per ABI
 */
void
__aeabi_uldivmod(uint64_t u, uint64_t v)
{
	uint64_t res;
	uint64_t mod;

	res = __udivdi3(u, v);
	mod = __umoddi3(u, v);
	{
		register uint32_t r0 asm("r0") = (res & 0xFFFFFFFF);
		register uint32_t r1 asm("r1") = (res >> 32);
		register uint32_t r2 asm("r2") = (mod & 0xFFFFFFFF);
		register uint32_t r3 asm("r3") = (mod >> 32);

		asm volatile(""
		    : "+r"(r0), "+r"(r1), "+r"(r2),"+r"(r3)  /* output */
		    : "r"(r0), "r"(r1), "r"(r2), "r"(r3));   /* input */

		return; /* r0; */
	}
}
EXPORT_SYMBOL(__aeabi_uldivmod);

void
__aeabi_ldivmod(int64_t u, int64_t v)
{
	int64_t res;
	uint64_t mod;

	res =  __divdi3(u, v);
	mod = __umoddi3(u, v);
	{
		register uint32_t r0 asm("r0") = (res & 0xFFFFFFFF);
		register uint32_t r1 asm("r1") = (res >> 32);
		register uint32_t r2 asm("r2") = (mod & 0xFFFFFFFF);
		register uint32_t r3 asm("r3") = (mod >> 32);

		asm volatile(""
		    : "+r"(r0), "+r"(r1), "+r"(r2),"+r"(r3)  /* output */
		    : "r"(r0), "r"(r1), "r"(r2), "r"(r3));   /* input */

		return; /* r0; */
	}
}
EXPORT_SYMBOL(__aeabi_ldivmod);
#endif /* __arm || __arm__ */
#endif /* BITS_PER_LONG */

/* NOTE: The strtoxx behavior is solely based on my reading of the Solaris
 * ddi_strtol(9F) man page.  I have not verified the behavior of these
 * functions against their Solaris counterparts.  It is possible that I
 * may have misinterpreted the man page or the man page is incorrect.
 */
int ddi_strtoul(const char *, char **, int, unsigned long *);
int ddi_strtol(const char *, char **, int, long *);
int ddi_strtoull(const char *, char **, int, unsigned long long *);
int ddi_strtoll(const char *, char **, int, long long *);

#define define_ddi_strtoux(type, valtype)				\
int ddi_strtou##type(const char *str, char **endptr,			\
		     int base, valtype *result)				\
{									\
	valtype last_value, value = 0;					\
	char *ptr = (char *)str;					\
	int flag = 1, digit;						\
									\
	if (strlen(ptr) == 0)						\
		return EINVAL;						\
									\
	/* Auto-detect base based on prefix */				\
	if (!base) {							\
		if (str[0] == '0') {					\
			if (tolower(str[1])=='x' && isxdigit(str[2])) {	\
				base = 16; /* hex */			\
				ptr += 2;				\
			} else if (str[1] >= '0' && str[1] < 8) {	\
				base = 8; /* octal */			\
				ptr += 1;				\
			} else {					\
				return EINVAL;				\
			}						\
		} else {						\
			base = 10; /* decimal */			\
		}							\
	}								\
									\
	while (1) {							\
		if (isdigit(*ptr))					\
			digit = *ptr - '0';				\
		else if (isalpha(*ptr))					\
			digit = tolower(*ptr) - 'a' + 10;		\
		else							\
			break;						\
									\
		if (digit >= base)					\
			break;						\
									\
		last_value = value;					\
		value = value * base + digit;				\
		if (last_value > value) /* Overflow */			\
			return ERANGE;					\
									\
		flag = 1;						\
		ptr++;							\
	}								\
									\
	if (flag)							\
		*result = value;					\
									\
	if (endptr)							\
		*endptr = (char *)(flag ? ptr : str);			\
									\
	return 0;							\
}									\

#define define_ddi_strtox(type, valtype)				\
int ddi_strto##type(const char *str, char **endptr,			\
		       int base, valtype *result)			\
{									\
	int rc;								\
									\
	if (*str == '-') {						\
		rc = ddi_strtou##type(str + 1, endptr, base, result);	\
		if (!rc) {						\
			if (*endptr == str + 1)				\
				*endptr = (char *)str;			\
			else						\
				*result = -*result;			\
		}							\
	} else {							\
		rc = ddi_strtou##type(str, endptr, base, result);	\
	}								\
									\
	return rc;							\
}

define_ddi_strtoux(l, unsigned long)
define_ddi_strtox(l, long)
define_ddi_strtoux(ll, unsigned long long)
define_ddi_strtox(ll, long long)

EXPORT_SYMBOL(ddi_strtoul);
EXPORT_SYMBOL(ddi_strtol);
EXPORT_SYMBOL(ddi_strtoll);
EXPORT_SYMBOL(ddi_strtoull);

int
ddi_copyin(const void *from, void *to, size_t len, int flags)
{
	/* Fake ioctl() issued by kernel, 'from' is a kernel address */
	if (flags & FKIOCTL) {
		memcpy(to, from, len);
		return 0;
	}

	return copyin(from, to, len);
}
EXPORT_SYMBOL(ddi_copyin);

int
ddi_copyout(const void *from, void *to, size_t len, int flags)
{
	/* Fake ioctl() issued by kernel, 'from' is a kernel address */
	if (flags & FKIOCTL) {
		memcpy(to, from, len);
		return 0;
	}

	return copyout(from, to, len);
}
EXPORT_SYMBOL(ddi_copyout);

#ifndef HAVE_PUT_TASK_STRUCT
/*
 * This is only a stub function which should never be used.  The SPL should
 * never be putting away the last reference on a task structure so this will
 * not be called.  However, we still need to define it so the module does not
 * have undefined symbol at load time.  That all said if this impossible
 * thing does somehow happen PANIC immediately so we know about it.
 */
void
__put_task_struct(struct task_struct *t)
{
	PANIC("Unexpectly put last reference on task %d\n", (int)t->pid);
}
EXPORT_SYMBOL(__put_task_struct);
#endif /* HAVE_PUT_TASK_STRUCT */

/*
 * Read the unique system identifier from the /etc/hostid file.
 *
 * The behavior of /usr/bin/hostid on Linux systems with the
 * regular eglibc and coreutils is:
 *
 *   1. Generate the value if the /etc/hostid file does not exist
 *      or if the /etc/hostid file is less than four bytes in size.
 *
 *   2. If the /etc/hostid file is at least 4 bytes, then return
 *      the first four bytes [0..3] in native endian order.
 *
 *   3. Always ignore bytes [4..] if they exist in the file.
 *
 * Only the first four bytes are significant, even on systems that
 * have a 64-bit word size.
 *
 * See:
 *
 *   eglibc: sysdeps/unix/sysv/linux/gethostid.c
 *   coreutils: src/hostid.c
 *
 * Notes:
 *
 * The /etc/hostid file on Solaris is a text file that often reads:
 *
 *   # DO NOT EDIT
 *   "0123456789"
 *
 * Directly copying this file to Linux results in a constant
 * hostid of 4f442023 because the default comment constitutes
 * the first four bytes of the file.
 *
 */

char *spl_hostid_path = HW_HOSTID_PATH;
module_param(spl_hostid_path, charp, 0444);
MODULE_PARM_DESC(spl_hostid_path, "The system hostid file (/etc/hostid)");

static int
hostid_read(void)
{
	int result;
	uint64_t size;
	struct _buf *file;
	uint32_t hostid = 0;

	file = kobj_open_file(spl_hostid_path);

	if (file == (struct _buf *)-1)
		return -1;

	result = kobj_get_filesize(file, &size);

	if (result != 0) {
		printk(KERN_WARNING
		       "SPL: kobj_get_filesize returned %i on %s\n",
		       result, spl_hostid_path);
		kobj_close_file(file);
		return -2;
	}

	if (size < sizeof(HW_HOSTID_MASK)) {
		printk(KERN_WARNING
		       "SPL: Ignoring the %s file because it is %llu bytes; "
		       "expecting %lu bytes instead.\n", spl_hostid_path,
		       size, (unsigned long)sizeof(HW_HOSTID_MASK));
		kobj_close_file(file);
		return -3;
	}

	/* Read directly into the variable like eglibc does. */
	/* Short reads are okay; native behavior is preserved. */
	result = kobj_read_file(file, (char *)&hostid, sizeof(hostid), 0);

	if (result < 0) {
		printk(KERN_WARNING
		       "SPL: kobj_read_file returned %i on %s\n",
		       result, spl_hostid_path);
		kobj_close_file(file);
		return -4;
	}

	/* Mask down to 32 bits like coreutils does. */
	spl_hostid = hostid & HW_HOSTID_MASK;
	kobj_close_file(file);
	return 0;
}

uint32_t
zone_get_hostid(void *zone)
{
	static int first = 1;

	/* Only the global zone is supported */
	ASSERT(zone == NULL);

	if (first) {
		first = 0;

		spl_hostid &= HW_HOSTID_MASK;
		/*
		 * Get the hostid if it was not passed as a module parameter.
		 * Try reading the /etc/hostid file directly.
		 */
		if (spl_hostid == 0 && hostid_read())
			spl_hostid = 0;


		printk(KERN_NOTICE "SPL: using hostid 0x%08x\n",
			(unsigned int) spl_hostid);
	}

	return spl_hostid;
}
EXPORT_SYMBOL(zone_get_hostid);

static int
spl_kvmem_init(void)
{
	int rc = 0;

	rc = spl_kmem_init();
	if (rc)
		goto out1;

	rc = spl_vmem_init();
	if (rc)
		goto out2;

	rc = spl_kmem_cache_init();
	if (rc)
		goto out3;

	return (rc);
out3:
	spl_vmem_fini();
out2:
	spl_kmem_fini();
out1:
	return (rc);
}

static void
spl_kvmem_fini(void)
{
	spl_kmem_cache_fini();
	spl_vmem_fini();
	spl_kmem_fini();
}

static int __init
spl_init(void)
{
	int rc = 0;

	bzero(&p0, sizeof (proc_t));

	if ((rc = spl_kvmem_init()))
		goto out1;

	if ((rc = spl_mutex_init()))
		goto out2;

	if ((rc = spl_rw_init()))
		goto out3;

	if ((rc = spl_taskq_init()))
		goto out4;

	if ((rc = spl_vn_init()))
		goto out5;

	if ((rc = spl_proc_init()))
		goto out6;

	if ((rc = spl_kstat_init()))
		goto out7;

	if ((rc = spl_tsd_init()))
		goto out8;

	if ((rc = spl_zlib_init()))
		goto out9;

	printk(KERN_NOTICE "SPL: Loaded module v%s-%s%s\n", SPL_META_VERSION,
	       SPL_META_RELEASE, SPL_DEBUG_STR);
	return (rc);

out9:
	spl_tsd_fini();
out8:
	spl_kstat_fini();
out7:
	spl_proc_fini();
out6:
	spl_vn_fini();
out5:
	spl_taskq_fini();
out4:
	spl_rw_fini();
out3:
	spl_mutex_fini();
out2:
	spl_kvmem_fini();
out1:
	printk(KERN_NOTICE "SPL: Failed to Load Solaris Porting Layer "
	       "v%s-%s%s, rc = %d\n", SPL_META_VERSION, SPL_META_RELEASE,
	       SPL_DEBUG_STR, rc);

	return (rc);
}

static void __exit
spl_fini(void)
{
	printk(KERN_NOTICE "SPL: Unloaded module v%s-%s%s\n",
	       SPL_META_VERSION, SPL_META_RELEASE, SPL_DEBUG_STR);
	spl_zlib_fini();
	spl_tsd_fini();
	spl_kstat_fini();
	spl_proc_fini();
	spl_vn_fini();
	spl_taskq_fini();
	spl_rw_fini();
	spl_mutex_fini();
	spl_kvmem_fini();
}

module_init(spl_init);
module_exit(spl_fini);

MODULE_DESCRIPTION("Solaris Porting Layer");
MODULE_AUTHOR(SPL_META_AUTHOR);
MODULE_LICENSE(SPL_META_LICENSE);
MODULE_VERSION(SPL_META_VERSION "-" SPL_META_RELEASE);
