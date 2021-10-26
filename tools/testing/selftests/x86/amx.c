// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <x86intrin.h>

#include <linux/futex.h>

#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#ifndef __x86_64__
# error This test is 64-bit only
#endif

#define XSAVE_HDR_OFFSET	512
#define XSAVE_HDR_SIZE		64

struct xsave_buffer {
	union {
		struct {
			char legacy[XSAVE_HDR_OFFSET];
			char header[XSAVE_HDR_SIZE];
			char extended[0];
		};
		char bytes[0];
	};
};

static inline uint64_t xgetbv(uint32_t index)
{
	uint32_t eax, edx;

	asm volatile("xgetbv;"
		     : "=a" (eax), "=d" (edx)
		     : "c" (index));
	return eax + ((uint64_t)edx << 32);
}

static inline void cpuid(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	asm volatile("cpuid;"
		     : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
		     : "0" (*eax), "2" (*ecx));
}

static inline void xsave(struct xsave_buffer *xbuf, uint64_t rfbm)
{
	uint32_t rfbm_lo = rfbm;
	uint32_t rfbm_hi = rfbm >> 32;

	asm volatile("xsave (%%rdi)"
		     : : "D" (xbuf), "a" (rfbm_lo), "d" (rfbm_hi)
		     : "memory");
}

static inline void xrstor(struct xsave_buffer *xbuf, uint64_t rfbm)
{
	uint32_t rfbm_lo = rfbm;
	uint32_t rfbm_hi = rfbm >> 32;

	asm volatile("xrstor (%%rdi)"
		     : : "D" (xbuf), "a" (rfbm_lo), "d" (rfbm_hi));
}

/* err() exits and will not return */
#define fatal_error(msg, ...)	err(1, "[FAIL]\t" msg, ##__VA_ARGS__)

static void sethandler(int sig, void (*handler)(int, siginfo_t *, void *),
		       int flags)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO | flags;
	sigemptyset(&sa.sa_mask);
	if (sigaction(sig, &sa, 0))
		fatal_error("sigaction");
}

static void clearhandler(int sig)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	if (sigaction(sig, &sa, 0))
		fatal_error("sigaction");
}

#define XFEATURE_XTILECFG	17
#define XFEATURE_XTILEDATA	18
#define XFEATURE_MASK_XTILECFG	(1 << XFEATURE_XTILECFG)
#define XFEATURE_MASK_XTILEDATA	(1 << XFEATURE_XTILEDATA)
#define XFEATURE_MASK_XTILE	(XFEATURE_MASK_XTILECFG | XFEATURE_MASK_XTILEDATA)

#define CPUID_LEAF1_ECX_XSAVE_MASK	(1 << 26)
#define CPUID_LEAF1_ECX_OSXSAVE_MASK	(1 << 27)
static inline void check_cpuid_xsave(void)
{
	uint32_t eax, ebx, ecx, edx;

	/*
	 * CPUID.1:ECX.XSAVE[bit 26] enumerates general
	 * support for the XSAVE feature set, including
	 * XGETBV.
	 */
	eax = 1;
	ecx = 0;
	cpuid(&eax, &ebx, &ecx, &edx);
	if (!(ecx & CPUID_LEAF1_ECX_XSAVE_MASK))
		fatal_error("cpuid: no CPU xsave support");
	if (!(ecx & CPUID_LEAF1_ECX_OSXSAVE_MASK))
		fatal_error("cpuid: no OS xsave support");
}

static uint32_t xbuf_size;

static struct {
	uint32_t xbuf_offset;
	uint32_t size;
} xtiledata;

#define CPUID_LEAF_XSTATE		0xd
#define CPUID_SUBLEAF_XSTATE_USER	0x0
#define TILE_CPUID			0x1d
#define TILE_PALETTE_ID			0x1

static void check_cpuid_xtiledata(void)
{
	uint32_t eax, ebx, ecx, edx;

	eax = CPUID_LEAF_XSTATE;
	ecx = CPUID_SUBLEAF_XSTATE_USER;
	cpuid(&eax, &ebx, &ecx, &edx);

	/*
	 * EBX enumerates the size (in bytes) required by the XSAVE
	 * instruction for an XSAVE area containing all the user state
	 * components corresponding to bits currently set in XCR0.
	 *
	 * Stash that off so it can be used to allocate buffers later.
	 */
	xbuf_size = ebx;

	eax = CPUID_LEAF_XSTATE;
	ecx = XFEATURE_XTILEDATA;

	cpuid(&eax, &ebx, &ecx, &edx);
	/*
	 * eax: XTILEDATA state component size
	 * ebx: XTILEDATA state component offset in user buffer
	 */
	if (!eax || !ebx)
		fatal_error("xstate cpuid: invalid tile data size/offset: %d/%d",
				eax, ebx);

	xtiledata.size	      = eax;
	xtiledata.xbuf_offset = ebx;
}

/* The helpers for managing XSAVE buffer and tile states: */

struct xsave_buffer *alloc_xbuf(void)
{
	struct xsave_buffer *xbuf;

	/* XSAVE buffer should be 64B-aligned. */
	xbuf = aligned_alloc(64, xbuf_size);
	if (!xbuf)
		fatal_error("aligned_alloc()");
	return xbuf;
}

static inline void clear_xstate_header(struct xsave_buffer *buffer)
{
	memset(&buffer->header, 0, sizeof(buffer->header));
}

static inline uint64_t get_xstatebv(struct xsave_buffer *buffer)
{
	/* XSTATE_BV is at the beginning of the header: */
	return *(uint64_t *)&buffer->header;
}

static inline void set_xstatebv(struct xsave_buffer *buffer, uint64_t bv)
{
	/* XSTATE_BV is at the beginning of the header: */
	*(uint64_t *)(&buffer->header) = bv;
}

static void set_rand_tiledata(struct xsave_buffer *xbuf)
{
	int *ptr = (int *)&xbuf->bytes[xtiledata.xbuf_offset];
	int data;
	int i;

	/*
	 * Ensure that 'data' is never 0.  This ensures that
	 * the registers are never in their initial configuration
	 * and thus never tracked as being in the init state.
	 */
	data = rand() | 1;

	for (i = 0; i < xtiledata.size / sizeof(int); i++, ptr++)
		*ptr = data;
}

struct xsave_buffer *stashed_xsave;

static void init_stashed_xsave(void)
{
	stashed_xsave = alloc_xbuf();
	if (!stashed_xsave)
		fatal_error("failed to allocate stashed_xsave\n");
	clear_xstate_header(stashed_xsave);
}

static void free_stashed_xsave(void)
{
	free(stashed_xsave);
}

/* See 'struct _fpx_sw_bytes' at sigcontext.h */
#define SW_BYTES_OFFSET		464
/* N.B. The struct's field name varies so read from the offset. */
#define SW_BYTES_BV_OFFSET	(SW_BYTES_OFFSET + 8)

static inline struct _fpx_sw_bytes *get_fpx_sw_bytes(void *buffer)
{
	return (struct _fpx_sw_bytes *)(buffer + SW_BYTES_OFFSET);
}

static inline uint64_t get_fpx_sw_bytes_features(void *buffer)
{
	return *(uint64_t *)(buffer + SW_BYTES_BV_OFFSET);
}

/* Work around printf() being unsafe in signals: */
#define SIGNAL_BUF_LEN 1000
char signal_message_buffer[SIGNAL_BUF_LEN];
void sig_print(char *msg)
{
	int left = SIGNAL_BUF_LEN - strlen(signal_message_buffer) - 1;

	strncat(signal_message_buffer, msg, left);
}

static volatile bool noperm_signaled;
static int noperm_errs;

/*
 * Signal handler for when AMX is used but
 * permission has not been obtained.
 */
static void handle_noperm(int sig, siginfo_t *si, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t *)ctx_void;
	void *xbuf = ctx->uc_mcontext.fpregs;
	struct _fpx_sw_bytes *sw_bytes;
	uint64_t features;

	/* Reset the signal message buffer: */
	signal_message_buffer[0] = '\0';
	sig_print("\tAt SIGILL handler,\n");

	if (si->si_code != ILL_ILLOPC) {
		noperm_errs++;
		sig_print("[FAIL]\tInvalid signal code.\n");
	} else {
		sig_print("[OK]\tValid signal code (ILL_ILLOPC).\n");
	}

	sw_bytes = get_fpx_sw_bytes(xbuf);
	/*
	 * Without permission, the signal XSAVE buffer should not
	 * have room for AMX register state (aka. xtiledata).
	 * Check that the size does not overlap with where xtiledata
	 * will reside.
	 *
	 * This also implies that no state components *PAST*
	 * XTILEDATA (features >=19) can be present in the buffer.
	 */
	if (sw_bytes->xstate_size <= xtiledata.xbuf_offset) {
		sig_print("[OK]\tValid xstate size\n");
	} else {
		noperm_errs++;
		sig_print("[FAIL]\tInvalid xstate size\n");
	}

	features = get_fpx_sw_bytes_features(xbuf);
	/*
	 * Without permission, the XTILEDATA feature
	 * bit should not be set.
	 */
	if ((features & XFEATURE_MASK_XTILEDATA) == 0) {
		sig_print("[OK]\tValid xstate mask\n");
	} else {
		noperm_errs++;
		sig_print("[FAIL]\tInvalid xstate mask\n");
	}

	noperm_signaled = true;
	ctx->uc_mcontext.gregs[REG_RIP] += 3; /* Skip the faulting XRSTOR */
}

/* Return true if XRSTOR is successful; otherwise, false. */
static inline bool xrstor_safe(struct xsave_buffer *xbuf, uint64_t mask)
{
	noperm_signaled = false;
	xrstor(xbuf, mask);

	/* Print any messages produced by the signal code: */
	printf("%s", signal_message_buffer);
	/*
	 * Reset the buffer to make sure any future printing
	 * only outputs new messages:
	 */
	signal_message_buffer[0] = '\0';

	if (noperm_errs)
		fatal_error("saw %d errors in noperm signal handler\n", noperm_errs);

	return !noperm_signaled;
}

/*
 * Use XRSTOR to populate the XTILEDATA registers with
 * random data.
 *
 * Return true if successful; otherwise, false.
 */
static inline bool load_rand_tiledata(struct xsave_buffer *xbuf)
{
	clear_xstate_header(xbuf);
	set_xstatebv(xbuf, XFEATURE_MASK_XTILEDATA);
	set_rand_tiledata(xbuf);
	return xrstor_safe(xbuf, XFEATURE_MASK_XTILEDATA);
}

/* Return XTILEDATA to its initial configuration. */
static inline void init_xtiledata(void)
{
	clear_xstate_header(stashed_xsave);
	xrstor_safe(stashed_xsave, XFEATURE_MASK_XTILEDATA);
}

enum expected_result { FAIL_EXPECTED, SUCCESS_EXPECTED };

/* arch_prctl() and sigaltstack() test */

#define ARCH_GET_XCOMP_PERM	0x1022
#define ARCH_REQ_XCOMP_PERM	0x1023

static void req_xtiledata_perm(void)
{
	syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA);
}

static void validate_req_xcomp_perm(enum expected_result exp)
{
	unsigned long bitmask;
	long rc;

	rc = syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA);
	if (exp == FAIL_EXPECTED) {
		if (rc) {
			printf("[OK]\tARCH_REQ_XCOMP_PERM saw expected failure..\n");
			return;
		}

		fatal_error("ARCH_REQ_XCOMP_PERM saw unexpected success.\n");
	} else if (rc) {
		fatal_error("ARCH_REQ_XCOMP_PERM saw unexpected failure.\n");
	}

	rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_PERM, &bitmask);
	if (rc) {
		fatal_error("prctl(ARCH_GET_XCOMP_PERM) error: %ld", rc);
	} else if (bitmask & XFEATURE_MASK_XTILE) {
		printf("\tARCH_REQ_XCOMP_PERM is successful.\n");
	}
}

static void validate_xcomp_perm(enum expected_result exp)
{
	bool load_success = load_rand_tiledata(stashed_xsave);

	if (exp == FAIL_EXPECTED) {
		if (load_success) {
			noperm_errs++;
			printf("[FAIL]\tLoad tiledata succeeded.\n");
		} else {
			printf("[OK]\tLoad tiledata failed.\n");
		}
	} else if (exp == SUCCESS_EXPECTED) {
		if (load_success) {
			printf("[OK]\tLoad tiledata succeeded.\n");
		} else {
			noperm_errs++;
			printf("[FAIL]\tLoad tiledata failed.\n");
		}
	}
}

#ifndef AT_MINSIGSTKSZ
#  define AT_MINSIGSTKSZ	51
#endif

static void *alloc_altstack(unsigned int size)
{
	void *altstack;

	altstack = mmap(NULL, size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);

	if (altstack == MAP_FAILED)
		fatal_error("mmap() for altstack");

	return altstack;
}

static void setup_altstack(void *addr, unsigned long size, enum expected_result exp)
{
	stack_t ss;
	int rc;

	memset(&ss, 0, sizeof(ss));
	ss.ss_size = size;
	ss.ss_sp = addr;

	rc = sigaltstack(&ss, NULL);

	if (exp == FAIL_EXPECTED) {
		if (rc) {
			printf("[OK]\tsigaltstack() failed.\n");
		} else {
			fatal_error("sigaltstack() succeeded unexpectedly.\n");
		}
	} else if (rc) {
		fatal_error("sigaltstack()");
	}
}

static void test_dynamic_sigaltstack(void)
{
	unsigned int small_size, enough_size;
	unsigned long minsigstksz;
	void *altstack;

	minsigstksz = getauxval(AT_MINSIGSTKSZ);
	printf("\tAT_MINSIGSTKSZ = %lu\n", minsigstksz);
	/*
	 * getauxval() itself can return 0 for failure or
	 * success.  But, in this case, AT_MINSIGSTKSZ
	 * will always return a >=0 value if implemented.
	 * Just check for 0.
	 */
	if (minsigstksz == 0) {
		printf("no support for AT_MINSIGSTKSZ, skipping sigaltstack tests\n");
		return;
	}

	enough_size = minsigstksz * 2;

	altstack = alloc_altstack(enough_size);
	printf("\tAllocate memory for altstack (%u bytes).\n", enough_size);

	/*
	 * Try setup_altstack() with a size which can not fit
	 * XTILEDATA.  ARCH_REQ_XCOMP_PERM should fail.
	 */
	small_size = minsigstksz - xtiledata.size;
	printf("\tAfter sigaltstack() with small size (%u bytes).\n", small_size);
	setup_altstack(altstack, small_size, SUCCESS_EXPECTED);
	validate_req_xcomp_perm(FAIL_EXPECTED);

	/*
	 * Try setup_altstack() with a size derived from
	 * AT_MINSIGSTKSZ.  It should be more than large enough
	 * and thus ARCH_REQ_XCOMP_PERM should succeed.
	 */
	printf("\tAfter sigaltstack() with enough size (%u bytes).\n", enough_size);
	setup_altstack(altstack, enough_size, SUCCESS_EXPECTED);
	validate_req_xcomp_perm(SUCCESS_EXPECTED);

	/*
	 * Try to coerce setup_altstack() to again accept a
	 * too-small altstack.  This ensures that big-enough
	 * sigaltstacks can not shrink to a too-small value
	 * once XTILEDATA permission is established.
	 */
	printf("\tThen, sigaltstack() with small size (%u bytes).\n", small_size);
	setup_altstack(altstack, small_size, FAIL_EXPECTED);
}

static void test_dynamic_state(void)
{
	pid_t parent, child, grandchild;

	parent = fork();
	if (parent < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (parent > 0) {
		int status;
		/* fork() succeeded.  Now in the parent. */

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("arch_prctl test parent exit");
		return;
	}
	/* fork() succeeded.  Now in the child . */

	printf("[RUN]\tCheck ARCH_REQ_XCOMP_PERM around process fork() and sigaltack() test.\n");

	printf("\tFork a child.\n");
	child = fork();
	if (child < 0) {
		fatal_error("fork");
	} else if (child > 0) {
		int status;

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("arch_prctl test child exit");
		_exit(0);
	}

	/*
	 * The permission request should fail without an
	 * XTILEDATA-compatible signal stack
	 */
	printf("\tTest XCOMP_PERM at child.\n");
	validate_xcomp_perm(FAIL_EXPECTED);

	/*
	 * Set up an XTILEDATA-compatible signal stack and
	 * also obtain permission to populate XTILEDATA.
	 */
	printf("\tTest dynamic sigaltstack at child:\n");
	test_dynamic_sigaltstack();

	/* Ensure that XTILEDATA can be populated. */
	printf("\tTest XCOMP_PERM again at child.\n");
	validate_xcomp_perm(SUCCESS_EXPECTED);

	printf("\tFork a grandchild.\n");
	grandchild = fork();
	if (grandchild < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (!grandchild) {
		/* fork() succeeded.  Now in the (grand)child. */
		printf("\tTest XCOMP_PERM at grandchild.\n");

		/*
		 * Ensure that the grandchild inherited
		 * permission and a compatible sigaltstack:
		 */
		validate_xcomp_perm(SUCCESS_EXPECTED);
	} else {
		int status;
		/* fork() succeeded.  Now in the parent. */

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("fork test grandchild");
	}

	_exit(0);
}

/*
 * Save current register state and compare it to @xbuf1.'
 *
 * Returns false if @xbuf1 matches the registers.
 * Returns true  if @xbuf1 differs from the registers.
 */
static inline bool __validate_tiledata_regs(struct xsave_buffer *xbuf1)
{
	struct xsave_buffer *xbuf2;
	int ret;

	xbuf2 = alloc_xbuf();
	if (!xbuf2)
		fatal_error("failed to allocate XSAVE buffer\n");

	xsave(xbuf2, XFEATURE_MASK_XTILEDATA);
	ret = memcmp(&xbuf1->bytes[xtiledata.xbuf_offset],
		     &xbuf2->bytes[xtiledata.xbuf_offset],
		     xtiledata.size);

	free(xbuf2);

	if (ret == 0)
		return false;
	return true;
}

static inline void validate_tiledata_regs_same(struct xsave_buffer *xbuf)
{
	int ret = __validate_tiledata_regs(xbuf);

	if (ret != 0)
		fatal_error("TILEDATA registers changed");
}

static inline void validate_tiledata_regs_changed(struct xsave_buffer *xbuf)
{
	int ret = __validate_tiledata_regs(xbuf);

	if (ret == 0)
		fatal_error("TILEDATA registers did not change");
}

/* tiledata inheritance test */

static void test_fork(void)
{
	pid_t child, grandchild;

	child = fork();
	if (child < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (child > 0) {
		/* fork() succeeded.  Now in the parent. */
		int status;

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("fork test child");
		return;
	}
	/* fork() succeeded.  Now in the child. */
	printf("[RUN]\tCheck tile data inheritance.\n\tBefore fork(), load tiledata\n");

	load_rand_tiledata(stashed_xsave);

	grandchild = fork();
	if (grandchild < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (grandchild > 0) {
		/* fork() succeeded.  Still in the first child. */
		int status;

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("fork test grand child");
		_exit(0);
	}
	/* fork() succeeded.  Now in the (grand)child. */

	/*
	 * TILEDATA registers are not preserved across fork().
	 * Ensure that their value has changed:
	 */
	validate_tiledata_regs_changed(stashed_xsave);

	_exit(0);
}

int main(void)
{
	/* Check hardware availability at first */
	check_cpuid_xsave();
	check_cpuid_xtiledata();

	init_stashed_xsave();
	sethandler(SIGILL, handle_noperm, 0);

	test_dynamic_state();

	/* Request permission for the following tests */
	req_xtiledata_perm();

	test_fork();

	clearhandler(SIGILL);
	free_stashed_xsave();

	return 0;
}
