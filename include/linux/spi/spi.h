/*
 * Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_SPI_H
#define __LINUX_SPI_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/scatterlist.h>

struct dma_chan;
struct property_entry;
struct spi_controller;
struct spi_transfer;
struct spi_flash_read_message;

/*
 * INTERFACES between SPI master-side drivers and SPI slave protocol handlers,
 * and SPI infrastructure.
 */
extern struct bus_type spi_bus_type;

/**
 * struct spi_statistics - statistics for spi transfers
 * @lock:          lock protecting this structure
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
 *                 transfer bytes histogramm
 *
 * @transfers_split_maxsize:
 *                 number of transfers that have been split because of
 *                 maxsize limit
 */
struct spi_statistics {
	spinlock_t		lock; /* lock for the whole structure */

	unsigned long		messages;
	unsigned long		transfers;
	unsigned long		errors;
	unsigned long		timedout;

	unsigned long		spi_sync;
	unsigned long		spi_sync_immediate;
	unsigned long		spi_async;

	unsigned long long	bytes;
	unsigned long long	bytes_rx;
	unsigned long long	bytes_tx;

#define SPI_STATISTICS_HISTO_SIZE 17
	unsigned long transfer_bytes_histo[SPI_STATISTICS_HISTO_SIZE];

	unsigned long transfers_split_maxsize;
};

void spi_statistics_add_transfer_stats(struct spi_statistics *stats,
				       struct spi_transfer *xfer,
				       struct spi_controller *ctlr);

#define SPI_STATISTICS_ADD_TO_FIELD(stats, field, count)	\
	do {							\
		unsigned long flags;				\
		spin_lock_irqsave(&(stats)->lock, flags);	\
		(stats)->field += count;			\
		spin_unlock_irqrestore(&(stats)->lock, flags);	\
	} while (0)

#define SPI_STATISTICS_INCREMENT_FIELD(stats, field)	\
	SPI_STATISTICS_ADD_TO_FIELD(stats, field, 1)

/**
 * struct spi_device - Controller side proxy for an SPI slave device
 * @dev: Driver model representation of the device.
 * @controller: SPI controller used with the device.
 * @master: Copy of controller, for backwards compatibility.
 * @max_speed_hz: Maximum clock rate to be used with this chip
 *	(on this board); may be changed by the device's driver.
 *	The spi_transfer.speed_hz can override this for each transfer.
 * @chip_select: Chipselect, distinguishing chips handled by @controller.
 * @mode: The spi mode defines how data is clocked out and in.
 *	This may be changed by the device's driver.
 *	The "active low" default for chipselect mode can be overridden
 *	(by specifying SPI_CS_HIGH) as can the "MSB first" default for
 *	each word in a transfer (by specifying SPI_LSB_FIRST).
 * @bits_per_word: Data transfers involve one or more words; word sizes
 *	like eight or 12 bits are common.  In-memory wordsizes are
 *	powers of two bytes (e.g. 20 bit samples use 32 bits).
 *	This may be changed by the device's driver, or left at the
 *	default (0) indicating protocol words are eight bit bytes.
 *	The spi_transfer.bits_per_word can override this for each transfer.
 * @irq: Negative, or the number passed to request_irq() to receive
 *	interrupts from this device.
 * @controller_state: Controller's runtime state
 * @controller_data: Board-specific definitions for controller, such as
 *	FIFO initialization parameters; from board_info.controller_data
 * @modalias: Name of the driver to use with this device, or an alias
 *	for that name.  This appears in the sysfs "modalias" attribute
 *	for driver coldplugging, and in uevents used for hotplugging
 * @cs_gpio: gpio number of the chipselect line (optional, -ENOENT when
 *	not using a GPIO line)
 *
 * @statistics: statistics for the spi_device
 *
 * A @spi_device is used to interchange data between an SPI slave
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
	struct spi_controller	*master;	/* compatibility layer */
	u32			max_speed_hz;
	u8			chip_select;
	u8			bits_per_word;
	u16			mode;
#define	SPI_CPHA	0x01			/* clock phase */
#define	SPI_CPOL	0x02			/* clock polarity */
#define	SPI_MODE_0	(0|0)			/* (original MicroWire) */
#define	SPI_MODE_1	(0|SPI_CPHA)
#define	SPI_MODE_2	(SPI_CPOL|0)
#define	SPI_MODE_3	(SPI_CPOL|SPI_CPHA)
#define	SPI_CS_HIGH	0x04			/* chipselect active high? */
#define	SPI_LSB_FIRST	0x08			/* per-word bits-on-wire */
#define	SPI_3WIRE	0x10			/* SI/SO signals shared */
#define	SPI_LOOP	0x20			/* loopback mode */
#define	SPI_NO_CS	0x40			/* 1 dev/bus, no chipselect */
#define	SPI_READY	0x80			/* slave pulls low to pause */
#define	SPI_TX_DUAL	0x100			/* transmit with 2 wires */
#define	SPI_TX_QUAD	0x200			/* transmit with 4 wires */
#define	SPI_RX_DUAL	0x400			/* receive with 2 wires */
#define	SPI_RX_QUAD	0x800			/* receive with 4 wires */
	int			irq;
	void			*controller_state;
	void			*controller_data;
	char			modalias[SPI_NAME_SIZE];
	int			cs_gpio;	/* chip select gpio */

	/* the statistics */
	struct spi_statistics	statistics;

	/*
	 * likely need more hooks for more protocol options affecting how
	 * the controller talks to each chip, like:
	 *  - memory packing (12 bit samples into low bits, others zeroed)
	 *  - priority
	 *  - drop chipselect after each word
	 *  - chipselect delays
	 *  - ...
	 */
};

static inline struct spi_device *to_spi_device(struct device *dev)
{
	return dev ? container_of(dev, struct spi_device, dev) : NULL;
}

/* most drivers won't need to care about device refcounting */
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
static inline void *spi_get_ctldata(struct spi_device *spi)
{
	return spi->controller_state;
}

static inline void spi_set_ctldata(struct spi_device *spi, void *state)
{
	spi->controller_state = state;
}

/* device driver data */

static inline void spi_set_drvdata(struct spi_device *spi, void *data)
{
	dev_set_drvdata(&spi->dev, data);
}

static inline void *spi_get_drvdata(struct spi_device *spi)
{
	return dev_get_drvdata(&spi->dev);
}

struct spi_message;
struct spi_transfer;

/**
 * struct spi_driver - Host side "protocol" driver
 * @id_table: List of SPI devices supported by this driver
 * @probe: Binds this driver to the spi device.  Drivers can verify
 *	that the device is actually present, and may need to configure
 *	characteristics (such as bits_per_word) which weren't needed for
 *	the initial configuration done during system setup.
 * @remove: Unbinds this driver from the spi device
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
	int			(*remove)(struct spi_device *spi);
	void			(*shutdown)(struct spi_device *spi);
	struct device_driver	driver;
};

static inline struct spi_driver *to_spi_driver(struct device_driver *drv)
{
	return drv ? container_of(drv, struct spi_driver, driver) : NULL;
}

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

/* use a define to avoid include chaining to get THIS_MODULE */
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
 * struct spi_controller - interface to SPI master or slave controller
 * @dev: device interface to this driver
 * @list: link with the global spi_controller list
 * @bus_num: board-specific (and often SOC-specific) identifier for a
 *	given SPI controller.
 * @num_chipselect: chipselects are used to distinguish individual
 *	SPI slaves, and are numbered from zero to num_chipselects.
 *	each slave has a chipselect signal, but it's common that not
 *	every chipselect is connected to a slave.
 * @dma_alignment: SPI controller constraint on DMA buffers alignment.
 * @mode_bits: flags understood by this controller driver
 * @bits_per_word_mask: A mask indicating which values of bits_per_word are
 *	supported by the driver. Bit n indicates that a bits_per_word n+1 is
 *	supported. If set, the SPI core will reject any transfer with an
 *	unsupported bits_per_word. If not set, this value is simply ignored,
 *	and it's up to the individual driver to perform any validation.
 * @min_speed_hz: Lowest supported transfer speed
 * @max_speed_hz: Highest supported transfer speed
 * @flags: other constraints relevant to this driver
 * @slave: indicates that this is an SPI slave controller
 * @max_transfer_size: function that returns the max transfer size for
 *	a &spi_device; may be %NULL, so the default %SIZE_MAX will be used.
 * @max_message_size: function that returns the max message size for
 *	a &spi_device; may be %NULL, so the default %SIZE_MAX will be used.
 * @io_mutex: mutex for physical bus access
 * @bus_lock_spinlock: spinlock for SPI bus locking
 * @bus_lock_mutex: mutex for exclusion of multiple callers
 * @bus_lock_flag: indicates that the SPI bus is locked for exclusive use
 * @setup: updates the device mode and clocking records used by a
 *	device's SPI controller; protocol code may call this.  This
 *	must fail if an unrecognized or unsupported mode is requested.
 *	It's always safe to call this unless transfers are pending on
 *	the device whose settings are being modified.
 * @transfer: adds a message to the controller's transfer queue.
 * @cleanup: frees controller-specific state
 * @can_dma: determine whether this controller supports DMA
 * @queued: whether this controller is providing an internal message queue
 * @kworker: thread struct for message pump
 * @kworker_task: pointer to task for message pump kworker thread
 * @pump_messages: work struct for scheduling work to the message pump
 * @queue_lock: spinlock to syncronise access to message queue
 * @queue: message queue
 * @idling: the device is entering idle state
 * @cur_msg: the currently in-flight message
 * @cur_msg_prepared: spi_prepare_message was called for the currently
 *                    in-flight message
 * @cur_msg_mapped: message has been mapped for DMA
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
 * @set_cs: set the logic level of the chip select line.  May be called
 *          from interrupt context.
 * @prepare_message: set up the controller to transfer a single message,
 *                   for example doing DMA mapping.  Called from threaded
 *                   context.
 * @transfer_one: transfer a single spi_transfer.
 *                  - return 0 if the transfer is finished,
 *                  - return 1 if the transfer is still in progress. When
 *                    the driver is finished with this transfer it must
 *                    call spi_finalize_current_transfer() so the subsystem
 *                    can issue the next transfer. Note: transfer_one and
 *                    transfer_one_message are mutually exclusive; when both
 *                    are set, the generic subsystem does not call your
 *                    transfer_one callback.
 * @handle_err: the subsystem calls the driver to handle an error that occurs
 *		in the generic implementation of transfer_one_message().
 * @unprepare_message: undo any work done by prepare_message().
 * @slave_abort: abort the ongoing transfer request on an SPI slave controller
 * @spi_flash_read: to support spi-controller hardwares that provide
 *                  accelerated interface to read from flash devices.
 * @spi_flash_can_dma: analogous to can_dma() interface, but for
 *		       controllers implementing spi_flash_read.
 * @flash_read_supported: spi device supports flash read
 * @cs_gpios: Array of GPIOs to use as chip select lines; one per CS
 *	number. Any individual value may be -ENOENT for CS lines that
 *	are not GPIOs (driven by the SPI controller itself).
 * @statistics: statistics for the spi_controller
 * @dma_tx: DMA transmit channel
 * @dma_rx: DMA receive channel
 * @dummy_rx: dummy receive buffer for full-duplex devices
 * @dummy_tx: dummy transmit buffer for full-duplex devices
 * @fw_translate_cs: If the boot firmware uses different numbering scheme
 *	what Linux expects, this optional hook can be used to translate
 *	between the two.
 *
 * Each SPI controller can communicate with one or more @spi_device
 * children.  These make a small bus, sharing MOSI, MISO and SCK signals
 * but not chip select signals.  Each device may be configured to use a
 * different clock rate, since those shared signals are ignored unless
 * the chip is selected.
 *
 * The driver for an SPI controller manages access to those devices through
 * a queue of spi_message transactions, copying data between CPU memory and
 * an SPI slave device.  For each such message it queues, it calls the
 * message's completion function when the transaction completes.
 */
struct spi_controller {
	struct device	dev;

	struct list_head list;

	/* other than negative (== assign one dynamically), bus_num is fully
	 * board-specific.  usually that simplifies to being SOC-specific.
	 * example:  one SOC has three SPI controllers, numbered 0..2,
	 * and one board's schematics might show it using SPI-2.  software
	 * would normally use bus_num=2 for that controller.
	 */
	s16			bus_num;

	/* chipselects will be integral to many controllers; some others
	 * might use board-specific GPIOs.
	 */
	u16			num_chipselect;

	/* some SPI controllers pose alignment requirements on DMAable
	 * buffers; let protocol drivers know about these requirements.
	 */
	u16			dma_alignment;

	/* spi_device.mode flags understood by this controller driver */
	u16			mode_bits;

	/* bitmask of supported bits_per_word for transfers */
	u32			bits_per_word_mask;
#define SPI_BPW_MASK(bits) BIT((bits) - 1)
#define SPI_BIT_MASK(bits) (((bits) == 32) ? ~0U : (BIT(bits) - 1))
#define SPI_BPW_RANGE_MASK(min, max) (SPI_BIT_MASK(max) - SPI_BIT_MASK(min - 1))

	/* limits on transfer speed */
	u32			min_speed_hz;
	u32			max_speed_hz;

	/* other constraints relevant to this driver */
	u16			flags;
#define SPI_CONTROLLER_HALF_DUPLEX	BIT(0)	/* can't do full duplex */
#define SPI_CONTROLLER_NO_RX		BIT(1)	/* can't do buffer read */
#define SPI_CONTROLLER_NO_TX		BIT(2)	/* can't do buffer write */
#define SPI_CONTROLLER_MUST_RX		BIT(3)	/* requires rx */
#define SPI_CONTROLLER_MUST_TX		BIT(4)	/* requires tx */

#define SPI_MASTER_GPIO_SS		BIT(5)	/* GPIO CS must select slave */

	/* flag indicating this is an SPI slave controller */
	bool			slave;

	/*
	 * on some hardware transfer / message size may be constrained
	 * the limit may depend on device transfer settings
	 */
	size_t (*max_transfer_size)(struct spi_device *spi);
	size_t (*max_message_size)(struct spi_device *spi);

	/* I/O mutex */
	struct mutex		io_mutex;

	/* lock and mutex for SPI bus locking */
	spinlock_t		bus_lock_spinlock;
	struct mutex		bus_lock_mutex;

	/* flag indicating that the SPI bus is locked for exclusive use */
	bool			bus_lock_flag;

	/* Setup mode and clock, etc (spi driver may call many times).
	 *
	 * IMPORTANT:  this may be called when transfers to another
	 * device are active.  DO NOT UPDATE SHARED REGISTERS in ways
	 * which could break those transfers.
	 */
	int			(*setup)(struct spi_device *spi);

	/* bidirectional bulk transfers
	 *
	 * + The transfer() method may not sleep; its main role is
	 *   just to add the message to the queue.
	 * + For now there's no remove-from-queue operation, or
	 *   any other request management
	 * + To a given spi_device, message queueing is pure fifo
	 *
	 * + The controller's main job is to process its message queue,
	 *   selecting a chip (for masters), then transferring data
	 * + If there are multiple spi_device children, the i/o queue
	 *   arbitration algorithm is unspecified (round robin, fifo,
	 *   priority, reservations, preemption, etc)
	 *
	 * + Chipselect stays active during the entire message
	 *   (unless modified by spi_transfer.cs_change != 0).
	 * + The message transfers use clock and SPI mode parameters
	 *   previously established by setup() for this device
	 */
	int			(*transfer)(struct spi_device *spi,
						struct spi_message *mesg);

	/* called on release() to free memory provided by spi_controller */
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

	/*
	 * These hooks are for drivers that want to use the generic
	 * controller transfer queueing mechanism. If these are used, the
	 * transfer() function above must NOT be specified by the driver.
	 * Over time we expect SPI drivers to be phased over to this API.
	 */
	bool				queued;
	struct kthread_worker		kworker;
	struct task_struct		*kworker_task;
	struct kthread_work		pump_messages;
	spinlock_t			queue_lock;
	struct list_head		queue;
	struct spi_message		*cur_msg;
	bool				idling;
	bool				busy;
	bool				running;
	bool				rt;
	bool				auto_runtime_pm;
	bool                            cur_msg_prepared;
	bool				cur_msg_mapped;
	struct completion               xfer_completion;
	size_t				max_dma_len;

	int (*prepare_transfer_hardware)(struct spi_controller *ctlr);
	int (*transfer_one_message)(struct spi_controller *ctlr,
				    struct spi_message *mesg);
	int (*unprepare_transfer_hardware)(struct spi_controller *ctlr);
	int (*prepare_message)(struct spi_controller *ctlr,
			       struct spi_message *message);
	int (*unprepare_message)(struct spi_controller *ctlr,
				 struct spi_message *message);
	int (*slave_abort)(struct spi_controller *ctlr);
	int (*spi_flash_read)(struct  spi_device *spi,
			      struct spi_flash_read_message *msg);
	bool (*spi_flash_can_dma)(struct spi_device *spi,
				  struct spi_flash_read_message *msg);
	bool (*flash_read_supported)(struct spi_device *spi);

	/*
	 * These hooks are for drivers that use a generic implementation
	 * of transfer_one_message() provied by the core.
	 */
	void (*set_cs)(struct spi_device *spi, bool enable);
	int (*transfer_one)(struct spi_controller *ctlr, struct spi_device *spi,
			    struct spi_transfer *transfer);
	void (*handle_err)(struct spi_controller *ctlr,
			   struct spi_message *message);

	/* gpio chip select */
	int			*cs_gpios;

	/* statistics */
	struct spi_statistics	statistics;

	/* DMA channels for use with core dmaengine helpers */
	struct dma_chan		*dma_tx;
	struct dma_chan		*dma_rx;

	/* dummy data for full duplex devices */
	void			*dummy_rx;
	void			*dummy_tx;

	int (*fw_translate_cs)(struct spi_controller *ctlr, unsigned cs);
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

static inline bool spi_controller_is_slave(struct spi_controller *ctlr)
{
	return IS_ENABLED(CONFIG_SPI_SLAVE) && ctlr->slave;
}

/* PM calls that need to be issued by the driver */
extern int spi_controller_suspend(struct spi_controller *ctlr);
extern int spi_controller_resume(struct spi_controller *ctlr);

/* Calls the driver make to interact with the message queue */
extern struct spi_message *spi_get_next_queued_message(struct spi_controller *ctlr);
extern void spi_finalize_current_message(struct spi_controller *ctlr);
extern void spi_finalize_current_transfer(struct spi_controller *ctlr);

/* the spi driver core manages memory for the spi_controller classdev */
extern struct spi_controller *__spi_alloc_controller(struct device *host,
						unsigned int size, bool slave);

static inline struct spi_controller *spi_alloc_master(struct device *host,
						      unsigned int size)
{
	return __spi_alloc_controller(host, size, false);
}

static inline struct spi_controller *spi_alloc_slave(struct device *host,
						     unsigned int size)
{
	if (!IS_ENABLED(CONFIG_SPI_SLAVE))
		return NULL;

	return __spi_alloc_controller(host, size, true);
}

extern int spi_register_controller(struct spi_controller *ctlr);
extern int devm_spi_register_controller(struct device *dev,
					struct spi_controller *ctlr);
extern void spi_unregister_controller(struct spi_controller *ctlr);

extern struct spi_controller *spi_busnum_to_master(u16 busnum);

/*
 * SPI resource management while processing a SPI message
 */

typedef void (*spi_res_release_t)(struct spi_controller *ctlr,
				  struct spi_message *msg,
				  void *res);

/**
 * struct spi_res - spi resource management structure
 * @entry:   list entry
 * @release: release code called prior to freeing this resource
 * @data:    extra data allocated for the specific use-case
 *
 * this is based on ideas from devres, but focused on life-cycle
 * management during spi_message processing
 */
struct spi_res {
	struct list_head        entry;
	spi_res_release_t       release;
	unsigned long long      data[]; /* guarantee ull alignment */
};

extern void *spi_res_alloc(struct spi_device *spi,
			   spi_res_release_t release,
			   size_t size, gfp_t gfp);
extern void spi_res_add(struct spi_message *message, void *res);
extern void spi_res_free(void *res);

extern void spi_res_release(struct spi_controller *ctlr,
			    struct spi_message *message);

/*---------------------------------------------------------------------------*/

/*
 * I/O INTERFACE between SPI controller and protocol drivers
 *
 * Protocol drivers use a queue of spi_messages, each transferring data
 * between the controller and memory buffers.
 *
 * The spi_messages themselves consist of a series of read+write transfer
 * segments.  Those segments always read the same number of bits as they
 * write; but one or the other is easily ignored by passing a null buffer
 * pointer.  (This is unlike most types of I/O API, because SPI hardware
 * is full duplex.)
 *
 * NOTE:  Allocation of spi_transfer and spi_message memory is entirely
 * up to the protocol driver, which guarantees the integrity of both (as
 * well as the data buffers) for as long as the message is queued.
 */

/**
 * struct spi_transfer - a read/write buffer pair
 * @tx_buf: data to be written (dma-safe memory), or NULL
 * @rx_buf: data to be read (dma-safe memory), or NULL
 * @tx_dma: DMA address of tx_buf, if @spi_message.is_dma_mapped
 * @rx_dma: DMA address of rx_buf, if @spi_message.is_dma_mapped
 * @tx_nbits: number of bits used for writing. If 0 the default
 *      (SPI_NBITS_SINGLE) is used.
 * @rx_nbits: number of bits used for reading. If 0 the default
 *      (SPI_NBITS_SINGLE) is used.
 * @len: size of rx and tx buffers (in bytes)
 * @speed_hz: Select a speed other than the device default for this
 *      transfer. If 0 the default (from @spi_device) is used.
 * @bits_per_word: select a bits_per_word other than the device default
 *      for this transfer. If 0 the default (from @spi_device) is used.
 * @cs_change: affects chipselect after this transfer completes
 * @delay_usecs: microseconds to delay after this transfer before
 *	(optionally) changing the chipselect status, then starting
 *	the next transfer or completing this @spi_message.
 * @transfer_list: transfers are sequenced through @spi_message.transfers
 * @tx_sg: Scatterlist for transmit, currently not for client use
 * @rx_sg: Scatterlist for receive, currently not for client use
 *
 * SPI transfers always write the same number of bytes as they read.
 * Protocol drivers should always provide @rx_buf and/or @tx_buf.
 * In some cases, they may also want to provide DMA addresses for
 * the data being transferred; that may reduce overhead, when the
 * underlying driver uses dma.
 *
 * If the transmit buffer is null, zeroes will be shifted out
 * while filling @rx_buf.  If the receive buffer is null, the data
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
 * ends when the chipselect goes intactive.
 *
 * When SPI can transfer in 1x,2x or 4x. It can get this transfer information
 * from device through @tx_nbits and @rx_nbits. In Bi-direction, these
 * two should both be set. User can set transfer mode with SPI_NBITS_SINGLE(1x)
 * SPI_NBITS_DUAL(2x) and SPI_NBITS_QUAD(4x) to support these three transfer.
 *
 * The code that submits an spi_message (and its spi_transfers)
 * to the lower layers is responsible for managing its memory.
 * Zero-initialize every field you don't set up explicitly, to
 * insulate against future API updates.  After you submit a message
 * and its transfers, ignore them until its completion callback.
 */
struct spi_transfer {
	/* it's ok if tx_buf == rx_buf (right?)
	 * for MicroWire, one buffer must be null
	 * buffers must work with dma_*map_single() calls, unless
	 *   spi_message.is_dma_mapped reports a pre-existing mapping
	 */
	const void	*tx_buf;
	void		*rx_buf;
	unsigned	len;

	dma_addr_t	tx_dma;
	dma_addr_t	rx_dma;
	struct sg_table tx_sg;
	struct sg_table rx_sg;

	unsigned	cs_change:1;
	unsigned	tx_nbits:3;
	unsigned	rx_nbits:3;
#define	SPI_NBITS_SINGLE	0x01 /* 1bit transfer */
#define	SPI_NBITS_DUAL		0x02 /* 2bits transfer */
#define	SPI_NBITS_QUAD		0x04 /* 4bits transfer */
	u8		bits_per_word;
	u16		delay_usecs;
	u32		speed_hz;

	struct list_head transfer_list;
};

/**
 * struct spi_message - one multi-segment SPI transaction
 * @transfers: list of transfer segments in this transaction
 * @spi: SPI device to which the transaction is queued
 * @is_dma_mapped: if true, the caller provided both dma and cpu virtual
 *	addresses for each transfer buffer
 * @complete: called to report transaction completions
 * @context: the argument to complete() when it's called
 * @frame_length: the total number of bytes in the message
 * @actual_length: the total number of bytes that were transferred in all
 *	successful segments
 * @status: zero for success, else negative errno
 * @queue: for use by whichever driver currently owns the message
 * @state: for use by whichever driver currently owns the message
 * @resources: for resource management when the spi message is processed
 *
 * A @spi_message is used to execute an atomic sequence of data transfers,
 * each represented by a struct spi_transfer.  The sequence is "atomic"
 * in the sense that no other spi_message may use that SPI bus until that
 * sequence completes.  On some systems, many such sequences can execute as
 * as single programmed DMA transfer.  On all systems, these messages are
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

	unsigned		is_dma_mapped:1;

	/* REVISIT:  we might want a flag affecting the behavior of the
	 * last transfer ... allowing things like "read 16 bit length L"
	 * immediately followed by "read L bytes".  Basically imposing
	 * a specific message scheduling algorithm.
	 *
	 * Some controller drivers (message-at-a-time queue processing)
	 * could provide that as their default scheduling algorithm.  But
	 * others (with multi-message pipelines) could need a flag to
	 * tell them about such special cases.
	 */

	/* completion is reported through a callback */
	void			(*complete)(void *context);
	void			*context;
	unsigned		frame_length;
	unsigned		actual_length;
	int			status;

	/* for optional use by whatever driver currently owns the
	 * spi_message ...  between calls to spi_async and then later
	 * complete(), that's the spi_controller controller driver.
	 */
	struct list_head	queue;
	void			*state;

	/* list of spi_res reources when the spi message is processed */
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

/**
 * spi_message_init_with_transfers - Initialize spi_message and append transfers
 * @m: spi_message to be initialized
 * @xfers: An array of spi transfers
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

/* It's fine to embed message and transaction structures in other data
 * structures so long as you don't free them while they're in use.
 */

static inline struct spi_message *spi_message_alloc(unsigned ntrans, gfp_t flags)
{
	struct spi_message *m;

	m = kzalloc(sizeof(struct spi_message)
			+ ntrans * sizeof(struct spi_transfer),
			flags);
	if (m) {
		unsigned i;
		struct spi_transfer *t = (struct spi_transfer *)(m + 1);

		spi_message_init_no_memset(m);
		for (i = 0; i < ntrans; i++, t++)
			spi_message_add_tail(t, m);
	}
	return m;
}

static inline void spi_message_free(struct spi_message *m)
{
	kfree(m);
}

extern int spi_setup(struct spi_device *spi);
extern int spi_async(struct spi_device *spi, struct spi_message *message);
extern int spi_async_locked(struct spi_device *spi,
			    struct spi_message *message);
extern int spi_slave_abort(struct spi_device *spi);

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

	/* transfer size limit must not be greater than messsage size limit */
	return min(tr_max, msg_max);
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
 *                      relasing this structure
 * @extradata:          pointer to some extra data if requested or NULL
 * @replaced_transfers: transfers that have been replaced and which need
 *                      to get restored
 * @replaced_after:     the transfer after which the @replaced_transfers
 *                      are to get re-inserted
 * @inserted:           number of transfers inserted
 * @inserted_transfers: array of spi_transfers of array-size @inserted,
 *                      that have been replacing replaced_transfers
 *
 * note: that @extradata will point to @inserted_transfers[@inserted]
 * if some extra allocation is requested, so alignment will be the same
 * as for spi_transfers
 */
struct spi_replaced_transfers {
	spi_replaced_release_t release;
	void *extradata;
	struct list_head replaced_transfers;
	struct list_head *replaced_after;
	size_t inserted;
	struct spi_transfer inserted_transfers[];
};

extern struct spi_replaced_transfers *spi_replace_transfers(
	struct spi_message *msg,
	struct spi_transfer *xfer_first,
	size_t remove,
	size_t insert,
	spi_replaced_release_t release,
	size_t extradatasize,
	gfp_t gfp);

/*---------------------------------------------------------------------------*/

/* SPI transfer transformation methods */

extern int spi_split_transfers_maxsize(struct spi_controller *ctlr,
				       struct spi_message *msg,
				       size_t maxsize,
				       gfp_t gfp);

/*---------------------------------------------------------------------------*/

/* All these synchronous SPI transfer routines are utilities layered
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
 * Return: Return: zero on success, else a negative error code.
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

/* this copies txbuf and rxbuf data; for small transfers only! */
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

	/* return negative errno or unsigned value */
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

	/* return negative errno or unsigned value */
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
 * Return: the (unsigned) sixteen bit number returned by the device in cpu
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

/**
 * struct spi_flash_read_message - flash specific information for
 * spi-masters that provide accelerated flash read interfaces
 * @buf: buffer to read data
 * @from: offset within the flash from where data is to be read
 * @len: length of data to be read
 * @retlen: actual length of data read
 * @read_opcode: read_opcode to be used to communicate with flash
 * @addr_width: number of address bytes
 * @dummy_bytes: number of dummy bytes
 * @opcode_nbits: number of lines to send opcode
 * @addr_nbits: number of lines to send address
 * @data_nbits: number of lines for data
 * @rx_sg: Scatterlist for receive data read from flash
 * @cur_msg_mapped: message has been mapped for DMA
 */
struct spi_flash_read_message {
	void *buf;
	loff_t from;
	size_t len;
	size_t retlen;
	u8 read_opcode;
	u8 addr_width;
	u8 dummy_bytes;
	u8 opcode_nbits;
	u8 addr_nbits;
	u8 data_nbits;
	struct sg_table rx_sg;
	bool cur_msg_mapped;
};

/* SPI core interface for flash read support */
static inline bool spi_flash_read_supported(struct spi_device *spi)
{
	return spi->controller->spi_flash_read &&
	       (!spi->controller->flash_read_supported ||
	       spi->controller->flash_read_supported(spi));
}

int spi_flash_read(struct spi_device *spi,
		   struct spi_flash_read_message *msg);

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
 * support for nonstatic configurations too; enough to handle adding
 * parport adapters, or microcontrollers acting as USB-to-SPI bridges.
 */

/**
 * struct spi_board_info - board-specific template for a SPI device
 * @modalias: Initializes spi_device.modalias; identifies the driver.
 * @platform_data: Initializes spi_device.platform_data; the particular
 *	data stored there is driver-specific.
 * @properties: Additional device properties for the device.
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
	/* the device name and module name are coupled, like platform_bus;
	 * "modalias" is normally the driver name.
	 *
	 * platform_data goes to spi_device.dev.platform_data,
	 * controller_data goes to spi_device.controller_data,
	 * device properties are copied and attached to spi_device,
	 * irq is copied too
	 */
	char		modalias[SPI_NAME_SIZE];
	const void	*platform_data;
	const struct property_entry *properties;
	void		*controller_data;
	int		irq;

	/* slower signaling on noisy or low voltage boards */
	u32		max_speed_hz;


	/* bus_num is board specific and matches the bus_num of some
	 * spi_controller that will probably be registered later.
	 *
	 * chip_select reflects how this chip is wired to that master;
	 * it's less than num_chipselect.
	 */
	u16		bus_num;
	u16		chip_select;

	/* mode becomes spi_device.mode, and is essential for chips
	 * where the default of SPI_CS_HIGH = 0 is wrong.
	 */
	u16		mode;

	/* ... may need additional spi_device chip config data here.
	 * avoid stuff protocol drivers can set; but include stuff
	 * needed to behave without being bound to a driver:
	 *  - quirks like clock rate mattering when not selected
	 */
};

#ifdef	CONFIG_SPI
extern int
spi_register_board_info(struct spi_board_info const *info, unsigned n);
#else
/* board init code may ignore whether SPI is configured or not */
static inline int
spi_register_board_info(struct spi_board_info const *info, unsigned n)
	{ return 0; }
#endif


/* If you're hotplugging an adapter with devices (parport, usb, etc)
 * use spi_new_device() to describe each device.  You can also call
 * spi_unregister_device() to start making that device vanish, but
 * normally that would be handled by spi_unregister_controller().
 *
 * You can also use spi_alloc_device() and spi_add_device() to use a two
 * stage registration sequence for each spi_device.  This gives the caller
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

static inline bool
spi_transfer_is_last(struct spi_controller *ctlr, struct spi_transfer *xfer)
{
	return list_is_last(&xfer->transfer_list, &ctlr->cur_msg->transfers);
}


/* Compatibility layer */
#define spi_master			spi_controller

#define SPI_MASTER_HALF_DUPLEX		SPI_CONTROLLER_HALF_DUPLEX
#define SPI_MASTER_NO_RX		SPI_CONTROLLER_NO_RX
#define SPI_MASTER_NO_TX		SPI_CONTROLLER_NO_TX
#define SPI_MASTER_MUST_RX		SPI_CONTROLLER_MUST_RX
#define SPI_MASTER_MUST_TX		SPI_CONTROLLER_MUST_TX

#define spi_master_get_devdata(_ctlr)	spi_controller_get_devdata(_ctlr)
#define spi_master_set_devdata(_ctlr, _data)	\
	spi_controller_set_devdata(_ctlr, _data)
#define spi_master_get(_ctlr)		spi_controller_get(_ctlr)
#define spi_master_put(_ctlr)		spi_controller_put(_ctlr)
#define spi_master_suspend(_ctlr)	spi_controller_suspend(_ctlr)
#define spi_master_resume(_ctlr)	spi_controller_resume(_ctlr)

#define spi_register_master(_ctlr)	spi_register_controller(_ctlr)
#define devm_spi_register_master(_dev, _ctlr) \
	devm_spi_register_controller(_dev, _ctlr)
#define spi_unregister_master(_ctlr)	spi_unregister_controller(_ctlr)

#endif /* __LINUX_SPI_H */
