/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __TSM_MR_H
#define __TSM_MR_H

#include <crypto/hash_info.h>

/**
 * struct tsm_measurement_register - describes an architectural measurement
 * register (MR)
 * @mr_name: name of the MR
 * @mr_value: buffer containing the current value of the MR
 * @mr_size: size of the MR - typically the digest size of @mr_hash
 * @mr_flags: bitwise OR of one or more flags, detailed below
 * @mr_hash: optional hash identifier defined in include/uapi/linux/hash_info.h.
 *
 * A CC guest driver encloses an array of this structure in struct
 * tsm_measurements to detail the measurement facility supported by the
 * underlying CC hardware.
 *
 * @mr_name and @mr_value must stay valid until this structure is no longer in
 * use.
 *
 * @mr_flags is the bitwise-OR of zero or more of the flags below.
 *
 * * %TSM_MR_F_READABLE - the sysfs attribute corresponding to this MR is readable.
 * * %TSM_MR_F_WRITABLE - the sysfs attribute corresponding to this MR is writable.
 *   The semantics is typically to extend the MR but could vary depending on the
 *   architecture and the MR.
 * * %TSM_MR_F_LIVE - this MR's value may differ from the last value written, so
 *   must be read back from the underlying CC hardware/firmware.
 * * %TSM_MR_F_RTMR - bitwise-OR of %TSM_MR_F_LIVE and %TSM_MR_F_WRITABLE.
 * * %TSM_MR_F_NOHASH - this MR does NOT have an associated hash algorithm.
 *   @mr_hash will be ignored when this flag is set.
 */
struct tsm_measurement_register {
	const char *mr_name;
	void *mr_value;
	u32 mr_size;
	u32 mr_flags;
	enum hash_algo mr_hash;
};

#define TSM_MR_F_NOHASH 1
#define TSM_MR_F_WRITABLE 2
#define TSM_MR_F_READABLE 4
#define TSM_MR_F_LIVE 8
#define TSM_MR_F_RTMR (TSM_MR_F_LIVE | TSM_MR_F_WRITABLE)

#define TSM_MR_(mr, hash)                              \
	.mr_name = #mr, .mr_size = hash##_DIGEST_SIZE, \
	.mr_hash = HASH_ALGO_##hash, .mr_flags = TSM_MR_F_READABLE

/**
 * struct tsm_measurements - defines the CC architecture specific measurement
 * facility and methods for updating measurement registers (MRs)
 * @mrs: Array of MR definitions.
 * @nr_mrs: Number of elements in @mrs.
 * @refresh: Callback function to load/sync all MRs from TVM hardware/firmware
 *           into the kernel cache.
 * @write: Callback function to write to the MR specified by the parameter @mr.
 *         Typically, writing to an MR extends the input buffer to that MR.
 *
 * The @refresh callback is invoked when an MR with %TSM_MR_F_LIVE set is being
 * read and the cache is stale. It must reload all MRs with %TSM_MR_F_LIVE set.
 * The function parameter @tm is a pointer pointing back to this structure.
 *
 * The @write callback is invoked whenever an MR is being written. It takes two
 * additional parameters besides @tm:
 *
 * * @mr - points to the MR (an element of @tm->mrs) being written.
 * * @data - contains the bytes to write and whose size is @mr->mr_size.
 *
 * Both @refresh and @write should return 0 on success and an appropriate error
 * code on failure.
 */
struct tsm_measurements {
	const struct tsm_measurement_register *mrs;
	size_t nr_mrs;
	int (*refresh)(const struct tsm_measurements *tm);
	int (*write)(const struct tsm_measurements *tm,
		     const struct tsm_measurement_register *mr, const u8 *data);
};

const struct attribute_group *
tsm_mr_create_attribute_group(const struct tsm_measurements *tm);
void tsm_mr_free_attribute_group(const struct attribute_group *attr_grp);

#endif
