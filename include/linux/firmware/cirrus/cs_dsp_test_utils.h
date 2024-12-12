/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Support utilities for cs_dsp testing.
 *
 * Copyright (C) 2024 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#include <linux/regmap.h>
#include <linux/firmware/cirrus/wmfw.h>

struct kunit;
struct cs_dsp_test;
struct cs_dsp_test_local;

/**
 * struct cs_dsp_test - base class for test utilities
 *
 * @test:	Pointer to struct kunit instance.
 * @dsp:	Pointer to struct cs_dsp instance.
 * @local:	Private data for each test suite.
 */
struct cs_dsp_test {
	struct kunit *test;
	struct cs_dsp *dsp;

	struct cs_dsp_test_local *local;

	/* Following members are private */
	bool saw_bus_write;
};

extern const unsigned int cs_dsp_mock_adsp2_32bit_sysbase;
extern const unsigned int cs_dsp_mock_adsp2_16bit_sysbase;
extern const unsigned int cs_dsp_mock_halo_core_base;
extern const unsigned int cs_dsp_mock_halo_sysinfo_base;

int cs_dsp_mock_regmap_init(struct cs_dsp_test *priv);
void cs_dsp_mock_regmap_drop_range(struct cs_dsp_test *priv,
				   unsigned int first_reg, unsigned int last_reg);
void cs_dsp_mock_regmap_drop_regs(struct cs_dsp_test *priv,
				  unsigned int first_reg, size_t num_regs);
void cs_dsp_mock_regmap_drop_bytes(struct cs_dsp_test *priv,
				   unsigned int first_reg, size_t num_bytes);
void cs_dsp_mock_regmap_drop_system_regs(struct cs_dsp_test *priv);
bool cs_dsp_mock_regmap_is_dirty(struct cs_dsp_test *priv, bool drop_system_regs);
