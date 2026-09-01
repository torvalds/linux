// SPDX-License-Identifier: GPL-2.0
/*
 * Payload for the binfmt_misc 'L' (loader substitution) selftest. It is
 * executed as the MAIN image - a fully native exec - with the registered
 * interpreter substituted for its PT_INTERP, and asserts the native
 * identity from the inside. Exits 0 when every surface checks out.
 *
 * Modes, selected by the orchestrator via the environment:
 *  - default:                full assertions, path-based ones included
 *  - BINFMT_TEST_MEMFD=1:    executed from an inaccessible memfd, skip
 *                            the path-based assertions
 *  - BINFMT_TEST_STATIC=1:   static build; the override was dropped, so
 *                            expect no interpreter at all
 */
#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <unistd.h>

#include "binfmt_misc_common.h"

/* Start of our own mapped image, courtesy of the linker. */
extern const char __ehdr_start[];

/* An image is never this large; used to bracket "within our image". */
#define IMAGE_SPAN (16UL << 20)

static int failed;

static void check(int cond, const char *what)
{
	if (cond)
		return;
	fprintf(stderr, "[payload] FAILED: %s (errno %d)\n", what, errno);
	failed = 1;
}

/* Return whether /proc/self/maps names a path starting with @prefix. */
static int maps_has_prefix(const char *prefix)
{
	char *line = NULL;
	size_t len = 0;
	int found = 0;
	FILE *f;

	f = fopen("/proc/self/maps", "r");
	if (!f)
		return -1;
	while (getline(&line, &len, f) > 0) {
		char *path = strchr(line, '/');

		if (path && !strncmp(path, prefix, strlen(prefix))) {
			found = 1;
			break;
		}
	}
	free(line);
	fclose(f);
	return found;
}

int main(int argc, char *argv[])
{
	const char *binary = getenv("BINFMT_TEST_BINARY");
	const char *interp = getenv("BINFMT_TEST_INTERP");
	int memfd_mode = getenv("BINFMT_TEST_MEMFD") != NULL;
	int static_mode = getenv("BINFMT_TEST_STATIC") != NULL;
	unsigned long self = (unsigned long)__ehdr_start;
	unsigned long base = getauxval(AT_BASE);
	unsigned long phdr = getauxval(AT_PHDR);
	unsigned long entry = getauxval(AT_ENTRY);
	unsigned long start_code, end_code;

	/* The argument vector is exactly what the caller built. */
	check(argc == 3 && !strcmp(argv[0], PAYLOAD_ARGV0) &&
	      !strcmp(argv[1], PAYLOAD_ARG1) && !strcmp(argv[2], PAYLOAD_ARG2),
	      "argv was rewritten");

	/* Native from birth: no execfd, no dispatch marker. */
	check(getauxval(AT_EXECFD) == 0, "AT_EXECFD present");
	check(getauxval(AT_FLAGS) == 0, "AT_FLAGS not native");

	if (static_mode) {
		/* The override was dropped: no interpreter was loaded. */
		check(base == 0, "AT_BASE set for a static payload");
	} else {
		/* A loader is mapped in the interpreter slot, not our image. */
		check(base != 0, "AT_BASE missing");
		check(base < self || base >= self + IMAGE_SPAN,
		      "AT_BASE inside our own image");
	}

	/* We occupy the main-image slot. */
	check(phdr >= self && phdr < self + IMAGE_SPAN,
	      "AT_PHDR outside our image");
	check(entry >= self && entry < self + IMAGE_SPAN,
	      "AT_ENTRY outside our image");

	/* The code statistics markers describe our image, natively placed. */
	if (stat_codes(getpid(), &start_code, &end_code) == 0) {
		check(start_code >= self && start_code < end_code &&
		      end_code < self + IMAGE_SPAN,
		      "stat start_code/end_code not our image");
		check(entry >= start_code && entry < end_code,
		      "AT_ENTRY outside [start_code, end_code)");
	} else {
		check(0, "cannot parse /proc/self/stat");
	}

	if (!memfd_mode && binary) {
		const char *execfn = (const char *)getauxval(AT_EXECFN);
		const char *base_name = strrchr(binary, '/');

		base_name = base_name ? base_name + 1 : binary;

		/* exe link, AT_EXECFN and comm all follow the binary. */
		check(exe_is(binary), "/proc/self/exe");
		check(execfn && !strcmp(execfn, binary), "AT_EXECFN");
		check(comm_is(base_name), "comm");

		/* The running binary is write-denied, natively. */
		check(write_denied(binary), "no ETXTBSY on the binary");
	}

	if (interp) {
		int found = maps_has_prefix(interp);

		if (static_mode)
			/* Nothing was substituted, nothing may be mapped. */
			check(found == 0, "loader mapped for a static payload");
		else
			/* The substituted loader shows under its real path. */
			check(found == 1, "loader path not in /proc/self/maps");
	}

	if (failed)
		return 1;
	printf("[payload] native identity checks out\n");
	return 0;
}
