/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KFuzzTest input parsing and validation.
 *
 * Copyright 2025 Google LLC
 */
#include <linux/kfuzztest.h>
#include <linux/kasan.h>

/*
 * Enforce a fixed struct size to ensure a consistent stride when iterating over
 * the array of these structs in the dedicated ELF section.
 */
static_assert(sizeof(struct kfuzztest_target) == 32, "struct kfuzztest_target should have size 32");
static_assert(sizeof(struct kfuzztest_constraint) == 64, "struct kfuzztest_constraint should have size 64");
static_assert(sizeof(struct kfuzztest_annotation) == 32, "struct kfuzztest_annotation should have size 32");

static int kfuzztest_relocate_v0(struct reloc_region_array *regions, struct reloc_table *rt, void *payload_start,
				 void *payload_end)
{
	struct reloc_region reg, src, dst;
	void *poison_start, *poison_end;
	uintptr_t *ptr_location;
	struct reloc_entry re;
	size_t i;

	/* Patch pointers. */
	for (i = 0; i < rt->num_entries; i++) {
		re = rt->entries[i];
		src = regions->regions[re.region_id];
		ptr_location = (uintptr_t *)((char *)payload_start + src.offset + re.region_offset);
		if (re.value == KFUZZTEST_REGIONID_NULL)
			*ptr_location = (uintptr_t)NULL;
		else if (re.value < regions->num_regions) {
			dst = regions->regions[re.value];
			*ptr_location = (uintptr_t)((char *)payload_start + dst.offset);
		} else
			return -EINVAL;
	}

	/* Poison the padding between regions. */
	for (i = 0; i < regions->num_regions; i++) {
		reg = regions->regions[i];

		/* Points to the beginning of the inter-region padding */
		poison_start = payload_start + reg.offset + reg.size;
		if (i < regions->num_regions - 1)
			poison_end = payload_start + regions->regions[i + 1].offset;
		else
			poison_end = payload_end;

		if ((char *)poison_end > (char *)payload_end)
			return -EINVAL;

		kasan_poison_range(poison_start, poison_end - poison_start);
	}

	/* Poison the padded area preceding the payload. */
	kasan_poison_range((char *)payload_start - rt->padding_size, rt->padding_size);
	return 0;
}

static bool kfuzztest_input_is_valid(struct reloc_region_array *regions, struct reloc_table *rt, void *payload_start,
				     void *payload_end)
{
	size_t payload_size = (char *)payload_end - (char *)payload_start;
	struct reloc_region reg, next_reg;
	size_t usable_payload_size;
	uint32_t region_end_offset;
	struct reloc_entry reloc;
	uint32_t i;

	if ((char *)payload_start > (char *)payload_end)
		return false;
	if (payload_size < KFUZZTEST_POISON_SIZE)
		return false;
	usable_payload_size = payload_size - KFUZZTEST_POISON_SIZE;

	for (i = 0; i < regions->num_regions; i++) {
		reg = regions->regions[i];
		if (check_add_overflow(reg.offset, reg.size, &region_end_offset))
			return false;
		if ((size_t)region_end_offset > usable_payload_size)
			return false;

		if (i < regions->num_regions - 1) {
			next_reg = regions->regions[i + 1];
			if (reg.offset > next_reg.offset)
				return false;
			/*
			 * Enforce the minimum poisonable gap between
			 * consecutive regions.
			 */
			if (reg.offset + reg.size + KFUZZTEST_POISON_SIZE > next_reg.offset)
				return false;
		}
	}

	if (rt->padding_size < KFUZZTEST_POISON_SIZE) {
		pr_info("validation failed because rt->padding_size = %u", rt->padding_size);
		return false;
	}

	for (i = 0; i < rt->num_entries; i++) {
		reloc = rt->entries[i];
		if (reloc.region_id >= regions->num_regions)
			return false;
		if (reloc.value != KFUZZTEST_REGIONID_NULL && reloc.value >= regions->num_regions)
			return false;

		reg = regions->regions[reloc.region_id];
		if (reloc.region_offset % (sizeof(uintptr_t)) || reloc.region_offset + sizeof(uintptr_t) > reg.size)
			return false;
	}

	return true;
}

static int kfuzztest_parse_input_v0(void *input, size_t input_size, struct reloc_region_array **ret_regions,
				    struct reloc_table **ret_reloc_table, void **ret_payload_start,
				    void **ret_payload_end)
{
	size_t reloc_entries_size, reloc_regions_size;
	size_t reloc_table_size, regions_size;
	struct reloc_region_array *regions;
	void *payload_end, *payload_start;
	struct reloc_table *rt;
	size_t curr_offset = 0;

	if (input_size < sizeof(struct reloc_region_array) + sizeof(struct reloc_table))
		return -EINVAL;

	regions = input;
	if (check_mul_overflow(regions->num_regions, sizeof(struct reloc_region), &reloc_regions_size))
		return -EINVAL;
	if (check_add_overflow(sizeof(*regions), reloc_regions_size, &regions_size))
		return -EINVAL;

	curr_offset = regions_size;
	if (curr_offset > input_size)
		return -EINVAL;
	if (input_size - curr_offset < sizeof(struct reloc_table))
		return -EINVAL;

	rt = (struct reloc_table *)((char *)input + curr_offset);

	if (check_mul_overflow((size_t)rt->num_entries, sizeof(struct reloc_entry), &reloc_entries_size))
		return -EINVAL;
	if (check_add_overflow(sizeof(*rt), reloc_entries_size, &reloc_table_size))
		return -EINVAL;
	if (check_add_overflow(reloc_table_size, rt->padding_size, &reloc_table_size))
		return -EINVAL;

	if (check_add_overflow(curr_offset, reloc_table_size, &curr_offset))
		return -EINVAL;
	if (curr_offset > input_size)
		return -EINVAL;

	payload_start = (char *)input + curr_offset;
	payload_end = (char *)input + input_size;

	if (!kfuzztest_input_is_valid(regions, rt, payload_start, payload_end))
		return -EINVAL;

	*ret_regions = regions;
	*ret_reloc_table = rt;
	*ret_payload_start = payload_start;
	*ret_payload_end = payload_end;
	return 0;
}

static int kfuzztest_parse_and_relocate_v0(void *input, size_t input_size, void **arg_ret)
{
	struct reloc_region_array *regions;
	void *payload_start, *payload_end;
	struct reloc_table *reloc_table;
	int ret;

	ret = kfuzztest_parse_input_v0(input, input_size, &regions, &reloc_table, &payload_start, &payload_end);
	if (ret < 0)
		return ret;

	ret = kfuzztest_relocate_v0(regions, reloc_table, payload_start, payload_end);
	if (ret < 0)
		return ret;
	*arg_ret = payload_start;
	return 0;
}

int kfuzztest_parse_and_relocate(void *input, size_t input_size, void **arg_ret)
{
	u32 version, magic;

	if (input_size < sizeof(u32) + sizeof(u32))
		return -EINVAL;

	magic = *(u32 *)input;
	if (magic != KFUZZTEST_HEADER_MAGIC)
		return -EINVAL;

	version = *(u32 *)((char *)input + sizeof(u32));
	switch (version) {
	case KFUZZTEST_V0:
		return kfuzztest_parse_and_relocate_v0(input + sizeof(u64), input_size - sizeof(u64), arg_ret);
	}

	return -EINVAL;
}
