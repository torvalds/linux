/* $OpenBSD: xstate.c,v 1.2 2025/05/22 04:34:18 bluhm Exp $ */

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <err.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct cpuid {
	uint32_t a, b, c, d;
};

struct xstate {
	struct {
		uint8_t		buf[1024];
		uint32_t	size;
	} area;

	struct {
		uint32_t	supported;
		uint32_t	offset;
		uint32_t	size;
	} components[3];
#define XSTATE_COMPONENT_X87		0
#define XSTATE_COMPONENT_SSE		1
#define XSTATE_COMPONENT_AVX		2
};

struct u128 {
	uint64_t v[2];
} __attribute__((packed));

struct ymm {
	struct u128 xmm;
	struct u128 ymm;
} __attribute__((packed));

extern void ymm_write(void);
extern void ymm_read(struct ymm[16]);

static inline void
cpuid(uint32_t leaf, uint32_t subleaf, struct cpuid *out)
{
	__asm__("cpuid"
	    : "=a" (out->a), "=b" (out->b), "=c" (out->c), "=d" (out->d)
	    : "a" (leaf), "c" (subleaf));
}

static int
xstate_init(struct xstate *xstate, pid_t pid)
{
#define CPUID_01_C_XSAVE_MASK		(1 << 26)
#define XCR0_XMM_MASK			(1 << 1)
#define XCR0_YMM_MASK			(1 << 2)

	struct cpuid leaf;
	struct ptrace_xstate_info info;

	cpuid(0x1, 0, &leaf);
	if ((leaf.c & CPUID_01_C_XSAVE_MASK) == 0) {
		printf("SKIPPED: XSAVE not enumerated");
		return 1;
	}

	memset(xstate, 0, sizeof(*xstate));

	if (ptrace(PT_GETXSTATE_INFO, pid,
	    (caddr_t)&info, sizeof(info)) == -1)
		err(1, "ptrace: PT_GETXSTATE_INFO");
	if (info.xsave_len > sizeof(xstate->area.buf))
		errx(1, "xstate buffer too small");
	xstate->area.size = info.xsave_len;

	if ((info.xsave_mask & XCR0_XMM_MASK) == 0 ||
	    (info.xsave_mask & XCR0_YMM_MASK) == 0) {
		printf("SKIPPED: SSE/AVX disabled in XCR0\n");
		return 1;
	}

	xstate->components[XSTATE_COMPONENT_SSE].supported = 1;
	/* Part of legacy region in XSAVE area. */
	xstate->components[XSTATE_COMPONENT_SSE].offset = 160;
	xstate->components[XSTATE_COMPONENT_SSE].size = 256;

	cpuid(0xd, XSTATE_COMPONENT_AVX, &leaf);
	xstate->components[XSTATE_COMPONENT_AVX].supported = 1;
	xstate->components[XSTATE_COMPONENT_AVX].offset = leaf.b;
	xstate->components[XSTATE_COMPONENT_AVX].size = leaf.a;

	return 0;
}

static void
xstate_ymm_read(struct xstate *xstate, int regno, struct ymm *rd)
{
	struct u128 *xmm = (struct u128 *)(xstate->area.buf +
	    xstate->components[XSTATE_COMPONENT_SSE].offset);
	struct u128 *ymm = (struct u128 *)(xstate->area.buf +
	    xstate->components[XSTATE_COMPONENT_AVX].offset);

	rd->xmm = xmm[regno];
	rd->ymm = ymm[regno];
}

static void
xstate_ymm_write(struct xstate *xstate, int regno, struct ymm *wr)
{
	struct u128 *xmm = (struct u128 *)(xstate->area.buf +
	    xstate->components[XSTATE_COMPONENT_SSE].offset);
	struct u128 *ymm = (struct u128 *)(xstate->area.buf +
	    xstate->components[XSTATE_COMPONENT_AVX].offset);

	xmm[regno] = wr->xmm;
	ymm[regno] = wr->ymm;
}

static void
wait_until_stopped(pid_t pid)
{
	int status;

	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");
	if (!WIFSTOPPED(status))
		errx(1, "expected traced process to be stopped");
}

static int
check_ymm(const struct ymm ymm[16])
{
	int error = 0;
	int i;

	for (i = 0; i < 16; i++) {
		struct ymm exp;

		memset(&exp, (i << 4) | i, 32);
		if (memcmp(&exp, &ymm[i], 32) == 0)
			continue;

		warnx("ymm%d: expected %016llx%016llx%016llx%016llx,"
		    " got %016llx%016llx%016llx%016llx", i,
		    exp.ymm.v[1], exp.ymm.v[0],
		    exp.xmm.v[1], exp.xmm.v[0],
		    ymm[i].ymm.v[1], ymm[i].ymm.v[0],
		    ymm[i].xmm.v[1], ymm[i].xmm.v[0]);
		error = 1;
	}

	return error;
}

static int
test_ymm_get(struct xstate *xstate)
{
	struct ymm ymm[16];
	pid_t pid;
	int i;

	pid = fork();
	if (pid == 0) {
		ptrace(PT_TRACE_ME, 0, 0, 0);
		ymm_write();
		raise(SIGSTOP);
		/* UNREACHABLE */
	}

	wait_until_stopped(pid);

	if (xstate_init(xstate, pid))
		return 0;

	if (ptrace(PT_GETXSTATE, pid,
	    xstate->area.buf, xstate->area.size) == -1)
		err(1, "ptrace: PT_GETXSTATE");
	for (i = 0; i < 16; i++)
		xstate_ymm_read(xstate, i, &ymm[i]);
	return check_ymm(ymm);
}

static int
test_ymm_set(struct xstate *xstate)
{
	pid_t pid;
	int i, status;

	pid = fork();
	if (pid == 0) {
		struct ymm ymm[16];

		ptrace(PT_TRACE_ME, 0, 0, 0);
		raise(SIGSTOP);
		ymm_read(ymm);
		_exit(check_ymm(ymm));
	}

	wait_until_stopped(pid);

	if (xstate_init(xstate, pid))
		return 0;

	if (ptrace(PT_GETXSTATE, pid,
	    xstate->area.buf, xstate->area.size) == -1)
		err(1, "ptrace: PT_GETXSTATE");
	for (i = 0; i < 16; i++) {
		struct ymm ymm;

		memset(&ymm, (i << 4) | i, 32);
		xstate_ymm_write(xstate, i, &ymm);
	}

	if (ptrace(PT_SETXSTATE, pid,
	    xstate->area.buf, xstate->area.size) == -1)
		err(1, "ptrace: PT_SETXSTATE");

	if (ptrace(PT_CONTINUE, pid, (caddr_t)1, 0) == -1)
		err(1, "ptrace: PT_CONTINUE");
	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
}

static void __attribute__((noreturn))
usage(void)
{
	fprintf(stderr, "usage: xstate test-case\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct {
		const char	*name;
		int		 (*test)(struct xstate *);
	} tests[] = {
		{ "xstate-ymm-get",	test_ymm_get },
		{ "xstate-ymm-set",	test_ymm_set },
	};
	struct xstate xstate;
	unsigned int i;

	if (argc != 2)
		usage();

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		if (strcmp(argv[1], tests[i].name) == 0)
			return tests[i].test(&xstate);
	}

	warnx("no such test case");
	return 1;
}
