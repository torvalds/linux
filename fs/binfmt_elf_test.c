// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

static void total_mapping_size_test(struct kunit *test)
{
	struct elf_phdr empty[] = {
		{ .p_type = PT_LOAD, .p_vaddr = 0, .p_memsz = 0, },
		{ .p_type = PT_INTERP, .p_vaddr = 10, .p_memsz = 999999, },
	};
	/*
	 * readelf -lW /bin/mount | grep '^  .*0x0' | awk '{print "\t\t{ .p_type = PT_" \
	 *				$1 ", .p_vaddr = " $3 ", .p_memsz = " $6 ", },"}'
	 */
	struct elf_phdr mount[] = {
		{ .p_type = PT_PHDR, .p_vaddr = 0x00000040, .p_memsz = 0x0002d8, },
		{ .p_type = PT_INTERP, .p_vaddr = 0x00000318, .p_memsz = 0x00001c, },
		{ .p_type = PT_LOAD, .p_vaddr = 0x00000000, .p_memsz = 0x0033a8, },
		{ .p_type = PT_LOAD, .p_vaddr = 0x00004000, .p_memsz = 0x005c91, },
		{ .p_type = PT_LOAD, .p_vaddr = 0x0000a000, .p_memsz = 0x0022f8, },
		{ .p_type = PT_LOAD, .p_vaddr = 0x0000d330, .p_memsz = 0x000d40, },
		{ .p_type = PT_DYNAMIC, .p_vaddr = 0x0000d928, .p_memsz = 0x000200, },
		{ .p_type = PT_NOTE, .p_vaddr = 0x00000338, .p_memsz = 0x000030, },
		{ .p_type = PT_NOTE, .p_vaddr = 0x00000368, .p_memsz = 0x000044, },
		{ .p_type = PT_GNU_PROPERTY, .p_vaddr = 0x00000338, .p_memsz = 0x000030, },
		{ .p_type = PT_GNU_EH_FRAME, .p_vaddr = 0x0000b490, .p_memsz = 0x0001ec, },
		{ .p_type = PT_GNU_STACK, .p_vaddr = 0x00000000, .p_memsz = 0x000000, },
		{ .p_type = PT_GNU_RELRO, .p_vaddr = 0x0000d330, .p_memsz = 0x000cd0, },
	};
	size_t mount_size = 0xE070;
	/* https://lore.kernel.org/linux-fsdevel/YfF18Dy85mCntXrx@fractal.localdomain */
	struct elf_phdr unordered[] = {
		{ .p_type = PT_LOAD, .p_vaddr = 0x00000000, .p_memsz = 0x0033a8, },
		{ .p_type = PT_LOAD, .p_vaddr = 0x0000d330, .p_memsz = 0x000d40, },
		{ .p_type = PT_LOAD, .p_vaddr = 0x00004000, .p_memsz = 0x005c91, },
		{ .p_type = PT_LOAD, .p_vaddr = 0x0000a000, .p_memsz = 0x0022f8, },
	};

	/* No headers, no size. */
	KUNIT_EXPECT_EQ(test, total_mapping_size(NULL, 0), 0);
	KUNIT_EXPECT_EQ(test, total_mapping_size(empty, 0), 0);
	/* Empty headers, no size. */
	KUNIT_EXPECT_EQ(test, total_mapping_size(empty, 1), 0);
	/* No PT_LOAD headers, no size. */
	KUNIT_EXPECT_EQ(test, total_mapping_size(&empty[1], 1), 0);
	/* Empty PT_LOAD and non-PT_LOAD headers, no size. */
	KUNIT_EXPECT_EQ(test, total_mapping_size(empty, 2), 0);

	/* Normal set of PT_LOADS, and expected size. */
	KUNIT_EXPECT_EQ(test, total_mapping_size(mount, ARRAY_SIZE(mount)), mount_size);
	/* Unordered PT_LOADs result in same size. */
	KUNIT_EXPECT_EQ(test, total_mapping_size(unordered, ARRAY_SIZE(unordered)), mount_size);
}

static struct kunit_case binfmt_elf_test_cases[] = {
	KUNIT_CASE(total_mapping_size_test),
	{},
};

static struct kunit_suite binfmt_elf_test_suite = {
	.name = KBUILD_MODNAME,
	.test_cases = binfmt_elf_test_cases,
};

kunit_test_suite(binfmt_elf_test_suite);
