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

#include "lio_bsd.h"
#include "lio_common.h"

#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_ctrl.h"
#include "lio_main.h"
#include "lio_network.h"
#include "cn23xx_pf_device.h"
#include "lio_image.h"
#include "lio_ioctl.h"
#include "lio_rxtx.h"
#include "lio_rss.h"

/* Number of milliseconds to wait for DDR initialization */
#define LIO_DDR_TIMEOUT	10000
#define LIO_MAX_FW_TYPE_LEN	8

static char fw_type[LIO_MAX_FW_TYPE_LEN];
TUNABLE_STR("hw.lio.fw_type", fw_type, sizeof(fw_type));

/*
 * Integers that specify number of queues per PF.
 * Valid range is 0 to 64.
 * Use 0 to derive from CPU count.
 */
static int	num_queues_per_pf0;
static int	num_queues_per_pf1;
TUNABLE_INT("hw.lio.num_queues_per_pf0", &num_queues_per_pf0);
TUNABLE_INT("hw.lio.num_queues_per_pf1", &num_queues_per_pf1);

#ifdef RSS
static int	lio_rss = 1;
TUNABLE_INT("hw.lio.rss", &lio_rss);
#endif	/* RSS */

/* Hardware LRO */
unsigned int	lio_hwlro = 0;
TUNABLE_INT("hw.lio.hwlro", &lio_hwlro);

/*
 * Bitmask indicating which consoles have debug
 * output redirected to syslog.
 */
static unsigned long	console_bitmask;
TUNABLE_ULONG("hw.lio.console_bitmask", &console_bitmask);

/*
 * \brief determines if a given console has debug enabled.
 * @param console console to check
 * @returns  1 = enabled. 0 otherwise
 */
int
lio_console_debug_enabled(uint32_t console)
{

	return (console_bitmask >> (console)) & 0x1;
}

static int	lio_detach(device_t dev);

static int	lio_device_init(struct octeon_device *octeon_dev);
static int	lio_chip_specific_setup(struct octeon_device *oct);
static void	lio_watchdog(void *param);
static int	lio_load_firmware(struct octeon_device *oct);
static int	lio_nic_starter(struct octeon_device *oct);
static int	lio_init_nic_module(struct octeon_device *oct);
static int	lio_setup_nic_devices(struct octeon_device *octeon_dev);
static int	lio_link_info(struct lio_recv_info *recv_info, void *ptr);
static void	lio_if_cfg_callback(struct octeon_device *oct, uint32_t status,
				    void *buf);
static int	lio_set_rxcsum_command(struct ifnet *ifp, int command,
				       uint8_t rx_cmd);
static int	lio_setup_glists(struct octeon_device *oct, struct lio *lio,
				 int num_iqs);
static void	lio_destroy_nic_device(struct octeon_device *oct, int ifidx);
static inline void	lio_update_link_status(struct ifnet *ifp,
					       union octeon_link_status *ls);
static void	lio_send_rx_ctrl_cmd(struct lio *lio, int start_stop);
static int	lio_stop_nic_module(struct octeon_device *oct);
static void	lio_destroy_resources(struct octeon_device *oct);
static int	lio_setup_rx_oom_poll_fn(struct ifnet *ifp);

static void	lio_vlan_rx_add_vid(void *arg, struct ifnet *ifp, uint16_t vid);
static void	lio_vlan_rx_kill_vid(void *arg, struct ifnet *ifp,
				     uint16_t vid);
static struct octeon_device *
	lio_get_other_octeon_device(struct octeon_device *oct);

static int	lio_wait_for_oq_pkts(struct octeon_device *oct);

int	lio_send_rss_param(struct lio *lio);
static int	lio_dbg_console_print(struct octeon_device *oct,
				      uint32_t console_num, char *prefix,
				      char *suffix);

/* Polling interval for determining when NIC application is alive */
#define LIO_STARTER_POLL_INTERVAL_MS	100

/*
 * vendor_info_array.
 * This array contains the list of IDs on which the driver should load.
 */
struct lio_vendor_info {
	uint16_t	vendor_id;
	uint16_t	device_id;
	uint16_t	subdevice_id;
	uint8_t		revision_id;
	uint8_t		index;
};

static struct lio_vendor_info lio_pci_tbl[] = {
	/* CN2350 10G */
	{PCI_VENDOR_ID_CAVIUM, LIO_CN23XX_PF_VID, LIO_CN2350_10G_SUBDEVICE,
		0x02, 0},

	/* CN2350 10G */
	{PCI_VENDOR_ID_CAVIUM, LIO_CN23XX_PF_VID, LIO_CN2350_10G_SUBDEVICE1,
		0x02, 0},

	/* CN2360 10G */
	{PCI_VENDOR_ID_CAVIUM, LIO_CN23XX_PF_VID, LIO_CN2360_10G_SUBDEVICE,
		0x02, 1},

	/* CN2350 25G */
	{PCI_VENDOR_ID_CAVIUM, LIO_CN23XX_PF_VID, LIO_CN2350_25G_SUBDEVICE,
		0x02, 2},

	/* CN2360 25G */
	{PCI_VENDOR_ID_CAVIUM, LIO_CN23XX_PF_VID, LIO_CN2360_25G_SUBDEVICE,
		0x02, 3},

	{0, 0, 0, 0, 0}
};

static char *lio_strings[] = {
	"LiquidIO 2350 10GbE Server Adapter",
	"LiquidIO 2360 10GbE Server Adapter",
	"LiquidIO 2350 25GbE Server Adapter",
	"LiquidIO 2360 25GbE Server Adapter",
};

struct lio_if_cfg_resp {
	uint64_t	rh;
	struct octeon_if_cfg_info cfg_info;
	uint64_t	status;
};

struct lio_if_cfg_context {
	int		octeon_id;
	volatile int	cond;
};

struct lio_rx_ctl_context {
	int		octeon_id;
	volatile int	cond;
};

static int
lio_probe(device_t dev)
{
	struct lio_vendor_info	*tbl;

	uint16_t	vendor_id;
	uint16_t	device_id;
	uint16_t	subdevice_id;
	uint8_t		revision_id;
	char		device_ver[256];

	vendor_id = pci_get_vendor(dev);
	if (vendor_id != PCI_VENDOR_ID_CAVIUM)
		return (ENXIO);

	device_id = pci_get_device(dev);
	subdevice_id = pci_get_subdevice(dev);
	revision_id = pci_get_revid(dev);

	tbl = lio_pci_tbl;
	while (tbl->vendor_id) {
		if ((vendor_id == tbl->vendor_id) &&
		    (device_id == tbl->device_id) &&
		    (subdevice_id == tbl->subdevice_id) &&
		    (revision_id == tbl->revision_id)) {
			sprintf(device_ver, "%s, Version - %s",
				lio_strings[tbl->index], LIO_VERSION);
			device_set_desc_copy(dev, device_ver);
			return (BUS_PROBE_DEFAULT);
		}

		tbl++;
	}

	return (ENXIO);
}

static int
lio_attach(device_t device)
{
	struct octeon_device	*oct_dev = NULL;
	uint64_t	scratch1;
	uint32_t	error;
	int		timeout, ret = 1;
	uint8_t		bus, dev, function;

	oct_dev = lio_allocate_device(device);
	if (oct_dev == NULL) {
		device_printf(device, "Error: Unable to allocate device\n");
		return (-ENOMEM);
	}

	oct_dev->tx_budget = LIO_DEFAULT_TX_PKTS_PROCESS_BUDGET;
	oct_dev->rx_budget = LIO_DEFAULT_RX_PKTS_PROCESS_BUDGET;
	oct_dev->msix_on = LIO_FLAG_MSIX_ENABLED;

	oct_dev->device = device;
	bus = pci_get_bus(device);
	dev = pci_get_slot(device);
	function = pci_get_function(device);

	lio_dev_info(oct_dev, "Initializing device %x:%x %02x:%02x.%01x\n",
		     pci_get_vendor(device), pci_get_device(device), bus, dev,
		     function);

	if (lio_device_init(oct_dev)) {
		lio_dev_err(oct_dev, "Failed to init device\n");
		lio_detach(device);
		return (-ENOMEM);
	}

	scratch1 = lio_read_csr64(oct_dev, LIO_CN23XX_SLI_SCRATCH1);
	if (!(scratch1 & 4ULL)) {
		/*
		 * Bit 2 of SLI_SCRATCH_1 is a flag that indicates that
		 * the lio watchdog kernel thread is running for this
		 * NIC.  Each NIC gets one watchdog kernel thread.
		 */
		scratch1 |= 4ULL;
		lio_write_csr64(oct_dev, LIO_CN23XX_SLI_SCRATCH1, scratch1);

		error = kproc_create(lio_watchdog, oct_dev,
				     &oct_dev->watchdog_task, 0, 0,
				     "liowd/%02hhx:%02hhx.%hhx", bus,
				     dev, function);
		if (!error) {
			kproc_resume(oct_dev->watchdog_task);
		} else {
			oct_dev->watchdog_task = NULL;
			lio_dev_err(oct_dev,
				    "failed to create kernel_thread\n");
			lio_detach(device);
			return (-1);
		}
	}
	oct_dev->rx_pause = 1;
	oct_dev->tx_pause = 1;

	timeout = 0;
	while (timeout < LIO_NIC_STARTER_TIMEOUT) {
		lio_mdelay(LIO_STARTER_POLL_INTERVAL_MS);
		timeout += LIO_STARTER_POLL_INTERVAL_MS;

		/*
		 * During the boot process interrupts are not available.
		 * So polling for first control message from FW.
		 */
		if (cold)
			lio_droq_bh(oct_dev->droq[0], 0);

		if (atomic_load_acq_int(&oct_dev->status) == LIO_DEV_CORE_OK) {
			ret = lio_nic_starter(oct_dev);
			break;
		}
	}

	if (ret) {
		lio_dev_err(oct_dev, "Firmware failed to start\n");
		lio_detach(device);
		return (-EIO);
	}

	lio_dev_dbg(oct_dev, "Device is ready\n");

	return (0);
}

static int
lio_detach(device_t dev)
{
	struct octeon_device	*oct_dev = device_get_softc(dev);

	lio_dev_dbg(oct_dev, "Stopping device\n");
	if (oct_dev->watchdog_task) {
		uint64_t	scratch1;

		kproc_suspend(oct_dev->watchdog_task, 0);

		scratch1 = lio_read_csr64(oct_dev, LIO_CN23XX_SLI_SCRATCH1);
		scratch1 &= ~4ULL;
		lio_write_csr64(oct_dev, LIO_CN23XX_SLI_SCRATCH1, scratch1);
	}

	if (oct_dev->app_mode && (oct_dev->app_mode == LIO_DRV_NIC_APP))
		lio_stop_nic_module(oct_dev);

	/*
	 * Reset the octeon device and cleanup all memory allocated for
	 * the octeon device by  driver.
	 */
	lio_destroy_resources(oct_dev);

	lio_dev_info(oct_dev, "Device removed\n");

	/*
	 * This octeon device has been removed. Update the global
	 * data structure to reflect this. Free the device structure.
	 */
	lio_free_device_mem(oct_dev);
	return (0);
}

static int
lio_shutdown(device_t dev)
{
	struct octeon_device	*oct_dev = device_get_softc(dev);
	struct lio	*lio = if_getsoftc(oct_dev->props.ifp);

	lio_send_rx_ctrl_cmd(lio, 0);

	return (0);
}

static int
lio_suspend(device_t dev)
{

	return (ENXIO);
}

static int
lio_resume(device_t dev)
{

	return (ENXIO);
}

static int
lio_event(struct module *mod, int event, void *junk)
{

	switch (event) {
	case MOD_LOAD:
		lio_init_device_list(LIO_CFG_TYPE_DEFAULT);
		break;
	default:
		break;
	}

	return (0);
}

/*********************************************************************
 *  FreeBSD Device Interface Entry Points
 * *******************************************************************/
static device_method_t lio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, lio_probe),
	DEVMETHOD(device_attach, lio_attach),
	DEVMETHOD(device_detach, lio_detach),
	DEVMETHOD(device_shutdown, lio_shutdown),
	DEVMETHOD(device_suspend, lio_suspend),
	DEVMETHOD(device_resume, lio_resume),
	DEVMETHOD_END
};

static driver_t lio_driver = {
	LIO_DRV_NAME, lio_methods, sizeof(struct octeon_device),
};

devclass_t lio_devclass;
DRIVER_MODULE(lio, pci, lio_driver, lio_devclass, lio_event, 0);

MODULE_DEPEND(lio, pci, 1, 1, 1);
MODULE_DEPEND(lio, ether, 1, 1, 1);
MODULE_DEPEND(lio, firmware, 1, 1, 1);

static bool
fw_type_is_none(void)
{
	return strncmp(fw_type, LIO_FW_NAME_TYPE_NONE,
		       sizeof(LIO_FW_NAME_TYPE_NONE)) == 0;
}

/*
 * \brief Device initialization for each Octeon device that is probed
 * @param octeon_dev  octeon device
 */
static int
lio_device_init(struct octeon_device *octeon_dev)
{
	unsigned long	ddr_timeout = LIO_DDR_TIMEOUT;
	char	*dbg_enb = NULL;
	int	fw_loaded = 0;
	int	i, j, ret;
	uint8_t	bus, dev, function;
	char	bootcmd[] = "\n";

	bus = pci_get_bus(octeon_dev->device);
	dev = pci_get_slot(octeon_dev->device);
	function = pci_get_function(octeon_dev->device);

	atomic_store_rel_int(&octeon_dev->status, LIO_DEV_BEGIN_STATE);

	/* Enable access to the octeon device */
	if (pci_enable_busmaster(octeon_dev->device)) {
		lio_dev_err(octeon_dev, "pci_enable_device failed\n");
		return (1);
	}

	atomic_store_rel_int(&octeon_dev->status, LIO_DEV_PCI_ENABLE_DONE);

	/* Identify the Octeon type and map the BAR address space. */
	if (lio_chip_specific_setup(octeon_dev)) {
		lio_dev_err(octeon_dev, "Chip specific setup failed\n");
		return (1);
	}

	atomic_store_rel_int(&octeon_dev->status, LIO_DEV_PCI_MAP_DONE);

	/*
	 * Only add a reference after setting status 'OCT_DEV_PCI_MAP_DONE',
	 * since that is what is required for the reference to be removed
	 * during de-initialization (see 'octeon_destroy_resources').
	 */
	lio_register_device(octeon_dev, bus, dev, function, true);


	octeon_dev->app_mode = LIO_DRV_INVALID_APP;

	if (!lio_cn23xx_pf_fw_loaded(octeon_dev) && !fw_type_is_none()) {
		fw_loaded = 0;
		/* Do a soft reset of the Octeon device. */
		if (octeon_dev->fn_list.soft_reset(octeon_dev))
			return (1);

		/* things might have changed */
		if (!lio_cn23xx_pf_fw_loaded(octeon_dev))
			fw_loaded = 0;
		else
			fw_loaded = 1;
	} else {
		fw_loaded = 1;
	}

	/*
	 * Initialize the dispatch mechanism used to push packets arriving on
	 * Octeon Output queues.
	 */
	if (lio_init_dispatch_list(octeon_dev))
		return (1);

	lio_register_dispatch_fn(octeon_dev, LIO_OPCODE_NIC,
				 LIO_OPCODE_NIC_CORE_DRV_ACTIVE,
				 lio_core_drv_init, octeon_dev);
	atomic_store_rel_int(&octeon_dev->status, LIO_DEV_DISPATCH_INIT_DONE);

	ret = octeon_dev->fn_list.setup_device_regs(octeon_dev);
	if (ret) {
		lio_dev_err(octeon_dev,
			    "Failed to configure device registers\n");
		return (ret);
	}

	/* Initialize soft command buffer pool */
	if (lio_setup_sc_buffer_pool(octeon_dev)) {
		lio_dev_err(octeon_dev, "sc buffer pool allocation failed\n");
		return (1);
	}

	atomic_store_rel_int(&octeon_dev->status,
			     LIO_DEV_SC_BUFF_POOL_INIT_DONE);

	if (lio_allocate_ioq_vector(octeon_dev)) {
		lio_dev_err(octeon_dev,
			    "IOQ vector allocation failed\n");
		return (1);
	}

	atomic_store_rel_int(&octeon_dev->status,
			     LIO_DEV_MSIX_ALLOC_VECTOR_DONE);

	for (i = 0; i < LIO_MAX_POSSIBLE_INSTR_QUEUES; i++) {
		octeon_dev->instr_queue[i] =
			malloc(sizeof(struct lio_instr_queue),
			       M_DEVBUF, M_NOWAIT | M_ZERO);
		if (octeon_dev->instr_queue[i] == NULL)
			return (1);
	}

	/* Setup the data structures that manage this Octeon's Input queues. */
	if (lio_setup_instr_queue0(octeon_dev)) {
		lio_dev_err(octeon_dev,
			    "Instruction queue initialization failed\n");
		return (1);
	}

	atomic_store_rel_int(&octeon_dev->status,
			     LIO_DEV_INSTR_QUEUE_INIT_DONE);

	/*
	 * Initialize lists to manage the requests of different types that
	 * arrive from user & kernel applications for this octeon device.
	 */

	if (lio_setup_response_list(octeon_dev)) {
		lio_dev_err(octeon_dev, "Response list allocation failed\n");
		return (1);
	}

	atomic_store_rel_int(&octeon_dev->status, LIO_DEV_RESP_LIST_INIT_DONE);

	for (i = 0; i < LIO_MAX_POSSIBLE_OUTPUT_QUEUES; i++) {
		octeon_dev->droq[i] = malloc(sizeof(*octeon_dev->droq[i]),
					     M_DEVBUF, M_NOWAIT | M_ZERO);
		if (octeon_dev->droq[i] == NULL)
			return (1);
	}

	if (lio_setup_output_queue0(octeon_dev)) {
		lio_dev_err(octeon_dev, "Output queue initialization failed\n");
		return (1);
	}

	atomic_store_rel_int(&octeon_dev->status, LIO_DEV_DROQ_INIT_DONE);

	/*
	 * Setup the interrupt handler and record the INT SUM register address
	 */
	if (lio_setup_interrupt(octeon_dev,
				octeon_dev->sriov_info.num_pf_rings))
		return (1);

	/* Enable Octeon device interrupts */
	octeon_dev->fn_list.enable_interrupt(octeon_dev, OCTEON_ALL_INTR);

	atomic_store_rel_int(&octeon_dev->status, LIO_DEV_INTR_SET_DONE);

	/*
	 * Send Credit for Octeon Output queues. Credits are always sent BEFORE
	 * the output queue is enabled.
	 * This ensures that we'll receive the f/w CORE DRV_ACTIVE message in
	 * case we've configured CN23XX_SLI_GBL_CONTROL[NOPTR_D] = 0.
	 * Otherwise, it is possible that the DRV_ACTIVE message will be sent
	 * before any credits have been issued, causing the ring to be reset
	 * (and the f/w appear to never have started).
	 */
	for (j = 0; j < octeon_dev->num_oqs; j++)
		lio_write_csr32(octeon_dev,
				octeon_dev->droq[j]->pkts_credit_reg,
				octeon_dev->droq[j]->max_count);

	/* Enable the input and output queues for this Octeon device */
	ret = octeon_dev->fn_list.enable_io_queues(octeon_dev);
	if (ret) {
		lio_dev_err(octeon_dev, "Failed to enable input/output queues");
		return (ret);
	}

	atomic_store_rel_int(&octeon_dev->status, LIO_DEV_IO_QUEUES_DONE);

	if (!fw_loaded) {
		lio_dev_dbg(octeon_dev, "Waiting for DDR initialization...\n");
		if (!ddr_timeout) {
			lio_dev_info(octeon_dev,
				     "WAITING. Set ddr_timeout to non-zero value to proceed with initialization.\n");
		}

		lio_sleep_timeout(LIO_RESET_MSECS);

		/*
		 * Wait for the octeon to initialize DDR after the
		 * soft-reset.
		 */
		while (!ddr_timeout) {
			if (pause("-", lio_ms_to_ticks(100))) {
				/* user probably pressed Control-C */
				return (1);
			}
		}

		ret = lio_wait_for_ddr_init(octeon_dev, &ddr_timeout);
		if (ret) {
			lio_dev_err(octeon_dev,
				    "DDR not initialized. Please confirm that board is configured to boot from Flash, ret: %d\n",
				    ret);
			return (1);
		}

		if (lio_wait_for_bootloader(octeon_dev, 1100)) {
			lio_dev_err(octeon_dev, "Board not responding\n");
			return (1);
		}

		/* Divert uboot to take commands from host instead. */
		ret = lio_console_send_cmd(octeon_dev, bootcmd, 50);

		lio_dev_dbg(octeon_dev, "Initializing consoles\n");
		ret = lio_init_consoles(octeon_dev);
		if (ret) {
			lio_dev_err(octeon_dev, "Could not access board consoles\n");
			return (1);
		}

		/*
		 * If console debug enabled, specify empty string to
		 * use default enablement ELSE specify NULL string for
		 * 'disabled'.
		 */
		dbg_enb = lio_console_debug_enabled(0) ? "" : NULL;
		ret = lio_add_console(octeon_dev, 0, dbg_enb);

		if (ret) {
			lio_dev_err(octeon_dev, "Could not access board console\n");
			return (1);
		} else if (lio_console_debug_enabled(0)) {
			/*
			 * If console was added AND we're logging console output
			 * then set our console print function.
			 */
			octeon_dev->console[0].print = lio_dbg_console_print;
		}

		atomic_store_rel_int(&octeon_dev->status,
				     LIO_DEV_CONSOLE_INIT_DONE);

		lio_dev_dbg(octeon_dev, "Loading firmware\n");

		ret = lio_load_firmware(octeon_dev);
		if (ret) {
			lio_dev_err(octeon_dev, "Could not load firmware to board\n");
			return (1);
		}
	}

	atomic_store_rel_int(&octeon_dev->status, LIO_DEV_HOST_OK);

	return (0);
}

/*
 * \brief PCI FLR for each Octeon device.
 * @param oct octeon device
 */
static void
lio_pci_flr(struct octeon_device *oct)
{
	uint32_t	exppos, status;

	pci_find_cap(oct->device, PCIY_EXPRESS, &exppos);

	pci_save_state(oct->device);

	/* Quiesce the device completely */
	pci_write_config(oct->device, PCIR_COMMAND, PCIM_CMD_INTxDIS, 2);

	/* Wait for Transaction Pending bit clean */
	lio_mdelay(100);

	status = pci_read_config(oct->device, exppos + PCIER_DEVICE_STA, 2);
	if (status & PCIEM_STA_TRANSACTION_PND) {
		lio_dev_info(oct, "Function reset incomplete after 100ms, sleeping for 5 seconds\n");
		lio_mdelay(5);

		status = pci_read_config(oct->device, exppos + PCIER_DEVICE_STA, 2);
		if (status & PCIEM_STA_TRANSACTION_PND)
			lio_dev_info(oct, "Function reset still incomplete after 5s, reset anyway\n");
	}

	pci_write_config(oct->device, exppos + PCIER_DEVICE_CTL, PCIEM_CTL_INITIATE_FLR, 2);
	lio_mdelay(100);

	pci_restore_state(oct->device);
}

/*
 * \brief Debug console print function
 * @param octeon_dev  octeon device
 * @param console_num console number
 * @param prefix      first portion of line to display
 * @param suffix      second portion of line to display
 *
 * The OCTEON debug console outputs entire lines (excluding '\n').
 * Normally, the line will be passed in the 'prefix' parameter.
 * However, due to buffering, it is possible for a line to be split into two
 * parts, in which case they will be passed as the 'prefix' parameter and
 * 'suffix' parameter.
 */
static int
lio_dbg_console_print(struct octeon_device *oct, uint32_t console_num,
		      char *prefix, char *suffix)
{

	if (prefix != NULL && suffix != NULL)
		lio_dev_info(oct, "%u: %s%s\n", console_num, prefix, suffix);
	else if (prefix != NULL)
		lio_dev_info(oct, "%u: %s\n", console_num, prefix);
	else if (suffix != NULL)
		lio_dev_info(oct, "%u: %s\n", console_num, suffix);

	return (0);
}

static void
lio_watchdog(void *param)
{
	int		core_num;
	uint16_t	mask_of_crashed_or_stuck_cores = 0;
	struct octeon_device	*oct = param;
	bool		err_msg_was_printed[12];

	bzero(err_msg_was_printed, sizeof(err_msg_was_printed));

	while (1) {
		kproc_suspend_check(oct->watchdog_task);
		mask_of_crashed_or_stuck_cores =
			(uint16_t)lio_read_csr64(oct, LIO_CN23XX_SLI_SCRATCH2);

		if (mask_of_crashed_or_stuck_cores) {
			struct octeon_device *other_oct;

			oct->cores_crashed = true;
			other_oct = lio_get_other_octeon_device(oct);
			if (other_oct != NULL)
				other_oct->cores_crashed = true;

			for (core_num = 0; core_num < LIO_MAX_CORES;
			     core_num++) {
				bool core_crashed_or_got_stuck;

				core_crashed_or_got_stuck =
				    (mask_of_crashed_or_stuck_cores >>
				     core_num) & 1;
				if (core_crashed_or_got_stuck &&
				    !err_msg_was_printed[core_num]) {
					lio_dev_err(oct,
						    "ERROR: Octeon core %d crashed or got stuck! See oct-fwdump for details.\n",
						    core_num);
					err_msg_was_printed[core_num] = true;
				}
			}

		}

		/* sleep for two seconds */
		pause("-", lio_ms_to_ticks(2000));
	}
}

static int
lio_chip_specific_setup(struct octeon_device *oct)
{
	char		*s;
	uint32_t	dev_id, rev_id;
	int		ret = 1;

	dev_id = lio_read_pci_cfg(oct, 0);
	rev_id = pci_get_revid(oct->device);
	oct->subdevice_id = pci_get_subdevice(oct->device);

	switch (dev_id) {
	case LIO_CN23XX_PF_PCIID:
		oct->chip_id = LIO_CN23XX_PF_VID;
		if (pci_get_function(oct->device) == 0) {
			if (num_queues_per_pf0 < 0) {
				lio_dev_info(oct, "Invalid num_queues_per_pf0: %d, Setting it to default\n",
					     num_queues_per_pf0);
				num_queues_per_pf0 = 0;
			}

			oct->sriov_info.num_pf_rings = num_queues_per_pf0;
		} else {
			if (num_queues_per_pf1 < 0) {
				lio_dev_info(oct, "Invalid num_queues_per_pf1: %d, Setting it to default\n",
					     num_queues_per_pf1);
				num_queues_per_pf1 = 0;
			}

			oct->sriov_info.num_pf_rings = num_queues_per_pf1;
		}

		ret = lio_cn23xx_pf_setup_device(oct);
		s = "CN23XX";
		break;

	default:
		s = "?";
		lio_dev_err(oct, "Unknown device found (dev_id: %x)\n", dev_id);
	}

	if (!ret)
		lio_dev_info(oct, "%s PASS%d.%d %s Version: %s\n", s,
			     OCTEON_MAJOR_REV(oct), OCTEON_MINOR_REV(oct),
			     lio_get_conf(oct)->card_name, LIO_VERSION);

	return (ret);
}

static struct octeon_device *
lio_get_other_octeon_device(struct octeon_device *oct)
{
	struct octeon_device	*other_oct;

	other_oct = lio_get_device(oct->octeon_id + 1);

	if ((other_oct != NULL) && other_oct->device) {
		int	oct_busnum, other_oct_busnum;

		oct_busnum = pci_get_bus(oct->device);
		other_oct_busnum = pci_get_bus(other_oct->device);

		if (oct_busnum == other_oct_busnum) {
			int	oct_slot, other_oct_slot;

			oct_slot = pci_get_slot(oct->device);
			other_oct_slot = pci_get_slot(other_oct->device);

			if (oct_slot == other_oct_slot)
				return (other_oct);
		}
	}
	return (NULL);
}

/*
 * \brief Load firmware to device
 * @param oct octeon device
 *
 * Maps device to firmware filename, requests firmware, and downloads it
 */
static int
lio_load_firmware(struct octeon_device *oct)
{
	const struct firmware	*fw;
	char	*tmp_fw_type = NULL;
	int	ret = 0;
	char	fw_name[LIO_MAX_FW_FILENAME_LEN];

	if (fw_type[0] == '\0')
		tmp_fw_type = LIO_FW_NAME_TYPE_NIC;
	else
		tmp_fw_type = fw_type;

	sprintf(fw_name, "%s%s_%s%s", LIO_FW_BASE_NAME,
		lio_get_conf(oct)->card_name, tmp_fw_type, LIO_FW_NAME_SUFFIX);

	fw = firmware_get(fw_name);
	if (fw == NULL) {
		lio_dev_err(oct, "Request firmware failed. Could not find file %s.\n",
			    fw_name);
		return (EINVAL);
	}

	ret = lio_download_firmware(oct, fw->data, fw->datasize);

	firmware_put(fw, FIRMWARE_UNLOAD);

	return (ret);
}

static int
lio_nic_starter(struct octeon_device *oct)
{
	int	ret = 0;

	atomic_store_rel_int(&oct->status, LIO_DEV_RUNNING);

	if (oct->app_mode && oct->app_mode == LIO_DRV_NIC_APP) {
		if (lio_init_nic_module(oct)) {
			lio_dev_err(oct, "NIC initialization failed\n");
			ret = -1;
#ifdef CAVIUM_ONiLY_23XX_VF
		} else {
			if (octeon_enable_sriov(oct) < 0)
				ret = -1;
#endif
		}
	} else {
		lio_dev_err(oct,
			    "Unexpected application running on NIC (%d). Check firmware.\n",
			    oct->app_mode);
		ret = -1;
	}

	return (ret);
}

static int
lio_init_nic_module(struct octeon_device *oct)
{
	int	num_nic_ports = LIO_GET_NUM_NIC_PORTS_CFG(lio_get_conf(oct));
	int	retval = 0;

	lio_dev_dbg(oct, "Initializing network interfaces\n");

	/*
	 * only default iq and oq were initialized
	 * initialize the rest as well
	 */

	/* run port_config command for each port */
	oct->ifcount = num_nic_ports;

	bzero(&oct->props, sizeof(struct lio_if_props));

	oct->props.gmxport = -1;

	retval = lio_setup_nic_devices(oct);
	if (retval) {
		lio_dev_err(oct, "Setup NIC devices failed\n");
		goto lio_init_failure;
	}

	lio_dev_dbg(oct, "Network interfaces ready\n");

	return (retval);

lio_init_failure:

	oct->ifcount = 0;

	return (retval);
}

static int
lio_ifmedia_update(struct ifnet *ifp)
{
	struct lio	*lio = if_getsoftc(ifp);
	struct ifmedia	*ifm;

	ifm = &lio->ifmedia;

	/* We only support Ethernet media type. */
	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		break;
	case IFM_10G_CX4:
	case IFM_10G_SR:
	case IFM_10G_T:
	case IFM_10G_TWINAX:
	default:
		/* We don't support changing the media type. */
		lio_dev_err(lio->oct_dev, "Invalid media type (%d)\n",
			    IFM_SUBTYPE(ifm->ifm_media));
		return (EINVAL);
	}

	return (0);
}

static int
lio_get_media_subtype(struct octeon_device *oct)
{

	switch(oct->subdevice_id) {
	case LIO_CN2350_10G_SUBDEVICE:
	case LIO_CN2350_10G_SUBDEVICE1:
	case LIO_CN2360_10G_SUBDEVICE:
		return (IFM_10G_SR);

	case LIO_CN2350_25G_SUBDEVICE:
	case LIO_CN2360_25G_SUBDEVICE:
		return (IFM_25G_SR);
	}

	return (IFM_10G_SR);
}

static uint64_t
lio_get_baudrate(struct octeon_device *oct)
{

	switch(oct->subdevice_id) {
	case LIO_CN2350_10G_SUBDEVICE:
	case LIO_CN2350_10G_SUBDEVICE1:
	case LIO_CN2360_10G_SUBDEVICE:
		return (IF_Gbps(10));

	case LIO_CN2350_25G_SUBDEVICE:
	case LIO_CN2360_25G_SUBDEVICE:
		return (IF_Gbps(25));
	}

	return (IF_Gbps(10));
}

static void
lio_ifmedia_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct lio	*lio = if_getsoftc(ifp);

	/* Report link down if the driver isn't running. */
	if (!lio_ifstate_check(lio, LIO_IFSTATE_RUNNING)) {
		ifmr->ifm_active |= IFM_NONE;
		return;
	}

	/* Setup the default interface info. */
	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (lio->linfo.link.s.link_up) {
		ifmr->ifm_status |= IFM_ACTIVE;
	} else {
		ifmr->ifm_active |= IFM_NONE;
		return;
	}

	ifmr->ifm_active |= lio_get_media_subtype(lio->oct_dev);

	if (lio->linfo.link.s.duplex)
		ifmr->ifm_active |= IFM_FDX;
	else
		ifmr->ifm_active |= IFM_HDX;
}

static uint64_t
lio_get_counter(if_t ifp, ift_counter cnt)
{
	struct lio	*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	uint64_t	counter = 0;
	int		i, q_no;

	switch (cnt) {
	case IFCOUNTER_IPACKETS:
		for (i = 0; i < oct->num_oqs; i++) {
			q_no = lio->linfo.rxpciq[i].s.q_no;
			counter += oct->droq[q_no]->stats.rx_pkts_received;
		}
		break;
	case IFCOUNTER_OPACKETS:
		for (i = 0; i < oct->num_iqs; i++) {
			q_no = lio->linfo.txpciq[i].s.q_no;
			counter += oct->instr_queue[q_no]->stats.tx_done;
		}
		break;
	case IFCOUNTER_IBYTES:
		for (i = 0; i < oct->num_oqs; i++) {
			q_no = lio->linfo.rxpciq[i].s.q_no;
			counter += oct->droq[q_no]->stats.rx_bytes_received;
		}
		break;
	case IFCOUNTER_OBYTES:
		for (i = 0; i < oct->num_iqs; i++) {
			q_no = lio->linfo.txpciq[i].s.q_no;
			counter += oct->instr_queue[q_no]->stats.tx_tot_bytes;
		}
		break;
	case IFCOUNTER_IQDROPS:
		for (i = 0; i < oct->num_oqs; i++) {
			q_no = lio->linfo.rxpciq[i].s.q_no;
			counter += oct->droq[q_no]->stats.rx_dropped;
		}
		break;
	case IFCOUNTER_OQDROPS:
		for (i = 0; i < oct->num_iqs; i++) {
			q_no = lio->linfo.txpciq[i].s.q_no;
			counter += oct->instr_queue[q_no]->stats.tx_dropped;
		}
		break;
	case IFCOUNTER_IMCASTS:
		counter = oct->link_stats.fromwire.total_mcst;
		break;
	case IFCOUNTER_OMCASTS:
		counter = oct->link_stats.fromhost.mcast_pkts_sent;
		break;
	case IFCOUNTER_COLLISIONS:
		counter = oct->link_stats.fromhost.total_collisions;
		break;
	case IFCOUNTER_IERRORS:
		counter = oct->link_stats.fromwire.fcs_err +
		    oct->link_stats.fromwire.l2_err +
		    oct->link_stats.fromwire.frame_err;
		break;
	default:
		return (if_get_counter_default(ifp, cnt));
	}

	return (counter);
}

static int
lio_init_ifnet(struct lio *lio)
{
	struct octeon_device	*oct = lio->oct_dev;
	if_t ifp = lio->ifp;

	/* ifconfig entrypoint for media type/status reporting */
	ifmedia_init(&lio->ifmedia, IFM_IMASK, lio_ifmedia_update,
		     lio_ifmedia_status);

	/* set the default interface values */
	ifmedia_add(&lio->ifmedia,
		    (IFM_ETHER | IFM_FDX | lio_get_media_subtype(oct)),
		    0, NULL);
	ifmedia_add(&lio->ifmedia, (IFM_ETHER | IFM_AUTO), 0, NULL);
	ifmedia_set(&lio->ifmedia, (IFM_ETHER | IFM_AUTO));

	lio->ifmedia.ifm_media = lio->ifmedia.ifm_cur->ifm_media;
	lio_dev_dbg(oct, "IFMEDIA flags : %x\n", lio->ifmedia.ifm_media);

	if_initname(ifp, device_get_name(oct->device),
		    device_get_unit(oct->device));
	if_setflags(ifp, (IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST));
	if_setioctlfn(ifp, lio_ioctl);
	if_setgetcounterfn(ifp, lio_get_counter);
	if_settransmitfn(ifp, lio_mq_start);
	if_setqflushfn(ifp, lio_qflush);
	if_setinitfn(ifp, lio_open);
	if_setmtu(ifp, lio->linfo.link.s.mtu);
	lio->mtu = lio->linfo.link.s.mtu;
	if_sethwassist(ifp, (CSUM_IP | CSUM_TCP | CSUM_UDP | CSUM_TSO |
			     CSUM_TCP_IPV6 | CSUM_UDP_IPV6));

	if_setcapabilitiesbit(ifp, (IFCAP_HWCSUM | IFCAP_HWCSUM_IPV6 |
				    IFCAP_TSO | IFCAP_LRO |
				    IFCAP_JUMBO_MTU | IFCAP_HWSTATS |
				    IFCAP_LINKSTATE | IFCAP_VLAN_HWFILTER |
				    IFCAP_VLAN_HWCSUM | IFCAP_VLAN_HWTAGGING |
				    IFCAP_VLAN_HWTSO | IFCAP_VLAN_MTU), 0);

	if_setcapenable(ifp, if_getcapabilities(ifp));
	if_setbaudrate(ifp, lio_get_baudrate(oct));

	return (0);
}

static void
lio_tcp_lro_free(struct octeon_device *octeon_dev, struct ifnet *ifp)
{
	struct lio	*lio = if_getsoftc(ifp);
	struct lio_droq	*droq;
	int		q_no;
	int		i;

	for (i = 0; i < octeon_dev->num_oqs; i++) {
		q_no = lio->linfo.rxpciq[i].s.q_no;
		droq = octeon_dev->droq[q_no];
		if (droq->lro.ifp) {
			tcp_lro_free(&droq->lro);
			droq->lro.ifp = NULL;
		}
	}
}

static int
lio_tcp_lro_init(struct octeon_device *octeon_dev, struct ifnet *ifp)
{
	struct lio	*lio = if_getsoftc(ifp);
	struct lio_droq	*droq;
	struct lro_ctrl	*lro;
	int		i, q_no, ret = 0;

	for (i = 0; i < octeon_dev->num_oqs; i++) {
		q_no = lio->linfo.rxpciq[i].s.q_no;
		droq = octeon_dev->droq[q_no];
		lro = &droq->lro;
		ret = tcp_lro_init(lro);
		if (ret) {
			lio_dev_err(octeon_dev, "LRO Initialization failed ret %d\n",
				    ret);
			goto lro_init_failed;
		}

		lro->ifp = ifp;
	}

	return (ret);

lro_init_failed:
	lio_tcp_lro_free(octeon_dev, ifp);

	return (ret);
}

static int
lio_setup_nic_devices(struct octeon_device *octeon_dev)
{
	union		octeon_if_cfg if_cfg;
	struct lio	*lio = NULL;
	struct ifnet	*ifp = NULL;
	struct lio_version		*vdata;
	struct lio_soft_command		*sc;
	struct lio_if_cfg_context	*ctx;
	struct lio_if_cfg_resp		*resp;
	struct lio_if_props		*props;
	int		num_iqueues, num_oqueues, retval;
	unsigned int	base_queue;
	unsigned int	gmx_port_id;
	uint32_t	ctx_size, data_size;
	uint32_t	ifidx_or_pfnum, resp_size;
	uint8_t		mac[ETHER_HDR_LEN], i, j;

	/* This is to handle link status changes */
	lio_register_dispatch_fn(octeon_dev, LIO_OPCODE_NIC,
				 LIO_OPCODE_NIC_INFO,
				 lio_link_info, octeon_dev);

	for (i = 0; i < octeon_dev->ifcount; i++) {
		resp_size = sizeof(struct lio_if_cfg_resp);
		ctx_size = sizeof(struct lio_if_cfg_context);
		data_size = sizeof(struct lio_version);
		sc = lio_alloc_soft_command(octeon_dev, data_size, resp_size,
					    ctx_size);
		if (sc == NULL)
			return (ENOMEM);

		resp = (struct lio_if_cfg_resp *)sc->virtrptr;
		ctx = (struct lio_if_cfg_context *)sc->ctxptr;
		vdata = (struct lio_version *)sc->virtdptr;

		*((uint64_t *)vdata) = 0;
		vdata->major = htobe16(LIO_BASE_MAJOR_VERSION);
		vdata->minor = htobe16(LIO_BASE_MINOR_VERSION);
		vdata->micro = htobe16(LIO_BASE_MICRO_VERSION);

		num_iqueues = octeon_dev->sriov_info.num_pf_rings;
		num_oqueues = octeon_dev->sriov_info.num_pf_rings;
		base_queue = octeon_dev->sriov_info.pf_srn;

		gmx_port_id = octeon_dev->pf_num;
		ifidx_or_pfnum = octeon_dev->pf_num;

		lio_dev_dbg(octeon_dev, "requesting config for interface %d, iqs %d, oqs %d\n",
			    ifidx_or_pfnum, num_iqueues, num_oqueues);
		ctx->cond = 0;
		ctx->octeon_id = lio_get_device_id(octeon_dev);

		if_cfg.if_cfg64 = 0;
		if_cfg.s.num_iqueues = num_iqueues;
		if_cfg.s.num_oqueues = num_oqueues;
		if_cfg.s.base_queue = base_queue;
		if_cfg.s.gmx_port_id = gmx_port_id;

		sc->iq_no = 0;

		lio_prepare_soft_command(octeon_dev, sc, LIO_OPCODE_NIC,
					 LIO_OPCODE_NIC_IF_CFG, 0,
					 if_cfg.if_cfg64, 0);

		sc->callback = lio_if_cfg_callback;
		sc->callback_arg = sc;
		sc->wait_time = 3000;

		retval = lio_send_soft_command(octeon_dev, sc);
		if (retval == LIO_IQ_SEND_FAILED) {
			lio_dev_err(octeon_dev, "iq/oq config failed status: %x\n",
				    retval);
			/* Soft instr is freed by driver in case of failure. */
			goto setup_nic_dev_fail;
		}

		/*
		 * Sleep on a wait queue till the cond flag indicates that the
		 * response arrived or timed-out.
		 */
		lio_sleep_cond(octeon_dev, &ctx->cond);

		retval = resp->status;
		if (retval) {
			lio_dev_err(octeon_dev, "iq/oq config failed\n");
			goto setup_nic_dev_fail;
		}

		lio_swap_8B_data((uint64_t *)(&resp->cfg_info),
				 (sizeof(struct octeon_if_cfg_info)) >> 3);

		num_iqueues = bitcount64(resp->cfg_info.iqmask);
		num_oqueues = bitcount64(resp->cfg_info.oqmask);

		if (!(num_iqueues) || !(num_oqueues)) {
			lio_dev_err(octeon_dev,
				    "Got bad iqueues (%016llX) or oqueues (%016llX) from firmware.\n",
				    LIO_CAST64(resp->cfg_info.iqmask),
				    LIO_CAST64(resp->cfg_info.oqmask));
			goto setup_nic_dev_fail;
		}

		lio_dev_dbg(octeon_dev,
			    "interface %d, iqmask %016llx, oqmask %016llx, numiqueues %d, numoqueues %d\n",
			    i, LIO_CAST64(resp->cfg_info.iqmask),
			    LIO_CAST64(resp->cfg_info.oqmask),
			    num_iqueues, num_oqueues);

		ifp = if_alloc(IFT_ETHER);

		if (ifp == NULL) {
			lio_dev_err(octeon_dev, "Device allocation failed\n");
			goto setup_nic_dev_fail;
		}

		lio = malloc(sizeof(struct lio), M_DEVBUF, M_NOWAIT | M_ZERO);

		if (lio == NULL) {
			lio_dev_err(octeon_dev, "Lio allocation failed\n");
			goto setup_nic_dev_fail;
		}

		if_setsoftc(ifp, lio);

		ifp->if_hw_tsomax = LIO_MAX_FRAME_SIZE;
		ifp->if_hw_tsomaxsegcount = LIO_MAX_SG;
		ifp->if_hw_tsomaxsegsize = PAGE_SIZE;

		lio->ifidx = ifidx_or_pfnum;

		props = &octeon_dev->props;
		props->gmxport = resp->cfg_info.linfo.gmxport;
		props->ifp = ifp;

		lio->linfo.num_rxpciq = num_oqueues;
		lio->linfo.num_txpciq = num_iqueues;
		for (j = 0; j < num_oqueues; j++) {
			lio->linfo.rxpciq[j].rxpciq64 =
			    resp->cfg_info.linfo.rxpciq[j].rxpciq64;
		}

		for (j = 0; j < num_iqueues; j++) {
			lio->linfo.txpciq[j].txpciq64 =
			    resp->cfg_info.linfo.txpciq[j].txpciq64;
		}

		lio->linfo.hw_addr = resp->cfg_info.linfo.hw_addr;
		lio->linfo.gmxport = resp->cfg_info.linfo.gmxport;
		lio->linfo.link.link_status64 =
		    resp->cfg_info.linfo.link.link_status64;

		/*
		 * Point to the properties for octeon device to which this
		 * interface belongs.
		 */
		lio->oct_dev = octeon_dev;
		lio->ifp = ifp;

		lio_dev_dbg(octeon_dev, "if%d gmx: %d hw_addr: 0x%llx\n", i,
			    lio->linfo.gmxport, LIO_CAST64(lio->linfo.hw_addr));
		lio_init_ifnet(lio);
		/* 64-bit swap required on LE machines */
		lio_swap_8B_data(&lio->linfo.hw_addr, 1);
		for (j = 0; j < 6; j++)
			mac[j] = *((uint8_t *)(
				   ((uint8_t *)&lio->linfo.hw_addr) + 2 + j));

		ether_ifattach(ifp, mac);

		/*
		 * By default all interfaces on a single Octeon uses the same
		 * tx and rx queues
		 */
		lio->txq = lio->linfo.txpciq[0].s.q_no;
		lio->rxq = lio->linfo.rxpciq[0].s.q_no;
		if (lio_setup_io_queues(octeon_dev, i, lio->linfo.num_txpciq,
					lio->linfo.num_rxpciq)) {
			lio_dev_err(octeon_dev, "I/O queues creation failed\n");
			goto setup_nic_dev_fail;
		}

		lio_ifstate_set(lio, LIO_IFSTATE_DROQ_OPS);

		lio->tx_qsize = lio_get_tx_qsize(octeon_dev, lio->txq);
		lio->rx_qsize = lio_get_rx_qsize(octeon_dev, lio->rxq);

		if (lio_setup_glists(octeon_dev, lio, num_iqueues)) {
			lio_dev_err(octeon_dev, "Gather list allocation failed\n");
			goto setup_nic_dev_fail;
		}

		if ((lio_hwlro == 0) && lio_tcp_lro_init(octeon_dev, ifp))
			goto setup_nic_dev_fail;

		if (lio_hwlro &&
		    (if_getcapenable(ifp) & IFCAP_LRO) &&
		    (if_getcapenable(ifp) & IFCAP_RXCSUM) &&
		    (if_getcapenable(ifp) & IFCAP_RXCSUM_IPV6))
			lio_set_feature(ifp, LIO_CMD_LRO_ENABLE,
					LIO_LROIPV4 | LIO_LROIPV6);

		if ((if_getcapenable(ifp) & IFCAP_VLAN_HWFILTER))
			lio_set_feature(ifp, LIO_CMD_VLAN_FILTER_CTL, 1);
		else
			lio_set_feature(ifp, LIO_CMD_VLAN_FILTER_CTL, 0);

		if (lio_setup_rx_oom_poll_fn(ifp))
			goto setup_nic_dev_fail;

		lio_dev_dbg(octeon_dev, "Setup NIC ifidx:%d mac:%02x%02x%02x%02x%02x%02x\n",
			    i, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		lio->link_changes++;

		lio_ifstate_set(lio, LIO_IFSTATE_REGISTERED);

		/*
		 * Sending command to firmware to enable Rx checksum offload
		 * by default at the time of setup of Liquidio driver for
		 * this device
		 */
		lio_set_rxcsum_command(ifp, LIO_CMD_TNL_RX_CSUM_CTL,
				       LIO_CMD_RXCSUM_ENABLE);
		lio_set_feature(ifp, LIO_CMD_TNL_TX_CSUM_CTL,
				LIO_CMD_TXCSUM_ENABLE);

#ifdef RSS
		if (lio_rss) {
			if (lio_send_rss_param(lio))
				goto setup_nic_dev_fail;
		} else
#endif	/* RSS */

			lio_set_feature(ifp, LIO_CMD_SET_FNV,
					LIO_CMD_FNV_ENABLE);

		lio_dev_dbg(octeon_dev, "NIC ifidx:%d Setup successful\n", i);

		lio_free_soft_command(octeon_dev, sc);
		lio->vlan_attach =
		    EVENTHANDLER_REGISTER(vlan_config,
					  lio_vlan_rx_add_vid, lio,
					  EVENTHANDLER_PRI_FIRST);
		lio->vlan_detach =
		    EVENTHANDLER_REGISTER(vlan_unconfig,
					  lio_vlan_rx_kill_vid, lio,
					  EVENTHANDLER_PRI_FIRST);

		/* Update stats periodically */
		callout_init(&lio->stats_timer, 0);
		lio->stats_interval = LIO_DEFAULT_STATS_INTERVAL;

		lio_add_hw_stats(lio);
	}

	return (0);

setup_nic_dev_fail:

	lio_free_soft_command(octeon_dev, sc);

	while (i--) {
		lio_dev_err(octeon_dev, "NIC ifidx:%d Setup failed\n", i);
		lio_destroy_nic_device(octeon_dev, i);
	}

	return (ENODEV);
}

static int
lio_link_info(struct lio_recv_info *recv_info, void *ptr)
{
	struct octeon_device	*oct = (struct octeon_device *)ptr;
	struct lio_recv_pkt	*recv_pkt = recv_info->recv_pkt;
	union octeon_link_status *ls;
	int	gmxport = 0, i;

	lio_dev_dbg(oct, "%s Called\n", __func__);
	if (recv_pkt->buffer_size[0] != (sizeof(*ls) + LIO_DROQ_INFO_SIZE)) {
		lio_dev_err(oct, "Malformed NIC_INFO, len=%d, ifidx=%d\n",
			    recv_pkt->buffer_size[0],
			    recv_pkt->rh.r_nic_info.gmxport);
		goto nic_info_err;
	}
	gmxport = recv_pkt->rh.r_nic_info.gmxport;
	ls = (union octeon_link_status *)(recv_pkt->buffer_ptr[0]->m_data +
					  LIO_DROQ_INFO_SIZE);
	lio_swap_8B_data((uint64_t *)ls,
			 (sizeof(union octeon_link_status)) >> 3);

	if (oct->props.gmxport == gmxport)
		lio_update_link_status(oct->props.ifp, ls);

nic_info_err:
	for (i = 0; i < recv_pkt->buffer_count; i++)
		lio_recv_buffer_free(recv_pkt->buffer_ptr[i]);

	lio_free_recv_info(recv_info);
	return (0);
}

void
lio_free_mbuf(struct lio_instr_queue *iq, struct lio_mbuf_free_info *finfo)
{

	bus_dmamap_sync(iq->txtag, finfo->map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(iq->txtag, finfo->map);
	m_freem(finfo->mb);
}

void
lio_free_sgmbuf(struct lio_instr_queue *iq, struct lio_mbuf_free_info *finfo)
{
	struct lio_gather	*g;
	struct octeon_device	*oct;
	struct lio		*lio;
	int	iq_no;

	g = finfo->g;
	iq_no = iq->txpciq.s.q_no;
	oct = iq->oct_dev;
	lio = if_getsoftc(oct->props.ifp);

	mtx_lock(&lio->glist_lock[iq_no]);
	STAILQ_INSERT_TAIL(&lio->ghead[iq_no], &g->node, entries);
	mtx_unlock(&lio->glist_lock[iq_no]);

	bus_dmamap_sync(iq->txtag, finfo->map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(iq->txtag, finfo->map);
	m_freem(finfo->mb);
}

static void
lio_if_cfg_callback(struct octeon_device *oct, uint32_t status, void *buf)
{
	struct lio_soft_command	*sc = (struct lio_soft_command *)buf;
	struct lio_if_cfg_resp	*resp;
	struct lio_if_cfg_context *ctx;

	resp = (struct lio_if_cfg_resp *)sc->virtrptr;
	ctx = (struct lio_if_cfg_context *)sc->ctxptr;

	oct = lio_get_device(ctx->octeon_id);
	if (resp->status)
		lio_dev_err(oct, "nic if cfg instruction failed. Status: %llx (0x%08x)\n",
			    LIO_CAST64(resp->status), status);
	ctx->cond = 1;

	snprintf(oct->fw_info.lio_firmware_version, 32, "%s",
		 resp->cfg_info.lio_firmware_version);

	/*
	 * This barrier is required to be sure that the response has been
	 * written fully before waking up the handler
	 */
	wmb();
}

static int
lio_is_mac_changed(uint8_t *new, uint8_t *old)
{

	return ((new[0] != old[0]) || (new[1] != old[1]) ||
		(new[2] != old[2]) || (new[3] != old[3]) ||
		(new[4] != old[4]) || (new[5] != old[5]));
}

void
lio_open(void *arg)
{
	struct lio	*lio = arg;
	struct ifnet	*ifp = lio->ifp;
	struct octeon_device	*oct = lio->oct_dev;
	uint8_t	*mac_new, mac_old[ETHER_HDR_LEN];
	int	ret = 0;

	lio_ifstate_set(lio, LIO_IFSTATE_RUNNING);

	/* Ready for link status updates */
	lio->intf_open = 1;

	lio_dev_info(oct, "Interface Open, ready for traffic\n");

	/* tell Octeon to start forwarding packets to host */
	lio_send_rx_ctrl_cmd(lio, 1);

	mac_new = IF_LLADDR(ifp);
	memcpy(mac_old, ((uint8_t *)&lio->linfo.hw_addr) + 2, ETHER_HDR_LEN);

	if (lio_is_mac_changed(mac_new, mac_old)) {
		ret = lio_set_mac(ifp, mac_new);
		if (ret)
			lio_dev_err(oct, "MAC change failed, error: %d\n", ret);
	}

	/* Now inform the stack we're ready */
	if_setdrvflagbits(ifp, IFF_DRV_RUNNING, 0);

	lio_dev_info(oct, "Interface is opened\n");
}

static int
lio_set_rxcsum_command(struct ifnet *ifp, int command, uint8_t rx_cmd)
{
	struct lio_ctrl_pkt	nctrl;
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	int	ret = 0;

	nctrl.ncmd.cmd64 = 0;
	nctrl.ncmd.s.cmd = command;
	nctrl.ncmd.s.param1 = rx_cmd;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.lio = lio;
	nctrl.cb_fn = lio_ctrl_cmd_completion;

	ret = lio_send_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		lio_dev_err(oct, "DEVFLAGS RXCSUM change failed in core(ret:0x%x)\n",
			    ret);
	}

	return (ret);
}

static int
lio_stop_nic_module(struct octeon_device *oct)
{
	int		i, j;
	struct lio	*lio;

	lio_dev_dbg(oct, "Stopping network interfaces\n");
	if (!oct->ifcount) {
		lio_dev_err(oct, "Init for Octeon was not completed\n");
		return (1);
	}

	mtx_lock(&oct->cmd_resp_wqlock);
	oct->cmd_resp_state = LIO_DRV_OFFLINE;
	mtx_unlock(&oct->cmd_resp_wqlock);

	for (i = 0; i < oct->ifcount; i++) {
		lio = if_getsoftc(oct->props.ifp);
		for (j = 0; j < oct->num_oqs; j++)
			lio_unregister_droq_ops(oct,
						lio->linfo.rxpciq[j].s.q_no);
	}

	callout_drain(&lio->stats_timer);

	for (i = 0; i < oct->ifcount; i++)
		lio_destroy_nic_device(oct, i);

	lio_dev_dbg(oct, "Network interface stopped\n");

	return (0);
}

static void
lio_delete_glists(struct octeon_device *oct, struct lio *lio)
{
	struct lio_gather	*g;
	int	i;

	if (lio->glist_lock != NULL) {
		free((void *)lio->glist_lock, M_DEVBUF);
		lio->glist_lock = NULL;
	}

	if (lio->ghead == NULL)
		return;

	for (i = 0; i < lio->linfo.num_txpciq; i++) {
		do {
			g = (struct lio_gather *)
			    lio_delete_first_node(&lio->ghead[i]);
			free(g, M_DEVBUF);
		} while (g);

		if ((lio->glists_virt_base != NULL) &&
		    (lio->glists_virt_base[i] != NULL)) {
			lio_dma_free(lio->glist_entry_size * lio->tx_qsize,
				     lio->glists_virt_base[i]);
		}
	}

	free(lio->glists_virt_base, M_DEVBUF);
	lio->glists_virt_base = NULL;

	free(lio->glists_dma_base, M_DEVBUF);
	lio->glists_dma_base = NULL;

	free(lio->ghead, M_DEVBUF);
	lio->ghead = NULL;
}

static int
lio_setup_glists(struct octeon_device *oct, struct lio *lio, int num_iqs)
{
	struct lio_gather	*g;
	int	i, j;

	lio->glist_lock = malloc(num_iqs * sizeof(*lio->glist_lock), M_DEVBUF,
				 M_NOWAIT | M_ZERO);
	if (lio->glist_lock == NULL)
		return (1);

	lio->ghead = malloc(num_iqs * sizeof(*lio->ghead), M_DEVBUF,
			    M_NOWAIT | M_ZERO);
	if (lio->ghead == NULL) {
		free((void *)lio->glist_lock, M_DEVBUF);
		lio->glist_lock = NULL;
		return (1);
	}

	lio->glist_entry_size = ROUNDUP8((ROUNDUP4(LIO_MAX_SG) >> 2) *
					 LIO_SG_ENTRY_SIZE);
	/*
	 * allocate memory to store virtual and dma base address of
	 * per glist consistent memory
	 */
	lio->glists_virt_base = malloc(num_iqs * sizeof(void *), M_DEVBUF,
				       M_NOWAIT | M_ZERO);
	lio->glists_dma_base = malloc(num_iqs * sizeof(vm_paddr_t), M_DEVBUF,
				      M_NOWAIT | M_ZERO);
	if ((lio->glists_virt_base == NULL) || (lio->glists_dma_base == NULL)) {
		lio_delete_glists(oct, lio);
		return (1);
	}

	for (i = 0; i < num_iqs; i++) {
		mtx_init(&lio->glist_lock[i], "glist_lock", NULL, MTX_DEF);

		STAILQ_INIT(&lio->ghead[i]);

		lio->glists_virt_base[i] =
		    lio_dma_alloc(lio->glist_entry_size * lio->tx_qsize,
				  (vm_paddr_t *)&lio->glists_dma_base[i]);
		if (lio->glists_virt_base[i] == NULL) {
			lio_delete_glists(oct, lio);
			return (1);
		}

		for (j = 0; j < lio->tx_qsize; j++) {
			g = malloc(sizeof(*g), M_DEVBUF, M_NOWAIT | M_ZERO);
			if (g == NULL)
				break;

			g->sg = (struct lio_sg_entry *)(uintptr_t)
			    ((uint64_t)(uintptr_t)lio->glists_virt_base[i] +
			     (j * lio->glist_entry_size));
			g->sg_dma_ptr = (uint64_t)lio->glists_dma_base[i] +
				(j * lio->glist_entry_size);
			STAILQ_INSERT_TAIL(&lio->ghead[i], &g->node, entries);
		}

		if (j != lio->tx_qsize) {
			lio_delete_glists(oct, lio);
			return (1);
		}
	}

	return (0);
}

void
lio_stop(struct ifnet *ifp)
{
	struct lio	*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;

	lio_ifstate_reset(lio, LIO_IFSTATE_RUNNING);
	if_link_state_change(ifp, LINK_STATE_DOWN);

	lio->intf_open = 0;
	lio->linfo.link.s.link_up = 0;
	lio->link_changes++;

	lio_send_rx_ctrl_cmd(lio, 0);

	/* Tell the stack that the interface is no longer active */
	if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);

	lio_dev_info(oct, "Interface is stopped\n");
}

static void
lio_check_rx_oom_status(struct lio *lio)
{
	struct lio_droq	*droq;
	struct octeon_device *oct = lio->oct_dev;
	int	desc_refilled;
	int	q, q_no = 0;

	for (q = 0; q < oct->num_oqs; q++) {
		q_no = lio->linfo.rxpciq[q].s.q_no;
		droq = oct->droq[q_no];
		if (droq == NULL)
			continue;
		if (lio_read_csr32(oct, droq->pkts_credit_reg) <= 0x40) {
			mtx_lock(&droq->lock);
			desc_refilled = lio_droq_refill(oct, droq);
			/*
			 * Flush the droq descriptor data to memory to be sure
			 * that when we update the credits the data in memory
			 * is accurate.
			 */
			wmb();
			lio_write_csr32(oct, droq->pkts_credit_reg,
					desc_refilled);
			/* make sure mmio write completes */
			__compiler_membar();
			mtx_unlock(&droq->lock);
		}
	}
}

static void
lio_poll_check_rx_oom_status(void *arg, int pending __unused)
{
	struct lio_tq	*rx_status_tq = arg;
	struct lio	*lio = rx_status_tq->ctxptr;

	if (lio_ifstate_check(lio, LIO_IFSTATE_RUNNING))
		lio_check_rx_oom_status(lio);

	taskqueue_enqueue_timeout(rx_status_tq->tq, &rx_status_tq->work,
				  lio_ms_to_ticks(50));
}

static int
lio_setup_rx_oom_poll_fn(struct ifnet *ifp)
{
	struct lio	*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	struct lio_tq	*rx_status_tq;

	rx_status_tq = &lio->rx_status_tq;

	rx_status_tq->tq = taskqueue_create("lio_rx_oom_status", M_WAITOK,
					    taskqueue_thread_enqueue,
					    &rx_status_tq->tq);
	if (rx_status_tq->tq == NULL) {
		lio_dev_err(oct, "unable to create lio rx oom status tq\n");
		return (-1);
	}

	TIMEOUT_TASK_INIT(rx_status_tq->tq, &rx_status_tq->work, 0,
			  lio_poll_check_rx_oom_status, (void *)rx_status_tq);

	rx_status_tq->ctxptr = lio;

	taskqueue_start_threads(&rx_status_tq->tq, 1, PI_NET,
				"lio%d_rx_oom_status",
				oct->octeon_id);

	taskqueue_enqueue_timeout(rx_status_tq->tq, &rx_status_tq->work,
				  lio_ms_to_ticks(50));

	return (0);
}

static void
lio_cleanup_rx_oom_poll_fn(struct ifnet *ifp)
{
	struct lio	*lio = if_getsoftc(ifp);

	if (lio->rx_status_tq.tq != NULL) {
		while (taskqueue_cancel_timeout(lio->rx_status_tq.tq,
						&lio->rx_status_tq.work, NULL))
			taskqueue_drain_timeout(lio->rx_status_tq.tq,
						&lio->rx_status_tq.work);

		taskqueue_free(lio->rx_status_tq.tq);

		lio->rx_status_tq.tq = NULL;
	}
}

static void
lio_destroy_nic_device(struct octeon_device *oct, int ifidx)
{
	struct ifnet	*ifp = oct->props.ifp;
	struct lio	*lio;

	if (ifp == NULL) {
		lio_dev_err(oct, "%s No ifp ptr for index %d\n",
			    __func__, ifidx);
		return;
	}

	lio = if_getsoftc(ifp);

	lio_ifstate_set(lio, LIO_IFSTATE_DETACH);

	lio_dev_dbg(oct, "NIC device cleanup\n");

	if (atomic_load_acq_int(&lio->ifstate) & LIO_IFSTATE_RUNNING)
		lio_stop(ifp);

	if (lio_wait_for_pending_requests(oct))
		lio_dev_err(oct, "There were pending requests\n");

	if (lio_wait_for_instr_fetch(oct))
		lio_dev_err(oct, "IQ had pending instructions\n");

	if (lio_wait_for_oq_pkts(oct))
		lio_dev_err(oct, "OQ had pending packets\n");

	if (atomic_load_acq_int(&lio->ifstate) & LIO_IFSTATE_REGISTERED)
		ether_ifdetach(ifp);

	lio_tcp_lro_free(oct, ifp);

	lio_cleanup_rx_oom_poll_fn(ifp);

	lio_delete_glists(oct, lio);

	EVENTHANDLER_DEREGISTER(vlan_config, lio->vlan_attach);
	EVENTHANDLER_DEREGISTER(vlan_unconfig, lio->vlan_detach);

	free(lio, M_DEVBUF);

	if_free(ifp);

	oct->props.gmxport = -1;

	oct->props.ifp = NULL;
}

static void
print_link_info(struct ifnet *ifp)
{
	struct lio	*lio = if_getsoftc(ifp);

	if (!lio_ifstate_check(lio, LIO_IFSTATE_RESETTING) &&
	    lio_ifstate_check(lio, LIO_IFSTATE_REGISTERED)) {
		struct octeon_link_info *linfo = &lio->linfo;

		if (linfo->link.s.link_up) {
			lio_dev_info(lio->oct_dev, "%d Mbps %s Duplex UP\n",
				     linfo->link.s.speed,
				     (linfo->link.s.duplex) ? "Full" : "Half");
		} else {
			lio_dev_info(lio->oct_dev, "Link Down\n");
		}
	}
}

static inline void
lio_update_link_status(struct ifnet *ifp, union octeon_link_status *ls)
{
	struct lio	*lio = if_getsoftc(ifp);
	int	changed = (lio->linfo.link.link_status64 != ls->link_status64);

	lio->linfo.link.link_status64 = ls->link_status64;

	if ((lio->intf_open) && (changed)) {
		print_link_info(ifp);
		lio->link_changes++;
		if (lio->linfo.link.s.link_up)
			if_link_state_change(ifp, LINK_STATE_UP);
		else
			if_link_state_change(ifp, LINK_STATE_DOWN);
	}
}

/*
 * \brief Callback for rx ctrl
 * @param status status of request
 * @param buf pointer to resp structure
 */
static void
lio_rx_ctl_callback(struct octeon_device *oct, uint32_t status, void *buf)
{
	struct lio_soft_command	*sc = (struct lio_soft_command *)buf;
	struct lio_rx_ctl_context *ctx;

	ctx = (struct lio_rx_ctl_context *)sc->ctxptr;

	oct = lio_get_device(ctx->octeon_id);
	if (status)
		lio_dev_err(oct, "rx ctl instruction failed. Status: %llx\n",
			    LIO_CAST64(status));
	ctx->cond = 1;

	/*
	 * This barrier is required to be sure that the response has been
	 * written fully before waking up the handler
	 */
	wmb();
}

static void
lio_send_rx_ctrl_cmd(struct lio *lio, int start_stop)
{
	struct lio_soft_command	*sc;
	struct lio_rx_ctl_context *ctx;
	union octeon_cmd	*ncmd;
	struct octeon_device	*oct = (struct octeon_device *)lio->oct_dev;
	int	ctx_size = sizeof(struct lio_rx_ctl_context);
	int	retval;

	if (oct->props.rx_on == start_stop)
		return;

	sc = lio_alloc_soft_command(oct, OCTEON_CMD_SIZE, 16, ctx_size);
	if (sc == NULL)
		return;

	ncmd = (union octeon_cmd *)sc->virtdptr;
	ctx = (struct lio_rx_ctl_context *)sc->ctxptr;

	ctx->cond = 0;
	ctx->octeon_id = lio_get_device_id(oct);
	ncmd->cmd64 = 0;
	ncmd->s.cmd = LIO_CMD_RX_CTL;
	ncmd->s.param1 = start_stop;

	lio_swap_8B_data((uint64_t *)ncmd, (OCTEON_CMD_SIZE >> 3));

	sc->iq_no = lio->linfo.txpciq[0].s.q_no;

	lio_prepare_soft_command(oct, sc, LIO_OPCODE_NIC, LIO_OPCODE_NIC_CMD, 0,
				 0, 0);

	sc->callback = lio_rx_ctl_callback;
	sc->callback_arg = sc;
	sc->wait_time = 5000;

	retval = lio_send_soft_command(oct, sc);
	if (retval == LIO_IQ_SEND_FAILED) {
		lio_dev_err(oct, "Failed to send RX Control message\n");
	} else {
		/*
		 * Sleep on a wait queue till the cond flag indicates that the
		 * response arrived or timed-out.
		 */
		lio_sleep_cond(oct, &ctx->cond);
		oct->props.rx_on = start_stop;
	}

	lio_free_soft_command(oct, sc);
}

static void
lio_vlan_rx_add_vid(void *arg, struct ifnet *ifp, uint16_t vid)
{
	struct lio_ctrl_pkt	nctrl;
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	int	ret = 0;

	if (if_getsoftc(ifp) != arg)	/* Not our event */
		return;

	if ((vid == 0) || (vid > 4095))	/* Invalid */
		return;

	bzero(&nctrl, sizeof(struct lio_ctrl_pkt));

	nctrl.ncmd.cmd64 = 0;
	nctrl.ncmd.s.cmd = LIO_CMD_ADD_VLAN_FILTER;
	nctrl.ncmd.s.param1 = vid;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.lio = lio;
	nctrl.cb_fn = lio_ctrl_cmd_completion;

	ret = lio_send_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		lio_dev_err(oct, "Add VLAN filter failed in core (ret: 0x%x)\n",
			    ret);
	}
}

static void
lio_vlan_rx_kill_vid(void *arg, struct ifnet *ifp, uint16_t vid)
{
	struct lio_ctrl_pkt	nctrl;
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	int	ret = 0;

	if (if_getsoftc(ifp) != arg)	/* Not our event */
		return;

	if ((vid == 0) || (vid > 4095))	/* Invalid */
		return;

	bzero(&nctrl, sizeof(struct lio_ctrl_pkt));

	nctrl.ncmd.cmd64 = 0;
	nctrl.ncmd.s.cmd = LIO_CMD_DEL_VLAN_FILTER;
	nctrl.ncmd.s.param1 = vid;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.lio = lio;
	nctrl.cb_fn = lio_ctrl_cmd_completion;

	ret = lio_send_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		lio_dev_err(oct,
			    "Kill VLAN filter failed in core (ret: 0x%x)\n",
			    ret);
	}
}

static int
lio_wait_for_oq_pkts(struct octeon_device *oct)
{
	int	i, pending_pkts, pkt_cnt = 0, retry = 100;

	do {
		pending_pkts = 0;

		for (i = 0; i < LIO_MAX_OUTPUT_QUEUES(oct); i++) {
			if (!(oct->io_qmask.oq & BIT_ULL(i)))
				continue;

			pkt_cnt = lio_droq_check_hw_for_pkts(oct->droq[i]);
			if (pkt_cnt > 0) {
				pending_pkts += pkt_cnt;
				taskqueue_enqueue(oct->droq[i]->droq_taskqueue,
						  &oct->droq[i]->droq_task);
			}
		}

		pkt_cnt = 0;
		lio_sleep_timeout(1);
	} while (retry-- && pending_pkts);

	return (pkt_cnt);
}

static void
lio_destroy_resources(struct octeon_device *oct)
{
	int i, refcount;

	switch (atomic_load_acq_int(&oct->status)) {
	case LIO_DEV_RUNNING:
	case LIO_DEV_CORE_OK:
		/* No more instructions will be forwarded. */
		atomic_store_rel_int(&oct->status, LIO_DEV_IN_RESET);

		oct->app_mode = LIO_DRV_INVALID_APP;
		lio_dev_dbg(oct, "Device state is now %s\n",
			    lio_get_state_string(&oct->status));

		lio_sleep_timeout(100);

		/* fallthrough */
	case LIO_DEV_HOST_OK:

		/* fallthrough */
	case LIO_DEV_CONSOLE_INIT_DONE:
		/* Remove any consoles */
		lio_remove_consoles(oct);

		/* fallthrough */
	case LIO_DEV_IO_QUEUES_DONE:
		if (lio_wait_for_pending_requests(oct))
			lio_dev_err(oct, "There were pending requests\n");

		if (lio_wait_for_instr_fetch(oct))
			lio_dev_err(oct, "IQ had pending instructions\n");

		/*
		 * Disable the input and output queues now. No more packets will
		 * arrive from Octeon, but we should wait for all packet
		 * processing to finish.
		 */
		oct->fn_list.disable_io_queues(oct);

		if (lio_wait_for_oq_pkts(oct))
			lio_dev_err(oct, "OQ had pending packets\n");

		/* fallthrough */
	case LIO_DEV_INTR_SET_DONE:
		/* Disable interrupts  */
		oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

		if (oct->msix_on) {
			for (i = 0; i < oct->num_msix_irqs - 1; i++) {
				if (oct->ioq_vector[i].tag != NULL) {
					bus_teardown_intr(oct->device,
						  oct->ioq_vector[i].msix_res,
						      oct->ioq_vector[i].tag);
					oct->ioq_vector[i].tag = NULL;
				}
				if (oct->ioq_vector[i].msix_res != NULL) {
					bus_release_resource(oct->device,
						SYS_RES_IRQ,
						oct->ioq_vector[i].vector,
						oct->ioq_vector[i].msix_res);
					oct->ioq_vector[i].msix_res = NULL;
				}
			}
			/* non-iov vector's argument is oct struct */
			if (oct->tag != NULL) {
				bus_teardown_intr(oct->device, oct->msix_res,
						  oct->tag);
				oct->tag = NULL;
			}

			if (oct->msix_res != NULL) {
				bus_release_resource(oct->device, SYS_RES_IRQ,
						     oct->aux_vector,
						     oct->msix_res);
				oct->msix_res = NULL;
			}

			pci_release_msi(oct->device);
		}
		/* fallthrough */
	case LIO_DEV_IN_RESET:
	case LIO_DEV_DROQ_INIT_DONE:
		/* Wait for any pending operations */
		lio_mdelay(100);
		for (i = 0; i < LIO_MAX_OUTPUT_QUEUES(oct); i++) {
			if (!(oct->io_qmask.oq & BIT_ULL(i)))
				continue;
			lio_delete_droq(oct, i);
		}

		/* fallthrough */
	case LIO_DEV_RESP_LIST_INIT_DONE:
		for (i = 0; i < LIO_MAX_POSSIBLE_OUTPUT_QUEUES; i++) {
			if (oct->droq[i] != NULL) {
				free(oct->droq[i], M_DEVBUF);
				oct->droq[i] = NULL;
			}
		}
		lio_delete_response_list(oct);

		/* fallthrough */
	case LIO_DEV_INSTR_QUEUE_INIT_DONE:
		for (i = 0; i < LIO_MAX_INSTR_QUEUES(oct); i++) {
			if (!(oct->io_qmask.iq & BIT_ULL(i)))
				continue;

			lio_delete_instr_queue(oct, i);
		}

		/* fallthrough */
	case LIO_DEV_MSIX_ALLOC_VECTOR_DONE:
		for (i = 0; i < LIO_MAX_POSSIBLE_INSTR_QUEUES; i++) {
			if (oct->instr_queue[i] != NULL) {
				free(oct->instr_queue[i], M_DEVBUF);
				oct->instr_queue[i] = NULL;
			}
		}
		lio_free_ioq_vector(oct);

		/* fallthrough */
	case LIO_DEV_SC_BUFF_POOL_INIT_DONE:
		lio_free_sc_buffer_pool(oct);

		/* fallthrough */
	case LIO_DEV_DISPATCH_INIT_DONE:
		lio_delete_dispatch_list(oct);

		/* fallthrough */
	case LIO_DEV_PCI_MAP_DONE:
		refcount = lio_deregister_device(oct);

		if (fw_type_is_none())
			lio_pci_flr(oct);

		if (!refcount)
			oct->fn_list.soft_reset(oct);

		lio_unmap_pci_barx(oct, 0);
		lio_unmap_pci_barx(oct, 1);

		/* fallthrough */
	case LIO_DEV_PCI_ENABLE_DONE:
		/* Disable the device, releasing the PCI INT */
		pci_disable_busmaster(oct->device);

		/* fallthrough */
	case LIO_DEV_BEGIN_STATE:
		break;
	}	/* end switch (oct->status) */
}
