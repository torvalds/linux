/*
 * mpx-mini-test.c: routines to test Intel MPX (Memory Protection eXtentions)
 *
 * Written by:
 * "Ren, Qiaowei" <qiaowei.ren@intel.com>
 * "Wei, Gang" <gang.wei@intel.com>
 * "Hansen, Dave" <dave.hansen@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2.
 */

/*
 * 2014-12-05: Dave Hansen: fixed all of the compiler warnings, and made sure
 *	       it works on 32-bit.
 */

int inspect_every_this_many_mallocs = 100;
int zap_all_every_this_many_mallocs = 1000;

#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "mpx-hw.h"
#include "mpx-debug.h"
#include "mpx-mm.h"

#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline)
#endif

#ifndef TEST_DURATION_SECS
#define TEST_DURATION_SECS 3
#endif

void write_int_to(char *prefix, char *file, int int_to_write)
{
	char buf[100];
	int fd = open(file, O_RDWR);
	int len;
	int ret;

	assert(fd >= 0);
	len = snprintf(buf, sizeof(buf), "%s%d", prefix, int_to_write);
	assert(len >= 0);
	assert(len < sizeof(buf));
	ret = write(fd, buf, len);
	assert(ret == len);
	ret = close(fd);
	assert(!ret);
}

void write_pid_to(char *prefix, char *file)
{
	write_int_to(prefix, file, getpid());
}

void trace_me(void)
{
/* tracing events dir */
#define TED "/sys/kernel/debug/tracing/events/"
/*
	write_pid_to("common_pid=", TED "signal/filter");
	write_pid_to("common_pid=", TED "exceptions/filter");
	write_int_to("", TED "signal/enable", 1);
	write_int_to("", TED "exceptions/enable", 1);
*/
	write_pid_to("", "/sys/kernel/debug/tracing/set_ftrace_pid");
	write_int_to("", "/sys/kernel/debug/tracing/trace", 0);
}

#define test_failed() __test_failed(__FILE__, __LINE__)
static void __test_failed(char *f, int l)
{
	fprintf(stderr, "abort @ %s::%d\n", f, l);
	abort();
}

/* Error Printf */
#define eprintf(args...)	fprintf(stderr, args)

#ifdef __i386__

/* i386 directory size is 4MB */
#define REG_IP_IDX	REG_EIP
#define REX_PREFIX

#define XSAVE_OFFSET_IN_FPMEM	sizeof(struct _libc_fpstate)

/*
 * __cpuid() is from the Linux Kernel:
 */
static inline void __cpuid(unsigned int *eax, unsigned int *ebx,
		unsigned int *ecx, unsigned int *edx)
{
	/* ecx is often an input as well as an output. */
	asm volatile(
		"push %%ebx;"
		"cpuid;"
		"mov %%ebx, %1;"
		"pop %%ebx"
		: "=a" (*eax),
		  "=g" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (*eax), "2" (*ecx));
}

#else /* __i386__ */

#define REG_IP_IDX	REG_RIP
#define REX_PREFIX "0x48, "

#define XSAVE_OFFSET_IN_FPMEM	0

/*
 * __cpuid() is from the Linux Kernel:
 */
static inline void __cpuid(unsigned int *eax, unsigned int *ebx,
		unsigned int *ecx, unsigned int *edx)
{
	/* ecx is often an input as well as an output. */
	asm volatile(
		"cpuid;"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (*eax), "2" (*ecx));
}

#endif /* !__i386__ */

struct xsave_hdr_struct {
	uint64_t xstate_bv;
	uint64_t reserved1[2];
	uint64_t reserved2[5];
} __attribute__((packed));

struct bndregs_struct {
	uint64_t bndregs[8];
} __attribute__((packed));

struct bndcsr_struct {
	uint64_t cfg_reg_u;
	uint64_t status_reg;
} __attribute__((packed));

struct xsave_struct {
	uint8_t fpu_sse[512];
	struct xsave_hdr_struct xsave_hdr;
	uint8_t ymm[256];
	uint8_t lwp[128];
	struct bndregs_struct bndregs;
	struct bndcsr_struct bndcsr;
} __attribute__((packed));

uint8_t __attribute__((__aligned__(64))) buffer[4096];
struct xsave_struct *xsave_buf = (struct xsave_struct *)buffer;

uint8_t __attribute__((__aligned__(64))) test_buffer[4096];
struct xsave_struct *xsave_test_buf = (struct xsave_struct *)test_buffer;

uint64_t num_bnd_chk;

static __always_inline void xrstor_state(struct xsave_struct *fx, uint64_t mask)
{
	uint32_t lmask = mask;
	uint32_t hmask = mask >> 32;

	asm volatile(".byte " REX_PREFIX "0x0f,0xae,0x2f\n\t"
		     : : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
		     :   "memory");
}

static __always_inline void xsave_state_1(void *_fx, uint64_t mask)
{
	uint32_t lmask = mask;
	uint32_t hmask = mask >> 32;
	unsigned char *fx = _fx;

	asm volatile(".byte " REX_PREFIX "0x0f,0xae,0x27\n\t"
		     : : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
		     :   "memory");
}

static inline uint64_t xgetbv(uint32_t index)
{
	uint32_t eax, edx;

	asm volatile(".byte 0x0f,0x01,0xd0" /* xgetbv */
		     : "=a" (eax), "=d" (edx)
		     : "c" (index));
	return eax + ((uint64_t)edx << 32);
}

static uint64_t read_mpx_status_sig(ucontext_t *uctxt)
{
	memset(buffer, 0, sizeof(buffer));
	memcpy(buffer,
		(uint8_t *)uctxt->uc_mcontext.fpregs + XSAVE_OFFSET_IN_FPMEM,
		sizeof(struct xsave_struct));

	return xsave_buf->bndcsr.status_reg;
}

#include <pthread.h>

static uint8_t *get_next_inst_ip(uint8_t *addr)
{
	uint8_t *ip = addr;
	uint8_t sib;
	uint8_t rm;
	uint8_t mod;
	uint8_t base;
	uint8_t modrm;

	/* determine the prefix. */
	switch(*ip) {
	case 0xf2:
	case 0xf3:
	case 0x66:
		ip++;
		break;
	}

	/* look for rex prefix */
	if ((*ip & 0x40) == 0x40)
		ip++;

	/* Make sure we have a MPX instruction. */
	if (*ip++ != 0x0f)
		return addr;

	/* Skip the op code byte. */
	ip++;

	/* Get the modrm byte. */
	modrm = *ip++;

	/* Break it down into parts. */
	rm = modrm & 7;
	mod = (modrm >> 6);

	/* Init the parts of the address mode. */
	base = 8;

	/* Is it a mem mode? */
	if (mod != 3) {
		/* look for scaled indexed addressing */
		if (rm == 4) {
			/* SIB addressing */
			sib = *ip++;
			base = sib & 7;
			switch (mod) {
			case 0:
				if (base == 5)
					ip += 4;
				break;

			case 1:
				ip++;
				break;

			case 2:
				ip += 4;
				break;
			}

		} else {
			/* MODRM addressing */
			switch (mod) {
			case 0:
				/* DISP32 addressing, no base */
				if (rm == 5)
					ip += 4;
				break;

			case 1:
				ip++;
				break;

			case 2:
				ip += 4;
				break;
			}
		}
	}
	return ip;
}

#ifdef si_lower
static inline void *__si_bounds_lower(siginfo_t *si)
{
	return si->si_lower;
}

static inline void *__si_bounds_upper(siginfo_t *si)
{
	return si->si_upper;
}
#else

/*
 * This deals with old version of _sigfault in some distros:
 *

old _sigfault:
        struct {
            void *si_addr;
	} _sigfault;

new _sigfault:
	struct {
		void __user *_addr;
		int _trapno;
		short _addr_lsb;
		union {
			struct {
				void __user *_lower;
				void __user *_upper;
			} _addr_bnd;
			__u32 _pkey;
		};
	} _sigfault;
 *
 */

static inline void **__si_bounds_hack(siginfo_t *si)
{
	void *sigfault = &si->_sifields._sigfault;
	void *end_sigfault = sigfault + sizeof(si->_sifields._sigfault);
	int *trapno = (int*)end_sigfault;
	/* skip _trapno and _addr_lsb */
	void **__si_lower = (void**)(trapno + 2);

	return __si_lower;
}

static inline void *__si_bounds_lower(siginfo_t *si)
{
	return *__si_bounds_hack(si);
}

static inline void *__si_bounds_upper(siginfo_t *si)
{
	return *(__si_bounds_hack(si) + 1);
}
#endif

static int br_count;
static int expected_bnd_index = -1;
uint64_t shadow_plb[NR_MPX_BOUNDS_REGISTERS][2]; /* shadow MPX bound registers */
unsigned long shadow_map[NR_MPX_BOUNDS_REGISTERS];

/*
 * The kernel is supposed to provide some information about the bounds
 * exception in the siginfo.  It should match what we have in the bounds
 * registers that we are checking against.  Just check against the shadow copy
 * since it is easily available, and we also check that *it* matches the real
 * registers.
 */
void check_siginfo_vs_shadow(siginfo_t* si)
{
	int siginfo_ok = 1;
	void *shadow_lower = (void *)(unsigned long)shadow_plb[expected_bnd_index][0];
	void *shadow_upper = (void *)(unsigned long)shadow_plb[expected_bnd_index][1];

	if ((expected_bnd_index < 0) ||
	    (expected_bnd_index >= NR_MPX_BOUNDS_REGISTERS)) {
		fprintf(stderr, "ERROR: invalid expected_bnd_index: %d\n",
			expected_bnd_index);
		exit(6);
	}
	if (__si_bounds_lower(si) != shadow_lower)
		siginfo_ok = 0;
	if (__si_bounds_upper(si) != shadow_upper)
		siginfo_ok = 0;

	if (!siginfo_ok) {
		fprintf(stderr, "ERROR: siginfo bounds do not match "
			"shadow bounds for register %d\n", expected_bnd_index);
		exit(7);
	}
}

void handler(int signum, siginfo_t *si, void *vucontext)
{
	int i;
	ucontext_t *uctxt = vucontext;
	int trapno;
	unsigned long ip;

	dprintf1("entered signal handler\n");

	trapno = uctxt->uc_mcontext.gregs[REG_TRAPNO];
	ip = uctxt->uc_mcontext.gregs[REG_IP_IDX];

	if (trapno == 5) {
		typeof(si->si_addr) *si_addr_ptr = &si->si_addr;
		uint64_t status = read_mpx_status_sig(uctxt);
		uint64_t br_reason =  status & 0x3;

		br_count++;
		dprintf1("#BR 0x%jx (total seen: %d)\n", status, br_count);

#define SEGV_BNDERR     3  /* failed address bound checks */

		dprintf2("Saw a #BR! status 0x%jx at %016lx br_reason: %jx\n",
				status, ip, br_reason);
		dprintf2("si_signo: %d\n", si->si_signo);
		dprintf2("  signum: %d\n", signum);
		dprintf2("info->si_code == SEGV_BNDERR: %d\n",
				(si->si_code == SEGV_BNDERR));
		dprintf2("info->si_code: %d\n", si->si_code);
		dprintf2("info->si_lower: %p\n", __si_bounds_lower(si));
		dprintf2("info->si_upper: %p\n", __si_bounds_upper(si));

		for (i = 0; i < 8; i++)
			dprintf3("[%d]: %p\n", i, si_addr_ptr[i]);
		switch (br_reason) {
		case 0: /* traditional BR */
			fprintf(stderr,
				"Undefined status with bound exception:%jx\n",
				 status);
			exit(5);
		case 1: /* #BR MPX bounds exception */
			/* these are normal and we expect to see them */

			check_siginfo_vs_shadow(si);

			dprintf1("bounds exception (normal): status 0x%jx at %p si_addr: %p\n",
				status, (void *)ip, si->si_addr);
			num_bnd_chk++;
			uctxt->uc_mcontext.gregs[REG_IP_IDX] =
				(greg_t)get_next_inst_ip((uint8_t *)ip);
			break;
		case 2:
			fprintf(stderr, "#BR status == 2, missing bounds table,"
					"kernel should have handled!!\n");
			exit(4);
			break;
		default:
			fprintf(stderr, "bound check error: status 0x%jx at %p\n",
				status, (void *)ip);
			num_bnd_chk++;
			uctxt->uc_mcontext.gregs[REG_IP_IDX] =
				(greg_t)get_next_inst_ip((uint8_t *)ip);
			fprintf(stderr, "bound check error: si_addr %p\n", si->si_addr);
			exit(3);
		}
	} else if (trapno == 14) {
		eprintf("ERROR: In signal handler, page fault, trapno = %d, ip = %016lx\n",
			trapno, ip);
		eprintf("si_addr %p\n", si->si_addr);
		eprintf("REG_ERR: %lx\n", (unsigned long)uctxt->uc_mcontext.gregs[REG_ERR]);
		test_failed();
	} else {
		eprintf("unexpected trap %d! at 0x%lx\n", trapno, ip);
		eprintf("si_addr %p\n", si->si_addr);
		eprintf("REG_ERR: %lx\n", (unsigned long)uctxt->uc_mcontext.gregs[REG_ERR]);
		test_failed();
	}
}

static inline void cpuid_count(unsigned int op, int count,
			       unsigned int *eax, unsigned int *ebx,
			       unsigned int *ecx, unsigned int *edx)
{
	*eax = op;
	*ecx = count;
	__cpuid(eax, ebx, ecx, edx);
}

#define XSTATE_CPUID	    0x0000000d

/*
 * List of XSAVE features Linux knows about:
 */
enum xfeature_bit {
	XSTATE_BIT_FP,
	XSTATE_BIT_SSE,
	XSTATE_BIT_YMM,
	XSTATE_BIT_BNDREGS,
	XSTATE_BIT_BNDCSR,
	XSTATE_BIT_OPMASK,
	XSTATE_BIT_ZMM_Hi256,
	XSTATE_BIT_Hi16_ZMM,

	XFEATURES_NR_MAX,
};

#define XSTATE_FP	       (1 << XSTATE_BIT_FP)
#define XSTATE_SSE	      (1 << XSTATE_BIT_SSE)
#define XSTATE_YMM	      (1 << XSTATE_BIT_YMM)
#define XSTATE_BNDREGS	  (1 << XSTATE_BIT_BNDREGS)
#define XSTATE_BNDCSR	   (1 << XSTATE_BIT_BNDCSR)
#define XSTATE_OPMASK	   (1 << XSTATE_BIT_OPMASK)
#define XSTATE_ZMM_Hi256	(1 << XSTATE_BIT_ZMM_Hi256)
#define XSTATE_Hi16_ZMM	 (1 << XSTATE_BIT_Hi16_ZMM)

#define MPX_XSTATES		(XSTATE_BNDREGS | XSTATE_BNDCSR) /* 0x18 */

bool one_bit(unsigned int x, int bit)
{
	return !!(x & (1<<bit));
}

void print_state_component(int state_bit_nr, char *name)
{
	unsigned int eax, ebx, ecx, edx;
	unsigned int state_component_size;
	unsigned int state_component_supervisor;
	unsigned int state_component_user;
	unsigned int state_component_aligned;

	/* See SDM Section 13.2 */
	cpuid_count(XSTATE_CPUID, state_bit_nr, &eax, &ebx, &ecx, &edx);
	assert(eax || ebx || ecx);
	state_component_size = eax;
	state_component_supervisor = ((!ebx) && one_bit(ecx, 0));
	state_component_user = !one_bit(ecx, 0);
	state_component_aligned = one_bit(ecx, 1);
	printf("%8s: size: %d user: %d supervisor: %d aligned: %d\n",
		name,
		state_component_size,	    state_component_user,
		state_component_supervisor, state_component_aligned);

}

/* Intel-defined CPU features, CPUID level 0x00000001 (ecx) */
#define XSAVE_FEATURE_BIT       (26)  /* XSAVE/XRSTOR/XSETBV/XGETBV */
#define OSXSAVE_FEATURE_BIT     (27) /* XSAVE enabled in the OS */

bool check_mpx_support(void)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid_count(1, 0, &eax, &ebx, &ecx, &edx);

	/* We can't do much without XSAVE, so just make these assert()'s */
	if (!one_bit(ecx, XSAVE_FEATURE_BIT)) {
		fprintf(stderr, "processor lacks XSAVE, can not run MPX tests\n");
		exit(0);
	}

	if (!one_bit(ecx, OSXSAVE_FEATURE_BIT)) {
		fprintf(stderr, "processor lacks OSXSAVE, can not run MPX tests\n");
		exit(0);
	}

	/* CPUs not supporting the XSTATE CPUID leaf do not support MPX */
	/* Is this redundant with the feature bit checks? */
	cpuid_count(0, 0, &eax, &ebx, &ecx, &edx);
	if (eax < XSTATE_CPUID) {
		fprintf(stderr, "processor lacks XSTATE CPUID leaf,"
				" can not run MPX tests\n");
		exit(0);
	}

	printf("XSAVE is supported by HW & OS\n");

	cpuid_count(XSTATE_CPUID, 0, &eax, &ebx, &ecx, &edx);

	printf("XSAVE processor supported state mask: 0x%x\n", eax);
	printf("XSAVE OS supported state mask: 0x%jx\n", xgetbv(0));

	/* Make sure that the MPX states are enabled in in XCR0 */
	if ((eax & MPX_XSTATES) != MPX_XSTATES) {
		fprintf(stderr, "processor lacks MPX XSTATE(s), can not run MPX tests\n");
		exit(0);
	}

	/* Make sure the MPX states are supported by XSAVE* */
	if ((xgetbv(0) & MPX_XSTATES) != MPX_XSTATES) {
		fprintf(stderr, "MPX XSTATE(s) no enabled in XCR0, "
				"can not run MPX tests\n");
		exit(0);
	}

	print_state_component(XSTATE_BIT_BNDREGS, "BNDREGS");
	print_state_component(XSTATE_BIT_BNDCSR,  "BNDCSR");

	return true;
}

void enable_mpx(void *l1base)
{
	/* enable point lookup */
	memset(buffer, 0, sizeof(buffer));
	xrstor_state(xsave_buf, 0x18);

	xsave_buf->xsave_hdr.xstate_bv = 0x10;
	xsave_buf->bndcsr.cfg_reg_u = (unsigned long)l1base | 1;
	xsave_buf->bndcsr.status_reg = 0;

	dprintf2("bf xrstor\n");
	dprintf2("xsave cndcsr: status %jx, configu %jx\n",
	       xsave_buf->bndcsr.status_reg, xsave_buf->bndcsr.cfg_reg_u);
	xrstor_state(xsave_buf, 0x18);
	dprintf2("after xrstor\n");

	xsave_state_1(xsave_buf, 0x18);

	dprintf1("xsave bndcsr: status %jx, configu %jx\n",
	       xsave_buf->bndcsr.status_reg, xsave_buf->bndcsr.cfg_reg_u);
}

#include <sys/prctl.h>

struct mpx_bounds_dir *bounds_dir_ptr;

unsigned long __bd_incore(const char *func, int line)
{
	unsigned long ret = nr_incore(bounds_dir_ptr, MPX_BOUNDS_DIR_SIZE_BYTES);
	return ret;
}
#define bd_incore() __bd_incore(__func__, __LINE__)

void check_clear(void *ptr, unsigned long sz)
{
	unsigned long *i;

	for (i = ptr; (void *)i < ptr + sz; i++) {
		if (*i) {
			dprintf1("%p is NOT clear at %p\n", ptr, i);
			assert(0);
		}
	}
	dprintf1("%p is clear for %lx\n", ptr, sz);
}

void check_clear_bd(void)
{
	check_clear(bounds_dir_ptr, 2UL << 30);
}

#define USE_MALLOC_FOR_BOUNDS_DIR 1
bool process_specific_init(void)
{
	unsigned long size;
	unsigned long *dir;
	/* Guarantee we have the space to align it, add padding: */
	unsigned long pad = getpagesize();

	size = 2UL << 30; /* 2GB */
	if (sizeof(unsigned long) == 4)
		size = 4UL << 20; /* 4MB */
	dprintf1("trying to allocate %ld MB bounds directory\n", (size >> 20));

	if (USE_MALLOC_FOR_BOUNDS_DIR) {
		unsigned long _dir;

		dir = malloc(size + pad);
		assert(dir);
		_dir = (unsigned long)dir;
		_dir += 0xfffUL;
		_dir &= ~0xfffUL;
		dir = (void *)_dir;
	} else {
		/*
		 * This makes debugging easier because the address
		 * calculations are simpler:
		 */
		dir = mmap((void *)0x200000000000, size + pad,
				PROT_READ|PROT_WRITE,
				MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
		if (dir == (void *)-1) {
			perror("unable to allocate bounds directory");
			abort();
		}
		check_clear(dir, size);
	}
	bounds_dir_ptr = (void *)dir;
	madvise(bounds_dir_ptr, size, MADV_NOHUGEPAGE);
	bd_incore();
	dprintf1("bounds directory: 0x%p -> 0x%p\n", bounds_dir_ptr,
			(char *)bounds_dir_ptr + size);
	check_clear(dir, size);
	enable_mpx(dir);
	check_clear(dir, size);
	if (prctl(43, 0, 0, 0, 0)) {
		printf("no MPX support\n");
		abort();
		return false;
	}
	return true;
}

bool process_specific_finish(void)
{
	if (prctl(44)) {
		printf("no MPX support\n");
		return false;
	}
	return true;
}

void setup_handler()
{
	int r, rs;
	struct sigaction newact;
	struct sigaction oldact;

	/* #BR is mapped to sigsegv */
	int signum  = SIGSEGV;

	newact.sa_handler = 0;   /* void(*)(int)*/
	newact.sa_sigaction = handler; /* void (*)(int, siginfo_t*, void *) */

	/*sigset_t - signals to block while in the handler */
	/* get the old signal mask. */
	rs = sigprocmask(SIG_SETMASK, 0, &newact.sa_mask);
	assert(rs == 0);

	/* call sa_sigaction, not sa_handler*/
	newact.sa_flags = SA_SIGINFO;

	newact.sa_restorer = 0;  /* void(*)(), obsolete */
	r = sigaction(signum, &newact, &oldact);
	assert(r == 0);
}

void mpx_prepare(void)
{
	dprintf2("%s()\n", __func__);
	setup_handler();
	process_specific_init();
}

void mpx_cleanup(void)
{
	printf("%s(): %jd BRs. bye...\n", __func__, num_bnd_chk);
	process_specific_finish();
}

/*-------------- the following is test case ---------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

uint64_t num_lower_brs;
uint64_t num_upper_brs;

#define MPX_CONFIG_OFFSET 1024
#define MPX_BOUNDS_OFFSET 960
#define MPX_HEADER_OFFSET 512
#define MAX_ADDR_TESTED (1<<28)
#define TEST_ROUNDS 100

/*
      0F 1A /r BNDLDX-Load
      0F 1B /r BNDSTX-Store Extended Bounds Using Address Translation
   66 0F 1A /r BNDMOV bnd1, bnd2/m128
   66 0F 1B /r BNDMOV bnd1/m128, bnd2
   F2 0F 1A /r BNDCU bnd, r/m64
   F2 0F 1B /r BNDCN bnd, r/m64
   F3 0F 1A /r BNDCL bnd, r/m64
   F3 0F 1B /r BNDMK bnd, m64
*/

static __always_inline void xsave_state(void *_fx, uint64_t mask)
{
	uint32_t lmask = mask;
	uint32_t hmask = mask >> 32;
	unsigned char *fx = _fx;

	asm volatile(".byte " REX_PREFIX "0x0f,0xae,0x27\n\t"
		     : : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
		     :   "memory");
}

static __always_inline void mpx_clear_bnd0(void)
{
	long size = 0;
	void *ptr = NULL;
	/* F3 0F 1B /r BNDMK bnd, m64			*/
	/* f3 0f 1b 04 11    bndmk  (%rcx,%rdx,1),%bnd0	*/
	asm volatile(".byte 0xf3,0x0f,0x1b,0x04,0x11\n\t"
		     : : "c" (ptr), "d" (size-1)
		     :   "memory");
}

static __always_inline void mpx_make_bound_helper(unsigned long ptr,
		unsigned long size)
{
	/* F3 0F 1B /r		BNDMK bnd, m64			*/
	/* f3 0f 1b 04 11       bndmk  (%rcx,%rdx,1),%bnd0	*/
	asm volatile(".byte 0xf3,0x0f,0x1b,0x04,0x11\n\t"
		     : : "c" (ptr), "d" (size-1)
		     :   "memory");
}

static __always_inline void mpx_check_lowerbound_helper(unsigned long ptr)
{
	/* F3 0F 1A /r	NDCL bnd, r/m64			*/
	/* f3 0f 1a 01	bndcl  (%rcx),%bnd0		*/
	asm volatile(".byte 0xf3,0x0f,0x1a,0x01\n\t"
		     : : "c" (ptr)
		     :   "memory");
}

static __always_inline void mpx_check_upperbound_helper(unsigned long ptr)
{
	/* F2 0F 1A /r	BNDCU bnd, r/m64	*/
	/* f2 0f 1a 01	bndcu  (%rcx),%bnd0	*/
	asm volatile(".byte 0xf2,0x0f,0x1a,0x01\n\t"
		     : : "c" (ptr)
		     :   "memory");
}

static __always_inline void mpx_movbndreg_helper()
{
	/* 66 0F 1B /r	BNDMOV bnd1/m128, bnd2	*/
	/* 66 0f 1b c2	bndmov %bnd0,%bnd2	*/

	asm volatile(".byte 0x66,0x0f,0x1b,0xc2\n\t");
}

static __always_inline void mpx_movbnd2mem_helper(uint8_t *mem)
{
	/* 66 0F 1B /r	BNDMOV bnd1/m128, bnd2	*/
	/* 66 0f 1b 01	bndmov %bnd0,(%rcx)	*/
	asm volatile(".byte 0x66,0x0f,0x1b,0x01\n\t"
		     : : "c" (mem)
		     :   "memory");
}

static __always_inline void mpx_movbnd_from_mem_helper(uint8_t *mem)
{
	/* 66 0F 1A /r	BNDMOV bnd1, bnd2/m128	*/
	/* 66 0f 1a 01	bndmov (%rcx),%bnd0	*/
	asm volatile(".byte 0x66,0x0f,0x1a,0x01\n\t"
		     : : "c" (mem)
		     :   "memory");
}

static __always_inline void mpx_store_dsc_helper(unsigned long ptr_addr,
		unsigned long ptr_val)
{
	/* 0F 1B /r	BNDSTX-Store Extended Bounds Using Address Translation	*/
	/* 0f 1b 04 11	bndstx %bnd0,(%rcx,%rdx,1)				*/
	asm volatile(".byte 0x0f,0x1b,0x04,0x11\n\t"
		     : : "c" (ptr_addr), "d" (ptr_val)
		     :   "memory");
}

static __always_inline void mpx_load_dsc_helper(unsigned long ptr_addr,
		unsigned long ptr_val)
{
	/* 0F 1A /r	BNDLDX-Load			*/
	/*/ 0f 1a 04 11	bndldx (%rcx,%rdx,1),%bnd0	*/
	asm volatile(".byte 0x0f,0x1a,0x04,0x11\n\t"
		     : : "c" (ptr_addr), "d" (ptr_val)
		     :   "memory");
}

void __print_context(void *__print_xsave_buffer, int line)
{
	uint64_t *bounds = (uint64_t *)(__print_xsave_buffer + MPX_BOUNDS_OFFSET);
	uint64_t *cfg    = (uint64_t *)(__print_xsave_buffer + MPX_CONFIG_OFFSET);

	int i;
	eprintf("%s()::%d\n", "print_context", line);
	for (i = 0; i < 4; i++) {
		eprintf("bound[%d]: 0x%016lx 0x%016lx(0x%016lx)\n", i,
		       (unsigned long)bounds[i*2],
		       ~(unsigned long)bounds[i*2+1],
			(unsigned long)bounds[i*2+1]);
	}

	eprintf("cpcfg: %jx  cpstatus: %jx\n", cfg[0], cfg[1]);
}
#define print_context(x) __print_context(x, __LINE__)
#ifdef DEBUG
#define dprint_context(x) print_context(x)
#else
#define dprint_context(x) do{}while(0)
#endif

void init()
{
	int i;

	srand((unsigned int)time(NULL));

	for (i = 0; i < 4; i++) {
		shadow_plb[i][0] = 0;
		shadow_plb[i][1] = ~(unsigned long)0;
	}
}

long int __mpx_random(int line)
{
#ifdef NOT_SO_RANDOM
	static long fake = 722122311;
	fake += 563792075;
	return fakse;
#else
	return random();
#endif
}
#define mpx_random() __mpx_random(__LINE__)

uint8_t *get_random_addr()
{
	uint8_t*addr = (uint8_t *)(unsigned long)(rand() % MAX_ADDR_TESTED);
	return (addr - (unsigned long)addr % sizeof(uint8_t *));
}

static inline bool compare_context(void *__xsave_buffer)
{
	uint64_t *bounds = (uint64_t *)(__xsave_buffer + MPX_BOUNDS_OFFSET);

	int i;
	for (i = 0; i < 4; i++) {
		dprintf3("shadow[%d]{%016lx/%016lx}\nbounds[%d]{%016lx/%016lx}\n",
		       i, (unsigned long)shadow_plb[i][0], (unsigned long)shadow_plb[i][1],
		       i, (unsigned long)bounds[i*2],     ~(unsigned long)bounds[i*2+1]);
		if ((shadow_plb[i][0] != bounds[i*2]) ||
		    (shadow_plb[i][1] != ~(unsigned long)bounds[i*2+1])) {
			eprintf("ERROR comparing shadow to real bound register %d\n", i);
			eprintf("shadow{0x%016lx/0x%016lx}\nbounds{0x%016lx/0x%016lx}\n",
			       (unsigned long)shadow_plb[i][0], (unsigned long)shadow_plb[i][1],
			       (unsigned long)bounds[i*2], (unsigned long)bounds[i*2+1]);
			return false;
		}
	}

	return true;
}

void mkbnd_shadow(uint8_t *ptr, int index, long offset)
{
	uint64_t *lower = (uint64_t *)&(shadow_plb[index][0]);
	uint64_t *upper = (uint64_t *)&(shadow_plb[index][1]);
	*lower = (unsigned long)ptr;
	*upper = (unsigned long)ptr + offset - 1;
}

void check_lowerbound_shadow(uint8_t *ptr, int index)
{
	uint64_t *lower = (uint64_t *)&(shadow_plb[index][0]);
	if (*lower > (uint64_t)(unsigned long)ptr)
		num_lower_brs++;
	else
		dprintf1("LowerBoundChk passed:%p\n", ptr);
}

void check_upperbound_shadow(uint8_t *ptr, int index)
{
	uint64_t upper = *(uint64_t *)&(shadow_plb[index][1]);
	if (upper < (uint64_t)(unsigned long)ptr)
		num_upper_brs++;
	else
		dprintf1("UpperBoundChk passed:%p\n", ptr);
}

__always_inline void movbndreg_shadow(int src, int dest)
{
	shadow_plb[dest][0] = shadow_plb[src][0];
	shadow_plb[dest][1] = shadow_plb[src][1];
}

__always_inline void movbnd2mem_shadow(int src, unsigned long *dest)
{
	unsigned long *lower = (unsigned long *)&(shadow_plb[src][0]);
	unsigned long *upper = (unsigned long *)&(shadow_plb[src][1]);
	*dest = *lower;
	*(dest+1) = *upper;
}

__always_inline void movbnd_from_mem_shadow(unsigned long *src, int dest)
{
	unsigned long *lower = (unsigned long *)&(shadow_plb[dest][0]);
	unsigned long *upper = (unsigned long *)&(shadow_plb[dest][1]);
	*lower = *src;
	*upper = *(src+1);
}

__always_inline void stdsc_shadow(int index, uint8_t *ptr, uint8_t *ptr_val)
{
	shadow_map[0] = (unsigned long)shadow_plb[index][0];
	shadow_map[1] = (unsigned long)shadow_plb[index][1];
	shadow_map[2] = (unsigned long)ptr_val;
	dprintf3("%s(%d, %p, %p) set shadow map[2]: %p\n", __func__,
			index, ptr, ptr_val, ptr_val);
	/*ptr ignored */
}

void lddsc_shadow(int index, uint8_t *ptr, uint8_t *ptr_val)
{
	uint64_t lower = shadow_map[0];
	uint64_t upper = shadow_map[1];
	uint8_t *value = (uint8_t *)shadow_map[2];

	if (value != ptr_val) {
		dprintf2("%s(%d, %p, %p) init shadow bounds[%d] "
			 "because %p != %p\n", __func__, index, ptr,
			 ptr_val, index, value, ptr_val);
		shadow_plb[index][0] = 0;
		shadow_plb[index][1] = ~(unsigned long)0;
	} else {
		shadow_plb[index][0] = lower;
		shadow_plb[index][1] = upper;
	}
	/* ptr ignored */
}

static __always_inline void mpx_test_helper0(uint8_t *buf, uint8_t *ptr)
{
	mpx_make_bound_helper((unsigned long)ptr, 0x1800);
}

static __always_inline void mpx_test_helper0_shadow(uint8_t *buf, uint8_t *ptr)
{
	mkbnd_shadow(ptr, 0, 0x1800);
}

static __always_inline void mpx_test_helper1(uint8_t *buf, uint8_t *ptr)
{
	/* these are hard-coded to check bnd0 */
	expected_bnd_index = 0;
	mpx_check_lowerbound_helper((unsigned long)(ptr-1));
	mpx_check_upperbound_helper((unsigned long)(ptr+0x1800));
	/* reset this since we do not expect any more bounds exceptions */
	expected_bnd_index = -1;
}

static __always_inline void mpx_test_helper1_shadow(uint8_t *buf, uint8_t *ptr)
{
	check_lowerbound_shadow(ptr-1, 0);
	check_upperbound_shadow(ptr+0x1800, 0);
}

static __always_inline void mpx_test_helper2(uint8_t *buf, uint8_t *ptr)
{
	mpx_make_bound_helper((unsigned long)ptr, 0x1800);
	mpx_movbndreg_helper();
	mpx_movbnd2mem_helper(buf);
	mpx_make_bound_helper((unsigned long)(ptr+0x12), 0x1800);
}

static __always_inline void mpx_test_helper2_shadow(uint8_t *buf, uint8_t *ptr)
{
	mkbnd_shadow(ptr, 0, 0x1800);
	movbndreg_shadow(0, 2);
	movbnd2mem_shadow(0, (unsigned long *)buf);
	mkbnd_shadow(ptr+0x12, 0, 0x1800);
}

static __always_inline void mpx_test_helper3(uint8_t *buf, uint8_t *ptr)
{
	mpx_movbnd_from_mem_helper(buf);
}

static __always_inline void mpx_test_helper3_shadow(uint8_t *buf, uint8_t *ptr)
{
	movbnd_from_mem_shadow((unsigned long *)buf, 0);
}

static __always_inline void mpx_test_helper4(uint8_t *buf, uint8_t *ptr)
{
	mpx_store_dsc_helper((unsigned long)buf, (unsigned long)ptr);
	mpx_make_bound_helper((unsigned long)(ptr+0x12), 0x1800);
}

static __always_inline void mpx_test_helper4_shadow(uint8_t *buf, uint8_t *ptr)
{
	stdsc_shadow(0, buf, ptr);
	mkbnd_shadow(ptr+0x12, 0, 0x1800);
}

static __always_inline void mpx_test_helper5(uint8_t *buf, uint8_t *ptr)
{
	mpx_load_dsc_helper((unsigned long)buf, (unsigned long)ptr);
}

static __always_inline void mpx_test_helper5_shadow(uint8_t *buf, uint8_t *ptr)
{
	lddsc_shadow(0, buf, ptr);
}

#define NR_MPX_TEST_FUNCTIONS 6

/*
 * For compatibility reasons, MPX will clear the bounds registers
 * when you make function calls (among other things).  We have to
 * preserve the registers in between calls to the "helpers" since
 * they build on each other.
 *
 * Be very careful not to make any function calls inside the
 * helpers, or anywhere else beween the xrstor and xsave.
 */
#define run_helper(helper_nr, buf, buf_shadow, ptr)	do {	\
	xrstor_state(xsave_test_buf, flags);			\
	mpx_test_helper##helper_nr(buf, ptr);			\
	xsave_state(xsave_test_buf, flags);			\
	mpx_test_helper##helper_nr##_shadow(buf_shadow, ptr);	\
} while (0)

static void run_helpers(int nr, uint8_t *buf, uint8_t *buf_shadow, uint8_t *ptr)
{
	uint64_t flags = 0x18;

	dprint_context(xsave_test_buf);
	switch (nr) {
	case 0:
		run_helper(0, buf, buf_shadow, ptr);
		break;
	case 1:
		run_helper(1, buf, buf_shadow, ptr);
		break;
	case 2:
		run_helper(2, buf, buf_shadow, ptr);
		break;
	case 3:
		run_helper(3, buf, buf_shadow, ptr);
		break;
	case 4:
		run_helper(4, buf, buf_shadow, ptr);
		break;
	case 5:
		run_helper(5, buf, buf_shadow, ptr);
		break;
	default:
		test_failed();
		break;
	}
	dprint_context(xsave_test_buf);
}

unsigned long buf_shadow[1024]; /* used to check load / store descriptors */
extern long inspect_me(struct mpx_bounds_dir *bounds_dir);

long cover_buf_with_bt_entries(void *buf, long buf_len)
{
	int i;
	long nr_to_fill;
	int ratio = 1000;
	unsigned long buf_len_in_ptrs;

	/* Fill about 1/100 of the space with bt entries */
	nr_to_fill = buf_len / (sizeof(unsigned long) * ratio);

	if (!nr_to_fill)
		dprintf3("%s() nr_to_fill: %ld\n", __func__, nr_to_fill);

	/* Align the buffer to pointer size */
	while (((unsigned long)buf) % sizeof(void *)) {
		buf++;
		buf_len--;
	}
	/* We are storing pointers, so make */
	buf_len_in_ptrs = buf_len / sizeof(void *);

	for (i = 0; i < nr_to_fill; i++) {
		long index = (mpx_random() % buf_len_in_ptrs);
		void *ptr = buf + index * sizeof(unsigned long);
		unsigned long ptr_addr = (unsigned long)ptr;

		/* ptr and size can be anything */
		mpx_make_bound_helper((unsigned long)ptr, 8);

		/*
		 * take bnd0 and put it in to bounds tables "buf + index" is an
		 * address inside the buffer where we are pretending that we
		 * are going to put a pointer We do not, though because we will
		 * never load entries from the table, so it doesn't matter.
		 */
		mpx_store_dsc_helper(ptr_addr, (unsigned long)ptr);
		dprintf4("storing bound table entry for %lx (buf start @ %p)\n",
				ptr_addr, buf);
	}
	return nr_to_fill;
}

unsigned long align_down(unsigned long alignme, unsigned long align_to)
{
	return alignme & ~(align_to-1);
}

unsigned long align_up(unsigned long alignme, unsigned long align_to)
{
	return (alignme + align_to - 1) & ~(align_to-1);
}

/*
 * Using 1MB alignment guarantees that each no allocation
 * will overlap with another's bounds tables.
 *
 * We have to cook our own allocator here.  malloc() can
 * mix other allocation with ours which means that even
 * if we free all of our allocations, there might still
 * be bounds tables for the *areas* since there is other
 * valid memory there.
 *
 * We also can't use malloc() because a free() of an area
 * might not free it back to the kernel.  We want it
 * completely unmapped an malloc() does not guarantee
 * that.
 */
#ifdef __i386__
long alignment = 4096;
long sz_alignment = 4096;
#else
long alignment = 1 * MB;
long sz_alignment = 1 * MB;
#endif
void *mpx_mini_alloc(unsigned long sz)
{
	unsigned long long tries = 0;
	static void *last;
	void *ptr;
	void *try_at;

	sz = align_up(sz, sz_alignment);

	try_at = last + alignment;
	while (1) {
		ptr = mmap(try_at, sz, PROT_READ|PROT_WRITE,
				MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
		if (ptr == (void *)-1)
			return NULL;
		if (ptr == try_at)
			break;

		munmap(ptr, sz);
		try_at += alignment;
#ifdef __i386__
		/*
		 * This isn't quite correct for 32-bit binaries
		 * on 64-bit kernels since they can use the
		 * entire 32-bit address space, but it's close
		 * enough.
		 */
		if (try_at > (void *)0xC0000000)
#else
		if (try_at > (void *)0x0000800000000000)
#endif
			try_at = (void *)0x0;
		if (!(++tries % 10000))
			dprintf1("stuck in %s(), tries: %lld\n", __func__, tries);
		continue;
	}
	last = ptr;
	dprintf3("mpx_mini_alloc(0x%lx) returning: %p\n", sz, ptr);
	return ptr;
}
void mpx_mini_free(void *ptr, long sz)
{
	dprintf2("%s() ptr: %p\n", __func__, ptr);
	if ((unsigned long)ptr > 0x100000000000) {
		dprintf1("uh oh !!!!!!!!!!!!!!! pointer too high: %p\n", ptr);
		test_failed();
	}
	sz = align_up(sz, sz_alignment);
	dprintf3("%s() ptr: %p before munmap\n", __func__, ptr);
	munmap(ptr, sz);
	dprintf3("%s() ptr: %p DONE\n", __func__, ptr);
}

#define NR_MALLOCS 100
struct one_malloc {
	char *ptr;
	int nr_filled_btes;
	unsigned long size;
};
struct one_malloc mallocs[NR_MALLOCS];

void free_one_malloc(int index)
{
	unsigned long free_ptr;
	unsigned long mask;

	if (!mallocs[index].ptr)
		return;

	mpx_mini_free(mallocs[index].ptr, mallocs[index].size);
	dprintf4("freed[%d]:  %p\n", index, mallocs[index].ptr);

	free_ptr = (unsigned long)mallocs[index].ptr;
	mask = alignment-1;
	dprintf4("lowerbits: %lx / %lx mask: %lx\n", free_ptr,
			(free_ptr & mask), mask);
	assert((free_ptr & mask) == 0);

	mallocs[index].ptr = NULL;
}

#ifdef __i386__
#define MPX_BOUNDS_TABLE_COVERS 4096
#else
#define MPX_BOUNDS_TABLE_COVERS (1 * MB)
#endif
void zap_everything(void)
{
	long after_zap;
	long before_zap;
	int i;

	before_zap = inspect_me(bounds_dir_ptr);
	dprintf1("zapping everything start: %ld\n", before_zap);
	for (i = 0; i < NR_MALLOCS; i++)
		free_one_malloc(i);

	after_zap = inspect_me(bounds_dir_ptr);
	dprintf1("zapping everything done: %ld\n", after_zap);
	/*
	 * We only guarantee to empty the thing out if our allocations are
	 * exactly aligned on the boundaries of a boudns table.
	 */
	if ((alignment >= MPX_BOUNDS_TABLE_COVERS) &&
	    (sz_alignment >= MPX_BOUNDS_TABLE_COVERS)) {
		if (after_zap != 0)
			test_failed();

		assert(after_zap == 0);
	}
}

void do_one_malloc(void)
{
	static int malloc_counter;
	long sz;
	int rand_index = (mpx_random() % NR_MALLOCS);
	void *ptr = mallocs[rand_index].ptr;

	dprintf3("%s() enter\n", __func__);

	if (ptr) {
		dprintf3("freeing one malloc at index: %d\n", rand_index);
		free_one_malloc(rand_index);
		if (mpx_random() % (NR_MALLOCS*3) == 3) {
			int i;
			dprintf3("zapping some more\n");
			for (i = rand_index; i < NR_MALLOCS; i++)
				free_one_malloc(i);
		}
		if ((mpx_random() % zap_all_every_this_many_mallocs) == 4)
			zap_everything();
	}

	/* 1->~1M */
	sz = (1 + mpx_random() % 1000) * 1000;
	ptr = mpx_mini_alloc(sz);
	if (!ptr) {
		/*
		 * If we are failing allocations, just assume we
		 * are out of memory and zap everything.
		 */
		dprintf3("zapping everything because out of memory\n");
		zap_everything();
		goto out;
	}

	dprintf3("malloc: %p size: 0x%lx\n", ptr, sz);
	mallocs[rand_index].nr_filled_btes = cover_buf_with_bt_entries(ptr, sz);
	mallocs[rand_index].ptr = ptr;
	mallocs[rand_index].size = sz;
out:
	if ((++malloc_counter) % inspect_every_this_many_mallocs == 0)
		inspect_me(bounds_dir_ptr);
}

void run_timed_test(void (*test_func)(void))
{
	int done = 0;
	long iteration = 0;
	static time_t last_print;
	time_t now;
	time_t start;

	time(&start);
	while (!done) {
		time(&now);
		if ((now - start) > TEST_DURATION_SECS)
			done = 1;

		test_func();
		iteration++;

		if ((now - last_print > 1) || done) {
			printf("iteration %ld complete, OK so far\n", iteration);
			last_print = now;
		}
	}
}

void check_bounds_table_frees(void)
{
	printf("executing unmaptest\n");
	inspect_me(bounds_dir_ptr);
	run_timed_test(&do_one_malloc);
	printf("done with malloc() fun\n");
}

void insn_test_failed(int test_nr, int test_round, void *buf,
		void *buf_shadow, void *ptr)
{
	print_context(xsave_test_buf);
	eprintf("ERROR: test %d round %d failed\n", test_nr, test_round);
	while (test_nr == 5) {
		struct mpx_bt_entry *bte;
		struct mpx_bounds_dir *bd = (void *)bounds_dir_ptr;
		struct mpx_bd_entry *bde = mpx_vaddr_to_bd_entry(buf, bd);

		printf("  bd: %p\n", bd);
		printf("&bde: %p\n", bde);
		printf("*bde: %lx\n", *(unsigned long *)bde);
		if (!bd_entry_valid(bde))
			break;

		bte = mpx_vaddr_to_bt_entry(buf, bd);
		printf(" te: %p\n", bte);
		printf("bte[0]: %lx\n", bte->contents[0]);
		printf("bte[1]: %lx\n", bte->contents[1]);
		printf("bte[2]: %lx\n", bte->contents[2]);
		printf("bte[3]: %lx\n", bte->contents[3]);
		break;
	}
	test_failed();
}

void check_mpx_insns_and_tables(void)
{
	int successes = 0;
	int failures  = 0;
	int buf_size = (1024*1024);
	unsigned long *buf = malloc(buf_size);
	const int total_nr_tests = NR_MPX_TEST_FUNCTIONS * TEST_ROUNDS;
	int i, j;

	memset(buf, 0, buf_size);
	memset(buf_shadow, 0, sizeof(buf_shadow));

	for (i = 0; i < TEST_ROUNDS; i++) {
		uint8_t *ptr = get_random_addr() + 8;

		for (j = 0; j < NR_MPX_TEST_FUNCTIONS; j++) {
			if (0 && j != 5) {
				successes++;
				continue;
			}
			dprintf2("starting test %d round %d\n", j, i);
			dprint_context(xsave_test_buf);
			/*
			 * test5 loads an address from the bounds tables.
			 * The load will only complete if 'ptr' matches
			 * the load and the store, so with random addrs,
			 * the odds of this are very small.  Make it
			 * higher by only moving 'ptr' 1/10 times.
			 */
			if (random() % 10 <= 0)
				ptr = get_random_addr() + 8;
			dprintf3("random ptr{%p}\n", ptr);
			dprint_context(xsave_test_buf);
			run_helpers(j, (void *)buf, (void *)buf_shadow, ptr);
			dprint_context(xsave_test_buf);
			if (!compare_context(xsave_test_buf)) {
				insn_test_failed(j, i, buf, buf_shadow, ptr);
				failures++;
				goto exit;
			}
			successes++;
			dprint_context(xsave_test_buf);
			dprintf2("finished test %d round %d\n", j, i);
			dprintf3("\n");
			dprint_context(xsave_test_buf);
		}
	}

exit:
	dprintf2("\nabout to free:\n");
	free(buf);
	dprintf1("successes: %d\n", successes);
	dprintf1(" failures: %d\n", failures);
	dprintf1("    tests: %d\n", total_nr_tests);
	dprintf1(" expected: %jd #BRs\n", num_upper_brs + num_lower_brs);
	dprintf1("      saw: %d #BRs\n", br_count);
	if (failures) {
		eprintf("ERROR: non-zero number of failures\n");
		exit(20);
	}
	if (successes != total_nr_tests) {
		eprintf("ERROR: succeded fewer than number of tries (%d != %d)\n",
				successes, total_nr_tests);
		exit(21);
	}
	if (num_upper_brs + num_lower_brs != br_count) {
		eprintf("ERROR: unexpected number of #BRs: %jd %jd %d\n",
				num_upper_brs, num_lower_brs, br_count);
		eprintf("successes: %d\n", successes);
		eprintf(" failures: %d\n", failures);
		eprintf("    tests: %d\n", total_nr_tests);
		eprintf(" expected: %jd #BRs\n", num_upper_brs + num_lower_brs);
		eprintf("      saw: %d #BRs\n", br_count);
		exit(22);
	}
}

/*
 * This is supposed to SIGSEGV nicely once the kernel
 * can no longer allocate vaddr space.
 */
void exhaust_vaddr_space(void)
{
	unsigned long ptr;
	/* Try to make sure there is no room for a bounds table anywhere */
	unsigned long skip = MPX_BOUNDS_TABLE_SIZE_BYTES - PAGE_SIZE;
#ifdef __i386__
	unsigned long max_vaddr = 0xf7788000UL;
#else
	unsigned long max_vaddr = 0x800000000000UL;
#endif

	dprintf1("%s() start\n", __func__);
	/* do not start at 0, we aren't allowed to map there */
	for (ptr = PAGE_SIZE; ptr < max_vaddr; ptr += skip) {
		void *ptr_ret;
		int ret = madvise((void *)ptr, PAGE_SIZE, MADV_NORMAL);

		if (!ret) {
			dprintf1("madvise() %lx ret: %d\n", ptr, ret);
			continue;
		}
		ptr_ret = mmap((void *)ptr, PAGE_SIZE, PROT_READ|PROT_WRITE,
				MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
		if (ptr_ret != (void *)ptr) {
			perror("mmap");
			dprintf1("mmap(%lx) ret: %p\n", ptr, ptr_ret);
			break;
		}
		if (!(ptr & 0xffffff))
			dprintf1("mmap(%lx) ret: %p\n", ptr, ptr_ret);
	}
	for (ptr = PAGE_SIZE; ptr < max_vaddr; ptr += skip) {
		dprintf2("covering 0x%lx with bounds table entries\n", ptr);
		cover_buf_with_bt_entries((void *)ptr, PAGE_SIZE);
	}
	dprintf1("%s() end\n", __func__);
	printf("done with vaddr space fun\n");
}

void mpx_table_test(void)
{
	printf("starting mpx bounds table test\n");
	run_timed_test(check_mpx_insns_and_tables);
	printf("done with mpx bounds table test\n");
}

int main(int argc, char **argv)
{
	int unmaptest = 0;
	int vaddrexhaust = 0;
	int tabletest = 0;
	int i;

	check_mpx_support();
	mpx_prepare();
	srandom(11179);

	bd_incore();
	init();
	bd_incore();

	trace_me();

	xsave_state((void *)xsave_test_buf, 0x1f);
	if (!compare_context(xsave_test_buf))
		printf("Init failed\n");

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "unmaptest"))
			unmaptest = 1;
		if (!strcmp(argv[i], "vaddrexhaust"))
			vaddrexhaust = 1;
		if (!strcmp(argv[i], "tabletest"))
			tabletest = 1;
	}
	if (!(unmaptest || vaddrexhaust || tabletest)) {
		unmaptest = 1;
		/* vaddrexhaust = 1; */
		tabletest = 1;
	}
	if (unmaptest)
		check_bounds_table_frees();
	if (tabletest)
		mpx_table_test();
	if (vaddrexhaust)
		exhaust_vaddr_space();
	printf("%s completed successfully\n", argv[0]);
	exit(0);
}

#include "mpx-dig.c"
