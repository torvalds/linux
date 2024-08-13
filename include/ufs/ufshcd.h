/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Universal Flash Storage Host controller driver
 * Copyright (C) 2011-2013 Samsung India Software Operations
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 *
 * Authors:
 *	Santosh Yaraganavi <santosh.sy@samsung.com>
 *	Vinayak Holikatti <h.vinayak@samsung.com>
 */

#ifndef _UFSHCD_H
#define _UFSHCD_H

#include <linux/bitfield.h>
#include <linux/blk-crypto-profile.h>
#include <linux/blk-mq.h>
#include <linux/devfreq.h>
#include <linux/fault-inject.h>
#include <linux/debugfs.h>
#include <linux/msi.h>
#include <linux/pm_runtime.h>
#include <linux/dma-direction.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <ufs/unipro.h>
#include <ufs/ufs.h>
#include <ufs/ufs_quirks.h>
#include <ufs/ufshci.h>

#define UFSHCD "ufshcd"

struct scsi_device;
struct ufs_hba;

enum dev_cmd_type {
	DEV_CMD_TYPE_NOP		= 0x0,
	DEV_CMD_TYPE_QUERY		= 0x1,
	DEV_CMD_TYPE_RPMB		= 0x2,
};

enum ufs_event_type {
	/* uic specific errors */
	UFS_EVT_PA_ERR = 0,
	UFS_EVT_DL_ERR,
	UFS_EVT_NL_ERR,
	UFS_EVT_TL_ERR,
	UFS_EVT_DME_ERR,

	/* fatal errors */
	UFS_EVT_AUTO_HIBERN8_ERR,
	UFS_EVT_FATAL_ERR,
	UFS_EVT_LINK_STARTUP_FAIL,
	UFS_EVT_RESUME_ERR,
	UFS_EVT_SUSPEND_ERR,
	UFS_EVT_WL_SUSP_ERR,
	UFS_EVT_WL_RES_ERR,

	/* abnormal events */
	UFS_EVT_DEV_RESET,
	UFS_EVT_HOST_RESET,
	UFS_EVT_ABORT,

	UFS_EVT_CNT,
};

/**
 * struct uic_command - UIC command structure
 * @command: UIC command
 * @argument1: UIC command argument 1
 * @argument2: UIC command argument 2
 * @argument3: UIC command argument 3
 * @cmd_active: Indicate if UIC command is outstanding
 * @done: UIC command completion
 */
struct uic_command {
	const u32 command;
	const u32 argument1;
	u32 argument2;
	u32 argument3;
	int cmd_active;
	struct completion done;
};

/* Used to differentiate the power management options */
enum ufs_pm_op {
	UFS_RUNTIME_PM,
	UFS_SYSTEM_PM,
	UFS_SHUTDOWN_PM,
};

/* Host <-> Device UniPro Link state */
enum uic_link_state {
	UIC_LINK_OFF_STATE	= 0, /* Link powered down or disabled */
	UIC_LINK_ACTIVE_STATE	= 1, /* Link is in Fast/Slow/Sleep state */
	UIC_LINK_HIBERN8_STATE	= 2, /* Link is in Hibernate state */
	UIC_LINK_BROKEN_STATE	= 3, /* Link is in broken state */
};

#define ufshcd_is_link_off(hba) ((hba)->uic_link_state == UIC_LINK_OFF_STATE)
#define ufshcd_is_link_active(hba) ((hba)->uic_link_state == \
				    UIC_LINK_ACTIVE_STATE)
#define ufshcd_is_link_hibern8(hba) ((hba)->uic_link_state == \
				    UIC_LINK_HIBERN8_STATE)
#define ufshcd_is_link_broken(hba) ((hba)->uic_link_state == \
				   UIC_LINK_BROKEN_STATE)
#define ufshcd_set_link_off(hba) ((hba)->uic_link_state = UIC_LINK_OFF_STATE)
#define ufshcd_set_link_active(hba) ((hba)->uic_link_state = \
				    UIC_LINK_ACTIVE_STATE)
#define ufshcd_set_link_hibern8(hba) ((hba)->uic_link_state = \
				    UIC_LINK_HIBERN8_STATE)
#define ufshcd_set_link_broken(hba) ((hba)->uic_link_state = \
				    UIC_LINK_BROKEN_STATE)

#define ufshcd_set_ufs_dev_active(h) \
	((h)->curr_dev_pwr_mode = UFS_ACTIVE_PWR_MODE)
#define ufshcd_set_ufs_dev_sleep(h) \
	((h)->curr_dev_pwr_mode = UFS_SLEEP_PWR_MODE)
#define ufshcd_set_ufs_dev_poweroff(h) \
	((h)->curr_dev_pwr_mode = UFS_POWERDOWN_PWR_MODE)
#define ufshcd_set_ufs_dev_deepsleep(h) \
	((h)->curr_dev_pwr_mode = UFS_DEEPSLEEP_PWR_MODE)
#define ufshcd_is_ufs_dev_active(h) \
	((h)->curr_dev_pwr_mode == UFS_ACTIVE_PWR_MODE)
#define ufshcd_is_ufs_dev_sleep(h) \
	((h)->curr_dev_pwr_mode == UFS_SLEEP_PWR_MODE)
#define ufshcd_is_ufs_dev_poweroff(h) \
	((h)->curr_dev_pwr_mode == UFS_POWERDOWN_PWR_MODE)
#define ufshcd_is_ufs_dev_deepsleep(h) \
	((h)->curr_dev_pwr_mode == UFS_DEEPSLEEP_PWR_MODE)

/*
 * UFS Power management levels.
 * Each level is in increasing order of power savings, except DeepSleep
 * which is lower than PowerDown with power on but not PowerDown with
 * power off.
 */
enum ufs_pm_level {
	UFS_PM_LVL_0,
	UFS_PM_LVL_1,
	UFS_PM_LVL_2,
	UFS_PM_LVL_3,
	UFS_PM_LVL_4,
	UFS_PM_LVL_5,
	UFS_PM_LVL_6,
	UFS_PM_LVL_MAX
};

struct ufs_pm_lvl_states {
	enum ufs_dev_pwr_mode dev_state;
	enum uic_link_state link_state;
};

/**
 * struct ufshcd_lrb - local reference block
 * @utr_descriptor_ptr: UTRD address of the command
 * @ucd_req_ptr: UCD address of the command
 * @ucd_rsp_ptr: Response UPIU address for this command
 * @ucd_prdt_ptr: PRDT address of the command
 * @utrd_dma_addr: UTRD dma address for debug
 * @ucd_prdt_dma_addr: PRDT dma address for debug
 * @ucd_rsp_dma_addr: UPIU response dma address for debug
 * @ucd_req_dma_addr: UPIU request dma address for debug
 * @cmd: pointer to SCSI command
 * @scsi_status: SCSI status of the command
 * @command_type: SCSI, UFS, Query.
 * @task_tag: Task tag of the command
 * @lun: LUN of the command
 * @intr_cmd: Interrupt command (doesn't participate in interrupt aggregation)
 * @issue_time_stamp: time stamp for debug purposes (CLOCK_MONOTONIC)
 * @issue_time_stamp_local_clock: time stamp for debug purposes (local_clock)
 * @compl_time_stamp: time stamp for statistics (CLOCK_MONOTONIC)
 * @compl_time_stamp_local_clock: time stamp for debug purposes (local_clock)
 * @crypto_key_slot: the key slot to use for inline crypto (-1 if none)
 * @data_unit_num: the data unit number for the first block for inline crypto
 * @req_abort_skip: skip request abort task flag
 */
struct ufshcd_lrb {
	struct utp_transfer_req_desc *utr_descriptor_ptr;
	struct utp_upiu_req *ucd_req_ptr;
	struct utp_upiu_rsp *ucd_rsp_ptr;
	struct ufshcd_sg_entry *ucd_prdt_ptr;

	dma_addr_t utrd_dma_addr;
	dma_addr_t ucd_req_dma_addr;
	dma_addr_t ucd_rsp_dma_addr;
	dma_addr_t ucd_prdt_dma_addr;

	struct scsi_cmnd *cmd;
	int scsi_status;

	int command_type;
	int task_tag;
	u8 lun; /* UPIU LUN id field is only 8-bit wide */
	bool intr_cmd;
	ktime_t issue_time_stamp;
	u64 issue_time_stamp_local_clock;
	ktime_t compl_time_stamp;
	u64 compl_time_stamp_local_clock;
#ifdef CONFIG_SCSI_UFS_CRYPTO
	int crypto_key_slot;
	u64 data_unit_num;
#endif

	bool req_abort_skip;
};

/**
 * struct ufs_query_req - parameters for building a query request
 * @query_func: UPIU header query function
 * @upiu_req: the query request data
 */
struct ufs_query_req {
	u8 query_func;
	struct utp_upiu_query upiu_req;
};

/**
 * struct ufs_query_resp - UPIU QUERY
 * @response: device response code
 * @upiu_res: query response data
 */
struct ufs_query_res {
	struct utp_upiu_query upiu_res;
};

/**
 * struct ufs_query - holds relevant data structures for query request
 * @request: request upiu and function
 * @descriptor: buffer for sending/receiving descriptor
 * @response: response upiu and response
 */
struct ufs_query {
	struct ufs_query_req request;
	u8 *descriptor;
	struct ufs_query_res response;
};

/**
 * struct ufs_dev_cmd - all assosiated fields with device management commands
 * @type: device management command type - Query, NOP OUT
 * @lock: lock to allow one command at a time
 * @complete: internal commands completion
 * @query: Device management query information
 */
struct ufs_dev_cmd {
	enum dev_cmd_type type;
	struct mutex lock;
	struct completion *complete;
	struct ufs_query query;
};

/**
 * struct ufs_clk_info - UFS clock related info
 * @list: list headed by hba->clk_list_head
 * @clk: clock node
 * @name: clock name
 * @max_freq: maximum frequency supported by the clock
 * @min_freq: min frequency that can be used for clock scaling
 * @curr_freq: indicates the current frequency that it is set to
 * @keep_link_active: indicates that the clk should not be disabled if
 *		      link is active
 * @enabled: variable to check against multiple enable/disable
 */
struct ufs_clk_info {
	struct list_head list;
	struct clk *clk;
	const char *name;
	u32 max_freq;
	u32 min_freq;
	u32 curr_freq;
	bool keep_link_active;
	bool enabled;
};

enum ufs_notify_change_status {
	PRE_CHANGE,
	POST_CHANGE,
};

struct ufs_pa_layer_attr {
	u32 gear_rx;
	u32 gear_tx;
	u32 lane_rx;
	u32 lane_tx;
	u32 pwr_rx;
	u32 pwr_tx;
	u32 hs_rate;
};

struct ufs_pwr_mode_info {
	bool is_valid;
	struct ufs_pa_layer_attr info;
};

/**
 * struct ufs_hba_variant_ops - variant specific callbacks
 * @name: variant name
 * @max_num_rtt: maximum RTT supported by the host
 * @init: called when the driver is initialized
 * @exit: called to cleanup everything done in init
 * @get_ufs_hci_version: called to get UFS HCI version
 * @clk_scale_notify: notifies that clks are scaled up/down
 * @setup_clocks: called before touching any of the controller registers
 * @hce_enable_notify: called before and after HCE enable bit is set to allow
 *                     variant specific Uni-Pro initialization.
 * @link_startup_notify: called before and after Link startup is carried out
 *                       to allow variant specific Uni-Pro initialization.
 * @pwr_change_notify: called before and after a power mode change
 *			is carried out to allow vendor spesific capabilities
 *			to be set.
 * @setup_xfer_req: called before any transfer request is issued
 *                  to set some things
 * @setup_task_mgmt: called before any task management request is issued
 *                  to set some things
 * @hibern8_notify: called around hibern8 enter/exit
 * @apply_dev_quirks: called to apply device specific quirks
 * @fixup_dev_quirks: called to modify device specific quirks
 * @suspend: called during host controller PM callback
 * @resume: called during host controller PM callback
 * @dbg_register_dump: used to dump controller debug information
 * @phy_initialization: used to initialize phys
 * @device_reset: called to issue a reset pulse on the UFS device
 * @config_scaling_param: called to configure clock scaling parameters
 * @program_key: program or evict an inline encryption key
 * @fill_crypto_prdt: initialize crypto-related fields in the PRDT
 * @event_notify: called to notify important events
 * @reinit_notify: called to notify reinit of UFSHCD during max gear switch
 * @mcq_config_resource: called to configure MCQ platform resources
 * @get_hba_mac: reports maximum number of outstanding commands supported by
 *	the controller. Should be implemented for UFSHCI 4.0 or later
 *	controllers that are not compliant with the UFSHCI 4.0 specification.
 * @op_runtime_config: called to config Operation and runtime regs Pointers
 * @get_outstanding_cqs: called to get outstanding completion queues
 * @config_esi: called to config Event Specific Interrupt
 * @config_scsi_dev: called to configure SCSI device parameters
 */
struct ufs_hba_variant_ops {
	const char *name;
	int	max_num_rtt;
	int	(*init)(struct ufs_hba *);
	void    (*exit)(struct ufs_hba *);
	u32	(*get_ufs_hci_version)(struct ufs_hba *);
	int	(*clk_scale_notify)(struct ufs_hba *, bool,
				    enum ufs_notify_change_status);
	int	(*setup_clocks)(struct ufs_hba *, bool,
				enum ufs_notify_change_status);
	int	(*hce_enable_notify)(struct ufs_hba *,
				     enum ufs_notify_change_status);
	int	(*link_startup_notify)(struct ufs_hba *,
				       enum ufs_notify_change_status);
	int	(*pwr_change_notify)(struct ufs_hba *,
					enum ufs_notify_change_status status,
					struct ufs_pa_layer_attr *,
					struct ufs_pa_layer_attr *);
	void	(*setup_xfer_req)(struct ufs_hba *hba, int tag,
				  bool is_scsi_cmd);
	void	(*setup_task_mgmt)(struct ufs_hba *, int, u8);
	void    (*hibern8_notify)(struct ufs_hba *, enum uic_cmd_dme,
					enum ufs_notify_change_status);
	int	(*apply_dev_quirks)(struct ufs_hba *hba);
	void	(*fixup_dev_quirks)(struct ufs_hba *hba);
	int     (*suspend)(struct ufs_hba *, enum ufs_pm_op,
					enum ufs_notify_change_status);
	int     (*resume)(struct ufs_hba *, enum ufs_pm_op);
	void	(*dbg_register_dump)(struct ufs_hba *hba);
	int	(*phy_initialization)(struct ufs_hba *);
	int	(*device_reset)(struct ufs_hba *hba);
	void	(*config_scaling_param)(struct ufs_hba *hba,
				struct devfreq_dev_profile *profile,
				struct devfreq_simple_ondemand_data *data);
	int	(*program_key)(struct ufs_hba *hba,
			       const union ufs_crypto_cfg_entry *cfg, int slot);
	int	(*fill_crypto_prdt)(struct ufs_hba *hba,
				    const struct bio_crypt_ctx *crypt_ctx,
				    void *prdt, unsigned int num_segments);
	void	(*event_notify)(struct ufs_hba *hba,
				enum ufs_event_type evt, void *data);
	void	(*reinit_notify)(struct ufs_hba *);
	int	(*mcq_config_resource)(struct ufs_hba *hba);
	int	(*get_hba_mac)(struct ufs_hba *hba);
	int	(*op_runtime_config)(struct ufs_hba *hba);
	int	(*get_outstanding_cqs)(struct ufs_hba *hba,
				       unsigned long *ocqs);
	int	(*config_esi)(struct ufs_hba *hba);
};

/* clock gating state  */
enum clk_gating_state {
	CLKS_OFF,
	CLKS_ON,
	REQ_CLKS_OFF,
	REQ_CLKS_ON,
};

/**
 * struct ufs_clk_gating - UFS clock gating related info
 * @gate_work: worker to turn off clocks after some delay as specified in
 * delay_ms
 * @ungate_work: worker to turn on clocks that will be used in case of
 * interrupt context
 * @state: the current clocks state
 * @delay_ms: gating delay in ms
 * @is_suspended: clk gating is suspended when set to 1 which can be used
 * during suspend/resume
 * @delay_attr: sysfs attribute to control delay_attr
 * @enable_attr: sysfs attribute to enable/disable clock gating
 * @is_enabled: Indicates the current status of clock gating
 * @is_initialized: Indicates whether clock gating is initialized or not
 * @active_reqs: number of requests that are pending and should be waited for
 * completion before gating clocks.
 * @clk_gating_workq: workqueue for clock gating work.
 */
struct ufs_clk_gating {
	struct delayed_work gate_work;
	struct work_struct ungate_work;
	enum clk_gating_state state;
	unsigned long delay_ms;
	bool is_suspended;
	struct device_attribute delay_attr;
	struct device_attribute enable_attr;
	bool is_enabled;
	bool is_initialized;
	int active_reqs;
	struct workqueue_struct *clk_gating_workq;
};

/**
 * struct ufs_clk_scaling - UFS clock scaling related data
 * @active_reqs: number of requests that are pending. If this is zero when
 * devfreq ->target() function is called then schedule "suspend_work" to
 * suspend devfreq.
 * @tot_busy_t: Total busy time in current polling window
 * @window_start_t: Start time (in jiffies) of the current polling window
 * @busy_start_t: Start time of current busy period
 * @enable_attr: sysfs attribute to enable/disable clock scaling
 * @saved_pwr_info: UFS power mode may also be changed during scaling and this
 * one keeps track of previous power mode.
 * @workq: workqueue to schedule devfreq suspend/resume work
 * @suspend_work: worker to suspend devfreq
 * @resume_work: worker to resume devfreq
 * @target_freq: frequency requested by devfreq framework
 * @min_gear: lowest HS gear to scale down to
 * @is_enabled: tracks if scaling is currently enabled or not, controlled by
 *		clkscale_enable sysfs node
 * @is_allowed: tracks if scaling is currently allowed or not, used to block
 *		clock scaling which is not invoked from devfreq governor
 * @is_initialized: Indicates whether clock scaling is initialized or not
 * @is_busy_started: tracks if busy period has started or not
 * @is_suspended: tracks if devfreq is suspended or not
 */
struct ufs_clk_scaling {
	int active_reqs;
	unsigned long tot_busy_t;
	ktime_t window_start_t;
	ktime_t busy_start_t;
	struct device_attribute enable_attr;
	struct ufs_pa_layer_attr saved_pwr_info;
	struct workqueue_struct *workq;
	struct work_struct suspend_work;
	struct work_struct resume_work;
	unsigned long target_freq;
	u32 min_gear;
	bool is_enabled;
	bool is_allowed;
	bool is_initialized;
	bool is_busy_started;
	bool is_suspended;
	bool suspend_on_no_request;
};

#define UFS_EVENT_HIST_LENGTH 8
/**
 * struct ufs_event_hist - keeps history of errors
 * @pos: index to indicate cyclic buffer position
 * @val: cyclic buffer for registers value
 * @tstamp: cyclic buffer for time stamp
 * @cnt: error counter
 */
struct ufs_event_hist {
	int pos;
	u32 val[UFS_EVENT_HIST_LENGTH];
	u64 tstamp[UFS_EVENT_HIST_LENGTH];
	unsigned long long cnt;
};

/**
 * struct ufs_stats - keeps usage/err statistics
 * @last_intr_status: record the last interrupt status.
 * @last_intr_ts: record the last interrupt timestamp.
 * @hibern8_exit_cnt: Counter to keep track of number of exits,
 *		reset this after link-startup.
 * @last_hibern8_exit_tstamp: Set time after the hibern8 exit.
 *		Clear after the first successful command completion.
 * @event: array with event history.
 */
struct ufs_stats {
	u32 last_intr_status;
	u64 last_intr_ts;

	u32 hibern8_exit_cnt;
	u64 last_hibern8_exit_tstamp;
	struct ufs_event_hist event[UFS_EVT_CNT];
};

/**
 * enum ufshcd_state - UFS host controller state
 * @UFSHCD_STATE_RESET: Link is not operational. Postpone SCSI command
 *	processing.
 * @UFSHCD_STATE_OPERATIONAL: The host controller is operational and can process
 *	SCSI commands.
 * @UFSHCD_STATE_EH_SCHEDULED_NON_FATAL: The error handler has been scheduled.
 *	SCSI commands may be submitted to the controller.
 * @UFSHCD_STATE_EH_SCHEDULED_FATAL: The error handler has been scheduled. Fail
 *	newly submitted SCSI commands with error code DID_BAD_TARGET.
 * @UFSHCD_STATE_ERROR: An unrecoverable error occurred, e.g. link recovery
 *	failed. Fail all SCSI commands with error code DID_ERROR.
 */
enum ufshcd_state {
	UFSHCD_STATE_RESET,
	UFSHCD_STATE_OPERATIONAL,
	UFSHCD_STATE_EH_SCHEDULED_NON_FATAL,
	UFSHCD_STATE_EH_SCHEDULED_FATAL,
	UFSHCD_STATE_ERROR,
};

enum ufshcd_quirks {
	/* Interrupt aggregation support is broken */
	UFSHCD_QUIRK_BROKEN_INTR_AGGR			= 1 << 0,

	/*
	 * delay before each dme command is required as the unipro
	 * layer has shown instabilities
	 */
	UFSHCD_QUIRK_DELAY_BEFORE_DME_CMDS		= 1 << 1,

	/*
	 * If UFS host controller is having issue in processing LCC (Line
	 * Control Command) coming from device then enable this quirk.
	 * When this quirk is enabled, host controller driver should disable
	 * the LCC transmission on UFS device (by clearing TX_LCC_ENABLE
	 * attribute of device to 0).
	 */
	UFSHCD_QUIRK_BROKEN_LCC				= 1 << 2,

	/*
	 * The attribute PA_RXHSUNTERMCAP specifies whether or not the
	 * inbound Link supports unterminated line in HS mode. Setting this
	 * attribute to 1 fixes moving to HS gear.
	 */
	UFSHCD_QUIRK_BROKEN_PA_RXHSUNTERMCAP		= 1 << 3,

	/*
	 * This quirk needs to be enabled if the host controller only allows
	 * accessing the peer dme attributes in AUTO mode (FAST AUTO or
	 * SLOW AUTO).
	 */
	UFSHCD_QUIRK_DME_PEER_ACCESS_AUTO_MODE		= 1 << 4,

	/*
	 * This quirk needs to be enabled if the host controller doesn't
	 * advertise the correct version in UFS_VER register. If this quirk
	 * is enabled, standard UFS host driver will call the vendor specific
	 * ops (get_ufs_hci_version) to get the correct version.
	 */
	UFSHCD_QUIRK_BROKEN_UFS_HCI_VERSION		= 1 << 5,

	/*
	 * Clear handling for transfer/task request list is just opposite.
	 */
	UFSHCI_QUIRK_BROKEN_REQ_LIST_CLR		= 1 << 6,

	/*
	 * This quirk needs to be enabled if host controller doesn't allow
	 * that the interrupt aggregation timer and counter are reset by s/w.
	 */
	UFSHCI_QUIRK_SKIP_RESET_INTR_AGGR		= 1 << 7,

	/*
	 * This quirks needs to be enabled if host controller cannot be
	 * enabled via HCE register.
	 */
	UFSHCI_QUIRK_BROKEN_HCE				= 1 << 8,

	/*
	 * This quirk needs to be enabled if the host controller regards
	 * resolution of the values of PRDTO and PRDTL in UTRD as byte.
	 */
	UFSHCD_QUIRK_PRDT_BYTE_GRAN			= 1 << 9,

	/*
	 * This quirk needs to be enabled if the host controller reports
	 * OCS FATAL ERROR with device error through sense data
	 */
	UFSHCD_QUIRK_BROKEN_OCS_FATAL_ERROR		= 1 << 10,

	/*
	 * This quirk needs to be enabled if the host controller has
	 * auto-hibernate capability but it doesn't work.
	 */
	UFSHCD_QUIRK_BROKEN_AUTO_HIBERN8		= 1 << 11,

	/*
	 * This quirk needs to disable manual flush for write booster
	 */
	UFSHCI_QUIRK_SKIP_MANUAL_WB_FLUSH_CTRL		= 1 << 12,

	/*
	 * This quirk needs to disable unipro timeout values
	 * before power mode change
	 */
	UFSHCD_QUIRK_SKIP_DEF_UNIPRO_TIMEOUT_SETTING = 1 << 13,

	/*
	 * This quirk needs to be enabled if the host controller does not
	 * support UIC command
	 */
	UFSHCD_QUIRK_BROKEN_UIC_CMD			= 1 << 15,

	/*
	 * This quirk needs to be enabled if the host controller cannot
	 * support physical host configuration.
	 */
	UFSHCD_QUIRK_SKIP_PH_CONFIGURATION		= 1 << 16,

	/*
	 * This quirk needs to be enabled if the host controller has
	 * 64-bit addressing supported capability but it doesn't work.
	 */
	UFSHCD_QUIRK_BROKEN_64BIT_ADDRESS		= 1 << 17,

	/*
	 * This quirk needs to be enabled if the host controller has
	 * auto-hibernate capability but it's FASTAUTO only.
	 */
	UFSHCD_QUIRK_HIBERN_FASTAUTO			= 1 << 18,

	/*
	 * This quirk needs to be enabled if the host controller needs
	 * to reinit the device after switching to maximum gear.
	 */
	UFSHCD_QUIRK_REINIT_AFTER_MAX_GEAR_SWITCH       = 1 << 19,

	/*
	 * Some host raises interrupt (per queue) in addition to
	 * CQES (traditional) when ESI is disabled.
	 * Enable this quirk will disable CQES and use per queue interrupt.
	 */
	UFSHCD_QUIRK_MCQ_BROKEN_INTR			= 1 << 20,

	/*
	 * Some host does not implement SQ Run Time Command (SQRTC) register
	 * thus need this quirk to skip related flow.
	 */
	UFSHCD_QUIRK_MCQ_BROKEN_RTC			= 1 << 21,

	/*
	 * This quirk needs to be enabled if the host controller supports inline
	 * encryption but it needs to initialize the crypto capabilities in a
	 * nonstandard way and/or needs to override blk_crypto_ll_ops.  If
	 * enabled, the standard code won't initialize the blk_crypto_profile;
	 * ufs_hba_variant_ops::init() must do it instead.
	 */
	UFSHCD_QUIRK_CUSTOM_CRYPTO_PROFILE		= 1 << 22,

	/*
	 * This quirk needs to be enabled if the host controller supports inline
	 * encryption but does not support the CRYPTO_GENERAL_ENABLE bit, i.e.
	 * host controller initialization fails if that bit is set.
	 */
	UFSHCD_QUIRK_BROKEN_CRYPTO_ENABLE		= 1 << 23,

	/*
	 * This quirk needs to be enabled if the host controller driver copies
	 * cryptographic keys into the PRDT in order to send them to hardware,
	 * and therefore the PRDT should be zeroized after each request (as per
	 * the standard best practice for managing keys).
	 */
	UFSHCD_QUIRK_KEYS_IN_PRDT			= 1 << 24,

	/*
	 * This quirk indicates that the controller reports the value 1 (not
	 * supported) in the Legacy Single DoorBell Support (LSDBS) bit of the
	 * Controller Capabilities register although it supports the legacy
	 * single doorbell mode.
	 */
	UFSHCD_QUIRK_BROKEN_LSDBS_CAP			= 1 << 25,
};

enum ufshcd_caps {
	/* Allow dynamic clk gating */
	UFSHCD_CAP_CLK_GATING				= 1 << 0,

	/* Allow hiberb8 with clk gating */
	UFSHCD_CAP_HIBERN8_WITH_CLK_GATING		= 1 << 1,

	/* Allow dynamic clk scaling */
	UFSHCD_CAP_CLK_SCALING				= 1 << 2,

	/* Allow auto bkops to enabled during runtime suspend */
	UFSHCD_CAP_AUTO_BKOPS_SUSPEND			= 1 << 3,

	/*
	 * This capability allows host controller driver to use the UFS HCI's
	 * interrupt aggregation capability.
	 * CAUTION: Enabling this might reduce overall UFS throughput.
	 */
	UFSHCD_CAP_INTR_AGGR				= 1 << 4,

	/*
	 * This capability allows the device auto-bkops to be always enabled
	 * except during suspend (both runtime and suspend).
	 * Enabling this capability means that device will always be allowed
	 * to do background operation when it's active but it might degrade
	 * the performance of ongoing read/write operations.
	 */
	UFSHCD_CAP_KEEP_AUTO_BKOPS_ENABLED_EXCEPT_SUSPEND = 1 << 5,

	/*
	 * This capability allows host controller driver to automatically
	 * enable runtime power management by itself instead of waiting
	 * for userspace to control the power management.
	 */
	UFSHCD_CAP_RPM_AUTOSUSPEND			= 1 << 6,

	/*
	 * This capability allows the host controller driver to turn-on
	 * WriteBooster, if the underlying device supports it and is
	 * provisioned to be used. This would increase the write performance.
	 */
	UFSHCD_CAP_WB_EN				= 1 << 7,

	/*
	 * This capability allows the host controller driver to use the
	 * inline crypto engine, if it is present
	 */
	UFSHCD_CAP_CRYPTO				= 1 << 8,

	/*
	 * This capability allows the controller regulators to be put into
	 * lpm mode aggressively during clock gating.
	 * This would increase power savings.
	 */
	UFSHCD_CAP_AGGR_POWER_COLLAPSE			= 1 << 9,

	/*
	 * This capability allows the host controller driver to use DeepSleep,
	 * if it is supported by the UFS device. The host controller driver must
	 * support device hardware reset via the hba->device_reset() callback,
	 * in order to exit DeepSleep state.
	 */
	UFSHCD_CAP_DEEPSLEEP				= 1 << 10,

	/*
	 * This capability allows the host controller driver to use temperature
	 * notification if it is supported by the UFS device.
	 */
	UFSHCD_CAP_TEMP_NOTIF				= 1 << 11,

	/*
	 * Enable WriteBooster when scaling up the clock and disable
	 * WriteBooster when scaling the clock down.
	 */
	UFSHCD_CAP_WB_WITH_CLK_SCALING			= 1 << 12,
};

struct ufs_hba_variant_params {
	struct devfreq_dev_profile devfreq_profile;
	struct devfreq_simple_ondemand_data ondemand_data;
	u16 hba_enable_delay_us;
	u32 wb_flush_threshold;
};

struct ufs_hba_monitor {
	unsigned long chunk_size;

	unsigned long nr_sec_rw[2];
	ktime_t total_busy[2];

	unsigned long nr_req[2];
	/* latencies*/
	ktime_t lat_sum[2];
	ktime_t lat_max[2];
	ktime_t lat_min[2];

	u32 nr_queued[2];
	ktime_t busy_start_ts[2];

	ktime_t enabled_ts;
	bool enabled;
};

/**
 * struct ufshcd_res_info_t - MCQ related resource regions
 *
 * @name: resource name
 * @resource: pointer to resource region
 * @base: register base address
 */
struct ufshcd_res_info {
	const char *name;
	struct resource *resource;
	void __iomem *base;
};

enum ufshcd_res {
	RES_UFS,
	RES_MCQ,
	RES_MCQ_SQD,
	RES_MCQ_SQIS,
	RES_MCQ_CQD,
	RES_MCQ_CQIS,
	RES_MCQ_VS,
	RES_MAX,
};

/**
 * struct ufshcd_mcq_opr_info_t - Operation and Runtime registers
 *
 * @offset: Doorbell Address Offset
 * @stride: Steps proportional to queue [0...31]
 * @base: base address
 */
struct ufshcd_mcq_opr_info_t {
	unsigned long offset;
	unsigned long stride;
	void __iomem *base;
};

enum ufshcd_mcq_opr {
	OPR_SQD,
	OPR_SQIS,
	OPR_CQD,
	OPR_CQIS,
	OPR_MAX,
};

/**
 * struct ufs_hba - per adapter private structure
 * @mmio_base: UFSHCI base register address
 * @ucdl_base_addr: UFS Command Descriptor base address
 * @utrdl_base_addr: UTP Transfer Request Descriptor base address
 * @utmrdl_base_addr: UTP Task Management Descriptor base address
 * @ucdl_dma_addr: UFS Command Descriptor DMA address
 * @utrdl_dma_addr: UTRDL DMA address
 * @utmrdl_dma_addr: UTMRDL DMA address
 * @host: Scsi_Host instance of the driver
 * @dev: device handle
 * @ufs_device_wlun: WLUN that controls the entire UFS device.
 * @hwmon_device: device instance registered with the hwmon core.
 * @curr_dev_pwr_mode: active UFS device power mode.
 * @uic_link_state: active state of the link to the UFS device.
 * @rpm_lvl: desired UFS power management level during runtime PM.
 * @spm_lvl: desired UFS power management level during system PM.
 * @pm_op_in_progress: whether or not a PM operation is in progress.
 * @ahit: value of Auto-Hibernate Idle Timer register.
 * @lrb: local reference block
 * @outstanding_tasks: Bits representing outstanding task requests
 * @outstanding_lock: Protects @outstanding_reqs.
 * @outstanding_reqs: Bits representing outstanding transfer requests
 * @capabilities: UFS Controller Capabilities
 * @mcq_capabilities: UFS Multi Circular Queue capabilities
 * @nutrs: Transfer Request Queue depth supported by controller
 * @nortt - Max outstanding RTTs supported by controller
 * @nutmrs: Task Management Queue depth supported by controller
 * @reserved_slot: Used to submit device commands. Protected by @dev_cmd.lock.
 * @ufs_version: UFS Version to which controller complies
 * @vops: pointer to variant specific operations
 * @vps: pointer to variant specific parameters
 * @priv: pointer to variant specific private data
 * @sg_entry_size: size of struct ufshcd_sg_entry (may include variant fields)
 * @irq: Irq number of the controller
 * @is_irq_enabled: whether or not the UFS controller interrupt is enabled.
 * @dev_ref_clk_freq: reference clock frequency
 * @quirks: bitmask with information about deviations from the UFSHCI standard.
 * @dev_quirks: bitmask with information about deviations from the UFS standard.
 * @tmf_tag_set: TMF tag set.
 * @tmf_queue: Used to allocate TMF tags.
 * @tmf_rqs: array with pointers to TMF requests while these are in progress.
 * @active_uic_cmd: handle of active UIC command
 * @uic_cmd_mutex: mutex for UIC command
 * @uic_async_done: completion used during UIC processing
 * @ufshcd_state: UFSHCD state
 * @eh_flags: Error handling flags
 * @intr_mask: Interrupt Mask Bits
 * @ee_ctrl_mask: Exception event control mask
 * @ee_drv_mask: Exception event mask for driver
 * @ee_usr_mask: Exception event mask for user (set via debugfs)
 * @ee_ctrl_mutex: Used to serialize exception event information.
 * @is_powered: flag to check if HBA is powered
 * @shutting_down: flag to check if shutdown has been invoked
 * @host_sem: semaphore used to serialize concurrent contexts
 * @eh_wq: Workqueue that eh_work works on
 * @eh_work: Worker to handle UFS errors that require s/w attention
 * @eeh_work: Worker to handle exception events
 * @errors: HBA errors
 * @uic_error: UFS interconnect layer error status
 * @saved_err: sticky error mask
 * @saved_uic_err: sticky UIC error mask
 * @ufs_stats: various error counters
 * @force_reset: flag to force eh_work perform a full reset
 * @force_pmc: flag to force a power mode change
 * @silence_err_logs: flag to silence error logs
 * @dev_cmd: ufs device management command information
 * @last_dme_cmd_tstamp: time stamp of the last completed DME command
 * @nop_out_timeout: NOP OUT timeout value
 * @dev_info: information about the UFS device
 * @auto_bkops_enabled: to track whether bkops is enabled in device
 * @vreg_info: UFS device voltage regulator information
 * @clk_list_head: UFS host controller clocks list node head
 * @use_pm_opp: Indicates whether OPP based scaling is used or not
 * @req_abort_count: number of times ufshcd_abort() has been called
 * @lanes_per_direction: number of lanes per data direction between the UFS
 *	controller and the UFS device.
 * @pwr_info: holds current power mode
 * @max_pwr_info: keeps the device max valid pwm
 * @clk_gating: information related to clock gating
 * @caps: bitmask with information about UFS controller capabilities
 * @devfreq: frequency scaling information owned by the devfreq core
 * @clk_scaling: frequency scaling information owned by the UFS driver
 * @system_suspending: system suspend has been started and system resume has
 *	not yet finished.
 * @is_sys_suspended: UFS device has been suspended because of system suspend
 * @urgent_bkops_lvl: keeps track of urgent bkops level for device
 * @is_urgent_bkops_lvl_checked: keeps track if the urgent bkops level for
 *  device is known or not.
 * @wb_mutex: used to serialize devfreq and sysfs write booster toggling
 * @clk_scaling_lock: used to serialize device commands and clock scaling
 * @desc_size: descriptor sizes reported by device
 * @scsi_block_reqs_cnt: reference counting for scsi block requests
 * @bsg_dev: struct device associated with the BSG queue
 * @bsg_queue: BSG queue associated with the UFS controller
 * @rpm_dev_flush_recheck_work: used to suspend from RPM (runtime power
 *	management) after the UFS device has finished a WriteBooster buffer
 *	flush or auto BKOP.
 * @monitor: statistics about UFS commands
 * @crypto_capabilities: Content of crypto capabilities register (0x100)
 * @crypto_cap_array: Array of crypto capabilities
 * @crypto_cfg_register: Start of the crypto cfg array
 * @crypto_profile: the crypto profile of this hba (if applicable)
 * @debugfs_root: UFS controller debugfs root directory
 * @debugfs_ee_work: used to restore ee_ctrl_mask after a delay
 * @debugfs_ee_rate_limit_ms: user configurable delay after which to restore
 *	ee_ctrl_mask
 * @luns_avail: number of regular and well known LUNs supported by the UFS
 *	device
 * @nr_hw_queues: number of hardware queues configured
 * @nr_queues: number of Queues of different queue types
 * @complete_put: whether or not to call ufshcd_rpm_put() from inside
 *	ufshcd_resume_complete()
 * @ext_iid_sup: is EXT_IID is supported by UFSHC
 * @mcq_sup: is mcq supported by UFSHC
 * @mcq_enabled: is mcq ready to accept requests
 * @res: array of resource info of MCQ registers
 * @mcq_base: Multi circular queue registers base address
 * @uhq: array of supported hardware queues
 * @dev_cmd_queue: Queue for issuing device management commands
 * @mcq_opr: MCQ operation and runtime registers
 * @ufs_rtc_update_work: A work for UFS RTC periodic update
 * @pm_qos_req: PM QoS request handle
 * @pm_qos_enabled: flag to check if pm qos is enabled
 */
struct ufs_hba {
	void __iomem *mmio_base;

	/* Virtual memory reference */
	struct utp_transfer_cmd_desc *ucdl_base_addr;
	struct utp_transfer_req_desc *utrdl_base_addr;
	struct utp_task_req_desc *utmrdl_base_addr;

	/* DMA memory reference */
	dma_addr_t ucdl_dma_addr;
	dma_addr_t utrdl_dma_addr;
	dma_addr_t utmrdl_dma_addr;

	struct Scsi_Host *host;
	struct device *dev;
	struct scsi_device *ufs_device_wlun;

#ifdef CONFIG_SCSI_UFS_HWMON
	struct device *hwmon_device;
#endif

	enum ufs_dev_pwr_mode curr_dev_pwr_mode;
	enum uic_link_state uic_link_state;
	/* Desired UFS power management level during runtime PM */
	enum ufs_pm_level rpm_lvl;
	/* Desired UFS power management level during system PM */
	enum ufs_pm_level spm_lvl;
	int pm_op_in_progress;

	/* Auto-Hibernate Idle Timer register value */
	u32 ahit;

	struct ufshcd_lrb *lrb;

	unsigned long outstanding_tasks;
	spinlock_t outstanding_lock;
	unsigned long outstanding_reqs;

	u32 capabilities;
	int nutrs;
	int nortt;
	u32 mcq_capabilities;
	int nutmrs;
	u32 reserved_slot;
	u32 ufs_version;
	const struct ufs_hba_variant_ops *vops;
	struct ufs_hba_variant_params *vps;
	void *priv;
#ifdef CONFIG_SCSI_UFS_VARIABLE_SG_ENTRY_SIZE
	size_t sg_entry_size;
#endif
	unsigned int irq;
	bool is_irq_enabled;
	enum ufs_ref_clk_freq dev_ref_clk_freq;

	unsigned int quirks;	/* Deviations from standard UFSHCI spec. */

	/* Device deviations from standard UFS device spec. */
	unsigned int dev_quirks;

	struct blk_mq_tag_set tmf_tag_set;
	struct request_queue *tmf_queue;
	struct request **tmf_rqs;

	struct uic_command *active_uic_cmd;
	struct mutex uic_cmd_mutex;
	struct completion *uic_async_done;

	enum ufshcd_state ufshcd_state;
	u32 eh_flags;
	u32 intr_mask;
	u16 ee_ctrl_mask;
	u16 ee_drv_mask;
	u16 ee_usr_mask;
	struct mutex ee_ctrl_mutex;
	bool is_powered;
	bool shutting_down;
	struct semaphore host_sem;

	/* Work Queues */
	struct workqueue_struct *eh_wq;
	struct work_struct eh_work;
	struct work_struct eeh_work;

	/* HBA Errors */
	u32 errors;
	u32 uic_error;
	u32 saved_err;
	u32 saved_uic_err;
	struct ufs_stats ufs_stats;
	bool force_reset;
	bool force_pmc;
	bool silence_err_logs;

	/* Device management request data */
	struct ufs_dev_cmd dev_cmd;
	ktime_t last_dme_cmd_tstamp;
	int nop_out_timeout;

	/* Keeps information of the UFS device connected to this host */
	struct ufs_dev_info dev_info;
	bool auto_bkops_enabled;
	struct ufs_vreg_info vreg_info;
	struct list_head clk_list_head;
	bool use_pm_opp;

	/* Number of requests aborts */
	int req_abort_count;

	/* Number of lanes available (1 or 2) for Rx/Tx */
	u32 lanes_per_direction;
	struct ufs_pa_layer_attr pwr_info;
	struct ufs_pwr_mode_info max_pwr_info;

	struct ufs_clk_gating clk_gating;
	/* Control to enable/disable host capabilities */
	u32 caps;

	struct devfreq *devfreq;
	struct ufs_clk_scaling clk_scaling;
	bool system_suspending;
	bool is_sys_suspended;

	enum bkops_status urgent_bkops_lvl;
	bool is_urgent_bkops_lvl_checked;

	struct mutex wb_mutex;
	struct rw_semaphore clk_scaling_lock;
	atomic_t scsi_block_reqs_cnt;

	struct device		bsg_dev;
	struct request_queue	*bsg_queue;
	struct delayed_work rpm_dev_flush_recheck_work;

	struct ufs_hba_monitor	monitor;

#ifdef CONFIG_SCSI_UFS_CRYPTO
	union ufs_crypto_capabilities crypto_capabilities;
	union ufs_crypto_cap_entry *crypto_cap_array;
	u32 crypto_cfg_register;
	struct blk_crypto_profile crypto_profile;
#endif
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_root;
	struct delayed_work debugfs_ee_work;
	u32 debugfs_ee_rate_limit_ms;
#endif
#ifdef CONFIG_SCSI_UFS_FAULT_INJECTION
	struct fault_attr trigger_eh_attr;
	struct fault_attr timeout_attr;
#endif
	u32 luns_avail;
	unsigned int nr_hw_queues;
	unsigned int nr_queues[HCTX_MAX_TYPES];
	bool complete_put;
	bool ext_iid_sup;
	bool scsi_host_added;
	bool mcq_sup;
	bool lsdb_sup;
	bool mcq_enabled;
	struct ufshcd_res_info res[RES_MAX];
	void __iomem *mcq_base;
	struct ufs_hw_queue *uhq;
	struct ufs_hw_queue *dev_cmd_queue;
	struct ufshcd_mcq_opr_info_t mcq_opr[OPR_MAX];

	struct delayed_work ufs_rtc_update_work;
	struct pm_qos_request pm_qos_req;
	bool pm_qos_enabled;
};

/**
 * struct ufs_hw_queue - per hardware queue structure
 * @mcq_sq_head: base address of submission queue head pointer
 * @mcq_sq_tail: base address of submission queue tail pointer
 * @mcq_cq_head: base address of completion queue head pointer
 * @mcq_cq_tail: base address of completion queue tail pointer
 * @sqe_base_addr: submission queue entry base address
 * @sqe_dma_addr: submission queue dma address
 * @cqe_base_addr: completion queue base address
 * @cqe_dma_addr: completion queue dma address
 * @max_entries: max number of slots in this hardware queue
 * @id: hardware queue ID
 * @sq_tp_slot: current slot to which SQ tail pointer is pointing
 * @sq_lock: serialize submission queue access
 * @cq_tail_slot: current slot to which CQ tail pointer is pointing
 * @cq_head_slot: current slot to which CQ head pointer is pointing
 * @cq_lock: Synchronize between multiple polling instances
 * @sq_mutex: prevent submission queue concurrent access
 */
struct ufs_hw_queue {
	void __iomem *mcq_sq_head;
	void __iomem *mcq_sq_tail;
	void __iomem *mcq_cq_head;
	void __iomem *mcq_cq_tail;

	struct utp_transfer_req_desc *sqe_base_addr;
	dma_addr_t sqe_dma_addr;
	struct cq_entry *cqe_base_addr;
	dma_addr_t cqe_dma_addr;
	u32 max_entries;
	u32 id;
	u32 sq_tail_slot;
	spinlock_t sq_lock;
	u32 cq_tail_slot;
	u32 cq_head_slot;
	spinlock_t cq_lock;
	/* prevent concurrent access to submission queue */
	struct mutex sq_mutex;
};

#define MCQ_QCFG_SIZE		0x40

static inline unsigned int ufshcd_mcq_opr_offset(struct ufs_hba *hba,
		enum ufshcd_mcq_opr opr, int idx)
{
	return hba->mcq_opr[opr].offset + hba->mcq_opr[opr].stride * idx;
}

static inline unsigned int ufshcd_mcq_cfg_offset(unsigned int reg, int idx)
{
	return reg + MCQ_QCFG_SIZE * idx;
}

#ifdef CONFIG_SCSI_UFS_VARIABLE_SG_ENTRY_SIZE
static inline size_t ufshcd_sg_entry_size(const struct ufs_hba *hba)
{
	return hba->sg_entry_size;
}

static inline void ufshcd_set_sg_entry_size(struct ufs_hba *hba, size_t sg_entry_size)
{
	WARN_ON_ONCE(sg_entry_size < sizeof(struct ufshcd_sg_entry));
	hba->sg_entry_size = sg_entry_size;
}
#else
static inline size_t ufshcd_sg_entry_size(const struct ufs_hba *hba)
{
	return sizeof(struct ufshcd_sg_entry);
}

#define ufshcd_set_sg_entry_size(hba, sg_entry_size)                   \
	({ (void)(hba); BUILD_BUG_ON(sg_entry_size != sizeof(struct ufshcd_sg_entry)); })
#endif

static inline size_t ufshcd_get_ucd_size(const struct ufs_hba *hba)
{
	return sizeof(struct utp_transfer_cmd_desc) + SG_ALL * ufshcd_sg_entry_size(hba);
}

/* Returns true if clocks can be gated. Otherwise false */
static inline bool ufshcd_is_clkgating_allowed(struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_CLK_GATING;
}
static inline bool ufshcd_can_hibern8_during_gating(struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_HIBERN8_WITH_CLK_GATING;
}
static inline int ufshcd_is_clkscaling_supported(struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_CLK_SCALING;
}
static inline bool ufshcd_can_autobkops_during_suspend(struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_AUTO_BKOPS_SUSPEND;
}
static inline bool ufshcd_is_rpm_autosuspend_allowed(struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_RPM_AUTOSUSPEND;
}

static inline bool ufshcd_is_intr_aggr_allowed(struct ufs_hba *hba)
{
	return (hba->caps & UFSHCD_CAP_INTR_AGGR) &&
		!(hba->quirks & UFSHCD_QUIRK_BROKEN_INTR_AGGR);
}

static inline bool ufshcd_can_aggressive_pc(struct ufs_hba *hba)
{
	return !!(ufshcd_is_link_hibern8(hba) &&
		  (hba->caps & UFSHCD_CAP_AGGR_POWER_COLLAPSE));
}

static inline bool ufshcd_is_auto_hibern8_supported(struct ufs_hba *hba)
{
	return (hba->capabilities & MASK_AUTO_HIBERN8_SUPPORT) &&
		!(hba->quirks & UFSHCD_QUIRK_BROKEN_AUTO_HIBERN8);
}

static inline bool ufshcd_is_auto_hibern8_enabled(struct ufs_hba *hba)
{
	return FIELD_GET(UFSHCI_AHIBERN8_TIMER_MASK, hba->ahit);
}

static inline bool ufshcd_is_wb_allowed(struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_WB_EN;
}

static inline bool ufshcd_enable_wb_if_scaling_up(struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_WB_WITH_CLK_SCALING;
}

#define ufsmcq_writel(hba, val, reg)	\
	writel((val), (hba)->mcq_base + (reg))
#define ufsmcq_readl(hba, reg)	\
	readl((hba)->mcq_base + (reg))

#define ufsmcq_writelx(hba, val, reg)	\
	writel_relaxed((val), (hba)->mcq_base + (reg))
#define ufsmcq_readlx(hba, reg)	\
	readl_relaxed((hba)->mcq_base + (reg))

#define ufshcd_writel(hba, val, reg)	\
	writel((val), (hba)->mmio_base + (reg))
#define ufshcd_readl(hba, reg)	\
	readl((hba)->mmio_base + (reg))

/**
 * ufshcd_rmwl - perform read/modify/write for a controller register
 * @hba: per adapter instance
 * @mask: mask to apply on read value
 * @val: actual value to write
 * @reg: register address
 */
static inline void ufshcd_rmwl(struct ufs_hba *hba, u32 mask, u32 val, u32 reg)
{
	u32 tmp;

	tmp = ufshcd_readl(hba, reg);
	tmp &= ~mask;
	tmp |= (val & mask);
	ufshcd_writel(hba, tmp, reg);
}

void ufshcd_enable_irq(struct ufs_hba *hba);
void ufshcd_disable_irq(struct ufs_hba *hba);
int ufshcd_alloc_host(struct device *, struct ufs_hba **);
void ufshcd_dealloc_host(struct ufs_hba *);
int ufshcd_hba_enable(struct ufs_hba *hba);
int ufshcd_init(struct ufs_hba *, void __iomem *, unsigned int);
int ufshcd_link_recovery(struct ufs_hba *hba);
int ufshcd_make_hba_operational(struct ufs_hba *hba);
void ufshcd_remove(struct ufs_hba *);
int ufshcd_uic_hibern8_enter(struct ufs_hba *hba);
int ufshcd_uic_hibern8_exit(struct ufs_hba *hba);
void ufshcd_delay_us(unsigned long us, unsigned long tolerance);
void ufshcd_parse_dev_ref_clk_freq(struct ufs_hba *hba, struct clk *refclk);
void ufshcd_update_evt_hist(struct ufs_hba *hba, u32 id, u32 val);
void ufshcd_hba_stop(struct ufs_hba *hba);
void ufshcd_schedule_eh_work(struct ufs_hba *hba);
void ufshcd_mcq_config_mac(struct ufs_hba *hba, u32 max_active_cmds);
unsigned int ufshcd_mcq_queue_cfg_addr(struct ufs_hba *hba);
u32 ufshcd_mcq_read_cqis(struct ufs_hba *hba, int i);
void ufshcd_mcq_write_cqis(struct ufs_hba *hba, u32 val, int i);
unsigned long ufshcd_mcq_poll_cqe_lock(struct ufs_hba *hba,
					 struct ufs_hw_queue *hwq);
void ufshcd_mcq_make_queues_operational(struct ufs_hba *hba);
void ufshcd_mcq_enable_esi(struct ufs_hba *hba);
void ufshcd_mcq_enable(struct ufs_hba *hba);
void ufshcd_mcq_config_esi(struct ufs_hba *hba, struct msi_msg *msg);

int ufshcd_opp_config_clks(struct device *dev, struct opp_table *opp_table,
			   struct dev_pm_opp *opp, void *data,
			   bool scaling_down);
/**
 * ufshcd_set_variant - set variant specific data to the hba
 * @hba: per adapter instance
 * @variant: pointer to variant specific data
 */
static inline void ufshcd_set_variant(struct ufs_hba *hba, void *variant)
{
	BUG_ON(!hba);
	hba->priv = variant;
}

/**
 * ufshcd_get_variant - get variant specific data from the hba
 * @hba: per adapter instance
 */
static inline void *ufshcd_get_variant(struct ufs_hba *hba)
{
	BUG_ON(!hba);
	return hba->priv;
}

#ifdef CONFIG_PM
extern int ufshcd_runtime_suspend(struct device *dev);
extern int ufshcd_runtime_resume(struct device *dev);
#endif
#ifdef CONFIG_PM_SLEEP
extern int ufshcd_system_suspend(struct device *dev);
extern int ufshcd_system_resume(struct device *dev);
extern int ufshcd_system_freeze(struct device *dev);
extern int ufshcd_system_thaw(struct device *dev);
extern int ufshcd_system_restore(struct device *dev);
#endif

extern int ufshcd_dme_configure_adapt(struct ufs_hba *hba,
				      int agreed_gear,
				      int adapt_val);
extern int ufshcd_dme_set_attr(struct ufs_hba *hba, u32 attr_sel,
			       u8 attr_set, u32 mib_val, u8 peer);
extern int ufshcd_dme_get_attr(struct ufs_hba *hba, u32 attr_sel,
			       u32 *mib_val, u8 peer);
extern int ufshcd_config_pwr_mode(struct ufs_hba *hba,
			struct ufs_pa_layer_attr *desired_pwr_mode);
extern int ufshcd_uic_change_pwr_mode(struct ufs_hba *hba, u8 mode);

/* UIC command interfaces for DME primitives */
#define DME_LOCAL	0
#define DME_PEER	1
#define ATTR_SET_NOR	0	/* NORMAL */
#define ATTR_SET_ST	1	/* STATIC */

static inline int ufshcd_dme_set(struct ufs_hba *hba, u32 attr_sel,
				 u32 mib_val)
{
	return ufshcd_dme_set_attr(hba, attr_sel, ATTR_SET_NOR,
				   mib_val, DME_LOCAL);
}

static inline int ufshcd_dme_st_set(struct ufs_hba *hba, u32 attr_sel,
				    u32 mib_val)
{
	return ufshcd_dme_set_attr(hba, attr_sel, ATTR_SET_ST,
				   mib_val, DME_LOCAL);
}

static inline int ufshcd_dme_peer_set(struct ufs_hba *hba, u32 attr_sel,
				      u32 mib_val)
{
	return ufshcd_dme_set_attr(hba, attr_sel, ATTR_SET_NOR,
				   mib_val, DME_PEER);
}

static inline int ufshcd_dme_peer_st_set(struct ufs_hba *hba, u32 attr_sel,
					 u32 mib_val)
{
	return ufshcd_dme_set_attr(hba, attr_sel, ATTR_SET_ST,
				   mib_val, DME_PEER);
}

static inline int ufshcd_dme_get(struct ufs_hba *hba,
				 u32 attr_sel, u32 *mib_val)
{
	return ufshcd_dme_get_attr(hba, attr_sel, mib_val, DME_LOCAL);
}

static inline int ufshcd_dme_peer_get(struct ufs_hba *hba,
				      u32 attr_sel, u32 *mib_val)
{
	return ufshcd_dme_get_attr(hba, attr_sel, mib_val, DME_PEER);
}

static inline bool ufshcd_is_hs_mode(struct ufs_pa_layer_attr *pwr_info)
{
	return (pwr_info->pwr_rx == FAST_MODE ||
		pwr_info->pwr_rx == FASTAUTO_MODE) &&
		(pwr_info->pwr_tx == FAST_MODE ||
		pwr_info->pwr_tx == FASTAUTO_MODE);
}

static inline int ufshcd_disable_host_tx_lcc(struct ufs_hba *hba)
{
	return ufshcd_dme_set(hba, UIC_ARG_MIB(PA_LOCAL_TX_LCC_ENABLE), 0);
}

void ufshcd_auto_hibern8_update(struct ufs_hba *hba, u32 ahit);
void ufshcd_fixup_dev_quirks(struct ufs_hba *hba,
			     const struct ufs_dev_quirk *fixups);
#define SD_ASCII_STD true
#define SD_RAW false
int ufshcd_read_string_desc(struct ufs_hba *hba, u8 desc_index,
			    u8 **buf, bool ascii);

void ufshcd_hold(struct ufs_hba *hba);
void ufshcd_release(struct ufs_hba *hba);

void ufshcd_clkgate_delay_set(struct device *dev, unsigned long value);

int ufshcd_get_vreg(struct device *dev, struct ufs_vreg *vreg);

int ufshcd_send_uic_cmd(struct ufs_hba *hba, struct uic_command *uic_cmd);

int ufshcd_advanced_rpmb_req_handler(struct ufs_hba *hba, struct utp_upiu_req *req_upiu,
				     struct utp_upiu_req *rsp_upiu, struct ufs_ehs *ehs_req,
				     struct ufs_ehs *ehs_rsp, int sg_cnt,
				     struct scatterlist *sg_list, enum dma_data_direction dir);
int ufshcd_wb_toggle(struct ufs_hba *hba, bool enable);
int ufshcd_wb_toggle_buf_flush(struct ufs_hba *hba, bool enable);
int ufshcd_suspend_prepare(struct device *dev);
int __ufshcd_suspend_prepare(struct device *dev, bool rpm_ok_for_spm);
void ufshcd_resume_complete(struct device *dev);
bool ufshcd_is_hba_active(struct ufs_hba *hba);
void ufshcd_pm_qos_init(struct ufs_hba *hba);
void ufshcd_pm_qos_exit(struct ufs_hba *hba);

/* Wrapper functions for safely calling variant operations */
static inline int ufshcd_vops_init(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->init)
		return hba->vops->init(hba);

	return 0;
}

static inline int ufshcd_vops_phy_initialization(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->phy_initialization)
		return hba->vops->phy_initialization(hba);

	return 0;
}

extern const struct ufs_pm_lvl_states ufs_pm_lvl_states[];

int ufshcd_dump_regs(struct ufs_hba *hba, size_t offset, size_t len,
		     const char *prefix);

int __ufshcd_write_ee_control(struct ufs_hba *hba, u32 ee_ctrl_mask);
int ufshcd_write_ee_control(struct ufs_hba *hba);
int ufshcd_update_ee_control(struct ufs_hba *hba, u16 *mask,
			     const u16 *other_mask, u16 set, u16 clr);

#endif /* End of Header */
