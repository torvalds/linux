// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#ifndef __SOUNDWIRE_H
#define __SOUNDWIRE_H

struct sdw_bus;
struct sdw_slave;

/* SDW spec defines and enums, as defined by MIPI 1.1. Spec */

/* SDW Broadcast Device Number */
#define SDW_BROADCAST_DEV_NUM		15

/* SDW Enumeration Device Number */
#define SDW_ENUM_DEV_NUM		0

/* SDW Group Device Numbers */
#define SDW_GROUP12_DEV_NUM		12
#define SDW_GROUP13_DEV_NUM		13

/* SDW Master Device Number, not supported yet */
#define SDW_MASTER_DEV_NUM		14

#define SDW_NUM_DEV_ID_REGISTERS	6

#define SDW_MAX_DEVICES			11

#define SDW_VALID_PORT_RANGE(n)		(n <= 14 && n >= 1)

/**
 * enum sdw_slave_status - Slave status
 * @SDW_SLAVE_UNATTACHED: Slave is not attached with the bus.
 * @SDW_SLAVE_ATTACHED: Slave is attached with bus.
 * @SDW_SLAVE_ALERT: Some alert condition on the Slave
 * @SDW_SLAVE_RESERVED: Reserved for future use
 */
enum sdw_slave_status {
	SDW_SLAVE_UNATTACHED = 0,
	SDW_SLAVE_ATTACHED = 1,
	SDW_SLAVE_ALERT = 2,
	SDW_SLAVE_RESERVED = 3,
};

/**
 * enum sdw_command_response - Command response as defined by SDW spec
 * @SDW_CMD_OK: cmd was successful
 * @SDW_CMD_IGNORED: cmd was ignored
 * @SDW_CMD_FAIL: cmd was NACKed
 * @SDW_CMD_TIMEOUT: cmd timedout
 * @SDW_CMD_FAIL_OTHER: cmd failed due to other reason than above
 *
 * NOTE: The enum is different than actual Spec as response in the Spec is
 * combination of ACK/NAK bits
 *
 * SDW_CMD_TIMEOUT/FAIL_OTHER is defined for SW use, not in spec
 */
enum sdw_command_response {
	SDW_CMD_OK = 0,
	SDW_CMD_IGNORED = 1,
	SDW_CMD_FAIL = 2,
	SDW_CMD_TIMEOUT = 3,
	SDW_CMD_FAIL_OTHER = 4,
};

/**
 * enum sdw_stream_type: data stream type
 *
 * @SDW_STREAM_PCM: PCM data stream
 * @SDW_STREAM_PDM: PDM data stream
 *
 * spec doesn't define this, but is used in implementation
 */
enum sdw_stream_type {
	SDW_STREAM_PCM = 0,
	SDW_STREAM_PDM = 1,
};

/**
 * enum sdw_data_direction: Data direction
 *
 * @SDW_DATA_DIR_RX: Data into Port
 * @SDW_DATA_DIR_TX: Data out of Port
 */
enum sdw_data_direction {
	SDW_DATA_DIR_RX = 0,
	SDW_DATA_DIR_TX = 1,
};

/*
 * SDW properties, defined in MIPI DisCo spec v1.0
 */
enum sdw_clk_stop_reset_behave {
	SDW_CLK_STOP_KEEP_STATUS = 1,
};

/**
 * enum sdw_p15_behave - Slave Port 15 behaviour when the Master attempts a
 * read
 * @SDW_P15_READ_IGNORED: Read is ignored
 * @SDW_P15_CMD_OK: Command is ok
 */
enum sdw_p15_behave {
	SDW_P15_READ_IGNORED = 0,
	SDW_P15_CMD_OK = 1,
};

/**
 * enum sdw_dpn_type - Data port types
 * @SDW_DPN_FULL: Full Data Port is supported
 * @SDW_DPN_SIMPLE: Simplified Data Port as defined in spec.
 * DPN_SampleCtrl2, DPN_OffsetCtrl2, DPN_HCtrl and DPN_BlockCtrl3
 * are not implemented.
 * @SDW_DPN_REDUCED: Reduced Data Port as defined in spec.
 * DPN_SampleCtrl2, DPN_HCtrl are not implemented.
 */
enum sdw_dpn_type {
	SDW_DPN_FULL = 0,
	SDW_DPN_SIMPLE = 1,
	SDW_DPN_REDUCED = 2,
};

/**
 * enum sdw_clk_stop_mode - Clock Stop modes
 * @SDW_CLK_STOP_MODE0: Slave can continue operation seamlessly on clock
 * restart
 * @SDW_CLK_STOP_MODE1: Slave may have entered a deeper power-saving mode,
 * not capable of continuing operation seamlessly when the clock restarts
 */
enum sdw_clk_stop_mode {
	SDW_CLK_STOP_MODE0 = 0,
	SDW_CLK_STOP_MODE1 = 1,
};

/**
 * struct sdw_dp0_prop - DP0 properties
 * @max_word: Maximum number of bits in a Payload Channel Sample, 1 to 64
 * (inclusive)
 * @min_word: Minimum number of bits in a Payload Channel Sample, 1 to 64
 * (inclusive)
 * @num_words: number of wordlengths supported
 * @words: wordlengths supported
 * @flow_controlled: Slave implementation results in an OK_NotReady
 * response
 * @simple_ch_prep_sm: If channel prepare sequence is required
 * @device_interrupts: If implementation-defined interrupts are supported
 *
 * The wordlengths are specified by Spec as max, min AND number of
 * discrete values, implementation can define based on the wordlengths they
 * support
 */
struct sdw_dp0_prop {
	u32 max_word;
	u32 min_word;
	u32 num_words;
	u32 *words;
	bool flow_controlled;
	bool simple_ch_prep_sm;
	bool device_interrupts;
};

/**
 * struct sdw_dpn_audio_mode - Audio mode properties for DPn
 * @bus_min_freq: Minimum bus frequency, in Hz
 * @bus_max_freq: Maximum bus frequency, in Hz
 * @bus_num_freq: Number of discrete frequencies supported
 * @bus_freq: Discrete bus frequencies, in Hz
 * @min_freq: Minimum sampling frequency, in Hz
 * @max_freq: Maximum sampling bus frequency, in Hz
 * @num_freq: Number of discrete sampling frequency supported
 * @freq: Discrete sampling frequencies, in Hz
 * @prep_ch_behave: Specifies the dependencies between Channel Prepare
 * sequence and bus clock configuration
 * If 0, Channel Prepare can happen at any Bus clock rate
 * If 1, Channel Prepare sequence shall happen only after Bus clock is
 * changed to a frequency supported by this mode or compatible modes
 * described by the next field
 * @glitchless: Bitmap describing possible glitchless transitions from this
 * Audio Mode to other Audio Modes
 */
struct sdw_dpn_audio_mode {
	u32 bus_min_freq;
	u32 bus_max_freq;
	u32 bus_num_freq;
	u32 *bus_freq;
	u32 max_freq;
	u32 min_freq;
	u32 num_freq;
	u32 *freq;
	u32 prep_ch_behave;
	u32 glitchless;
};

/**
 * struct sdw_dpn_prop - Data Port DPn properties
 * @num: port number
 * @max_word: Maximum number of bits in a Payload Channel Sample, 1 to 64
 * (inclusive)
 * @min_word: Minimum number of bits in a Payload Channel Sample, 1 to 64
 * (inclusive)
 * @num_words: Number of discrete supported wordlengths
 * @words: Discrete supported wordlength
 * @type: Data port type. Full, Simplified or Reduced
 * @max_grouping: Maximum number of samples that can be grouped together for
 * a full data port
 * @simple_ch_prep_sm: If the port supports simplified channel prepare state
 * machine
 * @ch_prep_timeout: Port-specific timeout value, in milliseconds
 * @device_interrupts: If set, each bit corresponds to support for
 * implementation-defined interrupts
 * @max_ch: Maximum channels supported
 * @min_ch: Minimum channels supported
 * @num_ch: Number of discrete channels supported
 * @ch: Discrete channels supported
 * @num_ch_combinations: Number of channel combinations supported
 * @ch_combinations: Channel combinations supported
 * @modes: SDW mode supported
 * @max_async_buffer: Number of samples that this port can buffer in
 * asynchronous modes
 * @block_pack_mode: Type of block port mode supported
 * @port_encoding: Payload Channel Sample encoding schemes supported
 * @audio_modes: Audio modes supported
 */
struct sdw_dpn_prop {
	u32 num;
	u32 max_word;
	u32 min_word;
	u32 num_words;
	u32 *words;
	enum sdw_dpn_type type;
	u32 max_grouping;
	bool simple_ch_prep_sm;
	u32 ch_prep_timeout;
	u32 device_interrupts;
	u32 max_ch;
	u32 min_ch;
	u32 num_ch;
	u32 *ch;
	u32 num_ch_combinations;
	u32 *ch_combinations;
	u32 modes;
	u32 max_async_buffer;
	bool block_pack_mode;
	u32 port_encoding;
	struct sdw_dpn_audio_mode *audio_modes;
};

/**
 * struct sdw_slave_prop - SoundWire Slave properties
 * @mipi_revision: Spec version of the implementation
 * @wake_capable: Wake-up events are supported
 * @test_mode_capable: If test mode is supported
 * @clk_stop_mode1: Clock-Stop Mode 1 is supported
 * @simple_clk_stop_capable: Simple clock mode is supported
 * @clk_stop_timeout: Worst-case latency of the Clock Stop Prepare State
 * Machine transitions, in milliseconds
 * @ch_prep_timeout: Worst-case latency of the Channel Prepare State Machine
 * transitions, in milliseconds
 * @reset_behave: Slave keeps the status of the SlaveStopClockPrepare
 * state machine (P=1 SCSP_SM) after exit from clock-stop mode1
 * @high_PHY_capable: Slave is HighPHY capable
 * @paging_support: Slave implements paging registers SCP_AddrPage1 and
 * SCP_AddrPage2
 * @bank_delay_support: Slave implements bank delay/bridge support registers
 * SCP_BankDelay and SCP_NextFrame
 * @p15_behave: Slave behavior when the Master attempts a read to the Port15
 * alias
 * @lane_control_support: Slave supports lane control
 * @master_count: Number of Masters present on this Slave
 * @source_ports: Bitmap identifying source ports
 * @sink_ports: Bitmap identifying sink ports
 * @dp0_prop: Data Port 0 properties
 * @src_dpn_prop: Source Data Port N properties
 * @sink_dpn_prop: Sink Data Port N properties
 */
struct sdw_slave_prop {
	u32 mipi_revision;
	bool wake_capable;
	bool test_mode_capable;
	bool clk_stop_mode1;
	bool simple_clk_stop_capable;
	u32 clk_stop_timeout;
	u32 ch_prep_timeout;
	enum sdw_clk_stop_reset_behave reset_behave;
	bool high_PHY_capable;
	bool paging_support;
	bool bank_delay_support;
	enum sdw_p15_behave p15_behave;
	bool lane_control_support;
	u32 master_count;
	u32 source_ports;
	u32 sink_ports;
	struct sdw_dp0_prop *dp0_prop;
	struct sdw_dpn_prop *src_dpn_prop;
	struct sdw_dpn_prop *sink_dpn_prop;
};

/**
 * struct sdw_master_prop - Master properties
 * @revision: MIPI spec version of the implementation
 * @master_count: Number of masters
 * @clk_stop_mode: Bitmap for Clock Stop modes supported
 * @max_freq: Maximum Bus clock frequency, in Hz
 * @num_clk_gears: Number of clock gears supported
 * @clk_gears: Clock gears supported
 * @num_freq: Number of clock frequencies supported, in Hz
 * @freq: Clock frequencies supported, in Hz
 * @default_frame_rate: Controller default Frame rate, in Hz
 * @default_row: Number of rows
 * @default_col: Number of columns
 * @dynamic_frame: Dynamic frame supported
 * @err_threshold: Number of times that software may retry sending a single
 * command
 * @dpn_prop: Data Port N properties
 */
struct sdw_master_prop {
	u32 revision;
	u32 master_count;
	enum sdw_clk_stop_mode clk_stop_mode;
	u32 max_freq;
	u32 num_clk_gears;
	u32 *clk_gears;
	u32 num_freq;
	u32 *freq;
	u32 default_frame_rate;
	u32 default_row;
	u32 default_col;
	bool dynamic_frame;
	u32 err_threshold;
	struct sdw_dpn_prop *dpn_prop;
};

int sdw_master_read_prop(struct sdw_bus *bus);
int sdw_slave_read_prop(struct sdw_slave *slave);

/*
 * SDW Slave Structures and APIs
 */

/**
 * struct sdw_slave_id - Slave ID
 * @mfg_id: MIPI Manufacturer ID
 * @part_id: Device Part ID
 * @class_id: MIPI Class ID, unused now.
 * Currently a placeholder in MIPI SoundWire Spec
 * @unique_id: Device unique ID
 * @sdw_version: SDW version implemented
 *
 * The order of the IDs here does not follow the DisCo spec definitions
 */
struct sdw_slave_id {
	__u16 mfg_id;
	__u16 part_id;
	__u8 class_id;
	__u8 unique_id:4;
	__u8 sdw_version:4;
};

/**
 * struct sdw_slave_intr_status - Slave interrupt status
 * @control_port: control port status
 * @port: data port status
 */
struct sdw_slave_intr_status {
	u8 control_port;
	u8 port[15];
};

/**
 * sdw_reg_bank - SoundWire register banks
 * @SDW_BANK0: Soundwire register bank 0
 * @SDW_BANK1: Soundwire register bank 1
 */
enum sdw_reg_bank {
	SDW_BANK0,
	SDW_BANK1,
};

/**
 * struct sdw_prepare_ch: Prepare/De-prepare Data Port channel
 *
 * @num: Port number
 * @ch_mask: Active channel mask
 * @prepare: Prepare (true) /de-prepare (false) channel
 * @bank: Register bank, which bank Slave/Master driver should program for
 * implementation defined registers. This is always updated to next_bank
 * value read from bus params.
 *
 */
struct sdw_prepare_ch {
	unsigned int num;
	unsigned int ch_mask;
	bool prepare;
	unsigned int bank;
};

/**
 * enum sdw_port_prep_ops: Prepare operations for Data Port
 *
 * @SDW_OPS_PORT_PRE_PREP: Pre prepare operation for the Port
 * @SDW_OPS_PORT_PREP: Prepare operation for the Port
 * @SDW_OPS_PORT_POST_PREP: Post prepare operation for the Port
 */
enum sdw_port_prep_ops {
	SDW_OPS_PORT_PRE_PREP = 0,
	SDW_OPS_PORT_PREP = 1,
	SDW_OPS_PORT_POST_PREP = 2,
};

/**
 * struct sdw_bus_params: Structure holding bus configuration
 *
 * @curr_bank: Current bank in use (BANK0/BANK1)
 * @next_bank: Next bank to use (BANK0/BANK1). next_bank will always be
 * set to !curr_bank
 */
struct sdw_bus_params {
	enum sdw_reg_bank curr_bank;
	enum sdw_reg_bank next_bank;
};

/**
 * struct sdw_slave_ops: Slave driver callback ops
 *
 * @read_prop: Read Slave properties
 * @interrupt_callback: Device interrupt notification (invoked in thread
 * context)
 * @update_status: Update Slave status
 * @port_prep: Prepare the port with parameters
 */
struct sdw_slave_ops {
	int (*read_prop)(struct sdw_slave *sdw);
	int (*interrupt_callback)(struct sdw_slave *slave,
			struct sdw_slave_intr_status *status);
	int (*update_status)(struct sdw_slave *slave,
			enum sdw_slave_status status);
	int (*port_prep)(struct sdw_slave *slave,
			struct sdw_prepare_ch *prepare_ch,
			enum sdw_port_prep_ops pre_ops);
};

/**
 * struct sdw_slave - SoundWire Slave
 * @id: MIPI device ID
 * @dev: Linux device
 * @status: Status reported by the Slave
 * @bus: Bus handle
 * @ops: Slave callback ops
 * @prop: Slave properties
 * @node: node for bus list
 * @port_ready: Port ready completion flag for each Slave port
 * @dev_num: Device Number assigned by Bus
 */
struct sdw_slave {
	struct sdw_slave_id id;
	struct device dev;
	enum sdw_slave_status status;
	struct sdw_bus *bus;
	const struct sdw_slave_ops *ops;
	struct sdw_slave_prop prop;
	struct list_head node;
	struct completion *port_ready;
	u16 dev_num;
};

#define dev_to_sdw_dev(_dev) container_of(_dev, struct sdw_slave, dev)

struct sdw_driver {
	const char *name;

	int (*probe)(struct sdw_slave *sdw,
			const struct sdw_device_id *id);
	int (*remove)(struct sdw_slave *sdw);
	void (*shutdown)(struct sdw_slave *sdw);

	const struct sdw_device_id *id_table;
	const struct sdw_slave_ops *ops;

	struct device_driver driver;
};

#define SDW_SLAVE_ENTRY(_mfg_id, _part_id, _drv_data) \
	{ .mfg_id = (_mfg_id), .part_id = (_part_id), \
	  .driver_data = (unsigned long)(_drv_data) }

int sdw_handle_slave_status(struct sdw_bus *bus,
			enum sdw_slave_status status[]);

/*
 * SDW master structures and APIs
 */

/**
 * struct sdw_port_params: Data Port parameters
 *
 * @num: Port number
 * @bps: Word length of the Port
 * @flow_mode: Port Data flow mode
 * @data_mode: Test modes or normal mode
 *
 * This is used to program the Data Port based on Data Port stream
 * parameters.
 */
struct sdw_port_params {
	unsigned int num;
	unsigned int bps;
	unsigned int flow_mode;
	unsigned int data_mode;
};

/**
 * struct sdw_transport_params: Data Port Transport Parameters
 *
 * @blk_grp_ctrl_valid: Port implements block group control
 * @num: Port number
 * @blk_grp_ctrl: Block group control value
 * @sample_interval: Sample interval
 * @offset1: Blockoffset of the payload data
 * @offset2: Blockoffset of the payload data
 * @hstart: Horizontal start of the payload data
 * @hstop: Horizontal stop of the payload data
 * @blk_pkg_mode: Block per channel or block per port
 * @lane_ctrl: Data lane Port uses for Data transfer. Currently only single
 * data lane is supported in bus
 *
 * This is used to program the Data Port based on Data Port transport
 * parameters. All these parameters are banked and can be modified
 * during a bank switch without any artifacts in audio stream.
 */
struct sdw_transport_params {
	bool blk_grp_ctrl_valid;
	unsigned int port_num;
	unsigned int blk_grp_ctrl;
	unsigned int sample_interval;
	unsigned int offset1;
	unsigned int offset2;
	unsigned int hstart;
	unsigned int hstop;
	unsigned int blk_pkg_mode;
	unsigned int lane_ctrl;
};

/**
 * struct sdw_enable_ch: Enable/disable Data Port channel
 *
 * @num: Port number
 * @ch_mask: Active channel mask
 * @enable: Enable (true) /disable (false) channel
 */
struct sdw_enable_ch {
	unsigned int port_num;
	unsigned int ch_mask;
	bool enable;
};

/**
 * struct sdw_master_port_ops: Callback functions from bus to Master
 * driver to set Master Data ports.
 *
 * @dpn_set_port_params: Set the Port parameters for the Master Port.
 * Mandatory callback
 * @dpn_set_port_transport_params: Set transport parameters for the Master
 * Port. Mandatory callback
 * @dpn_port_prep: Port prepare operations for the Master Data Port.
 * @dpn_port_enable_ch: Enable the channels of Master Port.
 */
struct sdw_master_port_ops {
	int (*dpn_set_port_params)(struct sdw_bus *bus,
			struct sdw_port_params *port_params,
			unsigned int bank);
	int (*dpn_set_port_transport_params)(struct sdw_bus *bus,
			struct sdw_transport_params *transport_params,
			enum sdw_reg_bank bank);
	int (*dpn_port_prep)(struct sdw_bus *bus,
			struct sdw_prepare_ch *prepare_ch);
	int (*dpn_port_enable_ch)(struct sdw_bus *bus,
			struct sdw_enable_ch *enable_ch, unsigned int bank);
};

struct sdw_msg;

/**
 * struct sdw_defer - SDW deffered message
 * @length: message length
 * @complete: message completion
 * @msg: SDW message
 */
struct sdw_defer {
	int length;
	struct completion complete;
	struct sdw_msg *msg;
};

/**
 * struct sdw_master_ops - Master driver ops
 * @read_prop: Read Master properties
 * @xfer_msg: Transfer message callback
 * @xfer_msg_defer: Defer version of transfer message callback
 * @reset_page_addr: Reset the SCP page address registers
 */
struct sdw_master_ops {
	int (*read_prop)(struct sdw_bus *bus);

	enum sdw_command_response (*xfer_msg)
			(struct sdw_bus *bus, struct sdw_msg *msg);
	enum sdw_command_response (*xfer_msg_defer)
			(struct sdw_bus *bus, struct sdw_msg *msg,
			struct sdw_defer *defer);
	enum sdw_command_response (*reset_page_addr)
			(struct sdw_bus *bus, unsigned int dev_num);
};

/**
 * struct sdw_bus - SoundWire bus
 * @dev: Master linux device
 * @link_id: Link id number, can be 0 to N, unique for each Master
 * @slaves: list of Slaves on this bus
 * @assigned: Bitmap for Slave device numbers.
 * Bit set implies used number, bit clear implies unused number.
 * @bus_lock: bus lock
 * @msg_lock: message lock
 * @ops: Master callback ops
 * @port_ops: Master port callback ops
 * @params: Current bus parameters
 * @prop: Master properties
 * @m_rt_list: List of Master instance of all stream(s) running on Bus. This
 * is used to compute and program bus bandwidth, clock, frame shape,
 * transport and port parameters
 * @defer_msg: Defer message
 * @clk_stop_timeout: Clock stop timeout computed
 */
struct sdw_bus {
	struct device *dev;
	unsigned int link_id;
	struct list_head slaves;
	DECLARE_BITMAP(assigned, SDW_MAX_DEVICES);
	struct mutex bus_lock;
	struct mutex msg_lock;
	const struct sdw_master_ops *ops;
	const struct sdw_master_port_ops *port_ops;
	struct sdw_bus_params params;
	struct sdw_master_prop prop;
	struct list_head m_rt_list;
	struct sdw_defer defer_msg;
	unsigned int clk_stop_timeout;
};

int sdw_add_bus_master(struct sdw_bus *bus);
void sdw_delete_bus_master(struct sdw_bus *bus);

/**
 * sdw_port_config: Master or Slave Port configuration
 *
 * @num: Port number
 * @ch_mask: channels mask for port
 */
struct sdw_port_config {
	unsigned int num;
	unsigned int ch_mask;
};

/**
 * sdw_stream_config: Master or Slave stream configuration
 *
 * @frame_rate: Audio frame rate of the stream, in Hz
 * @ch_count: Channel count of the stream
 * @bps: Number of bits per audio sample
 * @direction: Data direction
 * @type: Stream type PCM or PDM
 */
struct sdw_stream_config {
	unsigned int frame_rate;
	unsigned int ch_count;
	unsigned int bps;
	enum sdw_data_direction direction;
	enum sdw_stream_type type;
};

/**
 * sdw_stream_state: Stream states
 *
 * @SDW_STREAM_ALLOCATED: New stream allocated.
 * @SDW_STREAM_CONFIGURED: Stream configured
 * @SDW_STREAM_PREPARED: Stream prepared
 * @SDW_STREAM_ENABLED: Stream enabled
 * @SDW_STREAM_DISABLED: Stream disabled
 * @SDW_STREAM_DEPREPARED: Stream de-prepared
 * @SDW_STREAM_RELEASED: Stream released
 */
enum sdw_stream_state {
	SDW_STREAM_ALLOCATED = 0,
	SDW_STREAM_CONFIGURED = 1,
	SDW_STREAM_PREPARED = 2,
	SDW_STREAM_ENABLED = 3,
	SDW_STREAM_DISABLED = 4,
	SDW_STREAM_DEPREPARED = 5,
	SDW_STREAM_RELEASED = 6,
};

/**
 * sdw_stream_params: Stream parameters
 *
 * @rate: Sampling frequency, in Hz
 * @ch_count: Number of channels
 * @bps: bits per channel sample
 */
struct sdw_stream_params {
	unsigned int rate;
	unsigned int ch_count;
	unsigned int bps;
};

/**
 * sdw_stream_runtime: Runtime stream parameters
 *
 * @name: SoundWire stream name
 * @params: Stream parameters
 * @state: Current state of the stream
 * @type: Stream type PCM or PDM
 * @m_rt: Master runtime
 */
struct sdw_stream_runtime {
	char *name;
	struct sdw_stream_params params;
	enum sdw_stream_state state;
	enum sdw_stream_type type;
	struct sdw_master_runtime *m_rt;
};

struct sdw_stream_runtime *sdw_alloc_stream(char *stream_name);
void sdw_release_stream(struct sdw_stream_runtime *stream);
int sdw_stream_add_master(struct sdw_bus *bus,
		struct sdw_stream_config *stream_config,
		struct sdw_port_config *port_config,
		unsigned int num_ports,
		struct sdw_stream_runtime *stream);
int sdw_stream_add_slave(struct sdw_slave *slave,
		struct sdw_stream_config *stream_config,
		struct sdw_port_config *port_config,
		unsigned int num_ports,
		struct sdw_stream_runtime *stream);
int sdw_stream_remove_master(struct sdw_bus *bus,
		struct sdw_stream_runtime *stream);
int sdw_stream_remove_slave(struct sdw_slave *slave,
		struct sdw_stream_runtime *stream);

/* messaging and data APIs */

int sdw_read(struct sdw_slave *slave, u32 addr);
int sdw_write(struct sdw_slave *slave, u32 addr, u8 value);
int sdw_nread(struct sdw_slave *slave, u32 addr, size_t count, u8 *val);
int sdw_nwrite(struct sdw_slave *slave, u32 addr, size_t count, u8 *val);

#endif /* __SOUNDWIRE_H */
