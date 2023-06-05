/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * CXL IOCTLs for Memory Devices
 */

#ifndef _UAPI_CXL_MEM_H_
#define _UAPI_CXL_MEM_H_

#include <linux/types.h>

/**
 * DOC: UAPI
 *
 * Not all of the commands that the driver supports are available for use by
 * userspace at all times.  Userspace can check the result of the QUERY command
 * to determine the live set of commands.  Alternatively, it can issue the
 * command and check for failure.
 */

#define CXL_MEM_QUERY_COMMANDS _IOR(0xCE, 1, struct cxl_mem_query_commands)
#define CXL_MEM_SEND_COMMAND _IOWR(0xCE, 2, struct cxl_send_command)

/*
 * NOTE: New defines must be added to the end of the list to preserve
 * compatibility because this enum is exported to user space.
 */
#define CXL_CMDS                                                          \
	___C(INVALID, "Invalid Command"),                                 \
	___C(IDENTIFY, "Identify Command"),                               \
	___C(RAW, "Raw device command"),                                  \
	___C(GET_SUPPORTED_LOGS, "Get Supported Logs"),                   \
	___C(GET_FW_INFO, "Get FW Info"),                                 \
	___C(GET_PARTITION_INFO, "Get Partition Information"),            \
	___C(GET_LSA, "Get Label Storage Area"),                          \
	___C(GET_HEALTH_INFO, "Get Health Info"),                         \
	___C(GET_LOG, "Get Log"),                                         \
	___C(SET_PARTITION_INFO, "Set Partition Information"),            \
	___C(SET_LSA, "Set Label Storage Area"),                          \
	___C(GET_ALERT_CONFIG, "Get Alert Configuration"),                \
	___C(SET_ALERT_CONFIG, "Set Alert Configuration"),                \
	___C(GET_SHUTDOWN_STATE, "Get Shutdown State"),                   \
	___C(SET_SHUTDOWN_STATE, "Set Shutdown State"),                   \
	___DEPRECATED(GET_POISON, "Get Poison List"),                     \
	___DEPRECATED(INJECT_POISON, "Inject Poison"),                    \
	___DEPRECATED(CLEAR_POISON, "Clear Poison"),                      \
	___C(GET_SCAN_MEDIA_CAPS, "Get Scan Media Capabilities"),         \
	___DEPRECATED(SCAN_MEDIA, "Scan Media"),                          \
	___DEPRECATED(GET_SCAN_MEDIA, "Get Scan Media Results"),          \
	___C(MAX, "invalid / last command")

#define ___C(a, b) CXL_MEM_COMMAND_ID_##a
#define ___DEPRECATED(a, b) CXL_MEM_DEPRECATED_ID_##a
enum { CXL_CMDS };

#undef ___C
#undef ___DEPRECATED
#define ___C(a, b) { b }
#define ___DEPRECATED(a, b) { "Deprecated " b }
static const struct {
	const char *name;
} cxl_command_names[] __attribute__((__unused__)) = { CXL_CMDS };

/*
 * Here's how this actually breaks out:
 * cxl_command_names[] = {
 *	[CXL_MEM_COMMAND_ID_INVALID] = { "Invalid Command" },
 *	[CXL_MEM_COMMAND_ID_IDENTIFY] = { "Identify Command" },
 *	...
 *	[CXL_MEM_COMMAND_ID_MAX] = { "invalid / last command" },
 * };
 */

#undef ___C
#undef ___DEPRECATED
#define ___C(a, b) (0)
#define ___DEPRECATED(a, b) (1)

static const __u8 cxl_deprecated_commands[]
	__attribute__((__unused__)) = { CXL_CMDS };

/*
 * Here's how this actually breaks out:
 * cxl_deprecated_commands[] = {
 *	[CXL_MEM_COMMAND_ID_INVALID] = 0,
 *	[CXL_MEM_COMMAND_ID_IDENTIFY] = 0,
 *	...
 *	[CXL_MEM_DEPRECATED_ID_GET_POISON] = 1,
 *	[CXL_MEM_DEPRECATED_ID_INJECT_POISON] = 1,
 *	[CXL_MEM_DEPRECATED_ID_CLEAR_POISON] = 1,
 *	...
 * };
 */

#undef ___C
#undef ___DEPRECATED

/**
 * struct cxl_command_info - Command information returned from a query.
 * @id: ID number for the command.
 * @flags: Flags that specify command behavior.
 *
 *         CXL_MEM_COMMAND_FLAG_USER_ENABLED
 *
 *         The given command id is supported by the driver and is supported by
 *         a related opcode on the device.
 *
 *         CXL_MEM_COMMAND_FLAG_EXCLUSIVE
 *
 *         Requests with the given command id will terminate with EBUSY as the
 *         kernel actively owns management of the given resource. For example,
 *         the label-storage-area can not be written while the kernel is
 *         actively managing that space.
 *
 * @size_in: Expected input size, or ~0 if variable length.
 * @size_out: Expected output size, or ~0 if variable length.
 *
 * Represents a single command that is supported by both the driver and the
 * hardware. This is returned as part of an array from the query ioctl. The
 * following would be a command that takes a variable length input and returns 0
 * bytes of output.
 *
 *  - @id = 10
 *  - @flags = CXL_MEM_COMMAND_FLAG_ENABLED
 *  - @size_in = ~0
 *  - @size_out = 0
 *
 * See struct cxl_mem_query_commands.
 */
struct cxl_command_info {
	__u32 id;

	__u32 flags;
#define CXL_MEM_COMMAND_FLAG_MASK		GENMASK(1, 0)
#define CXL_MEM_COMMAND_FLAG_ENABLED		BIT(0)
#define CXL_MEM_COMMAND_FLAG_EXCLUSIVE		BIT(1)

	__u32 size_in;
	__u32 size_out;
};

/**
 * struct cxl_mem_query_commands - Query supported commands.
 * @n_commands: In/out parameter. When @n_commands is > 0, the driver will
 *		return min(num_support_commands, n_commands). When @n_commands
 *		is 0, driver will return the number of total supported commands.
 * @rsvd: Reserved for future use.
 * @commands: Output array of supported commands. This array must be allocated
 *            by userspace to be at least min(num_support_commands, @n_commands)
 *
 * Allow userspace to query the available commands supported by both the driver,
 * and the hardware. Commands that aren't supported by either the driver, or the
 * hardware are not returned in the query.
 *
 * Examples:
 *
 *  - { .n_commands = 0 } // Get number of supported commands
 *  - { .n_commands = 15, .commands = buf } // Return first 15 (or less)
 *    supported commands
 *
 *  See struct cxl_command_info.
 */
struct cxl_mem_query_commands {
	/*
	 * Input: Number of commands to return (space allocated by user)
	 * Output: Number of commands supported by the driver/hardware
	 *
	 * If n_commands is 0, kernel will only return number of commands and
	 * not try to populate commands[], thus allowing userspace to know how
	 * much space to allocate
	 */
	__u32 n_commands;
	__u32 rsvd;

	struct cxl_command_info __user commands[]; /* out: supported commands */
};

/**
 * struct cxl_send_command - Send a command to a memory device.
 * @id: The command to send to the memory device. This must be one of the
 *	commands returned by the query command.
 * @flags: Flags for the command (input).
 * @raw: Special fields for raw commands
 * @raw.opcode: Opcode passed to hardware when using the RAW command.
 * @raw.rsvd: Must be zero.
 * @rsvd: Must be zero.
 * @retval: Return value from the memory device (output).
 * @in: Parameters associated with input payload.
 * @in.size: Size of the payload to provide to the device (input).
 * @in.rsvd: Must be zero.
 * @in.payload: Pointer to memory for payload input, payload is little endian.
 * @out: Parameters associated with output payload.
 * @out.size: Size of the payload received from the device (input/output). This
 *	      field is filled in by userspace to let the driver know how much
 *	      space was allocated for output. It is populated by the driver to
 *	      let userspace know how large the output payload actually was.
 * @out.rsvd: Must be zero.
 * @out.payload: Pointer to memory for payload output, payload is little endian.
 *
 * Mechanism for userspace to send a command to the hardware for processing. The
 * driver will do basic validation on the command sizes. In some cases even the
 * payload may be introspected. Userspace is required to allocate large enough
 * buffers for size_out which can be variable length in certain situations.
 */
struct cxl_send_command {
	__u32 id;
	__u32 flags;
	union {
		struct {
			__u16 opcode;
			__u16 rsvd;
		} raw;
		__u32 rsvd;
	};
	__u32 retval;

	struct {
		__u32 size;
		__u32 rsvd;
		__u64 payload;
	} in;

	struct {
		__u32 size;
		__u32 rsvd;
		__u64 payload;
	} out;
};

#endif
