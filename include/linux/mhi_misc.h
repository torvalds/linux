/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef _MHI_MISC_H_
#define _MHI_MISC_H_

#include <linux/mhi.h>
#include <linux/ipc_logging.h>

/**
 * enum MHI_DEBUG_LEVEL - various debugging levels
 */
enum MHI_DEBUG_LEVEL {
	MHI_MSG_LVL_VERBOSE,
	MHI_MSG_LVL_INFO,
	MHI_MSG_LVL_ERROR,
	MHI_MSG_LVL_CRITICAL,
	MHI_MSG_LVL_MASK_ALL,
	MHI_MSG_LVL_MAX,
};

/**
 * struct mhi_buf - MHI Buffer description
 * @node: list entry point
 * @buf: Virtual address of the buffer
 * @name: Buffer label. For offload channel, configurations name must be:
 *        ECA - Event context array data
 *        CCA - Channel context array data
 * @dma_addr: IOMMU address of the buffer
 * @phys_addr: physical address of the buffer
 * @len: # of bytes
 * @is_io: buffer is of IO/registesr type (resource) rather than of DDR/RAM type
 */
struct mhi_buf_extended {
	struct list_head node;
	void *buf;
	const char *name;
	dma_addr_t dma_addr;
	phys_addr_t phys_addr;
	size_t len;
	bool is_io;
};

#ifdef CONFIG_MHI_BUS_MISC

/**
 * mhi_report_error - Can be used by controller to signal error condition to the
 * MHI core driver in case of any need to halt processing or incoming sideband
 * signal detects an error on endpoint
 * @mhi_cntrl: MHI controller
 *
 * Returns:
 * 0 if success in reporting the error condition to MHI core
 * error code on failure
 */
int mhi_report_error(struct mhi_controller *mhi_cntrl);

/**
 * mhi_controller_set_privdata - Set private data for MHI controller
 * @mhi_cntrl: MHI controller
 * @priv: pointer to data
 */
void mhi_controller_set_privdata(struct mhi_controller *mhi_cntrl, void *priv);

/**
 * mhi_controller_get_privdata - Get private data from MHI controller
 * @mhi_cntrl: MHI controller
 */
void *mhi_controller_get_privdata(struct mhi_controller *mhi_cntrl);

/**
 * mhi_bdf_to_controller - Get controller associated with given BDF values
 * @domain: Domain or root complex of PCIe port
 * @bus: Bus number
 * @slot: PCI slot or function number
 * @dev_id: Device ID of the endpoint
 *
 * Returns:
 * MHI controller structure pointer if BDF match is found
 * NULL if cookie is not found
 */
struct mhi_controller *mhi_bdf_to_controller(u32 domain, u32 bus, u32 slot, u32 dev_id);

/**
 * mhi_set_m2_timeout_ms - Set M2 timeout in milliseconds to wait before a
 * fast/silent suspend
 * @mhi_cntrl: MHI controller
 * @timeout: timeout in ms
 */
void mhi_set_m2_timeout_ms(struct mhi_controller *mhi_cntrl, u32 timeout);

/**
 * mhi_pm_fast_resume - Resume MHI from a fast/silent suspended state
 * @mhi_cntrl: MHI controller
 * @notify_clients: if true, clients will be notified of the resume transition
 */
int mhi_pm_fast_resume(struct mhi_controller *mhi_cntrl, bool notify_clients);

/**
 * mhi_pm_fast_suspend - Move MHI into a fast/silent suspended state
 * @mhi_cntrl: MHI controller
 * @notify_clients: if true, clients will be notified of the suspend transition
 */
int mhi_pm_fast_suspend(struct mhi_controller *mhi_cntrl, bool notify_clients);

/**
 * mhi_debug_reg_dump - dump MHI registers for debug purpose
 * @mhi_cntrl: MHI controller
 */
void mhi_debug_reg_dump(struct mhi_controller *mhi_cntrl);

/**
 * mhi_dump_sfr - Print SFR string from RDDM table.
 * @mhi_cntrl: MHI controller
 */
void mhi_dump_sfr(struct mhi_controller *mhi_cntrl);

/**
 * mhi_device_configure - Allow devices with offload channels to setup their own
 * channel and event ring context.
 * @mhi_dev: MHI device
 * @dir: direction associated with the channel needed to configure
 * @cfg_tbl: Buffer with ECA/CCA information and data needed to setup context
 * @elements: Number of items to iterate over from the configuration table
 */
int mhi_device_configure(struct mhi_device *mhi_dev,
			 enum dma_data_direction dir,
			 struct mhi_buf *cfg_tbl,
			 int elements);

/**
 * mhi_scan_rddm_cookie - Look for supplied cookie value in the BHI debug
 * registers set by device to indicate rddm readiness for debugging purposes.
 * @mhi_cntrl: MHI controller
 * @cookie: cookie/pattern value to match
 *
 * Returns:
 * true if cookie is found
 * false if cookie is not found
 */
bool mhi_scan_rddm_cookie(struct mhi_controller *mhi_cntrl, u32 cookie);

/**
 * mhi_device_get_sync_atomic - Asserts device_wait and moves device to M0
 * @mhi_dev: Device associated with the channels
 * @timeout_us: timeout, in micro-seconds
 * @in_panic: If requested while kernel is in panic state and no ISRs expected
 *
 * The device_wake is asserted to keep device in M0 or bring it to M0.
 * If device is not in M0 state, then this function will wait for device to
 * move to M0, until @timeout_us elapses.
 * However, if device's M1 state-change event races with this function
 * then there is a possiblity of device moving from M0 to M2 and back
 * to M0. That can't be avoided as host must transition device from M1 to M2
 * as per the spec.
 * Clients can ignore that transition after this function returns as the device
 * is expected to immediately  move from M2 to M0 as wake is asserted and
 * wouldn't enter low power state.
 * If in_panic boolean is set, no ISRs are expected, hence this API will have to
 * resort to reading the MHI status register and poll on M0 state change.
 *
 * Returns:
 * 0 if operation was successful (however, M0 -> M2 -> M0 is possible later) as
 * mentioned above.
 * -ETIMEDOUT is device faled to move to M0 before @timeout_us elapsed
 * -EIO if the MHI state is one of the ERROR states.
 */
int mhi_device_get_sync_atomic(struct mhi_device *mhi_dev, int timeout_us,
			       bool in_panic);

/**
 * mhi_controller_set_bw_scale_cb - Set the BW scale callback for MHI controller
 * @mhi_cntrl: MHI controller
 * @cb_func: Callback to set for the MHI controller to receive BW scale requests
 */
void mhi_controller_set_bw_scale_cb(struct mhi_controller *mhi_cntrl,
				int (*cb_func)(struct mhi_controller *mhi_cntrl,
					      struct mhi_link_info *link_info));
/**
 * mhi_controller_set_base - Set the controller base / resource start address
 * @mhi_cntrl: MHI controller
 * @base: Physical address to be set for future reference
 */
void mhi_controller_set_base(struct mhi_controller *mhi_cntrl,
			     phys_addr_t base);

/**
 * mhi_controller_get_base - Get the controller base / resource start address
 * @mhi_cntrl: MHI controller
 * @base: Pointer to physical address to be populated
 */
int mhi_controller_get_base(struct mhi_controller *mhi_cntrl,
			    phys_addr_t *base);

/**
 * mhi_controller_get_numeric_id - set numeric ID for controller
 * @mhi_cntrl: MHI controller
 * returns value set as ID or 0 if no value was set
 */
u32 mhi_controller_get_numeric_id(struct mhi_controller *mhi_cntrl);

/**
 * mhi_get_channel_db_base - retrieve the channel doorbell base address
 * @mhi_dev: Device associated with the channels
 * @value: Pointer to an address value which will be populated
 */
int mhi_get_channel_db_base(struct mhi_device *mhi_dev, phys_addr_t *value);

/**
 * mhi_get_event_ring_db_base - retrieve the event ring doorbell base address
 * @mhi_dev: Device associated with the channels
 * @value: Pointer to an address value which will be populated
 */
int mhi_get_event_ring_db_base(struct mhi_device *mhi_dev, phys_addr_t *value);

/**
 * mhi_get_device_for_channel - get the MHI device for a specific channel number
 * @mhi_cntrl: MHI controller
 * @channel - channel number
 *
 * Returns:
 * Pointer to the MHI device associated with the channel
 */
struct mhi_device *mhi_get_device_for_channel(struct mhi_controller *mhi_cntrl,
					      u32 channel);

/**
 * mhi_device_ioctl - user space IOCTL support for MHI channels
 * Native support for setting TIOCM
 * @mhi_dev: Device associated with the channels
 * @cmd: IOCTL cmd
 * @arg: Optional parameter, iotcl cmd specific
 */
long mhi_device_ioctl(struct mhi_device *mhi_dev, unsigned int cmd,
		      unsigned long arg);

/**
 * mhi_controller_set_sfr_support - Set support for subsystem failure reason
 * @mhi_cntrl: MHI controller
 *
 * Returns:
 * 0 for success, error code for failure
 */
int mhi_controller_set_sfr_support(struct mhi_controller *mhi_cntrl,
				   size_t len);

/**
 * mhi_controller_setup_timesync - Set support for time synchronization feature
 * @mhi_cntrl: MHI controller
 * @time_get: Callback to set for the MHI controller to receive host time
 * @lpm_disable: Callback to set for the MHI controller to disable link LPM
 * @lpm_enable: Callback to set for the MHI controller to enable link LPM
 *
 * Returns:
 * 0 for success, error code for failure
 */
int mhi_controller_setup_timesync(struct mhi_controller *mhi_cntrl,
				  u64 (*time_get)(struct mhi_controller *c),
				  int (*lpm_disable)(struct mhi_controller *c),
				  int (*lpm_enable)(struct mhi_controller *c));

/**
 * mhi_get_remote_time_sync - Get external soc time relative to local soc time
 * using MMIO method.
 * @mhi_dev: Device associated with the channels
 * @t_host: Pointer to output local soc time
 * @t_dev: Pointer to output remote soc time
 *
 * Returns:
 * 0 for success, error code for failure
 */
int mhi_get_remote_time_sync(struct mhi_device *mhi_dev,
			     u64 *t_host,
			     u64 *t_dev);

/**
 * mhi_get_remote_time - Get external modem time relative to host time
 * Trigger event to capture modem time, also capture host time so client
 * can do a relative drift comparision.
 * Recommended only tsync device calls this method and do not call this
 * from atomic context
 * @mhi_dev: Device associated with the channels
 * @sequence:unique sequence id track event
 * @cb_func: callback function to call back
 *
 * Returns:
 * 0 for success, error code for failure
 */
int mhi_get_remote_time(struct mhi_device *mhi_dev,
			u32 sequence,
			void (*cb_func)(struct mhi_device *mhi_dev,
					u32 sequence,
					u64 local_time,
					u64 remote_time));

/**
 * mhi_force_reset - does host reset request to collect device side dumps
 * for debugging purpose
 * @mhi_cntrl: MHI controller
 */
int mhi_force_reset(struct mhi_controller *mhi_cntrl);

/**
 * mhi_controller_set_loglevel - API for controller to set a desired log level
 * which will be set to VERBOSE or 0 by default
 * @mhi_cntrl: MHI controller
 * @lvl: Log level from MHI_DEBUG_LEVEL enumerator
 */
void mhi_controller_set_loglevel(struct mhi_controller *mhi_cntrl,
				 enum MHI_DEBUG_LEVEL lvl);

/**
 * mhi_get_soc_info - Get SoC info before registering mhi controller
 * @mhi_cntrl: MHI controller
 */
int mhi_get_soc_info(struct mhi_controller *mhi_cntrl);

/**
 * mhi_host_notify_db_disable_trace - Host notification to ring channel DB
 * to MHI device to stop tracing due SMMU fault
 * @mhi_cntrl: MHI controller
 */
int mhi_host_notify_db_disable_trace(struct mhi_controller *mhi_cntrl);

#else

/**
 * mhi_report_error - Can be used by controller to signal error condition to the
 * MHI core driver in case of any need to halt processing or incoming sideband
 * signal detects an error on endpoint
 * @mhi_cntrl: MHI controller
 *
 * Returns:
 * 0 if success in reporting the error condition to MHI core
 * error code on failure
 */
static inline int mhi_report_error(struct mhi_controller *mhi_cntrl)
{
	return -EPERM;
}

/**
 * mhi_controller_set_privdata - Set private data for MHI controller
 * @mhi_cntrl: MHI controller
 * @priv: pointer to data
 */
void mhi_controller_set_privdata(struct mhi_controller *mhi_cntrl, void *priv)
{
}

/**
 * mhi_controller_get_privdata - Get private data from MHI controller
 * @mhi_cntrl: MHI controller
 */
void *mhi_controller_get_privdata(struct mhi_controller *mhi_cntrl)
{
	return ERR_PTR(-EINVAL);
}

/**
 * mhi_bdf_to_controller - Get controller associated with given BDF values
 * @domain: Domain or root complex of PCIe port
 * @bus: Bus number
 * @slot: PCI slot or function number
 * @dev_id: Device ID of the endpoint
 *
 * Returns:
 * MHI controller structure pointer if BDF match is found
 * NULL if cookie is not found
 */
struct mhi_controller *mhi_bdf_to_controller(u32 domain, u32 bus, u32 slot, u32 dev_id)
{
	return ERR_PTR(-EINVAL);
}

/**
 * mhi_set_m2_timeout_ms - Set M2 timeout in milliseconds to wait before a
 * fast/silent suspend
 * @mhi_cntrl: MHI controller
 * @timeout: timeout in ms
 */
void mhi_set_m2_timeout_ms(struct mhi_controller *mhi_cntrl, u32 timeout)
{
}

/**
 * mhi_pm_fast_resume - Resume MHI from a fast/silent suspended state
 * @mhi_cntrl: MHI controller
 * @notify_clients: if true, clients will be notified of the resume transition
 */
int mhi_pm_fast_resume(struct mhi_controller *mhi_cntrl, bool notify_clients)
{
	return -EPERM;
}

/**
 * mhi_pm_fast_suspend - Move MHI into a fast/silent suspended state
 * @mhi_cntrl: MHI controller
 * @notify_clients: if true, clients will be notified of the suspend transition
 */
int mhi_pm_fast_suspend(struct mhi_controller *mhi_cntrl, bool notify_clients)
{
	return -EPERM;
}

/**
 * mhi_debug_reg_dump - dump MHI registers for debug purpose
 * @mhi_cntrl: MHI controller
 */
void mhi_debug_reg_dump(struct mhi_controller *mhi_cntrl)
{
}

/**
 * mhi_dump_sfr - Print SFR string from RDDM table.
 * @mhi_cntrl: MHI controller
 */
void mhi_dump_sfr(struct mhi_controller *mhi_cntrl)
{
}

/**
 * mhi_device_configure - Allow devices with offload channels to setup their own
 * channel and event ring context.
 * @mhi_dev: MHI device
 * @dir: direction associated with the channel needed to configure
 * @cfg_tbl: Buffer with ECA/CCA information and data needed to setup context
 * @elements: Number of items to iterate over from the configuration table
 */
int mhi_device_configure(struct mhi_device *mhi_dev,
			 enum dma_data_direction dir,
			 struct mhi_buf *cfg_tbl,
			 int elements)
{
	return -EPERM;
}

/**
 * mhi_scan_rddm_cookie - Look for supplied cookie value in the BHI debug
 * registers set by device to indicate rddm readiness for debugging purposes.
 * @mhi_cntrl: MHI controller
 * @cookie: cookie/pattern value to match
 *
 * Returns:
 * true if cookie is found
 * false if cookie is not found
 */
bool mhi_scan_rddm_cookie(struct mhi_controller *mhi_cntrl, u32 cookie)
{
	return false;
}

/**
 * mhi_device_get_sync_atomic - Asserts device_wait and moves device to M0
 * @mhi_dev: Device associated with the channels
 * @timeout_us: timeout, in micro-seconds
 * @in_panic: If requested while kernel is in panic state and no ISRs expected
 *
 * The device_wake is asserted to keep device in M0 or bring it to M0.
 * If device is not in M0 state, then this function will wait for device to
 * move to M0, until @timeout_us elapses.
 * However, if device's M1 state-change event races with this function
 * then there is a possiblity of device moving from M0 to M2 and back
 * to M0. That can't be avoided as host must transition device from M1 to M2
 * as per the spec.
 * Clients can ignore that transition after this function returns as the device
 * is expected to immediately  move from M2 to M0 as wake is asserted and
 * wouldn't enter low power state.
 * If in_panic boolean is set, no ISRs are expected, hence this API will have to
 * resort to reading the MHI status register and poll on M0 state change.
 *
 * Returns:
 * 0 if operation was successful (however, M0 -> M2 -> M0 is possible later) as
 * mentioned above.
 * -ETIMEDOUT is device faled to move to M0 before @timeout_us elapsed
 * -EIO if the MHI state is one of the ERROR states.
 */
int mhi_device_get_sync_atomic(struct mhi_device *mhi_dev, int timeout_us,
			       bool in_panic)
{
	return -EPERM;
}

/**
 * mhi_controller_set_bw_scale_cb - Set the BW scale callback for MHI controller
 * @mhi_cntrl: MHI controller
 * @cb_func: Callback to set for the MHI controller to receive BW scale requests
 */
void mhi_controller_set_bw_scale_cb(struct mhi_controller *mhi_cntrl,
				int (*cb_func)(struct mhi_controller *mhi_cntrl,
					      struct mhi_link_info *link_info))
{
}

/**
 * mhi_controller_set_base - Set the controller base / resource start address
 * @mhi_cntrl: MHI controller
 * @base: Physical address to be set for future reference
 */
void mhi_controller_set_base(struct mhi_controller *mhi_cntrl,
			     phys_addr_t base)
{
}

/**
 * mhi_controller_get_base - Get the controller base / resource start address
 * @mhi_cntrl: MHI controller
 * @base: Pointer to physical address to be populated
 */
int mhi_controller_get_base(struct mhi_controller *mhi_cntrl,
			    phys_addr_t *base)
{
	return -EINVAL;
}

/**
 * mhi_controller_get_numeric_id - set numeric ID for controller
 * @mhi_cntrl: MHI controller
 * returns value set as ID or 0 if no value was set
 */
u32 mhi_controller_get_numeric_id(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

/**
 * mhi_get_channel_db_base - retrieve the channel doorbell base address
 * @mhi_dev: Device associated with the channels
 * @value: Pointer to an address value which will be populated
 */
int mhi_get_channel_db_base(struct mhi_device *mhi_dev, phys_addr_t *value)
{
	return -EPERM;
}

/**
 * mhi_get_event_ring_db_base - retrieve the event ring doorbell base address
 * @mhi_dev: Device associated with the channels
 * @value: Pointer to an address value which will be populated
 */
int mhi_get_event_ring_db_base(struct mhi_device *mhi_dev, phys_addr_t *value)
{
	return -EPERM;
}

/**
 * mhi_get_device_for_channel - get the MHI device for a specific channel number
 * @mhi_cntrl: MHI controller
 * @channel - channel number
 *
 * Returns:
 * Pointer to the MHI device associated with the channel
 */
struct mhi_device *mhi_get_device_for_channel(struct mhi_controller *mhi_cntrl,
					      u32 channel)
{
	return ERR_PTR(-EINVAL);
}

/**
 * mhi_device_ioctl - user space IOCTL support for MHI channels
 * Native support for setting TIOCM
 * @mhi_dev: Device associated with the channels
 * @cmd: IOCTL cmd
 * @arg: Optional parameter, iotcl cmd specific
 */
long mhi_device_ioctl(struct mhi_device *mhi_dev, unsigned int cmd,
		      unsigned long arg)
{
	return -EPERM;
}

/**
 * mhi_controller_set_sfr_support - Set support for subsystem failure reason
 * @mhi_cntrl: MHI controller
 *
 * Returns:
 * 0 for success, error code for failure
 */
int mhi_controller_set_sfr_support(struct mhi_controller *mhi_cntrl,
				   size_t len)
{
	return -EPERM;
}

/**
 * mhi_controller_setup_timesync - Set support for time synchronization feature
 * @mhi_cntrl: MHI controller
 * @time_get: Callback to set for the MHI controller to receive host time
 * @lpm_disable: Callback to set for the MHI controller to disable link LPM
 * @lpm_enable: Callback to set for the MHI controller to enable link LPM
 *
 * Returns:
 * 0 for success, error code for failure
 */
int mhi_controller_setup_timesync(struct mhi_controller *mhi_cntrl,
				  u64 (*time_get)(struct mhi_controller *c),
				  int (*lpm_disable)(struct mhi_controller *c),
				  int (*lpm_enable)(struct mhi_controller *c))
{
	return -EPERM;
}

/**
 * mhi_get_remote_time_sync - Get external soc time relative to local soc time
 * using MMIO method.
 * @mhi_dev: Device associated with the channels
 * @t_host: Pointer to output local soc time
 * @t_dev: Pointer to output remote soc time
 *
 * Returns:
 * 0 for success, error code for failure
 */
int mhi_get_remote_time_sync(struct mhi_device *mhi_dev,
			     u64 *t_host,
			     u64 *t_dev)
{
	return -EPERM;
}

/**
 * mhi_get_remote_time - Get external modem time relative to host time
 * Trigger event to capture modem time, also capture host time so client
 * can do a relative drift comparision.
 * Recommended only tsync device calls this method and do not call this
 * from atomic context
 * @mhi_dev: Device associated with the channels
 * @sequence:unique sequence id track event
 * @cb_func: callback function to call back
 *
 * Returns:
 * 0 for success, error code for failure
 */
int mhi_get_remote_time(struct mhi_device *mhi_dev,
			u32 sequence,
			void (*cb_func)(struct mhi_device *mhi_dev,
					u32 sequence,
					u64 local_time,
					u64 remote_time))
{
	return -EPERM;
}

/**
 * mhi_force_reset - does host reset request to collect device side dumps
 * for debugging purpose
 * @mhi_cntrl: MHI controller
 */
int mhi_force_reset(struct mhi_controller *mhi_cntrl)
{
	return -EINVAL;
}

/**
 * mhi_controller_set_loglevel - API for controller to set a desired log level
 * which will be set to VERBOSE or 0 by default
 * @mhi_cntrl: MHI controller
 * @lvl: Log level from MHI_DEBUG_LEVEL enumerator
 */
void mhi_controller_set_loglevel(struct mhi_controller *mhi_cntrl,
				 enum MHI_DEBUG_LEVEL lvl)
{
}

/**
 * mhi_get_soc_info - Get SoC info before registering mhi controller
 * @mhi_cntrl: MHI controller
 */
int mhi_get_soc_info(struct mhi_controller *mhi_cntrl)
{
	return -EINVAL;
}

/**
 * mhi_host_notify_db_disable_trace - Host notification to ring channel DB
 * to MHI device to stop tracing due SMMU fault
 * @mhi_cntrl: MHI controller
 */
int mhi_host_notify_db_disable_trace(struct mhi_controller *mhi_cntrl)
{
	return -EPERM;
}

#endif /* CONFIG_MHI_BUS_MISC */

#endif /* _MHI_MISC_H_ */
