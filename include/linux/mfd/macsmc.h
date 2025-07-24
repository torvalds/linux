/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Apple SMC (System Management Controller) core definitions
 *
 * Copyright (C) The Asahi Linux Contributors
 */

#ifndef _LINUX_MFD_MACSMC_H
#define _LINUX_MFD_MACSMC_H

#include <linux/soc/apple/rtkit.h>

/**
 * typedef smc_key - Alias for u32 to be used for SMC keys
 *
 * SMC keys are 32bit integers containing packed ASCII characters in natural
 * integer order, i.e. 0xAABBCCDD, which represent the FourCC ABCD.
 * The SMC driver is designed with this assumption and ensures the right
 * endianness is used when these are stored to memory and sent to or received
 * from the actual SMC firmware (which can be done in either shared memory or
 * as 64bit mailbox message on Apple Silicon).
 * Internally, SMC stores these keys in a table sorted lexicographically and
 * allows resolving an index into this table to the corresponding SMC key.
 * Thus, storing keys as u32 is very convenient as it allows to e.g. use
 * normal comparison operators which directly map to the natural order used
 * by SMC firmware.
 *
 * This simple type alias is introduced to allow easy recognition of SMC key
 * variables and arguments.
 */
typedef u32 smc_key;

/**
 * SMC_KEY - Convert FourCC SMC keys in source code to smc_key
 *
 * This macro can be used to easily define FourCC SMC keys in source code
 * and convert these to u32 / smc_key, e.g. SMC_KEY(NTAP) will expand to
 * 0x4e544150.
 *
 * @s: FourCC SMC key to be converted
 */
#define SMC_KEY(s) (smc_key)(_SMC_KEY(#s))
#define _SMC_KEY(s) (((s)[0] << 24) | ((s)[1] << 16) | ((s)[2] << 8) | (s)[3])

#define APPLE_SMC_READABLE BIT(7)
#define APPLE_SMC_WRITABLE BIT(6)
#define APPLE_SMC_FUNCTION BIT(4)

/**
 * struct apple_smc_key_info - Information for a SMC key as returned by SMC
 * @type_code: FourCC code indicating the type for this key.
 *             Known types:
 *              ch8*: ASCII string
 *              flag: Boolean, 1 or 0
 *              flt: 32-bit single-precision IEEE 754 float
 *              hex: Binary data
 *              ioft: 64bit Unsigned fixed-point intger (48.16)
 *              {si,ui}{8,16,32,64}: Signed/Unsigned 8-/16-/32-/64-bit integer
 * @size: Size of the buffer associated with this key
 * @flags: Bitfield encoding flags (APPLE_SMC_{READABLE,WRITABLE,FUNCTION})
 */
struct apple_smc_key_info {
	u32 type_code;
	u8 size;
	u8 flags;
};

/**
 * enum apple_smc_boot_stage - SMC boot stage
 * @APPLE_SMC_BOOTING: SMC is booting
 * @APPLE_SMC_INITIALIZED: SMC is initialized and ready to use
 * @APPLE_SMC_ERROR_NO_SHMEM: Shared memory could not be initialized during boot
 * @APPLE_SMC_ERROR_CRASHED: SMC has crashed
 */
enum apple_smc_boot_stage {
	APPLE_SMC_BOOTING,
	APPLE_SMC_INITIALIZED,
	APPLE_SMC_ERROR_NO_SHMEM,
	APPLE_SMC_ERROR_CRASHED
};

/**
 * struct apple_smc
 * @dev: Underlying device struct for the physical backend device
 * @key_count: Number of available SMC keys
 * @first_key: First valid SMC key
 * @last_key: Last valid SMC key
 * @event_handlers: Notifier call chain for events received from SMC
 * @rtk: Pointer to Apple RTKit instance
 * @init_done: Completion for initialization
 * @boot_stage: Current boot stage of SMC
 * @sram: Pointer to SRAM resource
 * @sram_base: SRAM base address
 * @shmem: RTKit shared memory structure for SRAM
 * @msg_id: Current message id for commands, will be incremented for each command
 * @atomic_mode: Flag set when atomic mode is entered
 * @atomic_pending: Flag indicating pending atomic command
 * @cmd_done: Completion for command execution in non-atomic mode
 * @cmd_ret: Return value from SMC for last command
 * @mutex: Mutex for non-atomic mode
 * @lock: Spinlock for atomic mode
 */
struct apple_smc {
	struct device *dev;

	u32 key_count;
	smc_key first_key;
	smc_key last_key;

	struct blocking_notifier_head event_handlers;

	struct apple_rtkit *rtk;

	struct completion init_done;
	enum apple_smc_boot_stage boot_stage;

	struct resource *sram;
	void __iomem *sram_base;
	struct apple_rtkit_shmem shmem;

	unsigned int msg_id;

	bool atomic_mode;
	bool atomic_pending;
	struct completion cmd_done;
	u64 cmd_ret;

	struct mutex mutex;
	spinlock_t lock;
};

/**
 * apple_smc_read - Read size bytes from given SMC key into buf
 * @smc: Pointer to apple_smc struct
 * @key: smc_key to be read
 * @buf: Buffer into which size bytes of data will be read from SMC
 * @size: Number of bytes to be read into buf
 *
 * Return: Zero on success, negative errno on error
 */
int apple_smc_read(struct apple_smc *smc, smc_key key, void *buf, size_t size);

/**
 * apple_smc_write - Write size bytes into given SMC key from buf
 * @smc: Pointer to apple_smc struct
 * @key: smc_key data will be written to
 * @buf: Buffer from which size bytes of data will be written to SMC
 * @size: Number of bytes to be written
 *
 * Return: Zero on success, negative errno on error
 */
int apple_smc_write(struct apple_smc *smc, smc_key key, void *buf, size_t size);

/**
 * apple_smc_enter_atomic - Enter atomic mode to be able to use apple_smc_write_atomic
 * @smc: Pointer to apple_smc struct
 *
 * This function switches the SMC backend to atomic mode which allows the
 * use of apple_smc_write_atomic while disabling *all* other functions.
 * This is only used for shutdown/reboot which requires writing to a SMC
 * key from atomic context.
 *
 * Return: Zero on success, negative errno on error
 */
int apple_smc_enter_atomic(struct apple_smc *smc);

/**
 * apple_smc_write_atomic - Write size bytes into given SMC key from buf without sleeping
 * @smc: Pointer to apple_smc struct
 * @key: smc_key data will be written to
 * @buf: Buffer from which size bytes of data will be written to SMC
 * @size: Number of bytes to be written
 *
 * Note that this function will fail if apple_smc_enter_atomic hasn't been
 * called before.
 *
 * Return: Zero on success, negative errno on error
 */
int apple_smc_write_atomic(struct apple_smc *smc, smc_key key, void *buf, size_t size);

/**
 * apple_smc_rw - Write and then read using the given SMC key
 * @smc: Pointer to apple_smc struct
 * @key: smc_key data will be written to
 * @wbuf: Buffer from which size bytes of data will be written to SMC
 * @wsize: Number of bytes to be written
 * @rbuf: Buffer to which size bytes of data will be read from SMC
 * @rsize: Number of bytes to be read
 *
 * Return: Zero on success, negative errno on error
 */
int apple_smc_rw(struct apple_smc *smc, smc_key key, void *wbuf, size_t wsize,
		 void *rbuf, size_t rsize);

/**
 * apple_smc_get_key_by_index - Given an index return the corresponding SMC key
 * @smc: Pointer to apple_smc struct
 * @index: Index to be resolved
 * @key: Buffer for SMC key to be returned
 *
 * Return: Zero on success, negative errno on error
 */
int apple_smc_get_key_by_index(struct apple_smc *smc, int index, smc_key *key);

/**
 * apple_smc_get_key_info - Get key information from SMC
 * @smc: Pointer to apple_smc struct
 * @key: Key to acquire information for
 * @info: Pointer to struct apple_smc_key_info which will be filled
 *
 * Return: Zero on success, negative errno on error
 */
int apple_smc_get_key_info(struct apple_smc *smc, smc_key key, struct apple_smc_key_info *info);

/**
 * apple_smc_key_exists - Check if the given SMC key exists
 * @smc: Pointer to apple_smc struct
 * @key: smc_key to be checked
 *
 * Return: True if the key exists, false otherwise
 */
static inline bool apple_smc_key_exists(struct apple_smc *smc, smc_key key)
{
	return apple_smc_get_key_info(smc, key, NULL) >= 0;
}

#define APPLE_SMC_TYPE_OPS(type) \
	static inline int apple_smc_read_##type(struct apple_smc *smc, smc_key key, type *p) \
	{ \
		int ret = apple_smc_read(smc, key, p, sizeof(*p)); \
		return (ret < 0) ? ret : ((ret != sizeof(*p)) ? -EINVAL : 0); \
	} \
	static inline int apple_smc_write_##type(struct apple_smc *smc, smc_key key, type p) \
	{ \
		return apple_smc_write(smc, key, &p, sizeof(p)); \
	} \
	static inline int apple_smc_write_##type##_atomic(struct apple_smc *smc, smc_key key, type p) \
	{ \
		return apple_smc_write_atomic(smc, key, &p, sizeof(p)); \
	} \
	static inline int apple_smc_rw_##type(struct apple_smc *smc, smc_key key, \
					      type w, type *r) \
	{ \
		int ret = apple_smc_rw(smc, key, &w, sizeof(w), r, sizeof(*r)); \
		return (ret < 0) ? ret : ((ret != sizeof(*r)) ? -EINVAL : 0); \
	}

APPLE_SMC_TYPE_OPS(u64)
APPLE_SMC_TYPE_OPS(u32)
APPLE_SMC_TYPE_OPS(u16)
APPLE_SMC_TYPE_OPS(u8)
APPLE_SMC_TYPE_OPS(s64)
APPLE_SMC_TYPE_OPS(s32)
APPLE_SMC_TYPE_OPS(s16)
APPLE_SMC_TYPE_OPS(s8)

static inline int apple_smc_read_flag(struct apple_smc *smc, smc_key key, bool *flag)
{
	u8 val;
	int ret = apple_smc_read_u8(smc, key, &val);

	if (ret < 0)
		return ret;

	*flag = val ? true : false;
	return ret;
}

static inline int apple_smc_write_flag(struct apple_smc *smc, smc_key key, bool state)
{
	return apple_smc_write_u8(smc, key, state ? 1 : 0);
}

static inline int apple_smc_write_flag_atomic(struct apple_smc *smc, smc_key key, bool state)
{
	return apple_smc_write_u8_atomic(smc, key, state ? 1 : 0);
}

#endif
