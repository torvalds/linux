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
 *  \brief Host Driver: This file defines the octeon device structure.
 */

#ifndef _LIO_DEVICE_H_
#define _LIO_DEVICE_H_

#include <sys/endian.h>	/* for BYTE_ORDER */

/* PCI VendorId Device Id */
#define LIO_CN23XX_PF_PCIID	0x9702177d
/*
 *  Driver identifies chips by these Ids, created by clubbing together
 *  DeviceId+RevisionId; Where Revision Id is not used to distinguish
 *  between chips, a value of 0 is used for revision id.
 */
#define LIO_CN23XX_PF_VID		0x9702
#define LIO_CN2350_10G_SUBDEVICE	0x03
#define LIO_CN2350_10G_SUBDEVICE1	0x04
#define LIO_CN2360_10G_SUBDEVICE	0x05
#define LIO_CN2350_25G_SUBDEVICE	0x07
#define LIO_CN2360_25G_SUBDEVICE	0x06


/* Endian-swap modes supported by Octeon. */
enum lio_pci_swap_mode {
	LIO_PCI_PASSTHROUGH	= 0,
	LIO_PCI_SWAP_64BIT	= 1,
	LIO_PCI_SWAP_32BIT	= 2,
	LIO_PCI_LW_SWAP_32BIT	= 3
};

enum {
	LIO_CFG_TYPE_DEFAULT	= 0,
	LIO_NUM_CFGS,
};

#define OCTEON_OUTPUT_INTR	(2)
#define OCTEON_ALL_INTR		0xff

/*---------------   PCI BAR1 index registers -------------*/

/* BAR1 Mask */
#define LIO_PCI_BAR1_ENABLE_CA		1
#define LIO_PCI_BAR1_ENDIAN_MODE	LIO_PCI_SWAP_64BIT
#define LIO_PCI_BAR1_ENTRY_VALID	1
#define LIO_PCI_BAR1_MASK		((LIO_PCI_BAR1_ENABLE_CA << 3) |   \
					 (LIO_PCI_BAR1_ENDIAN_MODE << 1) | \
					 LIO_PCI_BAR1_ENTRY_VALID)

/*
 *  Octeon Device state.
 *  Each octeon device goes through each of these states
 *  as it is initialized.
 */
#define LIO_DEV_BEGIN_STATE		0x0
#define LIO_DEV_PCI_ENABLE_DONE		0x1
#define LIO_DEV_PCI_MAP_DONE		0x2
#define LIO_DEV_DISPATCH_INIT_DONE	0x3
#define LIO_DEV_INSTR_QUEUE_INIT_DONE	0x4
#define LIO_DEV_SC_BUFF_POOL_INIT_DONE	0x5
#define LIO_DEV_MSIX_ALLOC_VECTOR_DONE	0x6
#define LIO_DEV_RESP_LIST_INIT_DONE	0x7
#define LIO_DEV_DROQ_INIT_DONE		0x8
#define LIO_DEV_INTR_SET_DONE		0xa
#define LIO_DEV_IO_QUEUES_DONE		0xb
#define LIO_DEV_CONSOLE_INIT_DONE	0xc
#define LIO_DEV_HOST_OK			0xd
#define LIO_DEV_CORE_OK			0xe
#define LIO_DEV_RUNNING			0xf
#define LIO_DEV_IN_RESET		0x10
#define LIO_DEV_STATE_INVALID		0x11

#define LIO_DEV_STATES			LIO_DEV_STATE_INVALID

/*
 * Octeon Device interrupts
 * These interrupt bits are set in int_status filed of
 * octeon_device structure
 */
#define LIO_DEV_INTR_DMA0_FORCE	0x01
#define LIO_DEV_INTR_DMA1_FORCE	0x02
#define LIO_DEV_INTR_PKT_DATA	0x04

#define LIO_RESET_MSECS		(3000)

/*---------------------------DISPATCH LIST-------------------------------*/

/*
 *  The dispatch list entry.
 *  The driver keeps a record of functions registered for each
 *  response header opcode in this structure. Since the opcode is
 *  hashed to index into the driver's list, more than one opcode
 *  can hash to the same entry, in which case the list field points
 *  to a linked list with the other entries.
 */
struct lio_dispatch {
	/* Singly-linked tail queue node for this entry */
	struct lio_stailq_node	node;

	/* Singly-linked tail queue head for this entry */
	struct lio_stailq_head	head;

	/* The opcode for which the dispatch function & arg should be used */
	uint16_t		opcode;

	/* The function to be called for a packet received by the driver */
	lio_dispatch_fn_t	dispatch_fn;

	/*
	 * The application specified argument to be passed to the above
	 * function along with the received packet
	 */
	void			*arg;
};

/* The dispatch list structure. */
struct lio_dispatch_list {
	/* access to dispatch list must be atomic */
	struct mtx		lock;

	/* Count of dispatch functions currently registered */
	uint32_t		count;

	/* The list of dispatch functions */
	struct lio_dispatch	*dlist;
};

/*-----------------------  THE OCTEON DEVICE  ---------------------------*/

#define LIO_MEM_REGIONS		3
/*
 *  PCI address space information.
 *  Each of the 3 address spaces given by BAR0, BAR2 and BAR4 of
 *  Octeon gets mapped to different physical address spaces in
 *  the kernel.
 */
struct lio_mem_bus_space {
	struct resource		*pci_mem;
	bus_space_tag_t		tag;
	bus_space_handle_t	handle;
};

#define LIO_MAX_MAPS	32

struct lio_io_enable {
	uint64_t	iq;
	uint64_t	oq;
	uint64_t	iq64B;
};

struct lio_reg_list {
	uint32_t	pci_win_wr_addr;

	uint32_t	pci_win_rd_addr_hi;
	uint32_t	pci_win_rd_addr_lo;
	uint32_t	pci_win_rd_addr;

	uint32_t	pci_win_wr_data_hi;
	uint32_t	pci_win_wr_data_lo;
	uint32_t	pci_win_wr_data;

	uint32_t	pci_win_rd_data;
};

#define LIO_MAX_CONSOLE_READ_BYTES	512

typedef int (*octeon_console_print_fn)(struct octeon_device *oct,
				       uint32_t num, char *pre, char *suf);
struct lio_console {
	uint32_t	active;
	uint32_t	waiting;
	uint64_t	addr;
	uint32_t	buffer_size;
	uint64_t	input_base_addr;
	uint64_t	output_base_addr;
	octeon_console_print_fn	print;
	char		leftover[LIO_MAX_CONSOLE_READ_BYTES];
};

struct lio_board_info {
	char		name[LIO_BOARD_NAME];
	char		serial_number[LIO_SERIAL_NUM_LEN];
	uint64_t	major;
	uint64_t	minor;
};

struct lio_fn_list {
	void		(*setup_iq_regs) (struct octeon_device *, uint32_t);
	void		(*setup_oq_regs) (struct octeon_device *, uint32_t);

	void		(*process_interrupt_regs) (void *);
	uint64_t	(*msix_interrupt_handler) (void *);
	int		(*soft_reset) (struct octeon_device *);
	int		(*setup_device_regs) (struct octeon_device *);
	void		(*bar1_idx_setup) (struct octeon_device *, uint64_t,
					   uint32_t, int);
	void		(*bar1_idx_write) (struct octeon_device *, uint32_t,
					   uint32_t);
	uint32_t	(*bar1_idx_read) (struct octeon_device *, uint32_t);
	uint32_t	(*update_iq_read_idx) (struct lio_instr_queue *);

	void		(*enable_interrupt) (struct octeon_device *, uint8_t);
	void		(*disable_interrupt) (struct octeon_device *, uint8_t);

	int		(*enable_io_queues) (struct octeon_device *);
	void		(*disable_io_queues) (struct octeon_device *);
};

/* Must be multiple of 8, changing breaks ABI */
#define LIO_BOOTMEM_NAME_LEN	128

/*
 * Structure for named memory blocks
 * Number of descriptors
 * available can be changed without affecting compatibility,
 * but name length changes require a bump in the bootmem
 * descriptor version
 * Note: This structure must be naturally 64 bit aligned, as a single
 * memory image will be used by both 32 and 64 bit programs.
 */
struct cvmx_bootmem_named_block_desc {
	/* Base address of named block */
	uint64_t	base_addr;

	/* Size actually allocated for named block */
	uint64_t	size;

	/* name of named block */
	char		name[LIO_BOOTMEM_NAME_LEN];
};

struct lio_fw_info {
	uint32_t	max_nic_ports;		/* max nic ports for the device */
	uint32_t	num_gmx_ports;		/* num gmx ports */
	uint64_t	app_cap_flags;		/* firmware cap flags */

	/*
	 * The core application is running in this mode.
	 * See octeon-drv-opcodes.h for values.
	 */
	uint32_t	app_mode;
	char		lio_firmware_version[32];
};

struct lio_callout {
	struct callout	timer;
	void		*ctxptr;
	uint64_t	ctxul;
};

#define LIO_NIC_STARTER_TIMEOUT	30000	/* 30000ms (30s) */

struct lio_tq {
	struct taskqueue	*tq;
	struct timeout_task	work;
	void			*ctxptr;
	uint64_t		ctxul;
};

struct lio_if_props {
	/*
	 * Each interface in the Octeon device has a network
	 * device pointer (used for OS specific calls).
	 */
	int		rx_on;
	int		gmxport;
	struct ifnet	*ifp;
};

#define LIO_MSIX_PO_INT		0x1
#define LIO_MSIX_PI_INT		0x2

struct lio_pf_vf_hs_word {
#if BYTE_ORDER == LITTLE_ENDIAN
	/* PKIND value assigned for the DPI interface */
	uint64_t pkind:8;

	/* OCTEON core clock multiplier   */
	uint64_t core_tics_per_us:16;

	/* OCTEON coprocessor clock multiplier  */
	uint64_t coproc_tics_per_us:16;

	/* app that currently running on OCTEON  */
	uint64_t app_mode:8;

	/* RESERVED */
	uint64_t reserved:16;

#else					/* BYTE_ORDER != LITTLE_ENDIAN */

	/* RESERVED */
	uint64_t reserved:16;

	/* app that currently running on OCTEON  */
	uint64_t app_mode:8;

	/* OCTEON coprocessor clock multiplier  */
	uint64_t coproc_tics_per_us:16;

	/* OCTEON core clock multiplier   */
	uint64_t core_tics_per_us:16;

	/* PKIND value assigned for the DPI interface */
	uint64_t pkind:8;
#endif	/* BYTE_ORDER == LITTLE_ENDIAN */
};

struct lio_sriov_info {

	/* Actual rings left for PF device */
	uint32_t	num_pf_rings;

	/* SRN of PF usable IO queues */
	uint32_t	pf_srn;

	/* total pf rings */
	uint32_t	trs;
};

struct lio_ioq_vector {
	struct octeon_device	*oct_dev;
	struct resource		*msix_res;
	void			*tag;
	int			droq_index;
	int			vector;
	cpuset_t		affinity_mask;
	uint32_t		ioq_num;
};

/*
 *  The Octeon device.
 *  Each Octeon device has this structure to represent all its
 *  components.
 */
struct octeon_device {
	/* Lock for PCI window configuration accesses */
	struct mtx	pci_win_lock;

	/* Lock for memory accesses */
	struct mtx	mem_access_lock;

	/* PCI device pointer */
	device_t	device;

	/* Chip specific information. */
	void		*chip;

	/* Number of interfaces detected in this octeon device. */
	uint32_t	ifcount;

	struct lio_if_props props;

	/* Octeon Chip type. */
	uint16_t	chip_id;

	uint16_t	rev_id;

	uint16_t	subdevice_id;

	uint16_t	pf_num;


	/* This device's id - set by the driver. */
	uint32_t	octeon_id;

	/* This device's PCIe port used for traffic. */
	uint16_t	pcie_port;

	uint16_t	flags;
#define LIO_FLAG_MSIX_ENABLED		(uint32_t)(1 << 2)

	/* The state of this device */
	volatile int	status;

	/* memory mapped io range */
	struct lio_mem_bus_space mem_bus_space[LIO_MEM_REGIONS];

	struct lio_reg_list reg_list;

	struct lio_fn_list fn_list;

	struct lio_board_info boardinfo;

	uint32_t	num_iqs;

	/* The pool containing pre allocated buffers used for soft commands */
	struct lio_sc_buffer_pool sc_buf_pool;

	/* The input instruction queues */
	struct lio_instr_queue *instr_queue[LIO_MAX_POSSIBLE_INSTR_QUEUES];

	/* The doubly-linked list of instruction response */
	struct lio_response_list response_list[LIO_MAX_RESPONSE_LISTS];

	uint32_t	num_oqs;

	/* The DROQ output queues  */
	struct lio_droq	*droq[LIO_MAX_POSSIBLE_OUTPUT_QUEUES];

	struct lio_io_enable io_qmask;

	/* List of dispatch functions */
	struct lio_dispatch_list dispatch;

	uint32_t	int_status;

	/* Physical location of the cvmx_bootmem_desc_t in octeon memory */
	uint64_t	bootmem_desc_addr;

	/*
	 * Placeholder memory for named blocks.
	 * Assumes single-threaded access
	 */
	struct cvmx_bootmem_named_block_desc bootmem_named_block_desc;

	/* Address of consoles descriptor */
	uint64_t	console_desc_addr;

	/* Number of consoles available. 0 means they are inaccessible */
	uint32_t	num_consoles;

	/* Console caches */
	struct lio_console console[LIO_MAX_MAPS];

	/* Console named block info */
	struct {
		uint64_t	dram_region_base;
		int		bar1_index;
	}	console_nb_info;

	/* Coprocessor clock rate. */
	uint64_t	coproc_clock_rate;

	/*
	 * The core application is running in this mode. See lio_common.h
	 * for values.
	 */
	uint32_t	app_mode;

	struct lio_fw_info fw_info;

	/* The name given to this device. */
	char		device_name[32];

	struct lio_tq	dma_comp_tq;

	/* Lock for dma response list */
	struct mtx	cmd_resp_wqlock;
	uint32_t	cmd_resp_state;

	struct lio_tq	check_db_tq[LIO_MAX_POSSIBLE_INSTR_QUEUES];

	struct lio_callout console_timer[LIO_MAX_MAPS];

	int		num_msix_irqs;

	/* For PF, there is one non-ioq interrupt handler */
	struct resource	*msix_res;
	int		aux_vector;
	void		*tag;

#define INTRNAMSIZ (32)
#define IRQ_NAME_OFF(i) ((i) * INTRNAMSIZ)

	struct lio_sriov_info sriov_info;

	struct lio_pf_vf_hs_word pfvf_hsword;

	int		msix_on;

	/* IOq information of it's corresponding MSI-X interrupt. */
	struct lio_ioq_vector *ioq_vector;

	int		rx_pause;
	int		tx_pause;

	/* TX/RX process pkt budget */
	uint32_t	rx_budget;
	uint32_t	tx_budget;

	struct octeon_link_stats link_stats;	/* stastics from firmware */

	struct proc	*watchdog_task;

	volatile bool	cores_crashed;

	uint32_t	rx_coalesce_usecs;
	uint32_t	rx_max_coalesced_frames;
	uint32_t	tx_max_coalesced_frames;

#define OCTEON_UBOOT_BUFFER_SIZE 512
	char		uboot_version[OCTEON_UBOOT_BUFFER_SIZE];
	int		uboot_len;
	int		uboot_sidx, uboot_eidx;

	struct {
		int	bus;
		int	dev;
		int	func;
	}	loc;

	volatile int	*adapter_refcount;	/* reference count of adapter */
};

#define LIO_DRV_ONLINE		1
#define LIO_DRV_OFFLINE		2
#define LIO_CN23XX_PF(oct)	((oct)->chip_id == LIO_CN23XX_PF_VID)
#define LIO_CHIP_CONF(oct, TYPE)					\
	(((struct lio_ ## TYPE  *)((oct)->chip))->conf)
#define MAX_IO_PENDING_PKT_COUNT	100

/*------------------ Function Prototypes ----------------------*/

/* Initialize device list memory */
void	lio_init_device_list(int conf_type);

/* Free memory for Input and Output queue structures for a octeon device */
void	lio_free_device_mem(struct octeon_device *oct);

/*
 * Look up a free entry in the octeon_device table and allocate resources
 * for the octeon_device structure for an octeon device. Called at init
 * time.
 */
struct octeon_device	*lio_allocate_device(device_t device);

/*
 *  Register a device's bus location at initialization time.
 *  @param oct        - pointer to the octeon device structure.
 *  @param bus        - PCIe bus #
 *  @param dev        - PCIe device #
 *  @param func       - PCIe function #
 *  @param is_pf      - TRUE for PF, FALSE for VF
 *  @return reference count of device's adapter
 */
int	lio_register_device(struct octeon_device *oct, int bus, int dev,
			    int func, int is_pf);

/*
 *  Deregister a device at de-initialization time.
 *  @param oct - pointer to the octeon device structure.
 *  @return reference count of device's adapter
 */
int	lio_deregister_device(struct octeon_device *oct);

/*
 *  Initialize the driver's dispatch list which is a mix of a hash table
 *  and a linked list. This is done at driver load time.
 *  @param octeon_dev - pointer to the octeon device structure.
 *  @return 0 on success, else -ve error value
 */
int	lio_init_dispatch_list(struct octeon_device *octeon_dev);

/*
 * Delete the driver's dispatch list and all registered entries.
 * This is done at driver unload time.
 * @param octeon_dev - pointer to the octeon device structure.
 */
void	lio_delete_dispatch_list(struct octeon_device *octeon_dev);

/*
 * Initialize the core device fields with the info returned by the FW.
 * @param recv_info - Receive info structure
 * @param buf       - Receive buffer
 */
int	lio_core_drv_init(struct lio_recv_info *recv_info, void *buf);

/*
 *  Gets the dispatch function registered to receive packets with a
 *  given opcode/subcode.
 *  @param  octeon_dev  - the octeon device pointer.
 *  @param  opcode      - the opcode for which the dispatch function
 *                        is to checked.
 *  @param  subcode     - the subcode for which the dispatch function
 *                        is to checked.
 *
 *  @return Success: lio_dispatch_fn_t (dispatch function pointer)
 *  @return Failure: NULL
 *
 *  Looks up the dispatch list to get the dispatch function for a
 *  given opcode.
 */
lio_dispatch_fn_t	lio_get_dispatch(struct octeon_device *octeon_dev,
					 uint16_t opcode, uint16_t subcode);

/*
 *  Get the octeon device pointer.
 *  @param octeon_id  - The id for which the octeon device pointer is required.
 *  @return Success: Octeon device pointer.
 *  @return Failure: NULL.
 */
struct octeon_device	*lio_get_device(uint32_t octeon_id);

/*
 *  Get the octeon id assigned to the octeon device passed as argument.
 *  This function is exported to other modules.
 *  @param dev - octeon device pointer passed as a void *.
 *  @return octeon device id
 */
int	lio_get_device_id(void *dev);

static inline uint16_t
OCTEON_MAJOR_REV(struct octeon_device *oct)
{

	uint16_t rev = (oct->rev_id & 0xC) >> 2;

	return ((rev == 0) ? 1 : rev);
}

static inline uint16_t
OCTEON_MINOR_REV(struct octeon_device *oct)
{

	return (oct->rev_id & 0x3);
}

/*
 *  Read windowed register.
 *  @param  oct   -  pointer to the Octeon device.
 *  @param  addr  -  Address of the register to read.
 *
 *  This routine is called to read from the indirectly accessed
 *  Octeon registers that are visible through a PCI BAR0 mapped window
 *  register.
 *  @return  - 64 bit value read from the register.
 */

uint64_t	lio_pci_readq(struct octeon_device *oct, uint64_t addr);

/*
 *  Write windowed register.
 *  @param  oct  -  pointer to the Octeon device.
 *  @param  val  -  Value to write
 *  @param  addr -  Address of the register to write
 *
 *  This routine is called to write to the indirectly accessed
 *  Octeon registers that are visible through a PCI BAR0 mapped window
 *  register.
 *  @return   Nothing.
 */
void	lio_pci_writeq(struct octeon_device *oct, uint64_t val, uint64_t addr);

/*
 * Checks if memory access is okay
 *
 * @param oct which octeon to send to
 * @return Zero on success, negative on failure.
 */
int	lio_mem_access_ok(struct octeon_device *oct);

/*
 * Waits for DDR initialization.
 *
 * @param oct which octeon to send to
 * @param timeout_in_ms pointer to how long to wait until DDR is initialized
 * in ms.
 *                      If contents are 0, it waits until contents are non-zero
 *                      before starting to check.
 * @return Zero on success, negative on failure.
 */
int	lio_wait_for_ddr_init(struct octeon_device *oct,
			      unsigned long *timeout_in_ms);

/*
 * Wait for u-boot to boot and be waiting for a command.
 *
 * @param wait_time_hundredths
 *               Maximum time to wait
 *
 * @return Zero on success, negative on failure.
 */
int	lio_wait_for_bootloader(struct octeon_device *oct,
				uint32_t wait_time_hundredths);

/*
 * Initialize console access
 *
 * @param oct which octeon initialize
 * @return Zero on success, negative on failure.
 */
int	lio_init_consoles(struct octeon_device *oct);

/*
 * Adds access to a console to the device.
 *
 * @param oct:		which octeon to add to
 * @param console_num:	which console
 * @param dbg_enb:      ptr to debug enablement string, one of:
 *                    * NULL for no debug output (i.e. disabled)
 *                    * empty string enables debug output (via default method)
 *                    * specific string to enable debug console output
 *
 * @return Zero on success, negative on failure.
 */
int	lio_add_console(struct octeon_device *oct, uint32_t console_num,
			char *dbg_enb);

/* write or read from a console */
int	lio_console_write(struct octeon_device *oct, uint32_t console_num,
			  char *buffer, uint32_t write_request_size,
			  uint32_t flags);

/* Removes all attached consoles. */
void	lio_remove_consoles(struct octeon_device *oct);

/*
 * Send a string to u-boot on console 0 as a command.
 *
 * @param oct which octeon to send to
 * @param cmd_str String to send
 * @param wait_hundredths Time to wait for u-boot to accept the command.
 *
 * @return Zero on success, negative on failure.
 */
int	lio_console_send_cmd(struct octeon_device *oct, char *cmd_str,
			     uint32_t wait_hundredths);

/*
 *  Parses, validates, and downloads firmware, then boots associated cores.
 *  @param oct which octeon to download firmware to
 *  @param data  - The complete firmware file image
 *  @param size  - The size of the data
 *
 *  @return 0 if success.
 *         -EINVAL if file is incompatible or badly formatted.
 *         -ENODEV if no handler was found for the application type or an
 *         invalid octeon id was passed.
 */
int	lio_download_firmware(struct octeon_device *oct, const uint8_t *data,
			      size_t size);

char	*lio_get_state_string(volatile int *state_ptr);

/*
 *  Sets up instruction queues for the device
 *  @param oct which octeon to setup
 *
 *  @return 0 if success. 1 if fails
 */
int	lio_setup_instr_queue0(struct octeon_device *oct);

/*
 *  Sets up output queues for the device
 *  @param oct which octeon to setup
 *
 *  @return 0 if success. 1 if fails
 */
int	lio_setup_output_queue0(struct octeon_device *oct);

int	lio_get_tx_qsize(struct octeon_device *oct, uint32_t q_no);

int	lio_get_rx_qsize(struct octeon_device *oct, uint32_t q_no);

/*
 *  Retrieve the config for the device
 *  @param oct which octeon
 *  @param card_type type of card
 *
 *  @returns pointer to configuration
 */
void	*lio_get_config_info(struct octeon_device *oct, uint16_t card_type);

/*
 *  Gets the octeon device configuration
 *  @return - pointer to the octeon configuration struture
 */
struct lio_config	*lio_get_conf(struct octeon_device *oct);

void	lio_free_ioq_vector(struct octeon_device *oct);
int	lio_allocate_ioq_vector(struct octeon_device *oct);
void	lio_enable_irq(struct lio_droq *droq, struct lio_instr_queue *iq);

static inline uint32_t
lio_read_pci_cfg(struct octeon_device *oct, uint32_t reg)
{

	return (pci_read_config(oct->device, reg, 4));
}

static inline void
lio_write_pci_cfg(struct octeon_device *oct, uint32_t reg, uint32_t value)
{

	pci_write_config(oct->device, reg, value, 4);
}

static inline uint8_t
lio_read_csr8(struct octeon_device *oct, uint32_t reg)
{

	return (bus_space_read_1(oct->mem_bus_space[0].tag,
				 oct->mem_bus_space[0].handle, reg));
}

static inline void
lio_write_csr8(struct octeon_device *oct, uint32_t reg, uint8_t val)
{

	bus_space_write_1(oct->mem_bus_space[0].tag,
			  oct->mem_bus_space[0].handle, reg, val);
}

static inline uint16_t
lio_read_csr16(struct octeon_device *oct, uint32_t reg)
{

	return (bus_space_read_2(oct->mem_bus_space[0].tag,
				 oct->mem_bus_space[0].handle, reg));
}

static inline void
lio_write_csr16(struct octeon_device *oct, uint32_t reg, uint16_t val)
{

	bus_space_write_2(oct->mem_bus_space[0].tag,
			  oct->mem_bus_space[0].handle, reg, val);
}

static inline uint32_t
lio_read_csr32(struct octeon_device *oct, uint32_t reg)
{

	return (bus_space_read_4(oct->mem_bus_space[0].tag,
				 oct->mem_bus_space[0].handle, reg));
}

static inline void
lio_write_csr32(struct octeon_device *oct, uint32_t reg, uint32_t val)
{

	bus_space_write_4(oct->mem_bus_space[0].tag,
			  oct->mem_bus_space[0].handle, reg, val);
}

static inline uint64_t
lio_read_csr64(struct octeon_device *oct, uint32_t reg)
{

#ifdef __i386__
	return (lio_read_csr32(oct, reg) |
			((uint64_t)lio_read_csr32(oct, reg + 4) << 32));
#else
	return (bus_space_read_8(oct->mem_bus_space[0].tag,
				 oct->mem_bus_space[0].handle, reg));
#endif
}

static inline void
lio_write_csr64(struct octeon_device *oct, uint32_t reg, uint64_t val)
{

#ifdef __i386__
	lio_write_csr32(oct, reg, (uint32_t)val);
	lio_write_csr32(oct, reg + 4, val >> 32);
#else
	bus_space_write_8(oct->mem_bus_space[0].tag,
			  oct->mem_bus_space[0].handle, reg, val);
#endif
}

#endif	/* _LIO_DEVICE_H_ */
