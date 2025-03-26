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

/**
 * struct cs_dsp_mock_alg_def - Info for creating a mock algorithm entry.
 *
 * @id		  Algorithm ID.
 * @ver;	  Algorithm version.
 * @xm_base_words XM base address in DSP words.
 * @xm_size_words XM size in DSP words.
 * @ym_base_words YM base address in DSP words.
 * @ym_size_words YM size in DSP words.
 * @zm_base_words ZM base address in DSP words.
 * @zm_size_words ZM size in DSP words.
 */
struct cs_dsp_mock_alg_def {
	unsigned int id;
	unsigned int ver;
	unsigned int xm_base_words;
	unsigned int xm_size_words;
	unsigned int ym_base_words;
	unsigned int ym_size_words;
	unsigned int zm_base_words;
	unsigned int zm_size_words;
};

struct cs_dsp_mock_coeff_def {
	const char *shortname;
	const char *fullname;
	const char *description;
	u16 type;
	u16 flags;
	u16 mem_type;
	unsigned int offset_dsp_words;
	unsigned int length_bytes;
};

/**
 * struct cs_dsp_mock_xm_header - XM header builder
 *
 * @test_priv:	     Pointer to the struct cs_dsp_test.
 * @blob_data:	     Pointer to the created blob data.
 * @blob_size_bytes: Size of the data at blob_data.
 */
struct cs_dsp_mock_xm_header {
	struct cs_dsp_test *test_priv;
	void *blob_data;
	size_t blob_size_bytes;
};

struct cs_dsp_mock_wmfw_builder;
struct cs_dsp_mock_bin_builder;

extern const unsigned int cs_dsp_mock_adsp2_32bit_sysbase;
extern const unsigned int cs_dsp_mock_adsp2_16bit_sysbase;
extern const unsigned int cs_dsp_mock_halo_core_base;
extern const unsigned int cs_dsp_mock_halo_sysinfo_base;

extern const struct cs_dsp_region cs_dsp_mock_halo_dsp1_regions[];
extern const unsigned int cs_dsp_mock_halo_dsp1_region_sizes[];
extern const struct cs_dsp_region cs_dsp_mock_adsp2_32bit_dsp1_regions[];
extern const unsigned int cs_dsp_mock_adsp2_32bit_dsp1_region_sizes[];
extern const struct cs_dsp_region cs_dsp_mock_adsp2_16bit_dsp1_regions[];
extern const unsigned int cs_dsp_mock_adsp2_16bit_dsp1_region_sizes[];
int cs_dsp_mock_count_regions(const unsigned int *region_sizes);
unsigned int cs_dsp_mock_size_of_region(const struct cs_dsp *dsp, int mem_type);
unsigned int cs_dsp_mock_base_addr_for_mem(struct cs_dsp_test *priv, int mem_type);
unsigned int cs_dsp_mock_reg_addr_inc_per_unpacked_word(struct cs_dsp_test *priv);
unsigned int cs_dsp_mock_reg_block_length_bytes(struct cs_dsp_test *priv, int mem_type);
unsigned int cs_dsp_mock_reg_block_length_registers(struct cs_dsp_test *priv, int mem_type);
unsigned int cs_dsp_mock_reg_block_length_dsp_words(struct cs_dsp_test *priv, int mem_type);
bool cs_dsp_mock_has_zm(struct cs_dsp_test *priv);
int cs_dsp_mock_packed_to_unpacked_mem_type(int packed_mem_type);
unsigned int cs_dsp_mock_num_dsp_words_to_num_packed_regs(unsigned int num_dsp_words);
unsigned int cs_dsp_mock_xm_header_get_alg_base_in_words(struct cs_dsp_test *priv,
							 unsigned int alg_id,
							 int mem_type);
unsigned int cs_dsp_mock_xm_header_get_fw_version_from_regmap(struct cs_dsp_test *priv);
unsigned int cs_dsp_mock_xm_header_get_fw_version(struct cs_dsp_mock_xm_header *header);
void cs_dsp_mock_xm_header_drop_from_regmap_cache(struct cs_dsp_test *priv);
int cs_dsp_mock_xm_header_write_to_regmap(struct cs_dsp_mock_xm_header *header);
struct cs_dsp_mock_xm_header *cs_dsp_create_mock_xm_header(struct cs_dsp_test *priv,
							   const struct cs_dsp_mock_alg_def *algs,
							   size_t num_algs);

int cs_dsp_mock_regmap_init(struct cs_dsp_test *priv);
void cs_dsp_mock_regmap_drop_range(struct cs_dsp_test *priv,
				   unsigned int first_reg, unsigned int last_reg);
void cs_dsp_mock_regmap_drop_regs(struct cs_dsp_test *priv,
				  unsigned int first_reg, size_t num_regs);
void cs_dsp_mock_regmap_drop_bytes(struct cs_dsp_test *priv,
				   unsigned int first_reg, size_t num_bytes);
void cs_dsp_mock_regmap_drop_system_regs(struct cs_dsp_test *priv);
bool cs_dsp_mock_regmap_is_dirty(struct cs_dsp_test *priv, bool drop_system_regs);

struct cs_dsp_mock_bin_builder *cs_dsp_mock_bin_init(struct cs_dsp_test *priv,
						     int format_version,
						     unsigned int fw_version);
void cs_dsp_mock_bin_add_raw_block(struct cs_dsp_mock_bin_builder *builder,
				   unsigned int alg_id, unsigned int alg_ver,
				   int type, unsigned int offset,
				   const void *payload_data, size_t payload_len_bytes);
void cs_dsp_mock_bin_add_info(struct cs_dsp_mock_bin_builder *builder,
			      const char *info);
void cs_dsp_mock_bin_add_name(struct cs_dsp_mock_bin_builder *builder,
			      const char *name);
void cs_dsp_mock_bin_add_patch(struct cs_dsp_mock_bin_builder *builder,
			       unsigned int alg_id, unsigned int alg_ver,
			       int mem_region, unsigned int reg_addr_offset,
			       const void *payload_data, size_t payload_len_bytes);
struct firmware *cs_dsp_mock_bin_get_firmware(struct cs_dsp_mock_bin_builder *builder);

struct cs_dsp_mock_wmfw_builder *cs_dsp_mock_wmfw_init(struct cs_dsp_test *priv,
						       int format_version);
void cs_dsp_mock_wmfw_add_raw_block(struct cs_dsp_mock_wmfw_builder *builder,
				    int mem_region, unsigned int mem_offset_dsp_words,
				    const void *payload_data, size_t payload_len_bytes);
void cs_dsp_mock_wmfw_add_info(struct cs_dsp_mock_wmfw_builder *builder,
			       const char *info);
void cs_dsp_mock_wmfw_add_data_block(struct cs_dsp_mock_wmfw_builder *builder,
				     int mem_region, unsigned int mem_offset_dsp_words,
				     const void *payload_data, size_t payload_len_bytes);
void cs_dsp_mock_wmfw_start_alg_info_block(struct cs_dsp_mock_wmfw_builder *builder,
					   unsigned int alg_id,
					   const char *name,
					   const char *description);
void cs_dsp_mock_wmfw_add_coeff_desc(struct cs_dsp_mock_wmfw_builder *builder,
				     const struct cs_dsp_mock_coeff_def *def);
void cs_dsp_mock_wmfw_end_alg_info_block(struct cs_dsp_mock_wmfw_builder *builder);
struct firmware *cs_dsp_mock_wmfw_get_firmware(struct cs_dsp_mock_wmfw_builder *builder);
int cs_dsp_mock_wmfw_format_version(struct cs_dsp_mock_wmfw_builder *builder);
