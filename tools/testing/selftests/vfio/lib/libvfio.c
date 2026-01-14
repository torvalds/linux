// SPDX-License-Identifier: GPL-2.0-only

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <linux/align.h>

#include "../../../kselftest.h"
#include <libvfio.h>

static bool is_bdf(const char *str)
{
	unsigned int s, b, d, f;
	int length, count;

	count = sscanf(str, "%4x:%2x:%2x.%2x%n", &s, &b, &d, &f, &length);
	return count == 4 && length == strlen(str);
}

static char **get_bdfs_cmdline(int *argc, char *argv[], int *nr_bdfs)
{
	int i;

	for (i = *argc - 1; i > 0 && is_bdf(argv[i]); i--)
		continue;

	i++;
	*nr_bdfs = *argc - i;
	*argc -= *nr_bdfs;

	return *nr_bdfs ? &argv[i] : NULL;
}

static char *get_bdf_env(void)
{
	char *bdf;

	bdf = getenv("VFIO_SELFTESTS_BDF");
	if (!bdf)
		return NULL;

	VFIO_ASSERT_TRUE(is_bdf(bdf), "Invalid BDF: %s\n", bdf);
	return bdf;
}

char **vfio_selftests_get_bdfs(int *argc, char *argv[], int *nr_bdfs)
{
	static char *env_bdf;
	char **bdfs;

	bdfs = get_bdfs_cmdline(argc, argv, nr_bdfs);
	if (bdfs)
		return bdfs;

	env_bdf = get_bdf_env();
	if (env_bdf) {
		*nr_bdfs = 1;
		return &env_bdf;
	}

	fprintf(stderr, "Unable to determine which device(s) to use, skipping test.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "To pass the device address via environment variable:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    export VFIO_SELFTESTS_BDF=\"segment:bus:device.function\"\n");
	fprintf(stderr, "    %s [options]\n", argv[0]);
	fprintf(stderr, "\n");
	fprintf(stderr, "To pass the device address(es) via argv:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    %s [options] segment:bus:device.function ...\n", argv[0]);
	fprintf(stderr, "\n");
	exit(KSFT_SKIP);
}

const char *vfio_selftests_get_bdf(int *argc, char *argv[])
{
	int nr_bdfs;

	return vfio_selftests_get_bdfs(argc, argv, &nr_bdfs)[0];
}

void *mmap_reserve(size_t size, size_t align, size_t offset)
{
	void *map_base, *map_align;
	size_t delta;

	VFIO_ASSERT_GT(align, offset);
	delta = align - offset;

	map_base = mmap(NULL, size + align, PROT_NONE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	VFIO_ASSERT_NE(map_base, MAP_FAILED);

	map_align = (void *)(ALIGN((uintptr_t)map_base + delta, align) - delta);

	if (map_align > map_base)
		VFIO_ASSERT_EQ(munmap(map_base, map_align - map_base), 0);

	VFIO_ASSERT_EQ(munmap(map_align + size, map_base + align - map_align), 0);

	return map_align;
}
