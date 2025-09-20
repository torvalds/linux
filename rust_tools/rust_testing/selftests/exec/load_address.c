// SPDX-License-Identifier: GPL-2.0-only
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "../kselftest.h"

struct Statistics {
	unsigned long long load_address;
	unsigned long long alignment;
	bool interp;
};

int ExtractStatistics(struct dl_phdr_info *info, size_t size, void *data)
{
	struct Statistics *stats = (struct Statistics *) data;
	int i;

	if (info->dlpi_name != NULL && info->dlpi_name[0] != '\0') {
		// Ignore headers from other than the executable.
		return 2;
	}

	stats->load_address = (unsigned long long) info->dlpi_addr;
	stats->alignment = 0;

	for (i = 0; i < info->dlpi_phnum; i++) {
		unsigned long long align;

		if (info->dlpi_phdr[i].p_type == PT_INTERP) {
			stats->interp = true;
			continue;
		}

		if (info->dlpi_phdr[i].p_type != PT_LOAD)
			continue;

		align = info->dlpi_phdr[i].p_align;

		if (align > stats->alignment)
			stats->alignment = align;
	}

	return 1;  // Terminate dl_iterate_phdr.
}

int main(int argc, char **argv)
{
	struct Statistics extracted = { };
	unsigned long long misalign, pow2;
	bool interp_needed;
	char buf[1024];
	FILE *maps;
	int ret;

	ksft_print_header();
	ksft_set_plan(4);

	/* Dump maps file for debugging reference. */
	maps = fopen("/proc/self/maps", "r");
	if (!maps)
		ksft_exit_fail_msg("FAILED: /proc/self/maps: %s\n", strerror(errno));
	while (fgets(buf, sizeof(buf), maps)) {
		ksft_print_msg("%s", buf);
	}
	fclose(maps);

	/* Walk the program headers. */
	ret = dl_iterate_phdr(ExtractStatistics, &extracted);
	if (ret != 1)
		ksft_exit_fail_msg("FAILED: dl_iterate_phdr\n");

	/* Report our findings. */
	ksft_print_msg("load_address=%#llx alignment=%#llx\n",
		       extracted.load_address, extracted.alignment);

	/* If we're named with ".static." we expect no INTERP. */
	interp_needed = strstr(argv[0], ".static.") == NULL;

	/* Were we built as expected? */
	ksft_test_result(interp_needed == extracted.interp,
			 "%s INTERP program header %s\n",
			 interp_needed ? "Wanted" : "Unwanted",
			 extracted.interp ? "seen" : "missing");

	/* Did we find an alignment? */
	ksft_test_result(extracted.alignment != 0,
			 "Alignment%s found\n", extracted.alignment ? "" : " NOT");

	/* Is the alignment sane? */
	pow2 = extracted.alignment & (extracted.alignment - 1);
	ksft_test_result(pow2 == 0,
			 "Alignment is%s a power of 2: %#llx\n",
			 pow2 == 0 ? "" : " NOT", extracted.alignment);

	/* Is the load address aligned? */
	misalign = extracted.load_address & (extracted.alignment - 1);
	ksft_test_result(misalign == 0, "Load Address is %saligned (%#llx)\n",
			 misalign ? "MIS" : "", misalign);

	ksft_finished();
}
