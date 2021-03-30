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
 * Not all of all commands that the driver supports are always available for use
 * by userspace. Userspace must check the results from the QUERY command in
 * order to determine the live set of commands.
 */

#define CXL_MEM_QUERY_COMMANDS _IOR(0xCE, 1, struct cxl_mem_query_commands)
#define CXL_MEM_SEND_COMMAND _IOWR(0xCE, 2, struct cxl_send_command)

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
	___C(MAX, "invalid / last command")

#define ___C(a, b) CXL_MEM_COMMAND_ID_##a
enum { CXL_CMDS };

#undef ___C
#define ___C(a, b) { b }
static const struct {
	const char *name;
} cxl_command_names[] = { CXL_CMDS };

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

/**
 * struct cxl_command_info - Command information returned from a query.
 * @id: ID number for the command.
 * @flags: Flags that specify command behavior.
 * @size_in: Expected input size, or -1 if variable length.
 * @size_out: Expected output size, or -1 if variable length.
 *
 * Represents a single command that is supported by both the driver and the
 * hardware. This is returned as part of an array from the query ioctl. The
 * following would be a command that takes a variable length input and returns 0
 * bytes of output.
 *
 *  - @id = 10
 *  - @flags = 0
 *  - @size_in = -1
 *  - @size_out = 0
 *
 * See struct cxl_mem_query_commands.
 */
struct cxl_command_info {
	__u32 id;

	__u32 flags;
#define CXL_MEM_COMMAND_FLAG_MASK GENMASK(0, 0)

	__s32 size_in;
	__s32 size_out;
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
		__s32 size;
		__u32 rsvd;
		__u64 payload;
	} in;

	struct {
		__s32 size;
		__u32 rsvd;
		__u64 payload;
	} out;
};

#endif
