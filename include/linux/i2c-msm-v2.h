/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2015,2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

/*
 * I2C controller driver for Qualcomm Technologies Inc platforms
 */

#ifndef _I2C_MSM_V2_H
#define _I2C_MSM_V2_H

#include <linux/bitops.h>
#include <linux/dmaengine.h>

enum msm_i2_debug_level {
	MSM_ERR,	/* Error messages only. Always on */
	MSM_PROF,	/* High level events. Use for profiling */
	MSM_DBG,	/* Low level details. Use for debugging */
};

#define i2c_msm_dbg(ctrl, dbg_level, fmt, ...) do {\
		if (ctrl->dbgfs.dbg_lvl >= dbg_level)\
			dev_info(ctrl->dev, fmt, ##__VA_ARGS__);\
	} while (0)

#define BIT_IS_SET(val, idx)        ((val >> idx) & 0x1)
#define BITS_AT(val, idx, n_bits)(((val) & (((1 << n_bits) - 1) << idx)) >> idx)
#define MASK_IS_SET(val, mask)      ((val & mask) == mask)
#define MASK_IS_SET_BOOL(val, mask) (MASK_IS_SET(val, mask) ? 1 : 0)
#define KHz(freq) (1000 * freq)
#define I2C_MSM_CLK_FAST_PLUS_FREQ  (1000000)

/* QUP Registers */
enum {
	QUP_CONFIG              = 0x0,
	QUP_STATE               = 0x4,
	QUP_IO_MODES            = 0x8,
	QUP_SW_RESET            = 0xC,
	QUP_OPERATIONAL         = 0x18,
	QUP_ERROR_FLAGS         = 0x1C,
	QUP_ERROR_FLAGS_EN      = 0x20,
	QUP_TEST_CTRL           = 0x24,
	QUP_OPERATIONAL_MASK    = 0x28,
	QUP_HW_VERSION          = 0x30,
	QUP_MX_READ_COUNT       = 0x208,
	QUP_MX_WRITE_COUNT      = 0x150,
	QUP_MX_OUTPUT_COUNT     = 0x100,
	QUP_MX_INPUT_COUNT      = 0x200,
	QUP_MX_WR_CNT           = 0x100,
	QUP_OUT_DEBUG           = 0x108,
	QUP_OUT_FIFO_CNT        = 0x10C,
	QUP_OUT_FIFO_BASE       = 0x110,
	QUP_IN_READ_CUR         = 0x20C,
	QUP_IN_DEBUG            = 0x210,
	QUP_IN_FIFO_CNT         = 0x214,
	QUP_IN_FIFO_BASE        = 0x218,
	QUP_I2C_MASTER_CLK_CTL  = 0x400,
	QUP_I2C_STATUS          = 0x404,
	QUP_I2C_MASTER_CONFIG   = 0x408,
	QUP_I2C_MASTER_BUS_CLR  = 0x40C,
};

/* Register:QUP_STATE state field values */
enum i2c_msm_qup_state {
	QUP_STATE_RESET         = 0,
	QUP_STATE_RUN           = 1U,
	QUP_STATE_PAUSE         = 3U,
};

/* Register:QUP_STATE fields */
enum {
	QUP_STATE_MASK          = 3U,
	QUP_STATE_VALID         = BIT(2),
	QUP_I2C_MAST_GEN        = BIT(4),
	QUP_I2C_FLUSH           = BIT(6),
	QUP_I2C_STATUS_RESET    = 0x42,
};


/* Register:QUP_CONFIG fields */
enum {
	QUP_MINI_CORE_MASK      = 0xF00,
	QUP_MINI_CORE_I2C_VAL   = 0x200,
	QUP_N_MASK              = 0x1F,
	QUP_N_VAL               = 0x7, /* 0xF for A family */
	QUP_NO_OUTPUT           = BIT(6),
	QUP_NO_INPUT            = BIT(7),
	QUP_APP_CLK_ON_EN       = BIT(12),
	QUP_CORE_CLK_ON_EN      = BIT(13),
	QUP_FIFO_CLK_GATE_EN    = BIT(14),
};

/* Register:QUP_OPERATIONAL fields */
enum {
	QUP_INPUT_FIFO_NOT_EMPTY = BIT(5),
	QUP_OUTPUT_SERVICE_FLAG  = BIT(8),
	QUP_INPUT_SERVICE_FLAG   = BIT(9),
	QUP_MAX_OUTPUT_DONE_FLAG = BIT(10),
	QUP_MAX_INPUT_DONE_FLAG  = BIT(11),
	QUP_OUT_BLOCK_WRITE_REQ  = BIT(12),
	QUP_IN_BLOCK_READ_REQ    = BIT(13),
};

/* Register:QUP_OPERATIONAL_MASK fields */
enum {
	QUP_INPUT_SERVICE_MASK  = BIT(9),
	QUP_OUTPUT_SERVICE_MASK = BIT(8),
};

/* Register:QUP_IO_MODES fields */
enum {
	QUP_OUTPUT_MODE         = BIT(10) | BIT(11),
	QUP_INPUT_MODE          = BIT(12) | BIT(13),
	QUP_UNPACK_EN           = BIT(14),
	QUP_PACK_EN             = BIT(15),
	QUP_OUTPUT_BIT_SHIFT_EN = BIT(16),
};

/* Register:QUP_I2C_STATUS (a.k.a I2C_MASTER_STATUS) fields */
enum {
	QUP_BUS_ERROR           = BIT(2),
	QUP_PACKET_NACKED       = BIT(3),
	QUP_ARB_LOST            = BIT(4),
	QUP_INVALID_WRITE       = BIT(5),
	QUP_FAILED              = BIT(6),
	QUP_BUS_ACTIVE          = BIT(8),
	QUP_BUS_MASTER          = BIT(9),
	QUP_INVALID_TAG         = BIT(23),
	QUP_INVALID_READ_ADDR   = BIT(24),
	QUP_INVALID_READ_SEQ    = BIT(25),
	QUP_I2C_SDA             = BIT(26),
	QUP_I2C_SCL             = BIT(27),
	QUP_MSTR_STTS_ERR_MASK  = 0x380003C,
};

/* Register:QUP_I2C_MASTER_CONFIG fields */
enum {
	QUP_EN_VERSION_TWO_TAG  = 1U,
};

/* Register:QUP_I2C_MASTER_CLK_CTL field setters */
#define I2C_MSM_SCL_NOISE_REJECTION(reg_val, noise_rej_val) \
		(((reg_val) & ~(0x3 << 24)) | (((noise_rej_val) & 0x3) << 24))
#define I2C_MSM_SDA_NOISE_REJECTION(reg_val, noise_rej_val) \
		(((reg_val) & ~(0x3 << 26)) | (((noise_rej_val) & 0x3) << 26))

/* Register:QUP_ERROR_FLAGS_EN flags */
enum {
	QUP_OUTPUT_OVER_RUN_ERR_EN  = BIT(5),
	QUP_INPUT_UNDER_RUN_ERR_EN  = BIT(4),
	QUP_OUTPUT_UNDER_RUN_ERR_EN = BIT(3),
	QUP_INPUT_OVER_RUN_ERR_EN   = BIT(2),
};

/* Status, Error flags */
enum {
	I2C_STATUS_WR_BUFFER_FULL  = BIT(0),
	I2C_STATUS_BUS_ACTIVE      = BIT(8),
	I2C_STATUS_BUS_MASTER      = BIT(9),
	I2C_STATUS_ERROR_MASK      = 0x38000FC,
	QUP_I2C_NACK_FLAG          = BIT(3),
	QUP_IN_NOT_EMPTY           = BIT(5),
	QUP_ERR_FLGS_MASK           = 0x3C,
};

/* Master status clock states */
enum {
	I2C_CLK_RESET_BUSIDLE_STATE = 0,
	I2C_CLK_FORCED_LOW_STATE    = 5,
};

/* Controller's power state */
enum i2c_msm_power_state {
	I2C_MSM_PM_RT_ACTIVE,
	I2C_MSM_PM_RT_SUSPENDED,
	I2C_MSM_PM_SYS_SUSPENDED
};

/*
 * The max buffer size required for tags is for holding the following sequence:
 * [start] + [start | slv-addr] + [ rd/wr | len]
 * which sum up to 6 bytes. However, we use u64 to hold the value, thus we say
 * that max length is 8 bytes.
 */
#define I2C_MSM_TAG2_MAX_LEN            (4)
#define I2C_MSM_DMA_TX_SZ             (64) /* tx chan n entries */
#define I2C_MSM_DMA_RX_SZ             (32) /* rx chan n entries */
#define I2C_MSM_DMA_DESC_ARR_SIZ  (I2C_MSM_DMA_TX_SZ + I2C_MSM_DMA_RX_SZ)
#define I2C_MSM_REG_2_STR_BUF_SZ        (128)
/* Optimal value to hold the error strings */
#define I2C_MSM_MAX_ERR_BUF_SZ		(256)
#define I2C_MSM_BUF_DUMP_MAX_BC         (20)
#define I2C_MSM_MAX_POLL_MSEC           (100)
#define I2C_MSM_TIMEOUT_SAFETY_COEF     (10)
#define I2C_MSM_TIMEOUT_MIN_USEC        (500000)

/* QUP v2 tags */
#define QUP_TAG2_DATA_WRITE        (0x82ULL)
#define QUP_TAG2_DATA_WRITE_N_STOP (0x83ULL)
#define QUP_TAG2_DATA_READ         (0x85ULL)
#define QUP_TAG2_DATA_READ_N_STOP  (0x87ULL)
#define QUP_TAG2_START             (0x81ULL)
#define QUP_TAG2_DATA_READ_N_NACK  (0x86ULL)
#define QUP_TAG2_START_STOP        (0x8AULL)
#define QUP_TAG2_INPUT_EOT         (0x93ULL)
#define QUP_TAG2_FLUSH_STOP        (0x96ULL)
#define QUP_BUF_OVERHD_BC          (2)
#define QUP_MAX_BUF_SZ             (256)

enum i2c_msm_clk_path_vec_idx {
	I2C_MSM_CLK_PATH_SUSPEND_VEC,
	I2C_MSM_CLK_PATH_RESUME_VEC,
};
#define I2C_MSM_CLK_PATH_AVRG_BW(ctrl) (0)
/*
 * Reducing the frequency by 1 to make sure it is less than 19.2MHz
 * so that we don't need RPM ack to unvote which will work only if vote
 * is less than or equal to 19.2MHz. To be on the safe side we are decreasing
 * frequency by 1.
 */
#define I2C_MSM_CLK_PATH_BRST_BW(ctrl) ((ctrl->rsrcs.clk_freq_in - 1) * 4)

/* Path from Qup to DDR */
#define DST_ID 512

enum i2c_msm_gpio_name_idx {
	I2C_MSM_GPIO_SCL,
	I2C_MSM_GPIO_SDA,
};

extern const char * const i2c_msm_mode_str_tbl[];

struct i2c_msm_ctrl;

/*
 *  i2c_msm_dma_mem: utility struct which holds both physical and virtual addr
 */
struct i2c_msm_dma_mem {
	dma_addr_t               phy_addr;
	void                    *vrtl_addr;
};

/*
 * i2c_msm_tag: tag's data and its length.
 *
 * @len tag len can be two, four or six bytes.
 */
struct i2c_msm_tag {
	u64                    val;
	int                    len;
};

/*
 * i2c_msm_dma_tag: similar to struct i2c_msm_tag but holds physical address.
 *
 * @buf physical address of entry in the tag_arr of
 *          struct i2c_msm_xfer_mode_dma
 * @len tag len.
 *
 * Hold the information from i2c_msm_dma_xfer_prepare() which is used by
 * i2c_msm_dma_xfer_process() and freed by i2c_msm_dma_xfer_unprepare()
 */
struct i2c_msm_dma_tag {
	dma_addr_t             buf;
	size_t                 len;
};

/*
 * i2c_msm_dma_buf: dma mapped pointer to i2c_msg data buffer and related tag
 * @vir_addr ptr to i2c_msg buf beginning or with offset (when buf len > 256)
 */
struct i2c_msm_dma_buf {
	struct i2c_msm_dma_mem   ptr;
	enum dma_data_direction  dma_dir;
	size_t                   len;
	bool                     is_rx;
	bool                     is_last;
	struct i2c_msm_dma_tag   tag;
	/* DMA API */
	struct scatterlist	sg_list[2];
};

/*
 * i2c_msm_dma_chan: per channel info
 *
 * @is_init true when the channel is initialized and requires eventual teardown.
 * @name channel name (tx/rx) for debugging.
 * @desc_cnt_cur number of occupied descriptors
 */
struct i2c_msm_dma_chan {
	bool                     is_init;
	const char              *name;
	size_t                   desc_cnt_cur;
	struct dma_chan         *dma_chan;
	enum dma_transfer_direction dir;
};

enum i2c_msm_dma_chan_dir {
	I2C_MSM_DMA_TX,
	I2C_MSM_DMA_RX,
	I2C_MSM_DMA_CNT,
};

enum i2c_msm_dma_state {
	I2C_MSM_DMA_INIT_NONE, /* Uninitialized  DMA */
	I2C_MSM_DMA_INIT_CORE, /* Core init not channels, memory Allocated */
	I2C_MSM_DMA_INIT_CHAN, /* Both Core and channels are init */
};

/*
 * struct i2c_msm_xfer_mode_dma: DMA mode configuration and work space
 *
 * @state   specifies the DMA core and channel initialization states.
 * @buf_arr_cnt current number of valid buffers in buf_arr. The valid buffers
 *          are at index 0..buf_arr_cnt excluding buf_arr_cnt.
 * @buf_arr array of descriptors which point to the user's buffer
 *     virtual and physical address, and hold data about the buffer
 *     and respective tag.
 * @tag_arr array of tags in DMAable memory. Holds a tag per buffer of the same
 *          index, that is tag_arr[i] is related to buf_arr[i]. Also, tag_arr[i]
 *          is queued in the tx channel just before buf_arr[i] is queued in
 *          the tx (output buf) or rx channel (input buffer).
 * @eot_n_flush_stop_tags EOT and flush-stop tags to be queued to the tx
 *          DMA channel after the last transfer when it is a read.
 * @input_tag hw is placing input tags in the rx channel on read operations.
 *          The value of these tags is "don't care" from DMA transfer
 *          perspective. Thus, this single buffer is used for all the input
 *          tags. The field is used as write only.
 */
struct i2c_msm_xfer_mode_dma {
	enum i2c_msm_dma_state   state;
	size_t                   buf_arr_cnt;
	struct i2c_msm_dma_buf   buf_arr[I2C_MSM_DMA_DESC_ARR_SIZ];
	struct i2c_msm_dma_mem   tag_arr;
	struct i2c_msm_dma_mem   eot_n_flush_stop_tags;
	struct i2c_msm_dma_mem   input_tag;
	struct i2c_msm_dma_chan  chan[I2C_MSM_DMA_CNT];
};

/*
 * I2C_MSM_DMA_TAG_MEM_SZ includes the following fields of
 * struct i2c_msm_xfer_mode_dma (in order):
 *
 * Buffer of DMA memory:
 * +-----------+---------+-----------+-----------+----+-----------+
 * | input_tag | eot_... | tag_arr 0 | tag_arr 1 | .. | tag_arr n |
 * +-----------+---------+-----------+-----------+----+-----------+
 *
 * Why +2?
 * One tag buffer for the input tags. This is a write only buffer for DMA, it is
 *    used to read the tags of the input fifo. We let them overwrite each other,
 *    since it is a throw-away from the driver's perspective.
 * Second tag buffer for the EOT and flush-stop tags. This is a read only
 *    buffer (from DMA perspective). It is used to put EOT and flush-stop at the
 *    end of every transaction.
 */
#define I2C_MSM_DMA_TAG_MEM_SZ  \
	((I2C_MSM_DMA_DESC_ARR_SIZ + 2) * I2C_MSM_TAG2_MAX_LEN)

/*
 * i2c_msm_xfer_mode_fifo: operations and state of FIFO mode
 *
 * @ops     "base class" of i2c_msm_xfer_mode_dma. Contains the operations while
 *          the rest of the fields contain the data.
 * @input_fifo_sz input fifo size in bytes
 * @output_fifo_sz output fifo size in bytes
 * @in_rem  remaining u32 entries in input FIFO before empty
 * @out_rem remaining u32 entries in output FIFO before full
 * @out_buf buffer for collecting bytes to four bytes groups (u32) before
 *          writing them to the output fifo.
 * @out_buf_idx next free index in out_buf. 0..3
 */
struct i2c_msm_xfer_mode_fifo {
	size_t                   input_fifo_sz;
	size_t                   output_fifo_sz;
	size_t                   in_rem;
	size_t                   out_rem;
	u8                       out_buf[4];
	int                      out_buf_idx;
};

/* i2c_msm_xfer_mode_blk: operations and state of Block mode
 *
 * @is_init when true, struct is initialized and requires mem free on exit
 * @in_blk_sz size of input/rx block
 * @out_blk_sz size of output/tx block
 * @tx_cache internal buffer to store tx data
 * @rx_cache internal buffer to store rx data
 * @rx_cache_idx points to the next unread index in rx cache
 * @tx_cache_idx points to the next unwritten index in tx cache
 * @wait_rx_blk completion object to wait on for end of blk rx transfer.
 * @wait_tx_blk completion object to wait on for end of blk tx transfer.
 * @complete_mask applied to QUP_OPERATIONAL to determine when blk
 *  xfer is complete.
 */
struct i2c_msm_xfer_mode_blk {
	bool                     is_init;
	size_t                   in_blk_sz;
	size_t                   out_blk_sz;
	u8                       *tx_cache;
	u8                       *rx_cache;
	int                      rx_cache_idx;
	int                      tx_cache_idx;
	struct completion        wait_rx_blk;
	struct completion        wait_tx_blk;
	u32                      complete_mask;
};

/* INPUT_MODE and OUTPUT_MODE filds of QUP_IO_MODES register */
enum i2c_msm_xfer_mode_id {
	I2C_MSM_XFER_MODE_FIFO,
	I2C_MSM_XFER_MODE_BLOCK,
	I2C_MSM_XFER_MODE_DMA,
	I2C_MSM_XFER_MODE_NONE, /* keep last as a counter */
};


struct i2c_msm_dbgfs {
	struct dentry             *root;
	enum msm_i2_debug_level    dbg_lvl;
	enum i2c_msm_xfer_mode_id  force_xfer_mode;
};

/*
 * qup_i2c_clk_path_vote: data to use bus scaling driver for clock path vote
 *
 * @mstr_id master id number of the i2c core or its wrapper (BLSP/GSBI).
 *       When zero, clock path voting is disabled.
 * @client_hdl when zero, client is not registered with the bus scaling driver,
 *      and bus scaling functionality should not be used. When non zero, it
 *      is a bus scaling client id and may be used to vote for clock path.
 * @reg_err when true, registration error was detected and an error message was
 *      logged. i2c will attempt to re-register but will log error only once.
 *      once registration succeed, the flag is set to false.
 * @actv_only when set, votes when system active and removes the vote when
 *       system goes idle (optimises for performance). When unset, voting using
 *       runtime pm (optimizes for power).
 */

/*
 * i2c_msm_resources: OS resources
 *
 * @mem  I2C controller memory resource from platform data.
 * @base I2C controller virtual base address
 * @clk_freq_in core clock frequency in Hz
 * @clk_freq_out bus clock frequency in Hz
 */
struct i2c_msm_resources {
	struct resource             *mem;
	void __iomem                *base; /* virtual */
	struct clk                  *core_clk;
	struct clk                  *iface_clk;
	int                          clk_freq_in;
	int                          clk_freq_out;
	struct icc_path	*icc_path;
	u32                         mstr_id;
	int                          irq;
	bool                         disable_dma;
	struct pinctrl              *pinctrl;
	struct pinctrl_state        *gpio_state_active;
	struct pinctrl_state        *gpio_state_suspend;
};

#define I2C_MSM_PINCTRL_ACTIVE       "i2c_active"
#define I2C_MSM_PINCTRL_SUSPEND      "i2c_sleep"

/*
 * i2c_msm_xfer_buf: current xfer position and preprocessed tags
 *
 * @is_init the buf is marked initialized by the first call to
 *          i2c_msm_xfer_next_buf()
 * @msg_idx   index of the message that the buffer is pointing to
 * @byte_idx  index of first byte in the current buffer
 * @end_idx   count of bytes processed from the current message. This value
 *            is compared against len to find out if buffer is done processing.
 * @len       number of bytes in current buffer.
 * @is_rx when true, current buffer is pointing to a i2c read operation.
 * @slv_addr 8 bit address. This is the i2c_msg->addr + rd/wr bit.
 *
 * Keep track of current position in the client's transfer request and
 * pre-process a transfer's buffer and tags.
 */
struct i2c_msm_xfer_buf {
	bool                       is_init;
	int                        msg_idx;
	int                        byte_idx;
	int                        end_idx;
	int                        len;
	bool                       is_rx;
	bool                       is_last;
	u16                        slv_addr;
	struct i2c_msm_tag         in_tag;
	struct i2c_msm_tag         out_tag;
};

#ifdef DEBUG
#define I2C_MSM_PROF_MAX_EVNTS   (64)
#else
#define I2C_MSM_PROF_MAX_EVNTS   (16)
#endif

/*
 * i2c_msm_prof_event: profiling event
 *
 * @data Additional data about the event. The interpretation of the data is
 *       dependent on the type field.
 * @type event type (see enum i2c_msm_prof_event_type)
 */
struct i2c_msm_prof_event {
	struct timespec64 time;
	u64             data0;
	u32             data1;
	u32             data2;
	u8              type;
	u8              dump_func_id;
};

enum i2c_msm_err {
	I2C_MSM_NO_ERR = 0,
	I2C_MSM_ERR_NACK,
	I2C_MSM_ERR_ARB_LOST,
	I2C_MSM_ERR_BUS_ERR,
	I2C_MSM_ERR_TIMEOUT,
	I2C_MSM_ERR_CORE_CLK,
	I2C_MSM_ERR_OVR_UNDR_RUN,
};

/*
 * i2c_msm_xfer: A client transfer request. A list of one or more i2c messages
 *
 * @msgs         NULL when no active xfer. Points to array of i2c_msgs
 *               given by the client.
 * @msg_cnt      number of messages in msgs array.
 * @complete     completion object to wait on for end of transfer.
 * @rx_cnt       number of input  bytes in the client's request.
 * @tx_cnt       number of output bytes in the client's request.
 * @rx_ovrhd_cnt number of input  bytes due to tags.
 * @tx_ovrhd_cnt number of output bytes due to tags.
 * @event        profiling data. An array of timestamps of transfer events
 * @event_cnt    number of items in event array.
 * @is_active    true during xfer process and false after xfer end
 * @mtx          mutex to solve multithreaded problem in xfer
 */
struct i2c_msm_xfer {
	struct i2c_msg            *msgs;
	int                        msg_cnt;
	enum i2c_msm_xfer_mode_id  mode_id;
	struct completion          complete;
	struct completion          rx_complete;
	size_t                     rx_cnt;
	size_t                     tx_cnt;
	size_t                     rx_ovrhd_cnt;
	size_t                     tx_ovrhd_cnt;
	struct i2c_msm_xfer_buf    cur_buf;
	u32                        timeout;
	bool                       last_is_rx;
	enum i2c_msm_err           err;
	struct i2c_msm_prof_event  event[I2C_MSM_PROF_MAX_EVNTS];
	atomic_t                   event_cnt;
	atomic_t                   is_active;
	struct mutex               mtx;
	struct i2c_msm_xfer_mode_fifo	fifo;
	struct i2c_msm_xfer_mode_blk	blk;
	struct i2c_msm_xfer_mode_dma	dma;
};

/*
 * i2c_msm_ctrl: the driver's main struct
 *
 * @is_init true when
 * @ver info that is different between i2c controller versions
 * @ver_num  ha
 * @xfer     state of the currently processed transfer.
 * @dbgfs    debug-fs root and values that may be set via debug-fs.
 * @rsrcs    resources from platform data including clocks, gpios, irqs, and
 *           memory regions.
 * @mstr_clk_ctl cached value for programming to mstr_clk_ctl register
 * @i2c_sts_reg	 status of QUP_I2C_MASTER_STATUS register.
 * @qup_op_reg	 status of QUP_OPERATIONAL register.
 */
struct i2c_msm_ctrl {
	struct device             *dev;
	struct i2c_adapter         adapter;
	struct i2c_msm_xfer        xfer;
	struct i2c_msm_dbgfs       dbgfs;
	struct i2c_msm_resources   rsrcs;
	u32                        mstr_clk_ctl;
	u32			   i2c_sts_reg;
	u32			   qup_op_reg;
	enum i2c_msm_power_state   pwr_state;
};

/* Enum for the profiling event types */
enum i2c_msm_prof_evnt_type {
	I2C_MSM_VALID_END,
	I2C_MSM_PIP_DSCN,
	I2C_MSM_PIP_CNCT,
	I2C_MSM_ACTV_END,
	I2C_MSM_IRQ_BGN,
	I2C_MSM_IRQ_END,
	I2C_MSM_XFER_BEG,
	I2C_MSM_XFER_END,
	I2C_MSM_SCAN_SUM,
	I2C_MSM_NEXT_BUF,
	I2C_MSM_COMPLT_OK,
	I2C_MSM_COMPLT_FL,
	I2C_MSM_PROF_RESET,
};

#ifdef CONFIG_I2C_MSM_PROF_DBG
void i2c_msm_dbgfs_init(struct i2c_msm_ctrl *ctrl);

void i2c_msm_dbgfs_teardown(struct i2c_msm_ctrl *ctrl);

/* diagonisis the i2c registers and dump the errors accordingly */
const char *i2c_msm_dbg_tag_to_str(const struct i2c_msm_tag *tag,
						char *buf, size_t buf_len);

void i2c_msm_prof_evnt_dump(struct i2c_msm_ctrl *ctrl);

/* function definitions to be used from the i2c-msm-v2-debug file */
void i2c_msm_prof_evnt_add(struct i2c_msm_ctrl *ctrl,
				enum msm_i2_debug_level dbg_level,
				enum i2c_msm_prof_evnt_type event,
				u64 data0, u32 data1, u32 data2);

int i2c_msm_dbg_qup_reg_dump(struct i2c_msm_ctrl *ctrl);

const char *
i2c_msm_dbg_dma_tag_to_str(const struct i2c_msm_dma_tag *dma_tag, char *buf,
								size_t buf_len);
#else
/* use dummy functions */
static inline void i2c_msm_dbgfs_init(struct i2c_msm_ctrl *ctrl) {}
static inline void i2c_msm_dbgfs_teardown(struct i2c_msm_ctrl *ctrl) {}

static inline const char *i2c_msm_dbg_tag_to_str(const struct i2c_msm_tag *tag,
						char *buf, size_t buf_len)
{
	return NULL;
}
static inline void i2c_msm_prof_evnt_dump(struct i2c_msm_ctrl *ctrl) {}

/* function definitions to be used from the i2c-msm-v2-debug file */
static inline void i2c_msm_prof_evnt_add(struct i2c_msm_ctrl *ctrl,
				enum msm_i2_debug_level dbg_level,
				enum i2c_msm_prof_evnt_type event,
				u64 data0, u32 data1, u32 data2) {}

static inline int i2c_msm_dbg_qup_reg_dump(struct i2c_msm_ctrl *ctrl)
{
	return true;
}
static inline const char *i2c_msm_dbg_dma_tag_to_str(const struct
			i2c_msm_dma_tag * dma_tag, char *buf, size_t buf_len)
{
	return NULL;
}
#endif /* I2C_MSM_V2_PROF_DBG */
#endif /* _I2C_MSM_V2_H */
