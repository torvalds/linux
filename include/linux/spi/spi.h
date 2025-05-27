/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2005 David Brownell
 */

#ifndef __LINUX_SPI_H
#define __LINUX_SPI_H

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/kthread.h>
#include <linux/mod_devicetable.h>
#include <linux/overflow.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/u64_stats_sync.h>

#include <uapi/linux/spi/spi.h>

/* Max no. of CS supported per spi device */
#define SPI_CS_CNT_MAX 16

struct dma_chan;
struct software_node;
struct ptp_system_timestamp;
struct spi_controller;
struct spi_transfer;
struct spi_controller_mem_ops;
struct spi_controller_mem_caps;
struct spi_message;
struct spi_offload;
struct spi_offload_config;

/*
 * INTERFACES between SPI controller-side drivers and SPI target protocol handlers,
 * and SPI infrastructure.
 */
extern const struct bus_type spi_bus_type;

/**
 * struct spi_statistics - statistics for spi transfers
 * @syncp:         seqcount to protect members in this struct for per-cpu update
 *                 on 32-bit systems
 *
 * @messages:      number of spi-messages handled
 * @transfers:     number of spi_transfers handled
 * @errors:        number of errors during spi_transfer
 * @timedout:      number of timeouts during spi_transfer
 *
 * @spi_sync:      number of times spi_sync is used
 * @spi_sync_immediate:
 *                 number of times spi_sync is executed immediately
 *                 in calling context without queuing and scheduling
 * @spi_async:     number of times spi_async is used
 *
 * @bytes:         number of bytes transferred to/from device
 * @bytes_tx:      number of bytes sent to device
 * @bytes_rx:      number of bytes received from device
 *
 * @transfer_bytes_histo:
 *                 transfer bytes histogram
 *
 * @transfers_split_maxsize:
 *                 number of transfers that have been split because of
 *                 maxsize limit
 */
struct spi_statistics {
	struct u64_stats_sync	syncp;

	u64_stats_t		messages;
	u64_stats_t		transfers;
	u64_stats_t		errors;
	u64_stats_t		timedout;

	u64_stats_t		spi_sync;
	u64_stats_t		spi_sync_immediate;
	u64_stats_t		spi_async;

	u64_stats_t		bytes;
	u64_stats_t		bytes_rx;
	u64_stats_t		bytes_tx;

#define SPI_STATISTICS_HISTO_SIZE 17
	u64_stats_t	transfer_bytes_histo[SPI_STATISTICS_HISTO_SIZE];

	u64_stats_t	transfers_split_maxsize;
};

#define SPI_STATISTICS_ADD_TO_FIELD(pcpu_stats, field, count)		\
	do {								\
		struct spi_statistics *__lstats;			\
		get_cpu();						\
		__lstats = this_cpu_ptr(pcpu_stats);			\
		u64_stats_update_begin(&__lstats->syncp);		\
		u64_stats_add(&__lstats->field, count);			\
		u64_stats_update_end(&__lstats->syncp);			\
		put_cpu();						\
	} while (0)

#define SPI_STATISTICS_INCREMENT_FIELD(pcpu_stats, field)		\
	do {								\
		struct spi_statistics *__lstats;			\
		get_cpu();						\
		__lstats = this_cpu_ptr(pcpu_stats);			\
		u64_stats_update_begin(&__lstats->syncp);		\
		u64_stats_inc(&__lstats->field);			\
		u64_stats_update_end(&__lstats->syncp);			\
		put_cpu();						\
	} while (0)

/**
 * struct spi_delay - SPI delay information
 * @value: Value for the delay
 * @unit: Unit for the delay
 */
struct spi_delay {
#define SPI_DELAY_UNIT_USECS	0
#define SPI_DELAY_UNIT_NSECS	1
#define SPI_DELAY_UNIT_SCK	2
	u16	value;
	u8	unit;
};

extern int spi_delay_to_ns(struct spi_delay *_delay, struct spi_transfer *xfer);
extern int spi_delay_exec(struct spi_delay *_delay, struct spi_transfer *xfer);
extern void spi_transfer_cs_change_delay_exec(struct spi_message *msg,
						  struct spi_transfer *xfer);

/**
 * struct spi_device - Controller side proxy for an SPI target device
 * @dev: Driver model representation of the device.
 * @controller: SPI controller used with the device.
 * @max_speed_hz: Maximum clock rate to be used with this chip
 *	(on this board); may be changed by the device's driver.
 *	The spi_transfer.speed_hz can override this for each transfer.
 * @bits_per_word: Data transfers involve one or more words; word sizes
 *	like eight or 12 bits are common.  In-memory wordsizes are
 *	powers of two bytes (e.g. 20 bit samples use 32 bits).
 *	This may be changed by the device's driver, or left at the
 *	default (0) indicating protocol words are eight bit bytes.
 *	The spi_transfer.bits_per_word can override this for each transfer.
 * @rt: Make the pump thread real time priority.
 * @mode: The spi mode defines how data is clocked out and in.
 *	This may be changed by the device's driver.
 *	The "active low" default for chipselect mode can be overridden
 *	(by specifying SPI_CS_HIGH) as can the "MSB first" default for
 *	each word in a transfer (by specifying SPI_LSB_FIRST).
 * @irq: Negative, or the number passed to request_irq() to receive
 *	interrupts from this device.
 * @controller_state: Controller's runtime state
 * @controller_data: Board-specific definitions for controller, such as
 *	FIFO initialization parameters; from board_info.controller_data
 * @modalias: Name of the driver to use with this device, or an alias
 *	for that name.  This appears in the sysfs "modalias" attribute
 *	for driver coldplugging, and in uevents used for hotplugging
 * @driver_override: If the name of a driver is written to this attribute, then
 *	the device will bind to the named driver and only the named driver.
 *	Do not set directly, because core frees it; use driver_set_override() to
 *	set or clear it.
 * @pcpu_statistics: statistics for the spi_device
 * @word_delay: delay to be inserted between consecutive
 *	words of a transfer
 * @cs_setup: delay to be introduced by the controller after CS is asserted
 * @cs_hold: delay to be introduced by the controller before CS is deasserted
 * @cs_inactive: delay to be introduced by the controller after CS is
 *	deasserted. If @cs_change_delay is used from @spi_transfer, then the
 *	two delays will be added up.
 * @chip_select: Array of physical chipselect, spi->chipselect[i] gives
 *	the corresponding physical CS for logical CS i.
 * @cs_index_mask: Bit mask of the active chipselect(s) in the chipselect array
 * @cs_gpiod: Array of GPIO descriptors of the corresponding chipselect lines
 *	(optional, NULL when not using a GPIO line)
 *
 * A @spi_device is used to interchange data between an SPI target device
 * (usually a discrete chip) and CPU memory.
 *
 * In @dev, the platform_data is used to hold information about this
 * device that's meaningful to the device's protocol driver, but not
 * to its controller.  One example might be an identifier for a chip
 * variant with slightly different functionality; another might be
 * information about how this particular board wires the chip's pins.
 */
struct spi_device {
	struct device		dev;
	struct spi_controller	*controller;
	u32			max_speed_hz;
	u8			bits_per_word;
	bool			rt;
#define SPI_NO_TX		BIT(31)		/* No transmit wire */
#define SPI_NO_RX		BIT(30)		/* No receive wire */
	/*
	 * TPM specification defines flow control over SPI. Client device
	 * can insert a wait state on MISO when address is transmitted by
	 * controller on MOSI. Detecting the wait state in software is only
	 * possible for full duplex controllers. For controllers that support
	 * only half-duplex, the wait state detection needs to be implemented
	 * in hardware. TPM devices would set this flag when hardware flow
	 * control is expected from SPI controller.
	 */
#define SPI_TPM_HW_FLOW		BIT(29)		/* TPM HW flow control */
	/*
	 * All bits defined above should be covered by SPI_MODE_KERNEL_MASK.
	 * The SPI_MODE_KERNEL_MASK has the SPI_MODE_USER_MASK counterpart,
	 * which is defined in 'include/uapi/linux/spi/spi.h'.
	 * The bits defined here are from bit 31 downwards, while in
	 * SPI_MODE_USER_MASK are from 0 upwards.
	 * These bits must not overlap. A static assert check should make sure of that.
	 * If adding extra bits, make sure to decrease the bit index below as well.
	 */
#define SPI_MODE_KERNEL_MASK	(~(BIT(29) - 1))
	u32			mode;
	int			irq;
	void			*controller_state;
	void			*controller_data;
	char			modalias[SPI_NAME_SIZE];
	const char		*driver_override;

	/* The statistics */
	struct spi_statistics __percpu	*pcpu_statistics;

	struct spi_delay	word_delay; /* Inter-word delay */

	/* CS delays */
	struct spi_delay	cs_setup;
	struct spi_delay	cs_hold;
	struct spi_delay	cs_inactive;

	u8			chip_select[SPI_CS_CNT_MAX];

	/*
	 * Bit mask of the chipselect(s) that the driver need to use from
	 * the chipselect array. When the controller is capable to handle
	 * multiple chip selects & memories are connected in parallel
	 * then more than one bit need to be set in cs_index_mask.
	 */
	u32			cs_index_mask : SPI_CS_CNT_MAX;

	struct gpio_desc	*cs_gpiod[SPI_CS_CNT_MAX];	/* Chip select gpio desc */

	/*
	 * Likely need more hooks for more protocol options affecting how
	 * the controller talks to each chip, like:
	 *  - memory packing (12 bit samples into low bits, others zeroed)
	 *  - priority
	 *  - chipselect delays
	 *  - ...
	 */
};

/* Make sure that SPI_MODE_KERNEL_MASK & SPI_MODE_USER_MASK don't overlap */
static_assert((SPI_MODE_KERNEL_MASK & SPI_MODE_USER_MASK) == 0,
	      "SPI_MODE_USER_MASK & SPI_MODE_KERNEL_MASK must not overlap");

#define to_spi_device(__dev)	container_of_const(__dev, struct spi_device, dev)

/* Most drivers won't need to care about device refcounting */
static inline struct spi_device *spi_dev_get(struct spi_device *spi)
{
	return (spi && get_device(&spi->dev)) ? spi : NULL;
}

static inline void spi_dev_put(struct spi_device *spi)
{
	if (spi)
		put_device(&spi->dev);
}

/* ctldata is for the bus_controller driver's runtime state */
static inline void *spi_get_ctldata(const struct spi_device *spi)
{
	return spi->controller_state;
}

static inline void spi_set_ctldata(struct spi_device *spi, void *state)
{
	spi->controller_state = state;
}

/* Device driver data */

static inline void spi_set_drvdata(struct spi_device *spi, void *data)
{
	dev_set_drvdata(&spi->dev, data);
}

static inline void *spi_get_drvdata(const struct spi_device *spi)
{
	return dev_get_drvdata(&spi->dev);
}

static inline u8 spi_get_chipselect(const struct spi_device *spi, u8 idx)
{
	return spi->chip_select[idx];
}

static inline void spi_set_chipselect(struct spi_device *spi, u8 idx, u8 chipselect)
{
	spi->chip_select[idx] = chipselect;
}

static inline struct gpio_desc *spi_get_csgpiod(const struct spi_device *spi, u8 idx)
{
	return spi->cs_gpiod[idx];
}

static inline void spi_set_csgpiod(struct spi_device *spi, u8 idx, struct gpio_desc *csgpiod)
{
	spi->cs_gpiod[idx] = csgpiod;
}

static inline bool spi_is_csgpiod(struct spi_device *spi)
{
	u8 idx;

	for (idx = 0; idx < SPI_CS_CNT_MAX; idx++) {
		if (spi_get_csgpiod(spi, idx))
			return true;
	}
	return false;
}

/**
 * struct spi_driver - Host side "protocol" driver
 * @id_table: List of SPI devices supported by this driver
 * @probe: Binds this driver to the SPI device.  Drivers can verify
 *	that the device is actually present, and may need to configure
 *	characteristics (such as bits_per_word) which weren't needed for
 *	the initial configuration done during system setup.
 * @remove: Unbinds this driver from the SPI device
 * @shutdown: Standard shutdown callback used during system state
 *	transitions such as powerdown/halt and kexec
 * @driver: SPI device drivers should initialize the name and owner
 *	field of this structure.
 *
 * This represents the kind of device driver that uses SPI messages to
 * interact with the hardware at the other end of a SPI link.  It's called
 * a "protocol" driver because it works through messages rather than talking
 * directly to SPI hardware (which is what the underlying SPI controller
 * driver does to pass those messages).  These protocols are defined in the
 * specification for the device(s) supported by the driver.
 *
 * As a rule, those device protocols represent the lowest level interface
 * supported by a driver, and it will support upper level interfaces too.
 * Examples of such upper levels include frameworks like MTD, networking,
 * MMC, RTC, filesystem character device nodes, and hardware monitoring.
 */
struct spi_driver {
	const struct spi_device_id *id_table;
	int			(*probe)(struct spi_device *spi);
	void			(*remove)(struct spi_device *spi);
	void			(*shutdown)(struct spi_device *spi);
	struct device_driver	driver;
};

#define to_spi_driver(__drv)   \
	( __drv ? container_of_const(__drv, struct spi_driver, driver) : NULL )

extern int __spi_register_driver(struct module *owner, struct spi_driver *sdrv);

/**
 * spi_unregister_driver - reverse effect of spi_register_driver
 * @sdrv: the driver to unregister
 * Context: can sleep
 */
static inline void spi_unregister_driver(struct spi_driver *sdrv)
{
	if (sdrv)
		driver_unregister(&sdrv->driver);
}

extern struct spi_device *spi_new_ancillary_device(struct spi_device *spi, u8 chip_select);

/* Use a define to avoid include chaining to get THIS_MODULE */
#define spi_register_driver(driver) \
	__spi_register_driver(THIS_MODULE, driver)

/**
 * module_spi_driver() - Helper macro for registering a SPI driver
 * @__spi_driver: spi_driver struct
 *
 * Helper macro for SPI drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_spi_driver(__spi_driver) \
	module_driver(__spi_driver, spi_register_driver, \
			spi_unregister_driver)

/**
 * struct spi_controller - interface to SPI host or target controller
 * @dev: device interface to this driver
 * @list: link with the global spi_controller list
 * @bus_num: board-specific (and often SOC-specific) identifier for a
 *	given SPI controller.
 * @num_chipselect: chipselects are used to distinguish individual
 *	SPI targets, and are numbered from zero to num_chipselects.
 *	each target has a chipselect signal, but it's common that not
 *	every chipselect is connected to a target.
 * @dma_alignment: SPI controller constraint on DMA buffers alignment.
 * @mode_bits: flags understood by this controller driver
 * @buswidth_override_bits: flags to override for this controller driver
 * @bits_per_word_mask: A mask indicating which values of bits_per_word are
 *	supported by the driver. Bit n indicates that a bits_per_word n+1 is
 *	supported. If set, the SPI core will reject any transfer with an
 *	unsupported bits_per_word. If not set, this value is simply ignored,
 *	and it's up to the individual driver to perform any validation.
 * @min_speed_hz: Lowest supported transfer speed
 * @max_speed_hz: Highest supported transfer speed
 * @flags: other constraints relevant to this driver
 * @slave: indicates that this is an SPI slave controller
 * @target: indicates that this is an SPI target controller
 * @devm_allocated: whether the allocation of this struct is devres-managed
 * @max_transfer_size: function that returns the max transfer size for
 *	a &spi_device; may be %NULL, so the default %SIZE_MAX will be used.
 * @max_message_size: function that returns the max message size for
 *	a &spi_device; may be %NULL, so the default %SIZE_MAX will be used.
 * @io_mutex: mutex for physical bus access
 * @add_lock: mutex to avoid adding devices to the same chipselect
 * @bus_lock_spinlock: spinlock for SPI bus locking
 * @bus_lock_mutex: mutex for exclusion of multiple callers
 * @bus_lock_flag: indicates that the SPI bus is locked for exclusive use
 * @setup: updates the device mode and clocking records used by a
 *	device's SPI controller; protocol code may call this.  This
 *	must fail if an unrecognized or unsupported mode is requested.
 *	It's always safe to call this unless transfers are pending on
 *	the device whose settings are being modified.
 * @set_cs_timing: optional hook for SPI devices to request SPI
 * controller for configuring specific CS setup time, hold time and inactive
 * delay in terms of clock counts
 * @transfer: adds a message to the controller's transfer queue.
 * @cleanup: frees controller-specific state
 * @can_dma: determine whether this controller supports DMA
 * @dma_map_dev: device which can be used for DMA mapping
 * @cur_rx_dma_dev: device which is currently used for RX DMA mapping
 * @cur_tx_dma_dev: device which is currently used for TX DMA mapping
 * @queued: whether this controller is providing an internal message queue
 * @kworker: pointer to thread struct for message pump
 * @pump_messages: work struct for scheduling work to the message pump
 * @queue_lock: spinlock to synchronise access to message queue
 * @queue: message queue
 * @cur_msg: the currently in-flight message
 * @cur_msg_completion: a completion for the current in-flight message
 * @cur_msg_incomplete: Flag used internally to opportunistically skip
 *	the @cur_msg_completion. This flag is used to check if the driver has
 *	already called spi_finalize_current_message().
 * @cur_msg_need_completion: Flag used internally to opportunistically skip
 *	the @cur_msg_completion. This flag is used to signal the context that
 *	is running spi_finalize_current_message() that it needs to complete()
 * @fallback: fallback to PIO if DMA transfer return failure with
 *	SPI_TRANS_FAIL_NO_START.
 * @last_cs_mode_high: was (mode & SPI_CS_HIGH) true on the last call to set_cs.
 * @last_cs: the last chip_select that is recorded by set_cs, -1 on non chip
 *           selected
 * @last_cs_index_mask: bit mask the last chip selects that were used
 * @xfer_completion: used by core transfer_one_message()
 * @busy: message pump is busy
 * @running: message pump is running
 * @rt: whether this queue is set to run as a realtime task
 * @auto_runtime_pm: the core should ensure a runtime PM reference is held
 *                   while the hardware is prepared, using the parent
 *                   device for the spidev
 * @max_dma_len: Maximum length of a DMA transfer for the device.
 * @prepare_transfer_hardware: a message will soon arrive from the queue
 *	so the subsystem requests the driver to prepare the transfer hardware
 *	by issuing this call
 * @transfer_one_message: the subsystem calls the driver to transfer a single
 *	message while queuing transfers that arrive in the meantime. When the
 *	driver is finished with this message, it must call
 *	spi_finalize_current_message() so the subsystem can issue the next
 *	message
 * @unprepare_transfer_hardware: there are currently no more messages on the
 *	queue so the subsystem notifies the driver that it may relax the
 *	hardware by issuing this call
 *
 * @set_cs: set the logic level of the chip select line.  May be called
 *          from interrupt context.
 * @optimize_message: optimize the message for reuse
 * @unoptimize_message: release resources allocated by optimize_message
 * @prepare_message: set up the controller to transfer a single message,
 *                   for example doing DMA mapping.  Called from threaded
 *                   context.
 * @transfer_one: transfer a single spi_transfer.
 *
 *                  - return 0 if the transfer is finished,
 *                  - return 1 if the transfer is still in progress. When
 *                    the driver is finished with this transfer it must
 *                    call spi_finalize_current_transfer() so the subsystem
 *                    can issue the next transfer. If the transfer fails, the
 *                    driver must set the flag SPI_TRANS_FAIL_IO to
 *                    spi_transfer->error first, before calling
 *                    spi_finalize_current_transfer().
 *                    Note: transfer_one and transfer_one_message are mutually
 *                    exclusive; when both are set, the generic subsystem does
 *                    not call your transfer_one callback.
 * @handle_err: the subsystem calls the driver to handle an error that occurs
 *		in the generic implementation of transfer_one_message().
 * @mem_ops: optimized/dedicated operations for interactions with SPI memory.
 *	     This field is optional and should only be implemented if the
 *	     controller has native support for memory like operations.
 * @get_offload: callback for controllers with offload support to get matching
 *	offload instance. Implementations should return -ENODEV if no match is
 *	found.
 * @put_offload: release the offload instance acquired by @get_offload.
 * @mem_caps: controller capabilities for the handling of memory operations.
 * @dtr_caps: true if controller has dtr(single/dual transfer rate) capability.
 *	QSPI based controller should fill this based on controller's capability.
 * @unprepare_message: undo any work done by prepare_message().
 * @target_abort: abort the ongoing transfer request on an SPI target controller
 * @cs_gpiods: Array of GPIO descriptors to use as chip select lines; one per CS
 *	number. Any individual value may be NULL for CS lines that
 *	are not GPIOs (driven by the SPI controller itself).
 * @use_gpio_descriptors: Turns on the code in the SPI core to parse and grab
 *	GPIO descriptors. This will fill in @cs_gpiods and SPI devices will have
 *	the cs_gpiod assigned if a GPIO line is found for the chipselect.
 * @unused_native_cs: When cs_gpiods is used, spi_register_controller() will
 *	fill in this field with the first unused native CS, to be used by SPI
 *	controller drivers that need to drive a native CS when using GPIO CS.
 * @max_native_cs: When cs_gpiods is used, and this field is filled in,
 *	spi_register_controller() will validate all native CS (including the
 *	unused native CS) against this value.
 * @pcpu_statistics: statistics for the spi_controller
 * @dma_tx: DMA transmit channel
 * @dma_rx: DMA receive channel
 * @dummy_rx: dummy receive buffer for full-duplex devices
 * @dummy_tx: dummy transmit buffer for full-duplex devices
 * @fw_translate_cs: If the boot firmware uses different numbering scheme
 *	what Linux expects, this optional hook can be used to translate
 *	between the two.
 * @ptp_sts_supported: If the driver sets this to true, it must provide a
 *	time snapshot in @spi_transfer->ptp_sts as close as possible to the
 *	moment in time when @spi_transfer->ptp_sts_word_pre and
 *	@spi_transfer->ptp_sts_word_post were transmitted.
 *	If the driver does not set this, the SPI core takes the snapshot as
 *	close to the driver hand-over as possible.
 * @irq_flags: Interrupt enable state during PTP system timestamping
 * @queue_empty: signal green light for opportunistically skipping the queue
 *	for spi_sync transfers.
 * @must_async: disable all fast paths in the core
 * @defer_optimize_message: set to true if controller cannot pre-optimize messages
 *	and needs to defer the optimization step until the message is actually
 *	being transferred
 *
 * Each SPI controller can communicate with one or more @spi_device
 * children.  These make a small bus, sharing MOSI, MISO and SCK signals
 * but not chip select signals.  Each device may be configured to use a
 * different clock rate, since those shared signals are ignored unless
 * the chip is selected.
 *
 * The driver for an SPI controller manages access to those devices through
 * a queue of spi_message transactions, copying data between CPU memory and
 * an SPI target device.  For each such message it queues, it calls the
 * message's completion function when the transaction completes.
 */
struct spi_controller {
	struct device	dev;

	struct list_head list;

	/*
	 * Other than negative (== assign one dynamically), bus_num is fully
	 * board-specific. Usually that simplifies to being SoC-specific.
	 * example: one SoC has three SPI controllers, numbered 0..2,
	 * and one board's schematics might show it using SPI-2. Software
	 * would normally use bus_num=2 for that controller.
	 */
	s16			bus_num;

	/*
	 * Chipselects will be integral to many controllers; some others
	 * might use board-specific GPIOs.
	 */
	u16			num_chipselect;

	/* Some SPI controllers pose alignment requirements on DMAable
	 * buffers; let protocol drivers know about these requirements.
	 */
	u16			dma_alignment;

	/* spi_device.mode flags understood by this controller driver */
	u32			mode_bits;

	/* spi_device.mode flags override flags for this controller */
	u32			buswidth_override_bits;

	/* Bitmask of supported bits_per_word for transfers */
	u32			bits_per_word_mask;
#define SPI_BPW_MASK(bits) BIT((bits) - 1)
#define SPI_BPW_RANGE_MASK(min, max) GENMASK((max) - 1, (min) - 1)

	/* Limits on transfer speed */
	u32			min_speed_hz;
	u32			max_speed_hz;

	/* Other constraints relevant to this driver */
	u16			flags;
#define SPI_CONTROLLER_HALF_DUPLEX	BIT(0)	/* Can't do full duplex */
#define SPI_CONTROLLER_NO_RX		BIT(1)	/* Can't do buffer read */
#define SPI_CONTROLLER_NO_TX		BIT(2)	/* Can't do buffer write */
#define SPI_CONTROLLER_MUST_RX		BIT(3)	/* Requires rx */
#define SPI_CONTROLLER_MUST_TX		BIT(4)	/* Requires tx */
#define SPI_CONTROLLER_GPIO_SS		BIT(5)	/* GPIO CS must select target device */
#define SPI_CONTROLLER_SUSPENDED	BIT(6)	/* Currently suspended */
	/*
	 * The spi-controller has multi chip select capability and can
	 * assert/de-assert more than one chip select at once.
	 */
#define SPI_CONTROLLER_MULTI_CS		BIT(7)

	/* Flag indicating if the allocation of this struct is devres-managed */
	bool			devm_allocated;

	union {
		/* Flag indicating this is an SPI slave controller */
		bool			slave;
		/* Flag indicating this is an SPI target controller */
		bool			target;
	};

	/*
	 * On some hardware transfer / message size may be constrained
	 * the limit may depend on device transfer settings.
	 */
	size_t (*max_transfer_size)(struct spi_device *spi);
	size_t (*max_message_size)(struct spi_device *spi);

	/* I/O mutex */
	struct mutex		io_mutex;

	/* Used to avoid adding the same CS twice */
	struct mutex		add_lock;

	/* Lock and mutex for SPI bus locking */
	spinlock_t		bus_lock_spinlock;
	struct mutex		bus_lock_mutex;

	/* Flag indicating that the SPI bus is locked for exclusive use */
	bool			bus_lock_flag;

	/*
	 * Setup mode and clock, etc (SPI driver may call many times).
	 *
	 * IMPORTANT:  this may be called when transfers to another
	 * device are active.  DO NOT UPDATE SHARED REGISTERS in ways
	 * which could break those transfers.
	 */
	int			(*setup)(struct spi_device *spi);

	/*
	 * set_cs_timing() method is for SPI controllers that supports
	 * configuring CS timing.
	 *
	 * This hook allows SPI client drivers to request SPI controllers
	 * to configure specific CS timing through spi_set_cs_timing() after
	 * spi_setup().
	 */
	int (*set_cs_timing)(struct spi_device *spi);

	/*
	 * Bidirectional bulk transfers
	 *
	 * + The transfer() method may not sleep; its main role is
	 *   just to add the message to the queue.
	 * + For now there's no remove-from-queue operation, or
	 *   any other request management
	 * + To a given spi_device, message queueing is pure FIFO
	 *
	 * + The controller's main job is to process its message queue,
	 *   selecting a chip (for controllers), then transferring data
	 * + If there are multiple spi_device children, the i/o queue
	 *   arbitration algorithm is unspecified (round robin, FIFO,
	 *   priority, reservations, preemption, etc)
	 *
	 * + Chipselect stays active during the entire message
	 *   (unless modified by spi_transfer.cs_change != 0).
	 * + The message transfers use clock and SPI mode parameters
	 *   previously established by setup() for this device
	 */
	int			(*transfer)(struct spi_device *spi,
						struct spi_message *mesg);

	/* Called on release() to free memory provided by spi_controller */
	void			(*cleanup)(struct spi_device *spi);

	/*
	 * Used to enable core support for DMA handling, if can_dma()
	 * exists and returns true then the transfer will be mapped
	 * prior to transfer_one() being called.  The driver should
	 * not modify or store xfer and dma_tx and dma_rx must be set
	 * while the device is prepared.
	 */
	bool			(*can_dma)(struct spi_controller *ctlr,
					   struct spi_device *spi,
					   struct spi_transfer *xfer);
	struct device *dma_map_dev;
	struct device *cur_rx_dma_dev;
	struct device *cur_tx_dma_dev;

	/*
	 * These hooks are for drivers that want to use the generic
	 * controller transfer queueing mechanism. If these are used, the
	 * transfer() function above must NOT be specified by the driver.
	 * Over time we expect SPI drivers to be phased over to this API.
	 */
	bool				queued;
	struct kthread_worker		*kworker;
	struct kthread_work		pump_messages;
	spinlock_t			queue_lock;
	struct list_head		queue;
	struct spi_message		*cur_msg;
	struct completion               cur_msg_completion;
	bool				cur_msg_incomplete;
	bool				cur_msg_need_completion;
	bool				busy;
	bool				running;
	bool				rt;
	bool				auto_runtime_pm;
	bool                            fallback;
	bool				last_cs_mode_high;
	s8				last_cs[SPI_CS_CNT_MAX];
	u32				last_cs_index_mask : SPI_CS_CNT_MAX;
	struct completion               xfer_completion;
	size_t				max_dma_len;

	int (*optimize_message)(struct spi_message *msg);
	int (*unoptimize_message)(struct spi_message *msg);
	int (*prepare_transfer_hardware)(struct spi_controller *ctlr);
	int (*transfer_one_message)(struct spi_controller *ctlr,
				    struct spi_message *mesg);
	int (*unprepare_transfer_hardware)(struct spi_controller *ctlr);
	int (*prepare_message)(struct spi_controller *ctlr,
			       struct spi_message *message);
	int (*unprepare_message)(struct spi_controller *ctlr,
				 struct spi_message *message);
	int (*target_abort)(struct spi_controller *ctlr);

	/*
	 * These hooks are for drivers that use a generic implementation
	 * of transfer_one_message() provided by the core.
	 */
	void (*set_cs)(struct spi_device *spi, bool enable);
	int (*transfer_one)(struct spi_controller *ctlr, struct spi_device *spi,
			    struct spi_transfer *transfer);
	void (*handle_err)(struct spi_controller *ctlr,
			   struct spi_message *message);

	/* Optimized handlers for SPI memory-like operations. */
	const struct spi_controller_mem_ops *mem_ops;
	const struct spi_controller_mem_caps *mem_caps;

	/* SPI or QSPI controller can set to true if supports SDR/DDR transfer rate */
	bool			dtr_caps;

	struct spi_offload *(*get_offload)(struct spi_device *spi,
					   const struct spi_offload_config *config);
	void (*put_offload)(struct spi_offload *offload);

	/* GPIO chip select */
	struct gpio_desc	**cs_gpiods;
	bool			use_gpio_descriptors;
	s8			unused_native_cs;
	s8			max_native_cs;

	/* Statistics */
	struct spi_statistics __percpu	*pcpu_statistics;

	/* DMA channels for use with core dmaengine helpers */
	struct dma_chan		*dma_tx;
	struct dma_chan		*dma_rx;

	/* Dummy data for full duplex devices */
	void			*dummy_rx;
	void			*dummy_tx;

	int (*fw_translate_cs)(struct spi_controller *ctlr, unsigned cs);

	/*
	 * Driver sets this field to indicate it is able to snapshot SPI
	 * transfers (needed e.g. for reading the time of POSIX clocks)
	 */
	bool			ptp_sts_supported;

	/* Interrupt enable state during PTP system timestamping */
	unsigned long		irq_flags;

	/* Flag for enabling opportunistic skipping of the queue in spi_sync */
	bool			queue_empty;
	bool			must_async;
	bool			defer_optimize_message;
};

static inline void *spi_controller_get_devdata(struct spi_controller *ctlr)
{
	return dev_get_drvdata(&ctlr->dev);
}

static inline void spi_controller_set_devdata(struct spi_controller *ctlr,
					      void *data)
{
	dev_set_drvdata(&ctlr->dev, data);
}

static inline struct spi_controller *spi_controller_get(struct spi_controller *ctlr)
{
	if (!ctlr || !get_device(&ctlr->dev))
		return NULL;
	return ctlr;
}

static inline void spi_controller_put(struct spi_controller *ctlr)
{
	if (ctlr)
		put_device(&ctlr->dev);
}

static inline bool spi_controller_is_target(struct spi_controller *ctlr)
{
	return IS_ENABLED(CONFIG_SPI_SLAVE) && ctlr->target;
}

/* PM calls that need to be issued by the driver */
extern int spi_controller_suspend(struct spi_controller *ctlr);
extern int spi_controller_resume(struct spi_controller *ctlr);

/* Calls the driver make to interact with the message queue */
extern struct spi_message *spi_get_next_queued_message(struct spi_controller *ctlr);
extern void spi_finalize_current_message(struct spi_controller *ctlr);
extern void spi_finalize_current_transfer(struct spi_controller *ctlr);

/* Helper calls for driver to timestamp transfer */
void spi_take_timestamp_pre(struct spi_controller *ctlr,
			    struct spi_transfer *xfer,
			    size_t progress, bool irqs_off);
void spi_take_timestamp_post(struct spi_controller *ctlr,
			     struct spi_transfer *xfer,
			     size_t progress, bool irqs_off);

/* The SPI driver core manages memory for the spi_controller classdev */
extern struct spi_controller *__spi_alloc_controller(struct device *host,
						unsigned int size, bool target);

static inline struct spi_controller *spi_alloc_host(struct device *dev,
						    unsigned int size)
{
	return __spi_alloc_controller(dev, size, false);
}

static inline struct spi_controller *spi_alloc_target(struct device *dev,
						      unsigned int size)
{
	if (!IS_ENABLED(CONFIG_SPI_SLAVE))
		return NULL;

	return __spi_alloc_controller(dev, size, true);
}

struct spi_controller *__devm_spi_alloc_controller(struct device *dev,
						   unsigned int size,
						   bool target);

static inline struct spi_controller *devm_spi_alloc_host(struct device *dev,
							 unsigned int size)
{
	return __devm_spi_alloc_controller(dev, size, false);
}

static inline struct spi_controller *devm_spi_alloc_target(struct device *dev,
							   unsigned int size)
{
	if (!IS_ENABLED(CONFIG_SPI_SLAVE))
		return NULL;

	return __devm_spi_alloc_controller(dev, size, true);
}

extern int spi_register_controller(struct spi_controller *ctlr);
extern int devm_spi_register_controller(struct device *dev,
					struct spi_controller *ctlr);
extern void spi_unregister_controller(struct spi_controller *ctlr);

#if IS_ENABLED(CONFIG_ACPI) && IS_ENABLED(CONFIG_SPI_MASTER)
extern struct spi_controller *acpi_spi_find_controller_by_adev(struct acpi_device *adev);
extern struct spi_device *acpi_spi_device_alloc(struct spi_controller *ctlr,
						struct acpi_device *adev,
						int index);
int acpi_spi_count_resources(struct acpi_device *adev);
#else
static inline struct spi_controller *acpi_spi_find_controller_by_adev(struct acpi_device *adev)
{
	return NULL;
}

static inline struct spi_device *acpi_spi_device_alloc(struct spi_controller *ctlr,
						       struct acpi_device *adev,
						       int index)
{
	return ERR_PTR(-ENODEV);
}

static inline int acpi_spi_count_resources(struct acpi_device *adev)
{
	return 0;
}
#endif

/*
 * SPI resource management while processing a SPI message
 */

typedef void (*spi_res_release_t)(struct spi_controller *ctlr,
				  struct spi_message *msg,
				  void *res);

/**
 * struct spi_res - SPI resource management structure
 * @entry:   list entry
 * @release: release code called prior to freeing this resource
 * @data:    extra data allocated for the specific use-case
 *
 * This is based on ideas from devres, but focused on life-cycle
 * management during spi_message processing.
 */
struct spi_res {
	struct list_head        entry;
	spi_res_release_t       release;
	unsigned long long      data[]; /* Guarantee ull alignment */
};

/*---------------------------------------------------------------------------*/

/*
 * I/O INTERFACE between SPI controller and protocol drivers
 *
 * Protocol drivers use a queue of spi_messages, each transferring data
 * between the controller and memory buffers.
 *
 * The spi_messages themselves consist of a series of read+write transfer
 * segments.  Those segments always read the same number of bits as they
 * write; but one or the other is easily ignored by passing a NULL buffer
 * pointer.  (This is unlike most types of I/O API, because SPI hardware
 * is full duplex.)
 *
 * NOTE:  Allocation of spi_transfer and spi_message memory is entirely
 * up to the protocol driver, which guarantees the integrity of both (as
 * well as the data buffers) for as long as the message is queued.
 */

/**
 * struct spi_transfer - a read/write buffer pair
 * @tx_buf: data to be written (DMA-safe memory), or NULL
 * @rx_buf: data to be read (DMA-safe memory), or NULL
 * @tx_dma: DMA address of tx_buf, currently not for client use
 * @rx_dma: DMA address of rx_buf, currently not for client use
 * @tx_nbits: number of bits used for writing. If 0 the default
 *      (SPI_NBITS_SINGLE) is used.
 * @rx_nbits: number of bits used for reading. If 0 the default
 *      (SPI_NBITS_SINGLE) is used.
 * @len: size of rx and tx buffers (in bytes)
 * @speed_hz: Select a speed other than the device default for this
 *      transfer. If 0 the default (from @spi_device) is used.
 * @bits_per_word: select a bits_per_word other than the device default
 *      for this transfer. If 0 the default (from @spi_device) is used.
 * @dummy_data: indicates transfer is dummy bytes transfer.
 * @cs_off: performs the transfer with chipselect off.
 * @cs_change: affects chipselect after this transfer completes
 * @cs_change_delay: delay between cs deassert and assert when
 *      @cs_change is set and @spi_transfer is not the last in @spi_message
 * @delay: delay to be introduced after this transfer before
 *	(optionally) changing the chipselect status, then starting
 *	the next transfer or completing this @spi_message.
 * @word_delay: inter word delay to be introduced after each word size
 *	(set by bits_per_word) transmission.
 * @effective_speed_hz: the effective SCK-speed that was used to
 *      transfer this transfer. Set to 0 if the SPI bus driver does
 *      not support it.
 * @transfer_list: transfers are sequenced through @spi_message.transfers
 * @tx_sg_mapped: If true, the @tx_sg is mapped for DMA
 * @rx_sg_mapped: If true, the @rx_sg is mapped for DMA
 * @tx_sg: Scatterlist for transmit, currently not for client use
 * @rx_sg: Scatterlist for receive, currently not for client use
 * @offload_flags: Flags that are only applicable to specialized SPI offload
 *	transfers. See %SPI_OFFLOAD_XFER_* in spi-offload.h.
 * @ptp_sts_word_pre: The word (subject to bits_per_word semantics) offset
 *	within @tx_buf for which the SPI device is requesting that the time
 *	snapshot for this transfer begins. Upon completing the SPI transfer,
 *	this value may have changed compared to what was requested, depending
 *	on the available snapshotting resolution (DMA transfer,
 *	@ptp_sts_supported is false, etc).
 * @ptp_sts_word_post: See @ptp_sts_word_post. The two can be equal (meaning
 *	that a single byte should be snapshotted).
 *	If the core takes care of the timestamp (if @ptp_sts_supported is false
 *	for this controller), it will set @ptp_sts_word_pre to 0, and
 *	@ptp_sts_word_post to the length of the transfer. This is done
 *	purposefully (instead of setting to spi_transfer->len - 1) to denote
 *	that a transfer-level snapshot taken from within the driver may still
 *	be of higher quality.
 * @ptp_sts: Pointer to a memory location held by the SPI target device where a
 *	PTP system timestamp structure may lie. If drivers use PIO or their
 *	hardware has some sort of assist for retrieving exact transfer timing,
 *	they can (and should) assert @ptp_sts_supported and populate this
 *	structure using the ptp_read_system_*ts helper functions.
 *	The timestamp must represent the time at which the SPI target device has
 *	processed the word, i.e. the "pre" timestamp should be taken before
 *	transmitting the "pre" word, and the "post" timestamp after receiving
 *	transmit confirmation from the controller for the "post" word.
 * @dtr_mode: true if supports double transfer rate.
 * @timestamped: true if the transfer has been timestamped
 * @error: Error status logged by SPI controller driver.
 *
 * SPI transfers always write the same number of bytes as they read.
 * Protocol drivers should always provide @rx_buf and/or @tx_buf.
 * In some cases, they may also want to provide DMA addresses for
 * the data being transferred; that may reduce overhead, when the
 * underlying driver uses DMA.
 *
 * If the transmit buffer is NULL, zeroes will be shifted out
 * while filling @rx_buf.  If the receive buffer is NULL, the data
 * shifted in will be discarded.  Only "len" bytes shift out (or in).
 * It's an error to try to shift out a partial word.  (For example, by
 * shifting out three bytes with word size of sixteen or twenty bits;
 * the former uses two bytes per word, the latter uses four bytes.)
 *
 * In-memory data values are always in native CPU byte order, translated
 * from the wire byte order (big-endian except with SPI_LSB_FIRST).  So
 * for example when bits_per_word is sixteen, buffers are 2N bytes long
 * (@len = 2N) and hold N sixteen bit words in CPU byte order.
 *
 * When the word size of the SPI transfer is not a power-of-two multiple
 * of eight bits, those in-memory words include extra bits.  In-memory
 * words are always seen by protocol drivers as right-justified, so the
 * undefined (rx) or unused (tx) bits are always the most significant bits.
 *
 * All SPI transfers start with the relevant chipselect active.  Normally
 * it stays selected until after the last transfer in a message.  Drivers
 * can affect the chipselect signal using cs_change.
 *
 * (i) If the transfer isn't the last one in the message, this flag is
 * used to make the chipselect briefly go inactive in the middle of the
 * message.  Toggling chipselect in this way may be needed to terminate
 * a chip command, letting a single spi_message perform all of group of
 * chip transactions together.
 *
 * (ii) When the transfer is the last one in the message, the chip may
 * stay selected until the next transfer.  On multi-device SPI busses
 * with nothing blocking messages going to other devices, this is just
 * a performance hint; starting a message to another device deselects
 * this one.  But in other cases, this can be used to ensure correctness.
 * Some devices need protocol transactions to be built from a series of
 * spi_message submissions, where the content of one message is determined
 * by the results of previous messages and where the whole transaction
 * ends when the chipselect goes inactive.
 *
 * When SPI can transfer in 1x,2x or 4x. It can get this transfer information
 * from device through @tx_nbits and @rx_nbits. In Bi-direction, these
 * two should both be set. User can set transfer mode with SPI_NBITS_SINGLE(1x)
 * SPI_NBITS_DUAL(2x) and SPI_NBITS_QUAD(4x) to support these three transfer.
 *
 * User may also set dtr_mode to true to use dual transfer mode if desired. if
 * not, default considered as single transfer mode.
 *
 * The code that submits an spi_message (and its spi_transfers)
 * to the lower layers is responsible for managing its memory.
 * Zero-initialize every field you don't set up explicitly, to
 * insulate against future API updates.  After you submit a message
 * and its transfers, ignore them until its completion callback.
 */
struct spi_transfer {
	/*
	 * It's okay if tx_buf == rx_buf (right?).
	 * For MicroWire, one buffer must be NULL.
	 * Buffers must work with dma_*map_single() calls.
	 */
	const void	*tx_buf;
	void		*rx_buf;
	unsigned	len;

#define SPI_TRANS_FAIL_NO_START	BIT(0)
#define SPI_TRANS_FAIL_IO	BIT(1)
	u16		error;

	bool		tx_sg_mapped;
	bool		rx_sg_mapped;

	struct sg_table tx_sg;
	struct sg_table rx_sg;
	dma_addr_t	tx_dma;
	dma_addr_t	rx_dma;

	unsigned	dummy_data:1;
	unsigned	cs_off:1;
	unsigned	cs_change:1;
	unsigned	tx_nbits:4;
	unsigned	rx_nbits:4;
	unsigned	timestamped:1;
	bool		dtr_mode;
#define	SPI_NBITS_SINGLE	0x01 /* 1-bit transfer */
#define	SPI_NBITS_DUAL		0x02 /* 2-bit transfer */
#define	SPI_NBITS_QUAD		0x04 /* 4-bit transfer */
#define	SPI_NBITS_OCTAL	0x08 /* 8-bit transfer */
	u8		bits_per_word;
	struct spi_delay	delay;
	struct spi_delay	cs_change_delay;
	struct spi_delay	word_delay;
	u32		speed_hz;

	u32		effective_speed_hz;

	/* Use %SPI_OFFLOAD_XFER_* from spi-offload.h */
	unsigned int	offload_flags;

	unsigned int	ptp_sts_word_pre;
	unsigned int	ptp_sts_word_post;

	struct ptp_system_timestamp *ptp_sts;

	struct list_head transfer_list;
};

/**
 * struct spi_message - one multi-segment SPI transaction
 * @transfers: list of transfer segments in this transaction
 * @spi: SPI device to which the transaction is queued
 * @pre_optimized: peripheral driver pre-optimized the message
 * @optimized: the message is in the optimized state
 * @prepared: spi_prepare_message was called for the this message
 * @status: zero for success, else negative errno
 * @complete: called to report transaction completions
 * @context: the argument to complete() when it's called
 * @frame_length: the total number of bytes in the message
 * @actual_length: the total number of bytes that were transferred in all
 *	successful segments
 * @queue: for use by whichever driver currently owns the message
 * @state: for use by whichever driver currently owns the message
 * @opt_state: for use by whichever driver currently owns the message
 * @resources: for resource management when the SPI message is processed
 * @offload: (optional) offload instance used by this message
 *
 * A @spi_message is used to execute an atomic sequence of data transfers,
 * each represented by a struct spi_transfer.  The sequence is "atomic"
 * in the sense that no other spi_message may use that SPI bus until that
 * sequence completes.  On some systems, many such sequences can execute as
 * a single programmed DMA transfer.  On all systems, these messages are
 * queued, and might complete after transactions to other devices.  Messages
 * sent to a given spi_device are always executed in FIFO order.
 *
 * The code that submits an spi_message (and its spi_transfers)
 * to the lower layers is responsible for managing its memory.
 * Zero-initialize every field you don't set up explicitly, to
 * insulate against future API updates.  After you submit a message
 * and its transfers, ignore them until its completion callback.
 */
struct spi_message {
	struct list_head	transfers;

	struct spi_device	*spi;

	/* spi_optimize_message() was called for this message */
	bool			pre_optimized;
	/* __spi_optimize_message() was called for this message */
	bool			optimized;

	/* spi_prepare_message() was called for this message */
	bool			prepared;

	/*
	 * REVISIT: we might want a flag affecting the behavior of the
	 * last transfer ... allowing things like "read 16 bit length L"
	 * immediately followed by "read L bytes".  Basically imposing
	 * a specific message scheduling algorithm.
	 *
	 * Some controller drivers (message-at-a-time queue processing)
	 * could provide that as their default scheduling algorithm.  But
	 * others (with multi-message pipelines) could need a flag to
	 * tell them about such special cases.
	 */

	/* Completion is reported through a callback */
	int			status;
	void			(*complete)(void *context);
	void			*context;
	unsigned		frame_length;
	unsigned		actual_length;

	/*
	 * For optional use by whatever driver currently owns the
	 * spi_message ...  between calls to spi_async and then later
	 * complete(), that's the spi_controller controller driver.
	 */
	struct list_head	queue;
	void			*state;
	/*
	 * Optional state for use by controller driver between calls to
	 * __spi_optimize_message() and __spi_unoptimize_message().
	 */
	void			*opt_state;

	/*
	 * Optional offload instance used by this message. This must be set
	 * by the peripheral driver before calling spi_optimize_message().
	 */
	struct spi_offload	*offload;

	/* List of spi_res resources when the SPI message is processed */
	struct list_head        resources;
};

static inline void spi_message_init_no_memset(struct spi_message *m)
{
	INIT_LIST_HEAD(&m->transfers);
	INIT_LIST_HEAD(&m->resources);
}

static inline void spi_message_init(struct spi_message *m)
{
	memset(m, 0, sizeof *m);
	spi_message_init_no_memset(m);
}

static inline void
spi_message_add_tail(struct spi_transfer *t, struct spi_message *m)
{
	list_add_tail(&t->transfer_list, &m->transfers);
}

static inline void
spi_transfer_del(struct spi_transfer *t)
{
	list_del(&t->transfer_list);
}

static inline int
spi_transfer_delay_exec(struct spi_transfer *t)
{
	return spi_delay_exec(&t->delay, t);
}

/**
 * spi_message_init_with_transfers - Initialize spi_message and append transfers
 * @m: spi_message to be initialized
 * @xfers: An array of SPI transfers
 * @num_xfers: Number of items in the xfer array
 *
 * This function initializes the given spi_message and adds each spi_transfer in
 * the given array to the message.
 */
static inline void
spi_message_init_with_transfers(struct spi_message *m,
struct spi_transfer *xfers, unsigned int num_xfers)
{
	unsigned int i;

	spi_message_init(m);
	for (i = 0; i < num_xfers; ++i)
		spi_message_add_tail(&xfers[i], m);
}

/*
 * It's fine to embed message and transaction structures in other data
 * structures so long as you don't free them while they're in use.
 */
static inline struct spi_message *spi_message_alloc(unsigned ntrans, gfp_t flags)
{
	struct spi_message_with_transfers {
		struct spi_message m;
		struct spi_transfer t[];
	} *mwt;
	unsigned i;

	mwt = kzalloc(struct_size(mwt, t, ntrans), flags);
	if (!mwt)
		return NULL;

	spi_message_init_no_memset(&mwt->m);
	for (i = 0; i < ntrans; i++)
		spi_message_add_tail(&mwt->t[i], &mwt->m);

	return &mwt->m;
}

static inline void spi_message_free(struct spi_message *m)
{
	kfree(m);
}

extern int spi_optimize_message(struct spi_device *spi, struct spi_message *msg);
extern void spi_unoptimize_message(struct spi_message *msg);
extern int devm_spi_optimize_message(struct device *dev, struct spi_device *spi,
				     struct spi_message *msg);

extern int spi_setup(struct spi_device *spi);
extern int spi_async(struct spi_device *spi, struct spi_message *message);
extern int spi_target_abort(struct spi_device *spi);

static inline size_t
spi_max_message_size(struct spi_device *spi)
{
	struct spi_controller *ctlr = spi->controller;

	if (!ctlr->max_message_size)
		return SIZE_MAX;
	return ctlr->max_message_size(spi);
}

static inline size_t
spi_max_transfer_size(struct spi_device *spi)
{
	struct spi_controller *ctlr = spi->controller;
	size_t tr_max = SIZE_MAX;
	size_t msg_max = spi_max_message_size(spi);

	if (ctlr->max_transfer_size)
		tr_max = ctlr->max_transfer_size(spi);

	/* Transfer size limit must not be greater than message size limit */
	return min(tr_max, msg_max);
}

/**
 * spi_is_bpw_supported - Check if bits per word is supported
 * @spi: SPI device
 * @bpw: Bits per word
 *
 * This function checks to see if the SPI controller supports @bpw.
 *
 * Returns:
 * True if @bpw is supported, false otherwise.
 */
static inline bool spi_is_bpw_supported(struct spi_device *spi, u32 bpw)
{
	u32 bpw_mask = spi->controller->bits_per_word_mask;

	if (bpw == 8 || (bpw <= 32 && bpw_mask & SPI_BPW_MASK(bpw)))
		return true;

	return false;
}

/**
 * spi_bpw_to_bytes - Covert bits per word to bytes
 * @bpw: Bits per word
 *
 * This function converts the given @bpw to bytes. The result is always
 * power-of-two, e.g.,
 *
 *  ===============    =================
 *  Input (in bits)    Output (in bytes)
 *  ===============    =================
 *          5                   1
 *          9                   2
 *          21                  4
 *          37                  8
 *  ===============    =================
 *
 * It will return 0 for the 0 input.
 *
 * Returns:
 * Bytes for the given @bpw.
 */
static inline u32 spi_bpw_to_bytes(u32 bpw)
{
	return roundup_pow_of_two(BITS_TO_BYTES(bpw));
}

/**
 * spi_controller_xfer_timeout - Compute a suitable timeout value
 * @ctlr: SPI device
 * @xfer: Transfer descriptor
 *
 * Compute a relevant timeout value for the given transfer. We derive the time
 * that it would take on a single data line and take twice this amount of time
 * with a minimum of 500ms to avoid false positives on loaded systems.
 *
 * Returns: Transfer timeout value in milliseconds.
 */
static inline unsigned int spi_controller_xfer_timeout(struct spi_controller *ctlr,
						       struct spi_transfer *xfer)
{
	return max(xfer->len * 8 * 2 / (xfer->speed_hz / 1000), 500U);
}

/*---------------------------------------------------------------------------*/

/* SPI transfer replacement methods which make use of spi_res */

struct spi_replaced_transfers;
typedef void (*spi_replaced_release_t)(struct spi_controller *ctlr,
				       struct spi_message *msg,
				       struct spi_replaced_transfers *res);
/**
 * struct spi_replaced_transfers - structure describing the spi_transfer
 *                                 replacements that have occurred
 *                                 so that they can get reverted
 * @release:            some extra release code to get executed prior to
 *                      releasing this structure
 * @extradata:          pointer to some extra data if requested or NULL
 * @replaced_transfers: transfers that have been replaced and which need
 *                      to get restored
 * @replaced_after:     the transfer after which the @replaced_transfers
 *                      are to get re-inserted
 * @inserted:           number of transfers inserted
 * @inserted_transfers: array of spi_transfers of array-size @inserted,
 *                      that have been replacing replaced_transfers
 *
 * Note: that @extradata will point to @inserted_transfers[@inserted]
 * if some extra allocation is requested, so alignment will be the same
 * as for spi_transfers.
 */
struct spi_replaced_transfers {
	spi_replaced_release_t release;
	void *extradata;
	struct list_head replaced_transfers;
	struct list_head *replaced_after;
	size_t inserted;
	struct spi_transfer inserted_transfers[];
};

/*---------------------------------------------------------------------------*/

/* SPI transfer transformation methods */

extern int spi_split_transfers_maxsize(struct spi_controller *ctlr,
				       struct spi_message *msg,
				       size_t maxsize);
extern int spi_split_transfers_maxwords(struct spi_controller *ctlr,
					struct spi_message *msg,
					size_t maxwords);

/*---------------------------------------------------------------------------*/

/*
 * All these synchronous SPI transfer routines are utilities layered
 * over the core async transfer primitive.  Here, "synchronous" means
 * they will sleep uninterruptibly until the async transfer completes.
 */

extern int spi_sync(struct spi_device *spi, struct spi_message *message);
extern int spi_sync_locked(struct spi_device *spi, struct spi_message *message);
extern int spi_bus_lock(struct spi_controller *ctlr);
extern int spi_bus_unlock(struct spi_controller *ctlr);

/**
 * spi_sync_transfer - synchronous SPI data transfer
 * @spi: device with which data will be exchanged
 * @xfers: An array of spi_transfers
 * @num_xfers: Number of items in the xfer array
 * Context: can sleep
 *
 * Does a synchronous SPI data transfer of the given spi_transfer array.
 *
 * For more specific semantics see spi_sync().
 *
 * Return: zero on success, else a negative error code.
 */
static inline int
spi_sync_transfer(struct spi_device *spi, struct spi_transfer *xfers,
	unsigned int num_xfers)
{
	struct spi_message msg;

	spi_message_init_with_transfers(&msg, xfers, num_xfers);

	return spi_sync(spi, &msg);
}

/**
 * spi_write - SPI synchronous write
 * @spi: device to which data will be written
 * @buf: data buffer
 * @len: data buffer size
 * Context: can sleep
 *
 * This function writes the buffer @buf.
 * Callable only from contexts that can sleep.
 *
 * Return: zero on success, else a negative error code.
 */
static inline int
spi_write(struct spi_device *spi, const void *buf, size_t len)
{
	struct spi_transfer	t = {
			.tx_buf		= buf,
			.len		= len,
		};

	return spi_sync_transfer(spi, &t, 1);
}

/**
 * spi_read - SPI synchronous read
 * @spi: device from which data will be read
 * @buf: data buffer
 * @len: data buffer size
 * Context: can sleep
 *
 * This function reads the buffer @buf.
 * Callable only from contexts that can sleep.
 *
 * Return: zero on success, else a negative error code.
 */
static inline int
spi_read(struct spi_device *spi, void *buf, size_t len)
{
	struct spi_transfer	t = {
			.rx_buf		= buf,
			.len		= len,
		};

	return spi_sync_transfer(spi, &t, 1);
}

/* This copies txbuf and rxbuf data; for small transfers only! */
extern int spi_write_then_read(struct spi_device *spi,
		const void *txbuf, unsigned n_tx,
		void *rxbuf, unsigned n_rx);

/**
 * spi_w8r8 - SPI synchronous 8 bit write followed by 8 bit read
 * @spi: device with which data will be exchanged
 * @cmd: command to be written before data is read back
 * Context: can sleep
 *
 * Callable only from contexts that can sleep.
 *
 * Return: the (unsigned) eight bit number returned by the
 * device, or else a negative error code.
 */
static inline ssize_t spi_w8r8(struct spi_device *spi, u8 cmd)
{
	ssize_t			status;
	u8			result;

	status = spi_write_then_read(spi, &cmd, 1, &result, 1);

	/* Return negative errno or unsigned value */
	return (status < 0) ? status : result;
}

/**
 * spi_w8r16 - SPI synchronous 8 bit write followed by 16 bit read
 * @spi: device with which data will be exchanged
 * @cmd: command to be written before data is read back
 * Context: can sleep
 *
 * The number is returned in wire-order, which is at least sometimes
 * big-endian.
 *
 * Callable only from contexts that can sleep.
 *
 * Return: the (unsigned) sixteen bit number returned by the
 * device, or else a negative error code.
 */
static inline ssize_t spi_w8r16(struct spi_device *spi, u8 cmd)
{
	ssize_t			status;
	u16			result;

	status = spi_write_then_read(spi, &cmd, 1, &result, 2);

	/* Return negative errno or unsigned value */
	return (status < 0) ? status : result;
}

/**
 * spi_w8r16be - SPI synchronous 8 bit write followed by 16 bit big-endian read
 * @spi: device with which data will be exchanged
 * @cmd: command to be written before data is read back
 * Context: can sleep
 *
 * This function is similar to spi_w8r16, with the exception that it will
 * convert the read 16 bit data word from big-endian to native endianness.
 *
 * Callable only from contexts that can sleep.
 *
 * Return: the (unsigned) sixteen bit number returned by the device in CPU
 * endianness, or else a negative error code.
 */
static inline ssize_t spi_w8r16be(struct spi_device *spi, u8 cmd)

{
	ssize_t status;
	__be16 result;

	status = spi_write_then_read(spi, &cmd, 1, &result, 2);
	if (status < 0)
		return status;

	return be16_to_cpu(result);
}

/*---------------------------------------------------------------------------*/

/*
 * INTERFACE between board init code and SPI infrastructure.
 *
 * No SPI driver ever sees these SPI device table segments, but
 * it's how the SPI core (or adapters that get hotplugged) grows
 * the driver model tree.
 *
 * As a rule, SPI devices can't be probed.  Instead, board init code
 * provides a table listing the devices which are present, with enough
 * information to bind and set up the device's driver.  There's basic
 * support for non-static configurations too; enough to handle adding
 * parport adapters, or microcontrollers acting as USB-to-SPI bridges.
 */

/**
 * struct spi_board_info - board-specific template for a SPI device
 * @modalias: Initializes spi_device.modalias; identifies the driver.
 * @platform_data: Initializes spi_device.platform_data; the particular
 *	data stored there is driver-specific.
 * @swnode: Software node for the device.
 * @controller_data: Initializes spi_device.controller_data; some
 *	controllers need hints about hardware setup, e.g. for DMA.
 * @irq: Initializes spi_device.irq; depends on how the board is wired.
 * @max_speed_hz: Initializes spi_device.max_speed_hz; based on limits
 *	from the chip datasheet and board-specific signal quality issues.
 * @bus_num: Identifies which spi_controller parents the spi_device; unused
 *	by spi_new_device(), and otherwise depends on board wiring.
 * @chip_select: Initializes spi_device.chip_select; depends on how
 *	the board is wired.
 * @mode: Initializes spi_device.mode; based on the chip datasheet, board
 *	wiring (some devices support both 3WIRE and standard modes), and
 *	possibly presence of an inverter in the chipselect path.
 *
 * When adding new SPI devices to the device tree, these structures serve
 * as a partial device template.  They hold information which can't always
 * be determined by drivers.  Information that probe() can establish (such
 * as the default transfer wordsize) is not included here.
 *
 * These structures are used in two places.  Their primary role is to
 * be stored in tables of board-specific device descriptors, which are
 * declared early in board initialization and then used (much later) to
 * populate a controller's device tree after the that controller's driver
 * initializes.  A secondary (and atypical) role is as a parameter to
 * spi_new_device() call, which happens after those controller drivers
 * are active in some dynamic board configuration models.
 */
struct spi_board_info {
	/*
	 * The device name and module name are coupled, like platform_bus;
	 * "modalias" is normally the driver name.
	 *
	 * platform_data goes to spi_device.dev.platform_data,
	 * controller_data goes to spi_device.controller_data,
	 * IRQ is copied too.
	 */
	char		modalias[SPI_NAME_SIZE];
	const void	*platform_data;
	const struct software_node *swnode;
	void		*controller_data;
	int		irq;

	/* Slower signaling on noisy or low voltage boards */
	u32		max_speed_hz;


	/*
	 * bus_num is board specific and matches the bus_num of some
	 * spi_controller that will probably be registered later.
	 *
	 * chip_select reflects how this chip is wired to that controller;
	 * it's less than num_chipselect.
	 */
	u16		bus_num;
	u16		chip_select;

	/*
	 * mode becomes spi_device.mode, and is essential for chips
	 * where the default of SPI_CS_HIGH = 0 is wrong.
	 */
	u32		mode;

	/*
	 * ... may need additional spi_device chip config data here.
	 * avoid stuff protocol drivers can set; but include stuff
	 * needed to behave without being bound to a driver:
	 *  - quirks like clock rate mattering when not selected
	 */
};

#ifdef	CONFIG_SPI
extern int
spi_register_board_info(struct spi_board_info const *info, unsigned n);
#else
/* Board init code may ignore whether SPI is configured or not */
static inline int
spi_register_board_info(struct spi_board_info const *info, unsigned n)
	{ return 0; }
#endif

/*
 * If you're hotplugging an adapter with devices (parport, USB, etc)
 * use spi_new_device() to describe each device.  You can also call
 * spi_unregister_device() to start making that device vanish, but
 * normally that would be handled by spi_unregister_controller().
 *
 * You can also use spi_alloc_device() and spi_add_device() to use a two
 * stage registration sequence for each spi_device. This gives the caller
 * some more control over the spi_device structure before it is registered,
 * but requires that caller to initialize fields that would otherwise
 * be defined using the board info.
 */
extern struct spi_device *
spi_alloc_device(struct spi_controller *ctlr);

extern int
spi_add_device(struct spi_device *spi);

extern struct spi_device *
spi_new_device(struct spi_controller *, struct spi_board_info *);

extern void spi_unregister_device(struct spi_device *spi);

extern const struct spi_device_id *
spi_get_device_id(const struct spi_device *sdev);

extern const void *
spi_get_device_match_data(const struct spi_device *sdev);

static inline bool
spi_transfer_is_last(struct spi_controller *ctlr, struct spi_transfer *xfer)
{
	return list_is_last(&xfer->transfer_list, &ctlr->cur_msg->transfers);
}

#endif /* __LINUX_SPI_H */
