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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

/*
 * INTERFACES between SPI master-side drivers and SPI infrastructure.
 * (There's no SPI slave support for Linux yet...)
 */
extern struct bus_type spi_bus_type;

/**
 * struct spi_device - Master side proxy for an SPI slave device
 * @dev: Driver model representation of the device.
 * @master: SPI controller used with the device.
 * @max_speed_hz: Maximum clock rate to be used with this chip
 *	(on this board); may be changed by the device's driver.
 *	The spi_transfer.speed_hz can override this for each transfer.
 * @chip_select: Chipselect, distinguishing chips handled by @master.
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
 *	when not using a GPIO line)
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
	struct spi_master	*master;
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

/* ctldata is for the bus_master driver's runtime state */
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
 * @suspend: Standard suspend callback used during system state transitions
 * @resume: Standard resume callback used during system state transitions
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
	int			(*suspend)(struct spi_device *spi, pm_message_t mesg);
	int			(*resume)(struct spi_device *spi);
	struct device_driver	driver;
};

static inline struct spi_driver *to_spi_driver(struct device_driver *drv)
{
	return drv ? container_of(drv, struct spi_driver, driver) : NULL;
}

extern int spi_register_driver(struct spi_driver *sdrv);

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
 * struct spi_master - interface to SPI master controller
 * @dev: device interface to this driver
 * @list: link with the global spi_master list
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
 * @bus_lock_spinlock: spinlock for SPI bus locking
 * @bus_lock_mutex: mutex for SPI bus locking
 * @bus_lock_flag: indicates that the SPI bus is locked for exclusive use
 * @setup: updates the device mode and clocking records used by a
 *	device's SPI controller; protocol code may call this.  This
 *	must fail if an unrecognized or unsupported mode is requested.
 *	It's always safe to call this unless transfers are pending on
 *	the device whose settings are being modified.
 * @transfer: adds a message to the controller's transfer queue.
 * @cleanup: frees controller-specific state
 * @can_dma: determine whether this master supports DMA
 * @queued: whether this master is providing an internal message queue
 * @kworker: thread struct for message pump
 * @kworker_task: pointer to task for message pump kworker thread
 * @pump_messages: work struct for scheduling work to the message pump
 * @queue_lock: spinlock to syncronise access to message queue
 * @queue: message queue
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
 * @unprepare_message: undo any work done by prepare_message().
 * @cs_gpios: Array of GPIOs to use as chip select lines; one per CS
 *	number. Any individual value may be -ENOENT for CS lines that
 *	are not GPIOs (driven by the SPI controller itself).
 * @dma_tx: DMA transmit channel
 * @dma_rx: DMA receive channel
 * @dummy_rx: dummy receive buffer for full-duplex devices
 * @dummy_tx: dummy transmit buffer for full-duplex devices
 *
 * Each SPI master controller can communicate with one or more @spi_device
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
struct spi_master {
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
#define SPI_MASTER_HALF_DUPLEX	BIT(0)		/* can't do full duplex */
#define SPI_MASTER_NO_RX	BIT(1)		/* can't do buffer read */
#define SPI_MASTER_NO_TX	BIT(2)		/* can't do buffer write */
#define SPI_MASTER_MUST_RX      BIT(3)		/* requires rx */
#define SPI_MASTER_MUST_TX      BIT(4)		/* requires tx */

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
	 * + The master's main job is to process its message queue,
	 *   selecting a chip then transferring data
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

	/* called on release() to free memory provided by spi_master */
	void			(*cleanup)(struct spi_device *spi);

	/*
	 * Used to enable core support for DMA handling, if can_dma()
	 * exists and returns true then the transfer will be mapped
	 * prior to transfer_one() being called.  The driver should
	 * not modify or store xfer and dma_tx and dma_rx must be set
	 * while the device is prepared.
	 */
	bool			(*can_dma)(struct spi_master *master,
					   struct spi_device *spi,
					   struct spi_transfer *xfer);

	/*
	 * These hooks are for drivers that want to use the generic
	 * master transfer queueing mechanism. If these are used, the
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
	bool				busy;
	bool				running;
	bool				rt;
	bool				auto_runtime_pm;
	bool                            cur_msg_prepared;
	bool				cur_msg_mapped;
	struct completion               xfer_completion;
	size_t				max_dma_len;

	int (*prepare_transfer_hardware)(struct spi_master *master);
	int (*transfer_one_message)(struct spi_master *master,
				    struct spi_message *mesg);
	int (*unprepare_transfer_hardware)(struct spi_master *master);
	int (*prepare_message)(struct spi_master *master,
			       struct spi_message *message);
	int (*unprepare_message)(struct spi_master *master,
				 struct spi_message *message);

	/*
	 * These hooks are for drivers that use a generic implementation
	 * of transfer_one_message() provied by the core.
	 */
	void (*set_cs)(struct spi_device *spi, bool enable);
	int (*transfer_one)(struct spi_master *master, struct spi_device *spi,
			    struct spi_transfer *transfer);

	/* gpio chip select */
	int			*cs_gpios;

	/* DMA channels for use with core dmaengine helpers */
	struct dma_chan		*dma_tx;
	struct dma_chan		*dma_rx;

	/* dummy data for full duplex devices */
	void			*dummy_rx;
	void			*dummy_tx;
};

static inline void *spi_master_get_devdata(struct spi_master *master)
{
	return dev_get_drvdata(&master->dev);
}

static inline void spi_master_set_devdata(struct spi_master *master, void *data)
{
	dev_set_drvdata(&master->dev, data);
}

static inline struct spi_master *spi_master_get(struct spi_master *master)
{
	if (!master || !get_device(&master->dev))
		return NULL;
	return master;
}

static inline void spi_master_put(struct spi_master *master)
{
	if (master)
		put_device(&master->dev);
}

/* PM calls that need to be issued by the driver */
extern int spi_master_suspend(struct spi_master *master);
extern int spi_master_resume(struct spi_master *master);

/* Calls the driver make to interact with the message queue */
extern struct spi_message *spi_get_next_queued_message(struct spi_master *master);
extern void spi_finalize_current_message(struct spi_master *master);
extern void spi_finalize_current_transfer(struct spi_master *master);

/* the spi driver core manages memory for the spi_master classdev */
extern struct spi_master *
spi_alloc_master(struct device *host, unsigned size);

extern int spi_register_master(struct spi_master *master);
extern int devm_spi_register_master(struct device *dev,
				    struct spi_master *master);
extern void spi_unregister_master(struct spi_master *master);

extern struct spi_master *spi_busnum_to_master(u16 busnum);

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
 *
 * A @spi_message is used to execute an atomic sequence of data transfers,
 * each represented by a struct spi_transfer.  The sequence is "atomic"
 * in the sense that no other spi_message may use that SPI bus until that
 * sequence completes.  On some systems, many such sequences can execute as
 * as single programmed DMA transfer.  On all systems, these messages are
 * queued, and might complete after transactions to other devices.  Messages
 * sent to a given spi_device are alway executed in FIFO order.
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
	 * complete(), that's the spi_master controller driver.
	 */
	struct list_head	queue;
	void			*state;
};

static inline void spi_message_init(struct spi_message *m)
{
	memset(m, 0, sizeof *m);
	INIT_LIST_HEAD(&m->transfers);
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

		INIT_LIST_HEAD(&m->transfers);
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

/*---------------------------------------------------------------------------*/

/* All these synchronous SPI transfer routines are utilities layered
 * over the core async transfer primitive.  Here, "synchronous" means
 * they will sleep uninterruptibly until the async transfer completes.
 */

extern int spi_sync(struct spi_device *spi, struct spi_message *message);
extern int spi_sync_locked(struct spi_device *spi, struct spi_message *message);
extern int spi_bus_lock(struct spi_master *master);
extern int spi_bus_unlock(struct spi_master *master);

/**
 * spi_write - SPI synchronous write
 * @spi: device to which data will be written
 * @buf: data buffer
 * @len: data buffer size
 * Context: can sleep
 *
 * This writes the buffer and returns zero or a negative error code.
 * Callable only from contexts that can sleep.
 */
static inline int
spi_write(struct spi_device *spi, const void *buf, size_t len)
{
	struct spi_transfer	t = {
			.tx_buf		= buf,
			.len		= len,
		};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(spi, &m);
}

/**
 * spi_read - SPI synchronous read
 * @spi: device from which data will be read
 * @buf: data buffer
 * @len: data buffer size
 * Context: can sleep
 *
 * This reads the buffer and returns zero or a negative error code.
 * Callable only from contexts that can sleep.
 */
static inline int
spi_read(struct spi_device *spi, void *buf, size_t len)
{
	struct spi_transfer	t = {
			.rx_buf		= buf,
			.len		= len,
		};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(spi, &m);
}

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
 * It returns zero on success, else a negative error code.
 */
static inline int
spi_sync_transfer(struct spi_device *spi, struct spi_transfer *xfers,
	unsigned int num_xfers)
{
	struct spi_message msg;

	spi_message_init_with_transfers(&msg, xfers, num_xfers);

	return spi_sync(spi, &msg);
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
 * This returns the (unsigned) eight bit number returned by the
 * device, or else a negative error code.  Callable only from
 * contexts that can sleep.
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
 * This returns the (unsigned) sixteen bit number returned by the
 * device, or else a negative error code.  Callable only from
 * contexts that can sleep.
 *
 * The number is returned in wire-order, which is at least sometimes
 * big-endian.
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
 * This returns the (unsigned) sixteen bit number returned by the device in cpu
 * endianness, or else a negative error code. Callable only from contexts that
 * can sleep.
 *
 * This function is similar to spi_w8r16, with the exception that it will
 * convert the read 16 bit data word from big-endian to native endianness.
 *
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
 * support for nonstatic configurations too; enough to handle adding
 * parport adapters, or microcontrollers acting as USB-to-SPI bridges.
 */

/**
 * struct spi_board_info - board-specific template for a SPI device
 * @modalias: Initializes spi_device.modalias; identifies the driver.
 * @platform_data: Initializes spi_device.platform_data; the particular
 *	data stored there is driver-specific.
 * @controller_data: Initializes spi_device.controller_data; some
 *	controllers need hints about hardware setup, e.g. for DMA.
 * @irq: Initializes spi_device.irq; depends on how the board is wired.
 * @max_speed_hz: Initializes spi_device.max_speed_hz; based on limits
 *	from the chip datasheet and board-specific signal quality issues.
 * @bus_num: Identifies which spi_master parents the spi_device; unused
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
	 * irq is copied too
	 */
	char		modalias[SPI_NAME_SIZE];
	const void	*platform_data;
	void		*controller_data;
	int		irq;

	/* slower signaling on noisy or low voltage boards */
	u32		max_speed_hz;


	/* bus_num is board specific and matches the bus_num of some
	 * spi_master that will probably be registered later.
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
 * normally that would be handled by spi_unregister_master().
 *
 * You can also use spi_alloc_device() and spi_add_device() to use a two
 * stage registration sequence for each spi_device.  This gives the caller
 * some more control over the spi_device structure before it is registered,
 * but requires that caller to initialize fields that would otherwise
 * be defined using the board info.
 */
extern struct spi_device *
spi_alloc_device(struct spi_master *master);

extern int
spi_add_device(struct spi_device *spi);

extern struct spi_device *
spi_new_device(struct spi_master *, struct spi_board_info *);

static inline void
spi_unregister_device(struct spi_device *spi)
{
	if (spi)
		device_unregister(&spi->dev);
}

extern const struct spi_device_id *
spi_get_device_id(const struct spi_device *sdev);

#endif /* __LINUX_SPI_H */
