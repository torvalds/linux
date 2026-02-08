// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2025 Intel Corporation. All rights reserved.

/* Preface all log entries with "cxl_translate" */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <cxlmem.h>
#include <cxl.h>

/* Maximum number of test vectors and entry length */
#define MAX_TABLE_ENTRIES 128
#define MAX_ENTRY_LEN 128

/* Expected number of parameters in each test vector */
#define EXPECTED_PARAMS 7

/* Module parameters for test vectors */
static char *table[MAX_TABLE_ENTRIES];
static int table_num;

/* Interleave Arithmetic */
#define MODULO_MATH 0
#define XOR_MATH 1

/*
 * XOR mapping configuration
 * The test data sets all use the same set of xormaps. When additional
 * data sets arrive for validation, this static setup will need to
 * be changed to accept xormaps as additional parameters.
 */
struct cxl_cxims_data *cximsd;
static u64 xormaps[] = {
	0x2020900,
	0x4041200,
	0x1010400,
	0x800,
};

static int nr_maps = ARRAY_SIZE(xormaps);

#define HBIW_TO_NR_MAPS_SIZE (CXL_DECODER_MAX_INTERLEAVE + 1)
static const int hbiw_to_nr_maps[HBIW_TO_NR_MAPS_SIZE] = {
	[1] = 0, [2] = 1, [3] = 0, [4] = 2, [6] = 1, [8] = 3, [12] = 2, [16] = 4
};

/**
 * to_hpa - calculate an HPA offset from a DPA offset and position
 *
 * dpa_offset: device physical address offset
 * pos: devices position in interleave
 * r_eiw: region encoded interleave ways
 * r_eig: region encoded interleave granularity
 * hb_ways: host bridge interleave ways
 * math: interleave arithmetic (MODULO_MATH or XOR_MATH)
 *
 * Returns: host physical address offset
 */
static u64 to_hpa(u64 dpa_offset, int pos, u8 r_eiw, u16 r_eig, u8 hb_ways,
		  u8 math)
{
	u64 hpa_offset;

	/* Calculate base HPA offset from DPA and position */
	hpa_offset = cxl_calculate_hpa_offset(dpa_offset, pos, r_eiw, r_eig);
	if (hpa_offset == ULLONG_MAX)
		return ULLONG_MAX;

	if (math == XOR_MATH) {
		cximsd->nr_maps = hbiw_to_nr_maps[hb_ways];
		if (cximsd->nr_maps)
			return cxl_do_xormap_calc(cximsd, hpa_offset, hb_ways);
	}
	return hpa_offset;
}

/**
 * to_dpa - translate an HPA offset to DPA offset
 *
 * hpa_offset: host physical address offset
 * r_eiw: region encoded interleave ways
 * r_eig: region encoded interleave granularity
 * hb_ways: host bridge interleave ways
 * math: interleave arithmetic (MODULO_MATH or XOR_MATH)
 *
 * Returns: device physical address offset
 */
static u64 to_dpa(u64 hpa_offset, u8 r_eiw, u16 r_eig, u8 hb_ways, u8 math)
{
	u64 offset = hpa_offset;

	if (math == XOR_MATH) {
		cximsd->nr_maps = hbiw_to_nr_maps[hb_ways];
		if (cximsd->nr_maps)
			offset =
				cxl_do_xormap_calc(cximsd, hpa_offset, hb_ways);
	}
	return cxl_calculate_dpa_offset(offset, r_eiw, r_eig);
}

/**
 * to_pos - extract an interleave position from an HPA offset
 *
 * hpa_offset: host physical address offset
 * r_eiw: region encoded interleave ways
 * r_eig: region encoded interleave granularity
 * hb_ways: host bridge interleave ways
 * math: interleave arithmetic (MODULO_MATH or XOR_MATH)
 *
 * Returns: devices position in region interleave
 */
static u64 to_pos(u64 hpa_offset, u8 r_eiw, u16 r_eig, u8 hb_ways, u8 math)
{
	u64 offset = hpa_offset;

	/* Reverse XOR mapping if specified */
	if (math == XOR_MATH)
		offset = cxl_do_xormap_calc(cximsd, hpa_offset, hb_ways);

	return cxl_calculate_position(offset, r_eiw, r_eig);
}

/**
 * run_translation_test - execute forward and reverse translations
 *
 * @dpa: device physical address
 * @pos: expected position in region interleave
 * @r_eiw: region encoded interleave ways
 * @r_eig: region encoded interleave granularity
 * @hb_ways: host bridge interleave ways
 * @math: interleave arithmetic (MODULO_MATH or XOR_MATH)
 * @expect_spa: expected system physical address
 *
 * Returns: 0 on success, -1 on failure
 */
static int run_translation_test(u64 dpa, int pos, u8 r_eiw, u16 r_eig,
				u8 hb_ways, int math, u64 expect_hpa)
{
	u64 translated_spa, reverse_dpa;
	int reverse_pos;

	/* Test Device to Host translation: DPA + POS -> SPA */
	translated_spa = to_hpa(dpa, pos, r_eiw, r_eig, hb_ways, math);
	if (translated_spa != expect_hpa) {
		pr_err("Device to host failed: expected HPA %llu, got %llu\n",
		       expect_hpa, translated_spa);
		return -1;
	}

	/* Test Host to Device DPA translation: SPA -> DPA */
	reverse_dpa = to_dpa(translated_spa, r_eiw, r_eig, hb_ways, math);
	if (reverse_dpa != dpa) {
		pr_err("Host to Device DPA failed: expected %llu, got %llu\n",
		       dpa, reverse_dpa);
		return -1;
	}

	/* Test Host to Device Position translation: SPA -> POS */
	reverse_pos = to_pos(translated_spa, r_eiw, r_eig, hb_ways, math);
	if (reverse_pos != pos) {
		pr_err("Position lookup failed: expected %d, got %d\n", pos,
		       reverse_pos);
		return -1;
	}

	return 0;
}

/**
 * parse_test_vector - parse a single test vector string
 *
 * entry: test vector string to parse
 * dpa: device physical address
 * pos: expected position in region interleave
 * r_eiw: region encoded interleave ways
 * r_eig: region encoded interleave granularity
 * hb_ways: host bridge interleave ways
 * math: interleave arithmetic (MODULO_MATH or XOR_MATH)
 * expect_spa: expected system physical address
 *
 * Returns: 0 on success, negative error code on failure
 */
static int parse_test_vector(const char *entry, u64 *dpa, int *pos, u8 *r_eiw,
			     u16 *r_eig, u8 *hb_ways, int *math,
			     u64 *expect_hpa)
{
	unsigned int tmp_r_eiw, tmp_r_eig, tmp_hb_ways;
	int parsed;

	parsed = sscanf(entry, "%llu %d %u %u %u %d %llu", dpa, pos, &tmp_r_eiw,
			&tmp_r_eig, &tmp_hb_ways, math, expect_hpa);

	if (parsed != EXPECTED_PARAMS) {
		pr_err("Parse error: expected %d parameters, got %d in '%s'\n",
		       EXPECTED_PARAMS, parsed, entry);
		return -EINVAL;
	}
	if (tmp_r_eiw > U8_MAX || tmp_r_eig > U16_MAX || tmp_hb_ways > U8_MAX) {
		pr_err("Parameter overflow in entry: '%s'\n", entry);
		return -ERANGE;
	}
	if (*math != MODULO_MATH && *math != XOR_MATH) {
		pr_err("Invalid math type %d in entry: '%s'\n", *math, entry);
		return -EINVAL;
	}
	*r_eiw = tmp_r_eiw;
	*r_eig = tmp_r_eig;
	*hb_ways = tmp_hb_ways;

	return 0;
}

/*
 * setup_xor_mapping - Initialize XOR mapping data structure
 *
 * The test data sets all use the same HBIG so we can use one set
 * of xormaps, and set the number to apply based on HBIW before
 * calling cxl_do_xormap_calc().
 *
 * When additional data sets arrive for validation with different
 * HBIG's this static setup will need to be updated.
 *
 * Returns: 0 on success, negative error code on failure
 */
static int setup_xor_mapping(void)
{
	if (nr_maps <= 0)
		return -EINVAL;

	cximsd = kzalloc(struct_size(cximsd, xormaps, nr_maps), GFP_KERNEL);
	if (!cximsd)
		return -ENOMEM;

	memcpy(cximsd->xormaps, xormaps, nr_maps * sizeof(*cximsd->xormaps));
	cximsd->nr_maps = nr_maps;

	return 0;
}

static int test_random_params(void)
{
	u8 valid_eiws[] = { 0, 1, 2, 3, 4, 8, 9, 10 };
	u16 valid_eigs[] = { 0, 1, 2, 3, 4, 5, 6 };
	int i, ways, pos, reverse_pos;
	u64 dpa, hpa, reverse_dpa;
	int iterations = 10000;
	int failures = 0;

	for (i = 0; i < iterations; i++) {
		/* Generate valid random parameters for eiw, eig, pos, dpa */
		u8 eiw = valid_eiws[get_random_u32() % ARRAY_SIZE(valid_eiws)];
		u16 eig = valid_eigs[get_random_u32() % ARRAY_SIZE(valid_eigs)];

		eiw_to_ways(eiw, &ways);
		pos = get_random_u32() % ways;
		dpa = get_random_u64() >> 12;

		reverse_dpa = ULLONG_MAX;
		reverse_pos = -1;

		hpa = cxl_calculate_hpa_offset(dpa, pos, eiw, eig);
		if (hpa != ULLONG_MAX) {
			reverse_dpa = cxl_calculate_dpa_offset(hpa, eiw, eig);
			reverse_pos = cxl_calculate_position(hpa, eiw, eig);
			if (reverse_dpa == dpa && reverse_pos == pos)
				continue;
		}

		pr_err("test random iter %d FAIL hpa=%llu, dpa=%llu reverse_dpa=%llu, pos=%d reverse_pos=%d eiw=%u eig=%u\n",
		       i, hpa, dpa, reverse_dpa, pos, reverse_pos, eiw, eig);

		if (failures++ > 10) {
			pr_err("test random too many failures, stop\n");
			break;
		}
	}
	pr_info("..... test random: PASS %d FAIL %d\n", i - failures, failures);

	if (failures)
		return -EINVAL;

	return 0;
}

struct param_test {
	u8 eiw;
	u16 eig;
	int pos;
	bool expect; /* true: expect pass, false: expect fail */
	const char *desc;
};

static struct param_test param_tests[] = {
	{ 0x0, 0, 0, true, "1-way, min eig=0, pos=0" },
	{ 0x0, 3, 0, true, "1-way, mid eig=3, pos=0" },
	{ 0x0, 6, 0, true, "1-way, max eig=6, pos=0" },
	{ 0x1, 0, 0, true, "2-way, eig=0, pos=0" },
	{ 0x1, 3, 1, true, "2-way, eig=3, max pos=1" },
	{ 0x1, 6, 1, true, "2-way, eig=6, max pos=1" },
	{ 0x2, 0, 0, true, "4-way, eig=0, pos=0" },
	{ 0x2, 3, 3, true, "4-way, eig=3, max pos=3" },
	{ 0x2, 6, 3, true, "4-way, eig=6, max pos=3" },
	{ 0x3, 0, 0, true, "8-way, eig=0, pos=0" },
	{ 0x3, 3, 7, true, "8-way, eig=3, max pos=7" },
	{ 0x3, 6, 7, true, "8-way, eig=6, max pos=7" },
	{ 0x4, 0, 0, true, "16-way, eig=0, pos=0" },
	{ 0x4, 3, 15, true, "16-way, eig=3, max pos=15" },
	{ 0x4, 6, 15, true, "16-way, eig=6, max pos=15" },
	{ 0x8, 0, 0, true, "3-way, eig=0, pos=0" },
	{ 0x8, 3, 2, true, "3-way, eig=3, max pos=2" },
	{ 0x8, 6, 2, true, "3-way, eig=6, max pos=2" },
	{ 0x9, 0, 0, true, "6-way, eig=0, pos=0" },
	{ 0x9, 3, 5, true, "6-way, eig=3, max pos=5" },
	{ 0x9, 6, 5, true, "6-way, eig=6, max pos=5" },
	{ 0xA, 0, 0, true, "12-way, eig=0, pos=0" },
	{ 0xA, 3, 11, true, "12-way, eig=3, max pos=11" },
	{ 0xA, 6, 11, true, "12-way, eig=6, max pos=11" },
	{ 0x5, 0, 0, false, "invalid eiw=5" },
	{ 0x7, 0, 0, false, "invalid eiw=7" },
	{ 0xB, 0, 0, false, "invalid eiw=0xB" },
	{ 0xFF, 0, 0, false, "invalid eiw=0xFF" },
	{ 0x1, 7, 0, false, "invalid eig=7 (out of range)" },
	{ 0x2, 0x10, 0, false, "invalid eig=0x10" },
	{ 0x3, 0xFFFF, 0, false, "invalid eig=0xFFFF" },
	{ 0x1, 0, -1, false, "pos < 0" },
	{ 0x1, 0, 2, false, "2-way, pos=2 (>= ways)" },
	{ 0x2, 0, 4, false, "4-way, pos=4 (>= ways)" },
	{ 0x3, 0, 8, false, "8-way, pos=8 (>= ways)" },
	{ 0x4, 0, 16, false, "16-way, pos=16 (>= ways)" },
	{ 0x8, 0, 3, false, "3-way, pos=3 (>= ways)" },
	{ 0x9, 0, 6, false, "6-way, pos=6 (>= ways)" },
	{ 0xA, 0, 12, false, "12-way, pos=12 (>= ways)" },
};

static int test_cxl_validate_translation_params(void)
{
	int i, rc, failures = 0;
	bool valid;

	for (i = 0; i < ARRAY_SIZE(param_tests); i++) {
		struct param_test *t = &param_tests[i];

		rc = cxl_validate_translation_params(t->eiw, t->eig, t->pos);
		valid = (rc == 0);

		if (valid != t->expect) {
			pr_err("test params failed: %s\n", t->desc);
			failures++;
		}
	}
	pr_info("..... test params: PASS %d FAIL %d\n", i - failures, failures);

	if (failures)
		return -EINVAL;

	return 0;
}

/*
 * cxl_translate_init
 *
 * Run the internal validation tests when no params are passed.
 * Otherwise, parse the parameters (test vectors), and kick off
 * the translation test.
 *
 * Returns: 0 on success, negative error code on failure
 */
static int __init cxl_translate_init(void)
{
	int rc, i;

	/* If no tables are passed, validate module params only */
	if (table_num == 0) {
		pr_info("Internal validation test start...\n");
		rc = test_cxl_validate_translation_params();
		if (rc)
			return rc;

		rc = test_random_params();
		if (rc)
			return rc;

		pr_info("Internal validation test completed successfully\n");

		return 0;
	}

	pr_info("CXL translate test module loaded with %d test vectors\n",
		table_num);

	rc = setup_xor_mapping();
	if (rc)
		return rc;

	/* Process each test vector */
	for (i = 0; i < table_num; i++) {
		u64 dpa, expect_spa;
		int pos, math;
		u8 r_eiw, hb_ways;
		u16 r_eig;

		pr_debug("Processing test vector %d: '%s'\n", i, table[i]);

		/* Parse the test vector */
		rc = parse_test_vector(table[i], &dpa, &pos, &r_eiw, &r_eig,
				       &hb_ways, &math, &expect_spa);
		if (rc) {
			pr_err("CXL Translate Test %d: FAIL\n"
			       "    Failed to parse test vector '%s'\n",
			       i, table[i]);
			continue;
		}
		/* Run the translation test */
		rc = run_translation_test(dpa, pos, r_eiw, r_eig, hb_ways, math,
					  expect_spa);
		if (rc) {
			pr_err("CXL Translate Test %d: FAIL\n"
			       "    dpa=%llu pos=%d r_eiw=%u r_eig=%u hb_ways=%u math=%s expect_spa=%llu\n",
			       i, dpa, pos, r_eiw, r_eig, hb_ways,
			       (math == XOR_MATH) ? "XOR" : "MODULO",
			       expect_spa);
		} else {
			pr_info("CXL Translate Test %d: PASS\n", i);
		}
	}

	kfree(cximsd);
	pr_info("CXL translate test completed\n");

	return 0;
}

static void __exit cxl_translate_exit(void)
{
	pr_info("CXL translate test module unloaded\n");
}

module_param_array(table, charp, &table_num, 0444);
MODULE_PARM_DESC(table, "Test vectors as space-separated decimal strings");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("cxl_test: cxl address translation test module");
MODULE_IMPORT_NS("CXL");

module_init(cxl_translate_init);
module_exit(cxl_translate_exit);
