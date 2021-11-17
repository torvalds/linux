// SPDX-License-Identifier: GPL-2.0-only
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <link.h>
#include <stdio.h>
#include <stdlib.h>

struct Statistics {
	unsigned long long load_address;
	unsigned long long alignment;
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
		if (info->dlpi_phdr[i].p_type != PT_LOAD)
			continue;

		if (info->dlpi_phdr[i].p_align > stats->alignment)
			stats->alignment = info->dlpi_phdr[i].p_align;
	}

	return 1;  // Terminate dl_iterate_phdr.
}

int main(int argc, char **argv)
{
	struct Statistics extracted;
	unsigned long long misalign;
	int ret;

	ret = dl_iterate_phdr(ExtractStatistics, &extracted);
	if (ret != 1) {
		fprintf(stderr, "FAILED\n");
		return 1;
	}

	if (extracted.alignment == 0) {
		fprintf(stderr, "No alignment found\n");
		return 1;
	} else if (extracted.alignment & (extracted.alignment - 1)) {
		fprintf(stderr, "Alignment is not a power of 2\n");
		return 1;
	}

	misalign = extracted.load_address & (extracted.alignment - 1);
	if (misalign) {
		printf("alignment = %llu, load_address = %llu\n",
			extracted.alignment, extracted.load_address);
		fprintf(stderr, "FAILED\n");
		return 1;
	}

	fprintf(stderr, "PASS\n");
	return 0;
}
