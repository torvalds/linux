/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _KBASE_DEBUG_CORESIGHT_CSF_
#define _KBASE_DEBUG_CORESIGHT_CSF_

#include <linux/types.h>
#include <linux/list.h>

#define KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_NOP 0U
#define KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE_IMM 1U
#define KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE_IMM_RANGE 2U
#define KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE 3U
#define KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_READ 4U
#define KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_POLL 5U
#define KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_OR 6U
#define KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_XOR 7U
#define KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_AND 8U
#define KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_NOT 9U

/**
 * struct kbase_debug_coresight_csf_write_imm_op - Coresight immediate write operation structure
 *
 * @reg_addr: Register address to write to.
 * @val:      Value to write at @reg_addr.
 */
struct kbase_debug_coresight_csf_write_imm_op {
	__u32 reg_addr;
	__u32 val;
};

/**
 * struct kbase_debug_coresight_csf_write_imm_range_op - Coresight immediate write range
 *                                                       operation structure
 *
 * @reg_start: Register address to start writing from.
 * @reg_end:   Register address to stop writing from. End address included in the write range.
 * @val:       Value to write at @reg_addr.
 */
struct kbase_debug_coresight_csf_write_imm_range_op {
	__u32 reg_start;
	__u32 reg_end;
	__u32 val;
};

/**
 * struct kbase_debug_coresight_csf_write_op - Coresight write operation structure
 *
 * @reg_addr: Register address to write to.
 * @ptr:      Pointer to the value to write at @reg_addr.
 */
struct kbase_debug_coresight_csf_write_op {
	__u32 reg_addr;
	__u32 *ptr;
};

/**
 * struct kbase_debug_coresight_csf_read_op - Coresight read operation structure
 *
 * @reg_addr: Register address to read.
 * @ptr:      Pointer where to store the read value.
 */
struct kbase_debug_coresight_csf_read_op {
	__u32 reg_addr;
	__u32 *ptr;
};

/**
 * struct kbase_debug_coresight_csf_poll_op - Coresight poll operation structure
 *
 * @reg_addr: Register address to poll.
 * @val:      Expected value after poll.
 * @mask:     Mask to apply on the read value from @reg_addr when comparing against @val.
 */
struct kbase_debug_coresight_csf_poll_op {
	__u32 reg_addr;
	__u32 val;
	__u32 mask;
};

/**
 * struct kbase_debug_coresight_csf_bitw_op - Coresight bitwise operation structure
 *
 * @ptr: Pointer to the variable on which to execute the bit operation.
 * @val: Value with which the operation should be executed against @ptr value.
 */
struct kbase_debug_coresight_csf_bitw_op {
	__u32 *ptr;
	__u32 val;
};

/**
 * struct kbase_debug_coresight_csf_op - Coresight supported operations
 *
 * @type:               Operation type.
 * @padding:            Padding for 64bit alignment.
 * @op:                 Operation union.
 * @op.write_imm:       Parameters for immediate write operation.
 * @op.write_imm_range: Parameters for immediate range write operation.
 * @op.write:           Parameters for write operation.
 * @op.read:            Parameters for read operation.
 * @op.poll:            Parameters for poll operation.
 * @op.bitw:            Parameters for bitwise operation.
 * @op.padding:         Padding for 64bit alignment.
 *
 * All operation structures should include padding to ensure they are the same size.
 */
struct kbase_debug_coresight_csf_op {
	__u8 type;
	__u8 padding[7];
	union {
		struct kbase_debug_coresight_csf_write_imm_op write_imm;
		struct kbase_debug_coresight_csf_write_imm_range_op write_imm_range;
		struct kbase_debug_coresight_csf_write_op write;
		struct kbase_debug_coresight_csf_read_op read;
		struct kbase_debug_coresight_csf_poll_op poll;
		struct kbase_debug_coresight_csf_bitw_op bitw;
		u32 padding[3];
	} op;
};

/**
 * struct kbase_debug_coresight_csf_sequence - Coresight sequence of operations
 *
 * @ops:    Arrays containing Coresight operations.
 * @nr_ops: Size of @ops.
 */
struct kbase_debug_coresight_csf_sequence {
	struct kbase_debug_coresight_csf_op *ops;
	int nr_ops;
};

/**
 * struct kbase_debug_coresight_csf_address_range - Coresight client address range
 *
 * @start: Start offset of the address range.
 * @end:   End offset of the address range.
 */
struct kbase_debug_coresight_csf_address_range {
	__u32 start;
	__u32 end;
};

/**
 * kbase_debug_coresight_csf_register - Register as a client for set ranges of MCU memory.
 *
 * @drv_data:  Pointer to driver device data.
 * @ranges:    Pointer to an array of struct kbase_debug_coresight_csf_address_range
 *             that contains start and end addresses that the client will manage.
 * @nr_ranges: Size of @ranges array.
 *
 * This function checks @ranges against current client claimed ranges. If there
 * are no overlaps, a new client is created and added to the list.
 *
 * Return: A pointer of the registered client instance on success. NULL on failure.
 */
void *kbase_debug_coresight_csf_register(void *drv_data,
					 struct kbase_debug_coresight_csf_address_range *ranges,
					 int nr_ranges);

/**
 * kbase_debug_coresight_csf_unregister - Removes a coresight client.
 *
 * @client_data: A pointer to a coresight client.
 *
 * This function removes a client from the client list and frees the client struct.
 */
void kbase_debug_coresight_csf_unregister(void *client_data);

/**
 * kbase_debug_coresight_csf_config_create - Creates a configuration containing
 *                                           enable and disable sequence.
 *
 * @client_data:      Pointer to a coresight client.
 * @enable_seq:  Pointer to a struct containing the ops needed to enable coresight blocks.
 *               It's optional so could be NULL.
 * @disable_seq: Pointer to a struct containing ops to run to disable coresight blocks.
 *               It's optional so could be NULL.
 *
 * Return: Valid pointer on success. NULL on failure.
 */
void *
kbase_debug_coresight_csf_config_create(void *client_data,
					struct kbase_debug_coresight_csf_sequence *enable_seq,
					struct kbase_debug_coresight_csf_sequence *disable_seq);
/**
 * kbase_debug_coresight_csf_config_free - Frees a configuration containing
 *                                         enable and disable sequence.
 *
 * @config_data: Pointer to a coresight configuration.
 */
void kbase_debug_coresight_csf_config_free(void *config_data);

/**
 * kbase_debug_coresight_csf_config_enable - Enables a coresight configuration
 *
 * @config_data: Pointer to coresight configuration.
 *
 * If GPU is turned on, the configuration is immediately applied the CoreSight blocks.
 * If the GPU is turned off, the configuration is scheduled to be applied on the next
 * time the GPU is turned on.
 *
 * A configuration is enabled by executing read/write/poll ops defined in config->enable_seq.
 *
 * Return: 0 if success. Error code on failure.
 */
int kbase_debug_coresight_csf_config_enable(void *config_data);
/**
 * kbase_debug_coresight_csf_config_disable - Disables a coresight configuration
 *
 * @config_data: Pointer to coresight configuration.
 *
 * If the GPU is turned off, this is effective a NOP as kbase should have disabled
 * the configuration when GPU is off.
 * If the GPU is on, the configuration will be disabled.
 *
 * A configuration is disabled by executing read/write/poll ops defined in config->disable_seq.
 *
 * Return: 0 if success. Error code on failure.
 */
int kbase_debug_coresight_csf_config_disable(void *config_data);

#endif /* _KBASE_DEBUG_CORESIGHT_CSF_ */
