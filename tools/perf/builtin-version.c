// SPDX-License-Identifier: GPL-2.0
#include "builtin.h"
#include "perf.h"
#include "color.h"
#include <tools/config.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <subcmd/parse-options.h>

int version_verbose;

struct version {
	bool	build_options;
};

static struct version version;

static struct option version_options[] = {
	OPT_BOOLEAN(0, "build-options", &version.build_options,
		    "display the build options"),
	OPT_END(),
};

static const char * const version_usage[] = {
	"perf version [<options>]",
	NULL
};

static void on_off_print(const char *status)
{
	printf("[ ");

	if (!strcmp(status, "OFF"))
		color_fprintf(stdout, PERF_COLOR_RED, "%-3s", status);
	else
		color_fprintf(stdout, PERF_COLOR_GREEN, "%-3s", status);

	printf(" ]");
}

static void status_print(const char *name, const char *macro,
			 const char *status)
{
	printf("%22s: ", name);
	on_off_print(status);
	printf("  # %s\n", macro);
}

#define STATUS(__d, __m)				\
do {							\
	if (IS_BUILTIN(__d))				\
		status_print(#__m, #__d, "on");		\
	else						\
		status_print(#__m, #__d, "OFF");	\
} while (0)

static void library_status(void)
{
	STATUS(HAVE_DWARF_SUPPORT, dwarf);
	STATUS(HAVE_DWARF_GETLOCATIONS_SUPPORT, dwarf_getlocations);
	STATUS(HAVE_GLIBC_SUPPORT, glibc);
	STATUS(HAVE_GTK2_SUPPORT, gtk2);
#ifndef HAVE_SYSCALL_TABLE_SUPPORT
	STATUS(HAVE_LIBAUDIT_SUPPORT, libaudit);
#endif
	STATUS(HAVE_SYSCALL_TABLE_SUPPORT, syscall_table);
	STATUS(HAVE_LIBBFD_SUPPORT, libbfd);
	STATUS(HAVE_LIBELF_SUPPORT, libelf);
	STATUS(HAVE_LIBNUMA_SUPPORT, libnuma);
	STATUS(HAVE_LIBNUMA_SUPPORT, numa_num_possible_cpus);
	STATUS(HAVE_LIBPERL_SUPPORT, libperl);
	STATUS(HAVE_LIBPYTHON_SUPPORT, libpython);
	STATUS(HAVE_SLANG_SUPPORT, libslang);
	STATUS(HAVE_LIBCRYPTO_SUPPORT, libcrypto);
	STATUS(HAVE_LIBUNWIND_SUPPORT, libunwind);
	STATUS(HAVE_DWARF_SUPPORT, libdw-dwarf-unwind);
	STATUS(HAVE_ZLIB_SUPPORT, zlib);
	STATUS(HAVE_LZMA_SUPPORT, lzma);
	STATUS(HAVE_AUXTRACE_SUPPORT, get_cpuid);
	STATUS(HAVE_LIBBPF_SUPPORT, bpf);
	STATUS(HAVE_AIO_SUPPORT, aio);
	STATUS(HAVE_ZSTD_SUPPORT, zstd);
}

int cmd_version(int argc, const char **argv)
{
	argc = parse_options(argc, argv, version_options, version_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	printf("perf version %s\n", perf_version_string);

	if (version.build_options || version_verbose == 1)
		library_status();

	return 0;
}
