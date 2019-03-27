/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

/*
 * @file lio_console.c
 */

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_image.h"
#include "lio_mem_ops.h"
#include "lio_main.h"

static void	lio_get_uboot_version(struct octeon_device *oct);
static void	lio_remote_lock(void);
static void	lio_remote_unlock(void);
static uint64_t	cvmx_bootmem_phy_named_block_find(struct octeon_device *oct,
						  const char *name,
						  uint32_t flags);
static int	lio_console_read(struct octeon_device *oct,
				 uint32_t console_num, char *buffer,
				 uint32_t buf_size);

#define CAST_ULL(v)	((unsigned long long)(v))

#define LIO_BOOTLOADER_PCI_READ_BUFFER_DATA_ADDR	0x0006c008
#define LIO_BOOTLOADER_PCI_READ_BUFFER_LEN_ADDR		0x0006c004
#define LIO_BOOTLOADER_PCI_READ_BUFFER_OWNER_ADDR	0x0006c000
#define LIO_BOOTLOADER_PCI_READ_DESC_ADDR		0x0006c100
#define LIO_BOOTLOADER_PCI_WRITE_BUFFER_STR_LEN		248

#define LIO_PCI_IO_BUF_OWNER_OCTEON	0x00000001
#define LIO_PCI_IO_BUF_OWNER_HOST	0x00000002

#define LIO_PCI_CONSOLE_BLOCK_NAME	"__pci_console"
#define LIO_CONSOLE_POLL_INTERVAL_MS	100	/* 10 times per second */

/*
 * First three members of cvmx_bootmem_desc are left in original positions
 * for backwards compatibility. Assumes big endian target
 */
struct cvmx_bootmem_desc {
	/* lock to control access to list */
	uint32_t	lock;

	/* flags for indicating various conditions */
	uint32_t	flags;

	uint64_t	head_addr;

	/* incremented changed when incompatible changes made */
	uint32_t	major_version;

	/*
	 * incremented changed when compatible changes made, reset to zero
	 * when major incremented
	 */
	uint32_t	minor_version;

	uint64_t	app_data_addr;
	uint64_t	app_data_size;

	/* number of elements in named blocks array */
	uint32_t	nb_num_blocks;

	/* length of name array in bootmem blocks */
	uint32_t	named_block_name_len;

	/* address of named memory block descriptors */
	uint64_t	named_block_array_addr;
};

/*
 * Structure that defines a single console.
 *
 * Note: when read_index == write_index, the buffer is empty. The actual usable
 * size of each console is console_buf_size -1;
 */
struct lio_pci_console {
	uint64_t	input_base_addr;
	uint32_t	input_read_index;
	uint32_t	input_write_index;
	uint64_t	output_base_addr;
	uint32_t	output_read_index;
	uint32_t	output_write_index;
	uint32_t	lock;
	uint32_t	buf_size;
};

/*
 * This is the main container structure that contains all the information
 * about all PCI consoles.  The address of this structure is passed to
 * various routines that operation on PCI consoles.
 */
struct lio_pci_console_desc {
	uint32_t	major_version;
	uint32_t	minor_version;
	uint32_t	lock;
	uint32_t	flags;
	uint32_t	num_consoles;
	uint32_t	pad;
	/* must be 64 bit aligned here... */
	/* Array of addresses of octeon_pci_console structures */
	uint64_t	console_addr_array[1];
	/* Implicit storage for console_addr_array */
};

/*
 * This macro returns the size of a member of a structure. Logically it is
 * the same as "sizeof(s::field)" in C++, but C lacks the "::" operator.
 */
#define SIZEOF_FIELD(s, field) sizeof(((s *)NULL)->field)
/*
 * This function is the implementation of the get macros defined
 * for individual structure members. The argument are generated
 * by the macros inorder to read only the needed memory.
 *
 * @param oct    Pointer to current octeon device
 * @param base   64bit physical address of the complete structure
 * @param offset Offset from the beginning of the structure to the member being
 *		 accessed.
 * @param size   Size of the structure member.
 *
 * @return Value of the structure member promoted into a uint64_t.
 */
static inline uint64_t
__cvmx_bootmem_desc_get(struct octeon_device *oct, uint64_t base,
			uint32_t offset, uint32_t size)
{

	base = (1ull << 63) | (base + offset);
	switch (size) {
	case 4:
		return (lio_read_device_mem32(oct, base));
	case 8:
		return (lio_read_device_mem64(oct, base));
	default:
		return (0);
	}
}

/*
 * This function retrieves the string name of a named block. It is
 * more complicated than a simple memcpy() since the named block
 * descriptor may not be directly accessible.
 *
 * @param oct    Pointer to current octeon device
 * @param addr   Physical address of the named block descriptor
 * @param str    String to receive the named block string name
 * @param len    Length of the string buffer, which must match the length
 *		 stored in the bootmem descriptor.
 */
static void
lio_bootmem_named_get_name(struct octeon_device *oct, uint64_t addr, char *str,
			   uint32_t len)
{

	addr += offsetof(struct cvmx_bootmem_named_block_desc, name);
	lio_pci_read_core_mem(oct, addr, (uint8_t *) str, len);
	str[len] = 0;
}

/* See header file for descriptions of functions */

/*
 * Check the version information on the bootmem descriptor
 *
 * @param oct    Pointer to current octeon device
 * @param exact_match
 *		Exact major version to check against. A zero means
 *		check that the version supports named blocks.
 *
 * @return Zero if the version is correct. Negative if the version is
 *	   incorrect. Failures also cause a message to be displayed.
 */
static int
__cvmx_bootmem_check_version(struct octeon_device *oct, uint32_t exact_match)
{
	uint32_t	major_version;
	uint32_t	minor_version;

	if (!oct->bootmem_desc_addr)
		oct->bootmem_desc_addr =
			lio_read_device_mem64(oct,
					LIO_BOOTLOADER_PCI_READ_DESC_ADDR);

	major_version = (uint32_t) __cvmx_bootmem_desc_get(oct,
			oct->bootmem_desc_addr,
			offsetof(struct cvmx_bootmem_desc, major_version),
			SIZEOF_FIELD(struct cvmx_bootmem_desc, major_version));
	minor_version = (uint32_t) __cvmx_bootmem_desc_get(oct,
			oct->bootmem_desc_addr,
			offsetof(struct cvmx_bootmem_desc, minor_version),
			SIZEOF_FIELD(struct cvmx_bootmem_desc, minor_version));

	lio_dev_dbg(oct, "%s: major_version=%d\n", __func__, major_version);
	if ((major_version > 3) ||
	    (exact_match && major_version != exact_match)) {
		lio_dev_err(oct, "bootmem ver mismatch %d.%d addr:0x%llx\n",
			    major_version, minor_version,
			    CAST_ULL(oct->bootmem_desc_addr));
		return (-1);
	} else {
		return (0);
	}
}

static const struct cvmx_bootmem_named_block_desc *
__cvmx_bootmem_find_named_block_flags(struct octeon_device *oct,
				      const char *name, uint32_t flags)
{
	struct cvmx_bootmem_named_block_desc	*desc =
		&oct->bootmem_named_block_desc;
	uint64_t	named_addr;

	named_addr = cvmx_bootmem_phy_named_block_find(oct, name,
						       flags);
	if (named_addr) {
		desc->base_addr = __cvmx_bootmem_desc_get(oct, named_addr,
			offsetof(struct cvmx_bootmem_named_block_desc,
				 base_addr),
			SIZEOF_FIELD(struct cvmx_bootmem_named_block_desc,
				     base_addr));

		desc->size = __cvmx_bootmem_desc_get(oct, named_addr,
			 offsetof(struct cvmx_bootmem_named_block_desc, size),
			 SIZEOF_FIELD(struct cvmx_bootmem_named_block_desc,
				      size));

		strncpy(desc->name, name, sizeof(desc->name));
		desc->name[sizeof(desc->name) - 1] = 0;

		return (&oct->bootmem_named_block_desc);
	} else {
		return (NULL);
	}
}

static uint64_t
cvmx_bootmem_phy_named_block_find(struct octeon_device *oct, const char *name,
				  uint32_t flags)
{
	uint64_t	result = 0;

	if (!__cvmx_bootmem_check_version(oct, 3)) {
		uint32_t i;

		uint64_t named_block_array_addr =
			__cvmx_bootmem_desc_get(oct, oct->bootmem_desc_addr,
					offsetof(struct cvmx_bootmem_desc,
						 named_block_array_addr),
					SIZEOF_FIELD(struct cvmx_bootmem_desc,
						     named_block_array_addr));
		uint32_t num_blocks =
			(uint32_t) __cvmx_bootmem_desc_get(oct,
					oct->bootmem_desc_addr,
					offsetof(struct cvmx_bootmem_desc,
						 nb_num_blocks),
					SIZEOF_FIELD(struct cvmx_bootmem_desc,
						     nb_num_blocks));

		uint32_t name_length =
			(uint32_t) __cvmx_bootmem_desc_get(oct,
					oct->bootmem_desc_addr,
					offsetof(struct cvmx_bootmem_desc,
						 named_block_name_len),
					SIZEOF_FIELD(struct cvmx_bootmem_desc,
						     named_block_name_len));

		uint64_t named_addr = named_block_array_addr;

		for (i = 0; i < num_blocks; i++) {
			uint64_t named_size =
			  __cvmx_bootmem_desc_get(oct, named_addr,
			    offsetof(struct cvmx_bootmem_named_block_desc,
				     size),
			    SIZEOF_FIELD(struct cvmx_bootmem_named_block_desc,
					 size));

			if (name && named_size) {
				char	*name_tmp = malloc(name_length + 1,
							   M_DEVBUF, M_NOWAIT |
							   M_ZERO);
				if (!name_tmp)
					break;

				lio_bootmem_named_get_name(oct, named_addr,
							   name_tmp,
							   name_length);

				if (!strncmp(name, name_tmp, name_length)) {
					result = named_addr;
					free(name_tmp, M_DEVBUF);
					break;
				}

				free(name_tmp, M_DEVBUF);

			} else if (!name && !named_size) {
				result = named_addr;
				break;
			}

			named_addr +=
				sizeof(struct cvmx_bootmem_named_block_desc);
		}
	}
	return (result);
}

/*
 * Find a named block on the remote Octeon
 *
 * @param oct       Pointer to current octeon device
 * @param name      Name of block to find
 * @param base_addr Address the block is at (OUTPUT)
 * @param size      The size of the block (OUTPUT)
 *
 * @return Zero on success, One on failure.
 */
static int
lio_named_block_find(struct octeon_device *oct, const char *name,
		     uint64_t * base_addr, uint64_t * size)
{
	const struct cvmx_bootmem_named_block_desc	*named_block;

	lio_remote_lock();
	named_block = __cvmx_bootmem_find_named_block_flags(oct, name, 0);
	lio_remote_unlock();
	if (named_block != NULL) {
		*base_addr = named_block->base_addr;
		*size = named_block->size;
		return (0);
	}

	return (1);
}


static void
lio_remote_lock(void)
{

	/* fill this in if any sharing is needed */
}

static void
lio_remote_unlock(void)
{

	/* fill this in if any sharing is needed */
}

int
lio_console_send_cmd(struct octeon_device *oct, char *cmd_str,
		     uint32_t wait_hundredths)
{
	uint32_t	len = (uint32_t) strlen(cmd_str);

	lio_dev_dbg(oct, "sending \"%s\" to bootloader\n", cmd_str);

	if (len > LIO_BOOTLOADER_PCI_WRITE_BUFFER_STR_LEN - 1) {
		lio_dev_err(oct, "Command string too long, max length is: %d\n",
			    LIO_BOOTLOADER_PCI_WRITE_BUFFER_STR_LEN - 1);
		return (-1);
	}

	if (lio_wait_for_bootloader(oct, wait_hundredths)) {
		lio_dev_err(oct, "Bootloader not ready for command.\n");
		return (-1);
	}

	/* Write command to bootloader */
	lio_remote_lock();
	lio_pci_write_core_mem(oct, LIO_BOOTLOADER_PCI_READ_BUFFER_DATA_ADDR,
			       (uint8_t *) cmd_str, len);
	lio_write_device_mem32(oct, LIO_BOOTLOADER_PCI_READ_BUFFER_LEN_ADDR,
			       len);
	lio_write_device_mem32(oct, LIO_BOOTLOADER_PCI_READ_BUFFER_OWNER_ADDR,
			       LIO_PCI_IO_BUF_OWNER_OCTEON);

	/*
	 * Bootloader should accept command very quickly if it really was
	 * ready
	 */
	if (lio_wait_for_bootloader(oct, 200)) {
		lio_remote_unlock();
		lio_dev_err(oct, "Bootloader did not accept command.\n");
		return (-1);
	}

	lio_remote_unlock();
	return (0);
}

int
lio_wait_for_bootloader(struct octeon_device *oct,
			uint32_t wait_time_hundredths)
{
	lio_dev_dbg(oct, "waiting %d0 ms for bootloader\n",
		    wait_time_hundredths);

	if (lio_mem_access_ok(oct))
		return (-1);

	while (wait_time_hundredths > 0 &&
	       lio_read_device_mem32(oct,
				LIO_BOOTLOADER_PCI_READ_BUFFER_OWNER_ADDR) !=
	       LIO_PCI_IO_BUF_OWNER_HOST) {
		if (--wait_time_hundredths <= 0)
			return (-1);

		lio_sleep_timeout(10);
	}

	return (0);
}

static void
lio_console_handle_result(struct octeon_device *oct, size_t console_num)
{
	struct lio_console	*console;

	console = &oct->console[console_num];

	console->waiting = 0;
}

static char	console_buffer[LIO_MAX_CONSOLE_READ_BYTES];

static void
lio_output_console_line(struct octeon_device *oct, struct lio_console *console,
			size_t console_num, char *console_buffer,
			int32_t bytes_read)
{
	size_t		len;
	int32_t		i;
	char           *line;

	line = console_buffer;
	for (i = 0; i < bytes_read; i++) {
		/* Output a line at a time, prefixed */
		if (console_buffer[i] == '\n') {
			console_buffer[i] = '\0';
			/* We need to output 'line', prefaced by 'leftover'.
			 * However, it is possible we're being called to
			 * output 'leftover' by itself (in the case of nothing
			 * having been read from the console).
			 *
			 * To avoid duplication, check for this condition.
			 */
			if (console->leftover[0] &&
			    (line != console->leftover)) {
				if (console->print)
					(*console->print)(oct,
							  (uint32_t)console_num,
							console->leftover,line);
				console->leftover[0] = '\0';
			} else {
				if (console->print)
					(*console->print)(oct,
							  (uint32_t)console_num,
							  line, NULL);
			}

			line = &console_buffer[i + 1];
		}
	}

	/* Save off any leftovers */
	if (line != &console_buffer[bytes_read]) {
		console_buffer[bytes_read] = '\0';
		len = strlen(console->leftover);
		strncpy(&console->leftover[len], line,
			sizeof(console->leftover) - len);
	}
}

static void
lio_check_console(void *arg)
{
	struct lio_console *console;
	struct lio_callout *console_callout = arg;
	struct octeon_device *oct =
		(struct octeon_device *)console_callout->ctxptr;
	size_t		len;
	uint32_t	console_num = (uint32_t) console_callout->ctxul;
	int32_t		bytes_read, total_read, tries;

	console = &oct->console[console_num];
	tries = 0;
	total_read = 0;

	if (callout_pending(&console_callout->timer) ||
	    (callout_active(&console_callout->timer) == 0))
		return;

	do {
		/*
		 * Take console output regardless of whether it will be
		 * logged
		 */
		bytes_read = lio_console_read(oct, console_num, console_buffer,
					      sizeof(console_buffer) - 1);
		if (bytes_read > 0) {
			total_read += bytes_read;
			if (console->waiting)
				lio_console_handle_result(oct, console_num);

			if (console->print) {
				lio_output_console_line(oct, console,
							console_num,
							console_buffer,
							bytes_read);
			}

		} else if (bytes_read < 0) {
			lio_dev_err(oct, "Error reading console %u, ret=%d\n",
				    console_num, bytes_read);
		}

		tries++;
	} while ((bytes_read > 0) && (tries < 16));

	/*
	 * If nothing is read after polling the console, output any leftovers
	 * if any
	 */
	if (console->print && (total_read == 0) && (console->leftover[0])) {
		/* append '\n' as terminator for 'output_console_line' */
		len = strlen(console->leftover);
		console->leftover[len] = '\n';
		lio_output_console_line(oct, console, console_num,
					console->leftover, (int32_t)(len + 1));
		console->leftover[0] = '\0';
	}
	callout_schedule(&oct->console_timer[console_num].timer,
			 lio_ms_to_ticks(LIO_CONSOLE_POLL_INTERVAL_MS));
}


int
lio_init_consoles(struct octeon_device *oct)
{
	uint64_t	addr, size;
	int		ret = 0;

	ret = lio_mem_access_ok(oct);
	if (ret) {
		lio_dev_err(oct, "Memory access not okay'\n");
		return (ret);
	}
	ret = lio_named_block_find(oct, LIO_PCI_CONSOLE_BLOCK_NAME, &addr,
				   &size);
	if (ret) {
		lio_dev_err(oct, "Could not find console '%s'\n",
			    LIO_PCI_CONSOLE_BLOCK_NAME);
		return (ret);
	}

	/*
	 * Use BAR1_INDEX15 to create a static mapping to a region of
	 * Octeon's DRAM that contains the PCI console named block.
	 */
	oct->console_nb_info.bar1_index = 15;
	oct->fn_list.bar1_idx_setup(oct, addr, oct->console_nb_info.bar1_index,
				    1);
	oct->console_nb_info.dram_region_base = addr & 0xFFFFFFFFFFC00000ULL;

	/*
	 * num_consoles > 0, is an indication that the consoles are
	 * accessible
	 */
	oct->num_consoles = lio_read_device_mem32(oct,
				addr + offsetof(struct lio_pci_console_desc,
						num_consoles));
	oct->console_desc_addr = addr;

	lio_dev_dbg(oct, "Initialized consoles. %d available\n",
		    oct->num_consoles);

	return (ret);
}

int
lio_add_console(struct octeon_device *oct, uint32_t console_num, char *dbg_enb)
{
	struct callout *timer;
	struct lio_console *console;
	uint64_t	coreaddr;
	int		ret = 0;

	if (console_num >= oct->num_consoles) {
		lio_dev_err(oct, "trying to read from console number %d when only 0 to %d exist\n",
			    console_num, oct->num_consoles);
	} else {
		console = &oct->console[console_num];

		console->waiting = 0;

		coreaddr = oct->console_desc_addr + console_num * 8 +
			offsetof(struct lio_pci_console_desc,
				 console_addr_array);
		console->addr = lio_read_device_mem64(oct, coreaddr);
		coreaddr = console->addr + offsetof(struct lio_pci_console,
						    buf_size);
		console->buffer_size = lio_read_device_mem32(oct, coreaddr);
		coreaddr = console->addr + offsetof(struct lio_pci_console,
						    input_base_addr);
		console->input_base_addr = lio_read_device_mem64(oct, coreaddr);
		coreaddr = console->addr + offsetof(struct lio_pci_console,
						    output_base_addr);
		console->output_base_addr =
			lio_read_device_mem64(oct, coreaddr);
		console->leftover[0] = '\0';

		timer = &oct->console_timer[console_num].timer;

		if (oct->uboot_len == 0)
			lio_get_uboot_version(oct);

		callout_init(timer, 0);
		oct->console_timer[console_num].ctxptr = (void *)oct;
		oct->console_timer[console_num].ctxul = console_num;
		callout_reset(timer,
			      lio_ms_to_ticks(LIO_CONSOLE_POLL_INTERVAL_MS),
			      lio_check_console, timer);
		/* an empty string means use default debug console enablement */
		if (dbg_enb && !dbg_enb[0])
			dbg_enb = "setenv pci_console_active 1";

		if (dbg_enb)
			ret = lio_console_send_cmd(oct, dbg_enb, 2000);

		console->active = 1;
	}

	return (ret);
}

/*
 * Removes all consoles
 *
 * @param oct         octeon device
 */
void
lio_remove_consoles(struct octeon_device *oct)
{
	struct lio_console	*console;
	uint32_t		i;

	for (i = 0; i < oct->num_consoles; i++) {
		console = &oct->console[i];

		if (!console->active)
			continue;

		callout_stop(&oct->console_timer[i].timer);
		console->addr = 0;
		console->buffer_size = 0;
		console->input_base_addr = 0;
		console->output_base_addr = 0;
	}

	oct->num_consoles = 0;
}

static inline int
lio_console_free_bytes(uint32_t buffer_size, uint32_t wr_idx, uint32_t rd_idx)
{

	if (rd_idx >= buffer_size || wr_idx >= buffer_size)
		return (-1);

	return (((buffer_size - 1) - (wr_idx - rd_idx)) % buffer_size);
}

static inline int
lio_console_avail_bytes(uint32_t buffer_size, uint32_t wr_idx, uint32_t rd_idx)
{

	if (rd_idx >= buffer_size || wr_idx >= buffer_size)
		return (-1);

	return (buffer_size - 1 -
		lio_console_free_bytes(buffer_size, wr_idx, rd_idx));
}

static int
lio_console_read(struct octeon_device *oct, uint32_t console_num, char *buffer,
		 uint32_t buf_size)
{
	struct lio_console	*console;
	int			bytes_to_read;
	uint32_t		rd_idx, wr_idx;

	if (console_num >= oct->num_consoles) {
		lio_dev_err(oct, "Attempted to read from disabled console %d\n",
			    console_num);
		return (0);
	}

	console = &oct->console[console_num];

	/*
	 * Check to see if any data is available. Maybe optimize this with
	 * 64-bit read.
	 */
	rd_idx = lio_read_device_mem32(oct, console->addr +
		       offsetof(struct lio_pci_console, output_read_index));
	wr_idx = lio_read_device_mem32(oct, console->addr +
		      offsetof(struct lio_pci_console, output_write_index));

	bytes_to_read = lio_console_avail_bytes(console->buffer_size,
						wr_idx, rd_idx);
	if (bytes_to_read <= 0)
		return (bytes_to_read);

	bytes_to_read = min(bytes_to_read, buf_size);

	/*
	 * Check to see if what we want to read is not contiguous, and limit
	 * ourselves to the contiguous block
	 */
	if (rd_idx + bytes_to_read >= console->buffer_size)
		bytes_to_read = console->buffer_size - rd_idx;

	lio_pci_read_core_mem(oct, console->output_base_addr + rd_idx,
			      (uint8_t *) buffer, bytes_to_read);
	lio_write_device_mem32(oct, console->addr +
			       offsetof(struct lio_pci_console,
					output_read_index),
			       (rd_idx + bytes_to_read) % console->buffer_size);

	return (bytes_to_read);
}

static void
lio_get_uboot_version(struct octeon_device *oct)
{
	struct lio_console *console;
	int32_t		bytes_read, total_read, tries;
	uint32_t	console_num = 0;
	int		i, ret = 0;

	ret = lio_console_send_cmd(oct, "setenv stdout pci", 50);

	console = &oct->console[console_num];
	tries = 0;
	total_read = 0;

	ret = lio_console_send_cmd(oct, "version", 1);

	do {
		/*
		 * Take console output regardless of whether it will be
		 * logged
		 */
		bytes_read = lio_console_read(oct,
					      console_num, oct->uboot_version +
					      total_read,
					      OCTEON_UBOOT_BUFFER_SIZE - 1 -
					      total_read);
		if (bytes_read > 0) {
			oct->uboot_version[bytes_read] = 0x0;

			total_read += bytes_read;
			if (console->waiting)
				lio_console_handle_result(oct, console_num);

		} else if (bytes_read < 0) {
			lio_dev_err(oct, "Error reading console %u, ret=%d\n",
				    console_num, bytes_read);
		}

		tries++;
	} while ((bytes_read > 0) && (tries < 16));

	/*
	 * If nothing is read after polling the console, output any leftovers
	 * if any
	 */
	if ((total_read == 0) && (console->leftover[0])) {
		lio_dev_dbg(oct, "%u: %s\n", console_num, console->leftover);
		console->leftover[0] = '\0';
	}

	ret = lio_console_send_cmd(oct, "setenv stdout serial", 50);

	/* U-Boot */
	for (i = 0; i < (OCTEON_UBOOT_BUFFER_SIZE - 9); i++) {
		if (oct->uboot_version[i] == 'U' &&
		    oct->uboot_version[i + 2] == 'B' &&
		    oct->uboot_version[i + 3] == 'o' &&
		    oct->uboot_version[i + 4] == 'o' &&
		    oct->uboot_version[i + 5] == 't') {
			oct->uboot_sidx = i;
			i++;
			for (; oct->uboot_version[i] != 0x0; i++) {
				if (oct->uboot_version[i] == 'm' &&
				    oct->uboot_version[i + 1] == 'i' &&
				    oct->uboot_version[i + 2] == 'p' &&
				    oct->uboot_version[i + 3] == 's') {
					oct->uboot_eidx = i - 1;
					oct->uboot_version[i - 1] = 0x0;
					oct->uboot_len = oct->uboot_eidx -
						oct->uboot_sidx + 1;
					lio_dev_info(oct, "%s\n",
						     &oct->uboot_version
						     [oct->uboot_sidx]);
					return;
				}
			}
		}
	}
}


#define FBUF_SIZE	(4 * 1024 * 1024)

int
lio_download_firmware(struct octeon_device *oct, const uint8_t * data,
		      size_t size)
{
	struct lio_firmware_file_header *h;
	uint64_t	load_addr;
	uint32_t	crc32_result, i, image_len, rem;
	int		ret = 0;

	if (size < sizeof(struct lio_firmware_file_header)) {
		lio_dev_err(oct, "Firmware file too small (%d < %d).\n",
			    (uint32_t) size,
			    (uint32_t) sizeof(struct lio_firmware_file_header));
		return (-EINVAL);
	}

	h = __DECONST(struct lio_firmware_file_header *, data);

	if (be32toh(h->magic) != LIO_NIC_MAGIC) {
		lio_dev_err(oct, "Unrecognized firmware file.\n");
		return (-EINVAL);
	}

	crc32_result = crc32(data, sizeof(struct lio_firmware_file_header) -
			     sizeof(uint32_t));
	if (crc32_result != be32toh(h->crc32)) {
		lio_dev_err(oct, "Firmware CRC mismatch (0x%08x != 0x%08x).\n",
			    crc32_result, be32toh(h->crc32));
		return (-EINVAL);
	}

	if (memcmp(LIO_BASE_VERSION, h->version,
		   strlen(LIO_BASE_VERSION))) {
		lio_dev_err(oct, "Unmatched firmware version. Expected %s.x, got %s.\n",
			    LIO_BASE_VERSION, h->version);
		return (-EINVAL);
	}

	if (be32toh(h->num_images) > LIO_MAX_IMAGES) {
		lio_dev_err(oct, "Too many images in firmware file (%d).\n",
			    be32toh(h->num_images));
		return (-EINVAL);
	}

	lio_dev_info(oct, "Firmware version: %s\n", h->version);
	snprintf(oct->fw_info.lio_firmware_version, 32, "LIQUIDIO: %s",
		 h->version);

	data += sizeof(struct lio_firmware_file_header);

	lio_dev_info(oct, "Loading %d image(s)\n", be32toh(h->num_images));

	/* load all images */
	for (i = 0; i < be32toh(h->num_images); i++) {
		load_addr = be64toh(h->desc[i].addr);
		image_len = be32toh(h->desc[i].len);

		lio_dev_info(oct, "Loading firmware %d at %llx\n", image_len,
			     (unsigned long long)load_addr);

		/* Write in 4MB chunks */
		rem = image_len;

		while (rem) {
			if (rem < FBUF_SIZE)
				size = rem;
			else
				size = FBUF_SIZE;

			/* download the image */
			lio_pci_write_core_mem(oct, load_addr,
					       __DECONST(uint8_t *, data),
					       (uint32_t) size);

			data += size;
			rem -= (uint32_t) size;
			load_addr += size;
		}
	}

	lio_dev_info(oct, "Writing boot command: %s\n", h->bootcmd);

	/* Invoke the bootcmd */
	ret = lio_console_send_cmd(oct, h->bootcmd, 50);
	return (0);
}
