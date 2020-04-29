// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test the powerpc alignment handler on POWER8/POWER9
 *
 * Copyright (C) 2017 IBM Corporation (Michael Neuling, Andrew Donnellan)
 */

/*
 * This selftest exercises the powerpc alignment fault handler.
 *
 * We create two sets of source and destination buffers, one in regular memory,
 * the other cache-inhibited (we use /dev/fb0 for this).
 *
 * We initialise the source buffers, then use whichever set of load/store
 * instructions is under test to copy bytes from the source buffers to the
 * destination buffers. For the regular buffers, these instructions will
 * execute normally. For the cache-inhibited buffers, these instructions
 * will trap and cause an alignment fault, and the alignment fault handler
 * will emulate the particular instruction under test. We then compare the
 * destination buffers to ensure that the native and emulated cases give the
 * same result.
 *
 * TODO:
 *   - Any FIXMEs below
 *   - Test VSX regs < 32 and > 32
 *   - Test all loads and stores
 *   - Check update forms do update register
 *   - Test alignment faults over page boundary
 *
 * Some old binutils may not support all the instructions.
 */


#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <setjmp.h>
#include <signal.h>

#include <asm/cputable.h>

#include "utils.h"

int bufsize;
int debug;
int testing;
volatile int gotsig;

void sighandler(int sig, siginfo_t *info, void *ctx)
{
	ucontext_t *ucp = ctx;

	if (!testing) {
		signal(sig, SIG_DFL);
		kill(0, sig);
	}
	gotsig = sig;
#ifdef __powerpc64__
	ucp->uc_mcontext.gp_regs[PT_NIP] += 4;
#else
	ucp->uc_mcontext.uc_regs->gregs[PT_NIP] += 4;
#endif
}

#define XFORM(reg, n)  " " #reg " ,%"#n",%2 ;"
#define DFORM(reg, n)  " " #reg " ,0(%"#n") ;"

#define TEST(name, ld_op, st_op, form, ld_reg, st_reg)		\
	void test_##name(char *s, char *d)			\
	{							\
		asm volatile(					\
			#ld_op form(ld_reg, 0)			\
			#st_op form(st_reg, 1)			\
			:: "r"(s), "r"(d), "r"(0)		\
			: "memory", "vs0", "vs32", "r31");	\
	}							\
	rc |= do_test(#name, test_##name)

#define LOAD_VSX_XFORM_TEST(op) TEST(op, op, stxvd2x, XFORM, 32, 32)
#define STORE_VSX_XFORM_TEST(op) TEST(op, lxvd2x, op, XFORM, 32, 32)
#define LOAD_VSX_DFORM_TEST(op) TEST(op, op, stxv, DFORM, 32, 32)
#define STORE_VSX_DFORM_TEST(op) TEST(op, lxv, op, DFORM, 32, 32)
#define LOAD_VMX_XFORM_TEST(op) TEST(op, op, stxvd2x, XFORM, 0, 32)
#define STORE_VMX_XFORM_TEST(op) TEST(op, lxvd2x, op, XFORM, 32, 0)
#define LOAD_VMX_DFORM_TEST(op) TEST(op, op, stxv, DFORM, 0, 32)
#define STORE_VMX_DFORM_TEST(op) TEST(op, lxv, op, DFORM, 32, 0)

#define LOAD_XFORM_TEST(op) TEST(op, op, stdx, XFORM, 31, 31)
#define STORE_XFORM_TEST(op) TEST(op, ldx, op, XFORM, 31, 31)
#define LOAD_DFORM_TEST(op) TEST(op, op, std, DFORM, 31, 31)
#define STORE_DFORM_TEST(op) TEST(op, ld, op, DFORM, 31, 31)

#define LOAD_FLOAT_DFORM_TEST(op)  TEST(op, op, stfd, DFORM, 0, 0)
#define STORE_FLOAT_DFORM_TEST(op) TEST(op, lfd, op, DFORM, 0, 0)
#define LOAD_FLOAT_XFORM_TEST(op)  TEST(op, op, stfdx, XFORM, 0, 0)
#define STORE_FLOAT_XFORM_TEST(op) TEST(op, lfdx, op, XFORM, 0, 0)


/* FIXME: Unimplemented tests: */
// STORE_DFORM_TEST(stq)   /* FIXME: need two registers for quad */
// STORE_DFORM_TEST(stswi) /* FIXME: string instruction */

// STORE_XFORM_TEST(stwat) /* AMO can't emulate or run on CI */
// STORE_XFORM_TEST(stdat) /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */


/* preload byte by byte */
void preload_data(void *dst, int offset, int width)
{
	char *c = dst;
	int i;

	c += offset;

	for (i = 0 ; i < width ; i++)
		c[i] = i;
}

int test_memcpy(void *dst, void *src, int size, int offset,
		void (*test_func)(char *, char *))
{
	char *s, *d;

	s = src;
	s += offset;
	d = dst;
	d += offset;

	assert(size == 16);
	gotsig = 0;
	testing = 1;

	test_func(s, d); /* run the actual test */

	testing = 0;
	if (gotsig) {
		if (debug)
			printf("  Got signal %i\n", gotsig);
		return 1;
	}
	return 0;
}

void dumpdata(char *s1, char *s2, int n, char *test_name)
{
	int i;

	printf("  %s: unexpected result:\n", test_name);
	printf("    mem:");
	for (i = 0; i < n; i++)
		printf(" %02x", s1[i]);
	printf("\n");
	printf("    ci: ");
	for (i = 0; i < n; i++)
		printf(" %02x", s2[i]);
	printf("\n");
}

int test_memcmp(void *s1, void *s2, int n, int offset, char *test_name)
{
	char *s1c, *s2c;

	s1c = s1;
	s1c += offset;
	s2c = s2;
	s2c += offset;

	if (memcmp(s1c, s2c, n)) {
		if (debug) {
			printf("\n  Compare failed. Offset:%i length:%i\n",
			       offset, n);
			dumpdata(s1c, s2c, n, test_name);
		}
		return 1;
	}
	return 0;
}

/*
 * Do two memcpy tests using the same instructions. One cachable
 * memory and the other doesn't.
 */
int do_test(char *test_name, void (*test_func)(char *, char *))
{
	int offset, width, fd, rc, r;
	void *mem0, *mem1, *ci0, *ci1;

	printf("\tDoing %s:\t", test_name);

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) {
		printf("\n");
		perror("Can't open /dev/fb0 now?");
		return 1;
	}

	ci0 = mmap(NULL, bufsize, PROT_WRITE, MAP_SHARED,
		   fd, 0x0);
	ci1 = mmap(NULL, bufsize, PROT_WRITE, MAP_SHARED,
		   fd, bufsize);
	if ((ci0 == MAP_FAILED) || (ci1 == MAP_FAILED)) {
		printf("\n");
		perror("mmap failed");
		SKIP_IF(1);
	}

	rc = posix_memalign(&mem0, bufsize, bufsize);
	if (rc) {
		printf("\n");
		return rc;
	}

	rc = posix_memalign(&mem1, bufsize, bufsize);
	if (rc) {
		printf("\n");
		free(mem0);
		return rc;
	}

	rc = 0;
	/* offset = 0 no alignment fault, so skip */
	for (offset = 1; offset < 16; offset++) {
		width = 16; /* vsx == 16 bytes */
		r = 0;

		/* load pattern into memory byte by byte */
		preload_data(ci0, offset, width);
		preload_data(mem0, offset, width); // FIXME: remove??
		memcpy(ci0, mem0, bufsize);
		memcpy(ci1, mem1, bufsize); /* initialise output to the same */

		/* sanity check */
		test_memcmp(mem0, ci0, width, offset, test_name);

		r |= test_memcpy(ci1,  ci0,  width, offset, test_func);
		r |= test_memcpy(mem1, mem0, width, offset, test_func);
		if (r && !debug) {
			printf("FAILED: Got signal");
			rc = 1;
			break;
		}

		r |= test_memcmp(mem1, ci1, width, offset, test_name);
		if (r && !debug) {
			printf("FAILED: Wrong Data");
			rc = 1;
			break;
		}
	}

	if (rc == 0)
		printf("PASSED");

	printf("\n");

	munmap(ci0, bufsize);
	munmap(ci1, bufsize);
	free(mem0);
	free(mem1);
	close(fd);

	return rc;
}

static bool can_open_fb0(void)
{
	int fd;

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0)
		return false;

	close(fd);
	return true;
}

int test_alignment_handler_vsx_206(void)
{
	int rc = 0;

	SKIP_IF(!can_open_fb0());
	SKIP_IF(!have_hwcap(PPC_FEATURE_ARCH_2_06));

	printf("VSX: 2.06B\n");
	LOAD_VSX_XFORM_TEST(lxvd2x);
	LOAD_VSX_XFORM_TEST(lxvw4x);
	LOAD_VSX_XFORM_TEST(lxsdx);
	LOAD_VSX_XFORM_TEST(lxvdsx);
	STORE_VSX_XFORM_TEST(stxvd2x);
	STORE_VSX_XFORM_TEST(stxvw4x);
	STORE_VSX_XFORM_TEST(stxsdx);
	return rc;
}

int test_alignment_handler_vsx_207(void)
{
	int rc = 0;

	SKIP_IF(!can_open_fb0());
	SKIP_IF(!have_hwcap2(PPC_FEATURE2_ARCH_2_07));

	printf("VSX: 2.07B\n");
	LOAD_VSX_XFORM_TEST(lxsspx);
	LOAD_VSX_XFORM_TEST(lxsiwax);
	LOAD_VSX_XFORM_TEST(lxsiwzx);
	STORE_VSX_XFORM_TEST(stxsspx);
	STORE_VSX_XFORM_TEST(stxsiwx);
	return rc;
}

int test_alignment_handler_vsx_300(void)
{
	int rc = 0;

	SKIP_IF(!can_open_fb0());

	SKIP_IF(!have_hwcap2(PPC_FEATURE2_ARCH_3_00));
	printf("VSX: 3.00B\n");
	LOAD_VMX_DFORM_TEST(lxsd);
	LOAD_VSX_XFORM_TEST(lxsibzx);
	LOAD_VSX_XFORM_TEST(lxsihzx);
	LOAD_VMX_DFORM_TEST(lxssp);
	LOAD_VSX_DFORM_TEST(lxv);
	LOAD_VSX_XFORM_TEST(lxvb16x);
	LOAD_VSX_XFORM_TEST(lxvh8x);
	LOAD_VSX_XFORM_TEST(lxvx);
	LOAD_VSX_XFORM_TEST(lxvwsx);
	LOAD_VSX_XFORM_TEST(lxvl);
	LOAD_VSX_XFORM_TEST(lxvll);
	STORE_VMX_DFORM_TEST(stxsd);
	STORE_VSX_XFORM_TEST(stxsibx);
	STORE_VSX_XFORM_TEST(stxsihx);
	STORE_VMX_DFORM_TEST(stxssp);
	STORE_VSX_DFORM_TEST(stxv);
	STORE_VSX_XFORM_TEST(stxvb16x);
	STORE_VSX_XFORM_TEST(stxvh8x);
	STORE_VSX_XFORM_TEST(stxvx);
	STORE_VSX_XFORM_TEST(stxvl);
	STORE_VSX_XFORM_TEST(stxvll);
	return rc;
}

int test_alignment_handler_integer(void)
{
	int rc = 0;

	SKIP_IF(!can_open_fb0());

	printf("Integer\n");
	LOAD_DFORM_TEST(lbz);
	LOAD_DFORM_TEST(lbzu);
	LOAD_XFORM_TEST(lbzx);
	LOAD_XFORM_TEST(lbzux);
	LOAD_DFORM_TEST(lhz);
	LOAD_DFORM_TEST(lhzu);
	LOAD_XFORM_TEST(lhzx);
	LOAD_XFORM_TEST(lhzux);
	LOAD_DFORM_TEST(lha);
	LOAD_DFORM_TEST(lhau);
	LOAD_XFORM_TEST(lhax);
	LOAD_XFORM_TEST(lhaux);
	LOAD_XFORM_TEST(lhbrx);
	LOAD_DFORM_TEST(lwz);
	LOAD_DFORM_TEST(lwzu);
	LOAD_XFORM_TEST(lwzx);
	LOAD_XFORM_TEST(lwzux);
	LOAD_DFORM_TEST(lwa);
	LOAD_XFORM_TEST(lwax);
	LOAD_XFORM_TEST(lwaux);
	LOAD_XFORM_TEST(lwbrx);
	LOAD_DFORM_TEST(ld);
	LOAD_DFORM_TEST(ldu);
	LOAD_XFORM_TEST(ldx);
	LOAD_XFORM_TEST(ldux);
	LOAD_DFORM_TEST(lmw);
	STORE_DFORM_TEST(stb);
	STORE_XFORM_TEST(stbx);
	STORE_DFORM_TEST(stbu);
	STORE_XFORM_TEST(stbux);
	STORE_DFORM_TEST(sth);
	STORE_XFORM_TEST(sthx);
	STORE_DFORM_TEST(sthu);
	STORE_XFORM_TEST(sthux);
	STORE_XFORM_TEST(sthbrx);
	STORE_DFORM_TEST(stw);
	STORE_XFORM_TEST(stwx);
	STORE_DFORM_TEST(stwu);
	STORE_XFORM_TEST(stwux);
	STORE_XFORM_TEST(stwbrx);
	STORE_DFORM_TEST(std);
	STORE_XFORM_TEST(stdx);
	STORE_DFORM_TEST(stdu);
	STORE_XFORM_TEST(stdux);
	STORE_DFORM_TEST(stmw);

	return rc;
}

int test_alignment_handler_integer_206(void)
{
	int rc = 0;

	SKIP_IF(!can_open_fb0());
	SKIP_IF(!have_hwcap(PPC_FEATURE_ARCH_2_06));

	printf("Integer: 2.06\n");

	LOAD_XFORM_TEST(ldbrx);
	STORE_XFORM_TEST(stdbrx);

	return rc;
}

int test_alignment_handler_vmx(void)
{
	int rc = 0;

	SKIP_IF(!can_open_fb0());
	SKIP_IF(!have_hwcap(PPC_FEATURE_HAS_ALTIVEC));

	printf("VMX\n");
	LOAD_VMX_XFORM_TEST(lvx);

	/*
	 * FIXME: These loads only load part of the register, so our
	 * testing method doesn't work. Also they don't take alignment
	 * faults, so it's kinda pointless anyway
	 *
	 LOAD_VMX_XFORM_TEST(lvebx)
	 LOAD_VMX_XFORM_TEST(lvehx)
	 LOAD_VMX_XFORM_TEST(lvewx)
	 LOAD_VMX_XFORM_TEST(lvxl)
	*/
	STORE_VMX_XFORM_TEST(stvx);
	STORE_VMX_XFORM_TEST(stvebx);
	STORE_VMX_XFORM_TEST(stvehx);
	STORE_VMX_XFORM_TEST(stvewx);
	STORE_VMX_XFORM_TEST(stvxl);
	return rc;
}

int test_alignment_handler_fp(void)
{
	int rc = 0;

	SKIP_IF(!can_open_fb0());

	printf("Floating point\n");
	LOAD_FLOAT_DFORM_TEST(lfd);
	LOAD_FLOAT_XFORM_TEST(lfdx);
	LOAD_FLOAT_DFORM_TEST(lfdu);
	LOAD_FLOAT_XFORM_TEST(lfdux);
	LOAD_FLOAT_DFORM_TEST(lfs);
	LOAD_FLOAT_XFORM_TEST(lfsx);
	LOAD_FLOAT_DFORM_TEST(lfsu);
	LOAD_FLOAT_XFORM_TEST(lfsux);
	STORE_FLOAT_DFORM_TEST(stfd);
	STORE_FLOAT_XFORM_TEST(stfdx);
	STORE_FLOAT_DFORM_TEST(stfdu);
	STORE_FLOAT_XFORM_TEST(stfdux);
	STORE_FLOAT_DFORM_TEST(stfs);
	STORE_FLOAT_XFORM_TEST(stfsx);
	STORE_FLOAT_DFORM_TEST(stfsu);
	STORE_FLOAT_XFORM_TEST(stfsux);
	STORE_FLOAT_XFORM_TEST(stfiwx);

	return rc;
}

int test_alignment_handler_fp_205(void)
{
	int rc = 0;

	SKIP_IF(!can_open_fb0());
	SKIP_IF(!have_hwcap(PPC_FEATURE_ARCH_2_05));

	printf("Floating point: 2.05\n");

	LOAD_FLOAT_DFORM_TEST(lfdp);
	LOAD_FLOAT_XFORM_TEST(lfdpx);
	LOAD_FLOAT_XFORM_TEST(lfiwax);
	STORE_FLOAT_DFORM_TEST(stfdp);
	STORE_FLOAT_XFORM_TEST(stfdpx);

	return rc;
}

int test_alignment_handler_fp_206(void)
{
	int rc = 0;

	SKIP_IF(!can_open_fb0());
	SKIP_IF(!have_hwcap(PPC_FEATURE_ARCH_2_06));

	printf("Floating point: 2.06\n");

	LOAD_FLOAT_XFORM_TEST(lfiwzx);

	return rc;
}

void usage(char *prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("  -d	Enable debug error output\n");
	printf("\n");
	printf("This test requires a POWER8 or POWER9 CPU and a usable ");
	printf("framebuffer at /dev/fb0.\n");
}

int main(int argc, char *argv[])
{

	struct sigaction sa;
	int rc = 0;
	int option = 0;

	while ((option = getopt(argc, argv, "d")) != -1) {
		switch (option) {
		case 'd':
			debug++;
			break;
		default:
			usage(argv[0]);
			exit(1);
		}
	}

	bufsize = getpagesize();

	sa.sa_sigaction = sighandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGSEGV, &sa, NULL) == -1
	    || sigaction(SIGBUS, &sa, NULL) == -1
	    || sigaction(SIGILL, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	rc |= test_harness(test_alignment_handler_vsx_206,
			   "test_alignment_handler_vsx_206");
	rc |= test_harness(test_alignment_handler_vsx_207,
			   "test_alignment_handler_vsx_207");
	rc |= test_harness(test_alignment_handler_vsx_300,
			   "test_alignment_handler_vsx_300");
	rc |= test_harness(test_alignment_handler_integer,
			   "test_alignment_handler_integer");
	rc |= test_harness(test_alignment_handler_integer_206,
			   "test_alignment_handler_integer_206");
	rc |= test_harness(test_alignment_handler_vmx,
			   "test_alignment_handler_vmx");
	rc |= test_harness(test_alignment_handler_fp,
			   "test_alignment_handler_fp");
	rc |= test_harness(test_alignment_handler_fp_205,
			   "test_alignment_handler_fp_205");
	rc |= test_harness(test_alignment_handler_fp_206,
			   "test_alignment_handler_fp_206");
	return rc;
}
