// SPDX-License-Identifier: GPL-2.0-only
/*
 * Mediated virtual PCI serial host device driver
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
 *
 * Sample driver that creates mdev device that simulates serial port over PCI
 * card.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/vfio.h>
#include <linux/iommu.h>
#include <linux/sysfs.h>
#include <linux/ctype.h>
#include <linux/file.h>
#include <linux/mdev.h>
#include <linux/pci.h>
#include <linux/serial.h>
#include <uapi/linux/serial_reg.h>
#include <linux/eventfd.h>
#include <linux/anon_inodes.h>

/*
 * #defines
 */

#define VERSION_STRING  "0.1"
#define DRIVER_AUTHOR   "NVIDIA Corporation"

#define MTTY_CLASS_NAME "mtty"

#define MTTY_NAME       "mtty"

#define MTTY_STRING_LEN		16

#define MTTY_CONFIG_SPACE_SIZE  0xff
#define MTTY_IO_BAR_SIZE        0x8
#define MTTY_MMIO_BAR_SIZE      0x100000

#define STORE_LE16(addr, val)   (*(u16 *)addr = val)
#define STORE_LE32(addr, val)   (*(u32 *)addr = val)

#define MAX_FIFO_SIZE   16

#define CIRCULAR_BUF_INC_IDX(idx)    (idx = (idx + 1) & (MAX_FIFO_SIZE - 1))

#define MTTY_VFIO_PCI_OFFSET_SHIFT   40

#define MTTY_VFIO_PCI_OFFSET_TO_INDEX(off)   (off >> MTTY_VFIO_PCI_OFFSET_SHIFT)
#define MTTY_VFIO_PCI_INDEX_TO_OFFSET(index) \
				((u64)(index) << MTTY_VFIO_PCI_OFFSET_SHIFT)
#define MTTY_VFIO_PCI_OFFSET_MASK    \
				(((u64)(1) << MTTY_VFIO_PCI_OFFSET_SHIFT) - 1)
#define MAX_MTTYS	24

/*
 * Global Structures
 */

static struct mtty_dev {
	dev_t		vd_devt;
	struct class	*vd_class;
	struct cdev	vd_cdev;
	struct idr	vd_idr;
	struct device	dev;
	struct mdev_parent parent;
} mtty_dev;

struct mdev_region_info {
	u64 start;
	u64 phys_start;
	u32 size;
	u64 vfio_offset;
};

#if defined(DEBUG_REGS)
static const char *wr_reg[] = {
	"TX",
	"IER",
	"FCR",
	"LCR",
	"MCR",
	"LSR",
	"MSR",
	"SCR"
};

static const char *rd_reg[] = {
	"RX",
	"IER",
	"IIR",
	"LCR",
	"MCR",
	"LSR",
	"MSR",
	"SCR"
};
#endif

/* loop back buffer */
struct rxtx {
	u8 fifo[MAX_FIFO_SIZE];
	u8 head, tail;
	u8 count;
};

struct serial_port {
	u8 uart_reg[8];         /* 8 registers */
	struct rxtx rxtx;       /* loop back buffer */
	bool dlab;
	bool overrun;
	u16 divisor;
	u8 fcr;                 /* FIFO control register */
	u8 max_fifo_size;
	u8 intr_trigger_level;  /* interrupt trigger level */
};

struct mtty_data {
	u64 magic;
#define MTTY_MAGIC 0x7e9d09898c3e2c4e /* Nothing clever, just random */
	u32 major_ver;
#define MTTY_MAJOR_VER 1
	u32 minor_ver;
#define MTTY_MINOR_VER 0
	u32 nr_ports;
	u32 flags;
	struct serial_port ports[2];
};

struct mdev_state;

struct mtty_migration_file {
	struct file *filp;
	struct mutex lock;
	struct mdev_state *mdev_state;
	struct mtty_data data;
	ssize_t filled_size;
	u8 disabled:1;
};

/* State of each mdev device */
struct mdev_state {
	struct vfio_device vdev;
	struct eventfd_ctx *intx_evtfd;
	struct eventfd_ctx *msi_evtfd;
	int irq_index;
	u8 *vconfig;
	struct mutex ops_lock;
	struct mdev_device *mdev;
	struct mdev_region_info region_info[VFIO_PCI_NUM_REGIONS];
	u32 bar_mask[VFIO_PCI_NUM_REGIONS];
	struct list_head next;
	struct serial_port s[2];
	struct mutex rxtx_lock;
	struct vfio_device_info dev_info;
	int nr_ports;
	enum vfio_device_mig_state state;
	struct mutex state_mutex;
	struct mutex reset_mutex;
	struct mtty_migration_file *saving_migf;
	struct mtty_migration_file *resuming_migf;
	u8 deferred_reset:1;
	u8 intx_mask:1;
};

static struct mtty_type {
	struct mdev_type type;
	int nr_ports;
} mtty_types[2] = {
	{ .nr_ports = 1, .type.sysfs_name = "1",
	  .type.pretty_name = "Single port serial" },
	{ .nr_ports = 2, .type.sysfs_name = "2",
	  .type.pretty_name = "Dual port serial" },
};

static struct mdev_type *mtty_mdev_types[] = {
	&mtty_types[0].type,
	&mtty_types[1].type,
};

static atomic_t mdev_avail_ports = ATOMIC_INIT(MAX_MTTYS);

static const struct file_operations vd_fops = {
	.owner          = THIS_MODULE,
};

static const struct vfio_device_ops mtty_dev_ops;

/* Helper functions */

static void dump_buffer(u8 *buf, uint32_t count)
{
#if defined(DEBUG)
	int i;

	pr_info("Buffer:\n");
	for (i = 0; i < count; i++) {
		pr_info("%2x ", *(buf + i));
		if ((i + 1) % 16 == 0)
			pr_info("\n");
	}
#endif
}

static bool is_intx(struct mdev_state *mdev_state)
{
	return mdev_state->irq_index == VFIO_PCI_INTX_IRQ_INDEX;
}

static bool is_msi(struct mdev_state *mdev_state)
{
	return mdev_state->irq_index == VFIO_PCI_MSI_IRQ_INDEX;
}

static bool is_noirq(struct mdev_state *mdev_state)
{
	return !is_intx(mdev_state) && !is_msi(mdev_state);
}

static void mtty_trigger_interrupt(struct mdev_state *mdev_state)
{
	lockdep_assert_held(&mdev_state->ops_lock);

	if (is_msi(mdev_state)) {
		if (mdev_state->msi_evtfd)
			eventfd_signal(mdev_state->msi_evtfd);
	} else if (is_intx(mdev_state)) {
		if (mdev_state->intx_evtfd && !mdev_state->intx_mask) {
			eventfd_signal(mdev_state->intx_evtfd);
			mdev_state->intx_mask = true;
		}
	}
}

static void mtty_create_config_space(struct mdev_state *mdev_state)
{
	/* PCI dev ID */
	STORE_LE32((u32 *) &mdev_state->vconfig[0x0], 0x32534348);

	/* Control: I/O+, Mem-, BusMaster- */
	STORE_LE16((u16 *) &mdev_state->vconfig[0x4], 0x0001);

	/* Status: capabilities list absent */
	STORE_LE16((u16 *) &mdev_state->vconfig[0x6], 0x0200);

	/* Rev ID */
	mdev_state->vconfig[0x8] =  0x10;

	/* programming interface class : 16550-compatible serial controller */
	mdev_state->vconfig[0x9] =  0x02;

	/* Sub class : 00 */
	mdev_state->vconfig[0xa] =  0x00;

	/* Base class : Simple Communication controllers */
	mdev_state->vconfig[0xb] =  0x07;

	/* base address registers */
	/* BAR0: IO space */
	STORE_LE32((u32 *) &mdev_state->vconfig[0x10], 0x000001);
	mdev_state->bar_mask[0] = ~(MTTY_IO_BAR_SIZE) + 1;

	if (mdev_state->nr_ports == 2) {
		/* BAR1: IO space */
		STORE_LE32((u32 *) &mdev_state->vconfig[0x14], 0x000001);
		mdev_state->bar_mask[1] = ~(MTTY_IO_BAR_SIZE) + 1;
	}

	/* Subsystem ID */
	STORE_LE32((u32 *) &mdev_state->vconfig[0x2c], 0x32534348);

	mdev_state->vconfig[0x34] =  0x00;   /* Cap Ptr */
	mdev_state->vconfig[0x3d] =  0x01;   /* interrupt pin (INTA#) */

	/* Vendor specific data */
	mdev_state->vconfig[0x40] =  0x23;
	mdev_state->vconfig[0x43] =  0x80;
	mdev_state->vconfig[0x44] =  0x23;
	mdev_state->vconfig[0x48] =  0x23;
	mdev_state->vconfig[0x4c] =  0x23;

	mdev_state->vconfig[0x60] =  0x50;
	mdev_state->vconfig[0x61] =  0x43;
	mdev_state->vconfig[0x62] =  0x49;
	mdev_state->vconfig[0x63] =  0x20;
	mdev_state->vconfig[0x64] =  0x53;
	mdev_state->vconfig[0x65] =  0x65;
	mdev_state->vconfig[0x66] =  0x72;
	mdev_state->vconfig[0x67] =  0x69;
	mdev_state->vconfig[0x68] =  0x61;
	mdev_state->vconfig[0x69] =  0x6c;
	mdev_state->vconfig[0x6a] =  0x2f;
	mdev_state->vconfig[0x6b] =  0x55;
	mdev_state->vconfig[0x6c] =  0x41;
	mdev_state->vconfig[0x6d] =  0x52;
	mdev_state->vconfig[0x6e] =  0x54;
}

static void handle_pci_cfg_write(struct mdev_state *mdev_state, u16 offset,
				 u8 *buf, u32 count)
{
	u32 cfg_addr, bar_mask, bar_index = 0;

	switch (offset) {
	case 0x04: /* device control */
	case 0x06: /* device status */
		/* do nothing */
		break;
	case 0x3c:  /* interrupt line */
		mdev_state->vconfig[0x3c] = buf[0];
		break;
	case 0x3d:
		/*
		 * Interrupt Pin is hardwired to INTA.
		 * This field is write protected by hardware
		 */
		break;
	case 0x10:  /* BAR0 */
	case 0x14:  /* BAR1 */
		if (offset == 0x10)
			bar_index = 0;
		else if (offset == 0x14)
			bar_index = 1;

		if ((mdev_state->nr_ports == 1) && (bar_index == 1)) {
			STORE_LE32(&mdev_state->vconfig[offset], 0);
			break;
		}

		cfg_addr = *(u32 *)buf;
		pr_info("BAR%d addr 0x%x\n", bar_index, cfg_addr);

		if (cfg_addr == 0xffffffff) {
			bar_mask = mdev_state->bar_mask[bar_index];
			cfg_addr = (cfg_addr & bar_mask);
		}

		cfg_addr |= (mdev_state->vconfig[offset] & 0x3ul);
		STORE_LE32(&mdev_state->vconfig[offset], cfg_addr);
		break;
	case 0x18:  /* BAR2 */
	case 0x1c:  /* BAR3 */
	case 0x20:  /* BAR4 */
		STORE_LE32(&mdev_state->vconfig[offset], 0);
		break;
	default:
		pr_info("PCI config write @0x%x of %d bytes not handled\n",
			offset, count);
		break;
	}
}

static void handle_bar_write(unsigned int index, struct mdev_state *mdev_state,
				u16 offset, u8 *buf, u32 count)
{
	u8 data = *buf;

	/* Handle data written by guest */
	switch (offset) {
	case UART_TX:
		/* if DLAB set, data is LSB of divisor */
		if (mdev_state->s[index].dlab) {
			mdev_state->s[index].divisor |= data;
			break;
		}

		mutex_lock(&mdev_state->rxtx_lock);

		/* save in TX buffer */
		if (mdev_state->s[index].rxtx.count <
				mdev_state->s[index].max_fifo_size) {
			mdev_state->s[index].rxtx.fifo[
					mdev_state->s[index].rxtx.head] = data;
			mdev_state->s[index].rxtx.count++;
			CIRCULAR_BUF_INC_IDX(mdev_state->s[index].rxtx.head);
			mdev_state->s[index].overrun = false;

			/*
			 * Trigger interrupt if receive data interrupt is
			 * enabled and fifo reached trigger level
			 */
			if ((mdev_state->s[index].uart_reg[UART_IER] &
						UART_IER_RDI) &&
			   (mdev_state->s[index].rxtx.count ==
				    mdev_state->s[index].intr_trigger_level)) {
				/* trigger interrupt */
#if defined(DEBUG_INTR)
				pr_err("Serial port %d: Fifo level trigger\n",
					index);
#endif
				mtty_trigger_interrupt(mdev_state);
			}
		} else {
#if defined(DEBUG_INTR)
			pr_err("Serial port %d: Buffer Overflow\n", index);
#endif
			mdev_state->s[index].overrun = true;

			/*
			 * Trigger interrupt if receiver line status interrupt
			 * is enabled
			 */
			if (mdev_state->s[index].uart_reg[UART_IER] &
								UART_IER_RLSI)
				mtty_trigger_interrupt(mdev_state);
		}
		mutex_unlock(&mdev_state->rxtx_lock);
		break;

	case UART_IER:
		/* if DLAB set, data is MSB of divisor */
		if (mdev_state->s[index].dlab)
			mdev_state->s[index].divisor |= (u16)data << 8;
		else {
			mdev_state->s[index].uart_reg[offset] = data;
			mutex_lock(&mdev_state->rxtx_lock);
			if ((data & UART_IER_THRI) &&
			    (mdev_state->s[index].rxtx.head ==
					mdev_state->s[index].rxtx.tail)) {
#if defined(DEBUG_INTR)
				pr_err("Serial port %d: IER_THRI write\n",
					index);
#endif
				mtty_trigger_interrupt(mdev_state);
			}

			mutex_unlock(&mdev_state->rxtx_lock);
		}

		break;

	case UART_FCR:
		mdev_state->s[index].fcr = data;

		mutex_lock(&mdev_state->rxtx_lock);
		if (data & (UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT)) {
			/* clear loop back FIFO */
			mdev_state->s[index].rxtx.count = 0;
			mdev_state->s[index].rxtx.head = 0;
			mdev_state->s[index].rxtx.tail = 0;
		}
		mutex_unlock(&mdev_state->rxtx_lock);

		switch (data & UART_FCR_TRIGGER_MASK) {
		case UART_FCR_TRIGGER_1:
			mdev_state->s[index].intr_trigger_level = 1;
			break;

		case UART_FCR_TRIGGER_4:
			mdev_state->s[index].intr_trigger_level = 4;
			break;

		case UART_FCR_TRIGGER_8:
			mdev_state->s[index].intr_trigger_level = 8;
			break;

		case UART_FCR_TRIGGER_14:
			mdev_state->s[index].intr_trigger_level = 14;
			break;
		}

		/*
		 * Set trigger level to 1 otherwise or  implement timer with
		 * timeout of 4 characters and on expiring that timer set
		 * Recevice data timeout in IIR register
		 */
		mdev_state->s[index].intr_trigger_level = 1;
		if (data & UART_FCR_ENABLE_FIFO)
			mdev_state->s[index].max_fifo_size = MAX_FIFO_SIZE;
		else {
			mdev_state->s[index].max_fifo_size = 1;
			mdev_state->s[index].intr_trigger_level = 1;
		}

		break;

	case UART_LCR:
		if (data & UART_LCR_DLAB) {
			mdev_state->s[index].dlab = true;
			mdev_state->s[index].divisor = 0;
		} else
			mdev_state->s[index].dlab = false;

		mdev_state->s[index].uart_reg[offset] = data;
		break;

	case UART_MCR:
		mdev_state->s[index].uart_reg[offset] = data;

		if ((mdev_state->s[index].uart_reg[UART_IER] & UART_IER_MSI) &&
				(data & UART_MCR_OUT2)) {
#if defined(DEBUG_INTR)
			pr_err("Serial port %d: MCR_OUT2 write\n", index);
#endif
			mtty_trigger_interrupt(mdev_state);
		}

		if ((mdev_state->s[index].uart_reg[UART_IER] & UART_IER_MSI) &&
				(data & (UART_MCR_RTS | UART_MCR_DTR))) {
#if defined(DEBUG_INTR)
			pr_err("Serial port %d: MCR RTS/DTR write\n", index);
#endif
			mtty_trigger_interrupt(mdev_state);
		}
		break;

	case UART_LSR:
	case UART_MSR:
		/* do nothing */
		break;

	case UART_SCR:
		mdev_state->s[index].uart_reg[offset] = data;
		break;

	default:
		break;
	}
}

static void handle_bar_read(unsigned int index, struct mdev_state *mdev_state,
			    u16 offset, u8 *buf, u32 count)
{
	/* Handle read requests by guest */
	switch (offset) {
	case UART_RX:
		/* if DLAB set, data is LSB of divisor */
		if (mdev_state->s[index].dlab) {
			*buf  = (u8)mdev_state->s[index].divisor;
			break;
		}

		mutex_lock(&mdev_state->rxtx_lock);
		/* return data in tx buffer */
		if (mdev_state->s[index].rxtx.head !=
				 mdev_state->s[index].rxtx.tail) {
			*buf = mdev_state->s[index].rxtx.fifo[
						mdev_state->s[index].rxtx.tail];
			mdev_state->s[index].rxtx.count--;
			CIRCULAR_BUF_INC_IDX(mdev_state->s[index].rxtx.tail);
		}

		if (mdev_state->s[index].rxtx.head ==
				mdev_state->s[index].rxtx.tail) {
		/*
		 *  Trigger interrupt if tx buffer empty interrupt is
		 *  enabled and fifo is empty
		 */
#if defined(DEBUG_INTR)
			pr_err("Serial port %d: Buffer Empty\n", index);
#endif
			if (mdev_state->s[index].uart_reg[UART_IER] &
							 UART_IER_THRI)
				mtty_trigger_interrupt(mdev_state);
		}
		mutex_unlock(&mdev_state->rxtx_lock);

		break;

	case UART_IER:
		if (mdev_state->s[index].dlab) {
			*buf = (u8)(mdev_state->s[index].divisor >> 8);
			break;
		}
		*buf = mdev_state->s[index].uart_reg[offset] & 0x0f;
		break;

	case UART_IIR:
	{
		u8 ier = mdev_state->s[index].uart_reg[UART_IER];
		*buf = 0;

		mutex_lock(&mdev_state->rxtx_lock);
		/* Interrupt priority 1: Parity, overrun, framing or break */
		if ((ier & UART_IER_RLSI) && mdev_state->s[index].overrun)
			*buf |= UART_IIR_RLSI;

		/* Interrupt priority 2: Fifo trigger level reached */
		if ((ier & UART_IER_RDI) &&
		    (mdev_state->s[index].rxtx.count >=
		      mdev_state->s[index].intr_trigger_level))
			*buf |= UART_IIR_RDI;

		/* Interrupt priotiry 3: transmitter holding register empty */
		if ((ier & UART_IER_THRI) &&
		    (mdev_state->s[index].rxtx.head ==
				mdev_state->s[index].rxtx.tail))
			*buf |= UART_IIR_THRI;

		/* Interrupt priotiry 4: Modem status: CTS, DSR, RI or DCD  */
		if ((ier & UART_IER_MSI) &&
		    (mdev_state->s[index].uart_reg[UART_MCR] &
				 (UART_MCR_RTS | UART_MCR_DTR)))
			*buf |= UART_IIR_MSI;

		/* bit0: 0=> interrupt pending, 1=> no interrupt is pending */
		if (*buf == 0)
			*buf = UART_IIR_NO_INT;

		/* set bit 6 & 7 to be 16550 compatible */
		*buf |= 0xC0;
		mutex_unlock(&mdev_state->rxtx_lock);
	}
	break;

	case UART_LCR:
	case UART_MCR:
		*buf = mdev_state->s[index].uart_reg[offset];
		break;

	case UART_LSR:
	{
		u8 lsr = 0;

		mutex_lock(&mdev_state->rxtx_lock);
		/* atleast one char in FIFO */
		if (mdev_state->s[index].rxtx.head !=
				 mdev_state->s[index].rxtx.tail)
			lsr |= UART_LSR_DR;

		/* if FIFO overrun */
		if (mdev_state->s[index].overrun)
			lsr |= UART_LSR_OE;

		/* transmit FIFO empty and tramsitter empty */
		if (mdev_state->s[index].rxtx.head ==
				 mdev_state->s[index].rxtx.tail)
			lsr |= UART_LSR_TEMT | UART_LSR_THRE;

		mutex_unlock(&mdev_state->rxtx_lock);
		*buf = lsr;
		break;
	}
	case UART_MSR:
		*buf = UART_MSR_DSR | UART_MSR_DDSR | UART_MSR_DCD;

		mutex_lock(&mdev_state->rxtx_lock);
		/* if AFE is 1 and FIFO have space, set CTS bit */
		if (mdev_state->s[index].uart_reg[UART_MCR] &
						 UART_MCR_AFE) {
			if (mdev_state->s[index].rxtx.count <
					mdev_state->s[index].max_fifo_size)
				*buf |= UART_MSR_CTS | UART_MSR_DCTS;
		} else
			*buf |= UART_MSR_CTS | UART_MSR_DCTS;
		mutex_unlock(&mdev_state->rxtx_lock);

		break;

	case UART_SCR:
		*buf = mdev_state->s[index].uart_reg[offset];
		break;

	default:
		break;
	}
}

static void mdev_read_base(struct mdev_state *mdev_state)
{
	int index, pos;
	u32 start_lo, start_hi;
	u32 mem_type;

	pos = PCI_BASE_ADDRESS_0;

	for (index = 0; index <= VFIO_PCI_BAR5_REGION_INDEX; index++) {

		if (!mdev_state->region_info[index].size)
			continue;

		start_lo = (*(u32 *)(mdev_state->vconfig + pos)) &
			PCI_BASE_ADDRESS_MEM_MASK;
		mem_type = (*(u32 *)(mdev_state->vconfig + pos)) &
			PCI_BASE_ADDRESS_MEM_TYPE_MASK;

		switch (mem_type) {
		case PCI_BASE_ADDRESS_MEM_TYPE_64:
			start_hi = (*(u32 *)(mdev_state->vconfig + pos + 4));
			pos += 4;
			break;
		case PCI_BASE_ADDRESS_MEM_TYPE_32:
		case PCI_BASE_ADDRESS_MEM_TYPE_1M:
			/* 1M mem BAR treated as 32-bit BAR */
		default:
			/* mem unknown type treated as 32-bit BAR */
			start_hi = 0;
			break;
		}
		pos += 4;
		mdev_state->region_info[index].start = ((u64)start_hi << 32) |
							start_lo;
	}
}

static ssize_t mdev_access(struct mdev_state *mdev_state, u8 *buf, size_t count,
			   loff_t pos, bool is_write)
{
	unsigned int index;
	loff_t offset;
	int ret = 0;

	if (!buf)
		return -EINVAL;

	mutex_lock(&mdev_state->ops_lock);

	index = MTTY_VFIO_PCI_OFFSET_TO_INDEX(pos);
	offset = pos & MTTY_VFIO_PCI_OFFSET_MASK;
	switch (index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:

#if defined(DEBUG)
		pr_info("%s: PCI config space %s at offset 0x%llx\n",
			 __func__, is_write ? "write" : "read", offset);
#endif
		if (is_write) {
			dump_buffer(buf, count);
			handle_pci_cfg_write(mdev_state, offset, buf, count);
		} else {
			memcpy(buf, (mdev_state->vconfig + offset), count);
			dump_buffer(buf, count);
		}

		break;

	case VFIO_PCI_BAR0_REGION_INDEX ... VFIO_PCI_BAR5_REGION_INDEX:
		if (!mdev_state->region_info[index].start)
			mdev_read_base(mdev_state);

		if (is_write) {
			dump_buffer(buf, count);

#if defined(DEBUG_REGS)
			pr_info("%s: BAR%d  WR @0x%llx %s val:0x%02x dlab:%d\n",
				__func__, index, offset, wr_reg[offset],
				*buf, mdev_state->s[index].dlab);
#endif
			handle_bar_write(index, mdev_state, offset, buf, count);
		} else {
			handle_bar_read(index, mdev_state, offset, buf, count);
			dump_buffer(buf, count);

#if defined(DEBUG_REGS)
			pr_info("%s: BAR%d  RD @0x%llx %s val:0x%02x dlab:%d\n",
				__func__, index, offset, rd_reg[offset],
				*buf, mdev_state->s[index].dlab);
#endif
		}
		break;

	default:
		ret = -1;
		goto accessfailed;
	}

	ret = count;


accessfailed:
	mutex_unlock(&mdev_state->ops_lock);

	return ret;
}

static size_t mtty_data_size(struct mdev_state *mdev_state)
{
	return offsetof(struct mtty_data, ports) +
		(mdev_state->nr_ports * sizeof(struct serial_port));
}

static void mtty_disable_file(struct mtty_migration_file *migf)
{
	mutex_lock(&migf->lock);
	migf->disabled = true;
	migf->filled_size = 0;
	migf->filp->f_pos = 0;
	mutex_unlock(&migf->lock);
}

static void mtty_disable_files(struct mdev_state *mdev_state)
{
	if (mdev_state->saving_migf) {
		mtty_disable_file(mdev_state->saving_migf);
		fput(mdev_state->saving_migf->filp);
		mdev_state->saving_migf = NULL;
	}

	if (mdev_state->resuming_migf) {
		mtty_disable_file(mdev_state->resuming_migf);
		fput(mdev_state->resuming_migf->filp);
		mdev_state->resuming_migf = NULL;
	}
}

static void mtty_state_mutex_unlock(struct mdev_state *mdev_state)
{
again:
	mutex_lock(&mdev_state->reset_mutex);
	if (mdev_state->deferred_reset) {
		mdev_state->deferred_reset = false;
		mutex_unlock(&mdev_state->reset_mutex);
		mdev_state->state = VFIO_DEVICE_STATE_RUNNING;
		mtty_disable_files(mdev_state);
		goto again;
	}
	mutex_unlock(&mdev_state->state_mutex);
	mutex_unlock(&mdev_state->reset_mutex);
}

static int mtty_release_migf(struct inode *inode, struct file *filp)
{
	struct mtty_migration_file *migf = filp->private_data;

	mtty_disable_file(migf);
	mutex_destroy(&migf->lock);
	kfree(migf);

	return 0;
}

static long mtty_precopy_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct mtty_migration_file *migf = filp->private_data;
	struct mdev_state *mdev_state = migf->mdev_state;
	loff_t *pos = &filp->f_pos;
	struct vfio_precopy_info info = {};
	unsigned long minsz;
	int ret;

	if (cmd != VFIO_MIG_GET_PRECOPY_INFO)
		return -ENOTTY;

	minsz = offsetofend(struct vfio_precopy_info, dirty_bytes);

	if (copy_from_user(&info, (void __user *)arg, minsz))
		return -EFAULT;
	if (info.argsz < minsz)
		return -EINVAL;

	mutex_lock(&mdev_state->state_mutex);
	if (mdev_state->state != VFIO_DEVICE_STATE_PRE_COPY &&
	    mdev_state->state != VFIO_DEVICE_STATE_PRE_COPY_P2P) {
		ret = -EINVAL;
		goto unlock;
	}

	mutex_lock(&migf->lock);

	if (migf->disabled) {
		mutex_unlock(&migf->lock);
		ret = -ENODEV;
		goto unlock;
	}

	if (*pos > migf->filled_size) {
		mutex_unlock(&migf->lock);
		ret = -EINVAL;
		goto unlock;
	}

	info.dirty_bytes = 0;
	info.initial_bytes = migf->filled_size - *pos;
	mutex_unlock(&migf->lock);

	ret = copy_to_user((void __user *)arg, &info, minsz) ? -EFAULT : 0;
unlock:
	mtty_state_mutex_unlock(mdev_state);
	return ret;
}

static ssize_t mtty_save_read(struct file *filp, char __user *buf,
			      size_t len, loff_t *pos)
{
	struct mtty_migration_file *migf = filp->private_data;
	ssize_t ret = 0;

	if (pos)
		return -ESPIPE;

	pos = &filp->f_pos;

	mutex_lock(&migf->lock);

	dev_dbg(migf->mdev_state->vdev.dev, "%s ask %zu\n", __func__, len);

	if (migf->disabled) {
		ret = -ENODEV;
		goto out_unlock;
	}

	if (*pos > migf->filled_size) {
		ret = -EINVAL;
		goto out_unlock;
	}

	len = min_t(size_t, migf->filled_size - *pos, len);
	if (len) {
		if (copy_to_user(buf, (void *)&migf->data + *pos, len)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		*pos += len;
		ret = len;
	}
out_unlock:
	dev_dbg(migf->mdev_state->vdev.dev, "%s read %zu\n", __func__, ret);
	mutex_unlock(&migf->lock);
	return ret;
}

static const struct file_operations mtty_save_fops = {
	.owner = THIS_MODULE,
	.read = mtty_save_read,
	.unlocked_ioctl = mtty_precopy_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.release = mtty_release_migf,
	.llseek = no_llseek,
};

static void mtty_save_state(struct mdev_state *mdev_state)
{
	struct mtty_migration_file *migf = mdev_state->saving_migf;
	int i;

	mutex_lock(&migf->lock);
	for (i = 0; i < mdev_state->nr_ports; i++) {
		memcpy(&migf->data.ports[i],
			&mdev_state->s[i], sizeof(struct serial_port));
		migf->filled_size += sizeof(struct serial_port);
	}
	dev_dbg(mdev_state->vdev.dev,
		"%s filled to %zu\n", __func__, migf->filled_size);
	mutex_unlock(&migf->lock);
}

static int mtty_load_state(struct mdev_state *mdev_state)
{
	struct mtty_migration_file *migf = mdev_state->resuming_migf;
	int i;

	mutex_lock(&migf->lock);
	/* magic and version already tested by resume write fn */
	if (migf->filled_size < mtty_data_size(mdev_state)) {
		dev_dbg(mdev_state->vdev.dev, "%s expected %zu, got %zu\n",
			__func__, mtty_data_size(mdev_state),
			migf->filled_size);
		mutex_unlock(&migf->lock);
		return -EINVAL;
	}

	for (i = 0; i < mdev_state->nr_ports; i++)
		memcpy(&mdev_state->s[i],
		       &migf->data.ports[i], sizeof(struct serial_port));

	mutex_unlock(&migf->lock);
	return 0;
}

static struct mtty_migration_file *
mtty_save_device_data(struct mdev_state *mdev_state,
		      enum vfio_device_mig_state state)
{
	struct mtty_migration_file *migf = mdev_state->saving_migf;
	struct mtty_migration_file *ret = NULL;

	if (migf) {
		if (state == VFIO_DEVICE_STATE_STOP_COPY)
			goto fill_data;
		return ret;
	}

	migf = kzalloc(sizeof(*migf), GFP_KERNEL_ACCOUNT);
	if (!migf)
		return ERR_PTR(-ENOMEM);

	migf->filp = anon_inode_getfile("mtty_mig", &mtty_save_fops,
					migf, O_RDONLY);
	if (IS_ERR(migf->filp)) {
		int rc = PTR_ERR(migf->filp);

		kfree(migf);
		return ERR_PTR(rc);
	}

	stream_open(migf->filp->f_inode, migf->filp);
	mutex_init(&migf->lock);
	migf->mdev_state = mdev_state;

	migf->data.magic = MTTY_MAGIC;
	migf->data.major_ver = MTTY_MAJOR_VER;
	migf->data.minor_ver = MTTY_MINOR_VER;
	migf->data.nr_ports = mdev_state->nr_ports;

	migf->filled_size = offsetof(struct mtty_data, ports);

	dev_dbg(mdev_state->vdev.dev, "%s filled header to %zu\n",
		__func__, migf->filled_size);

	ret = mdev_state->saving_migf = migf;

fill_data:
	if (state == VFIO_DEVICE_STATE_STOP_COPY)
		mtty_save_state(mdev_state);

	return ret;
}

static ssize_t mtty_resume_write(struct file *filp, const char __user *buf,
				 size_t len, loff_t *pos)
{
	struct mtty_migration_file *migf = filp->private_data;
	struct mdev_state *mdev_state = migf->mdev_state;
	loff_t requested_length;
	ssize_t ret = 0;

	if (pos)
		return -ESPIPE;

	pos = &filp->f_pos;

	if (*pos < 0 ||
	    check_add_overflow((loff_t)len, *pos, &requested_length))
		return -EINVAL;

	if (requested_length > mtty_data_size(mdev_state))
		return -ENOMEM;

	mutex_lock(&migf->lock);

	if (migf->disabled) {
		ret = -ENODEV;
		goto out_unlock;
	}

	if (copy_from_user((void *)&migf->data + *pos, buf, len)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	*pos += len;
	ret = len;

	dev_dbg(migf->mdev_state->vdev.dev, "%s received %zu, total %zu\n",
		__func__, len, migf->filled_size + len);

	if (migf->filled_size < offsetof(struct mtty_data, ports) &&
	    migf->filled_size + len >= offsetof(struct mtty_data, ports)) {
		if (migf->data.magic != MTTY_MAGIC || migf->data.flags ||
		    migf->data.major_ver != MTTY_MAJOR_VER ||
		    migf->data.minor_ver != MTTY_MINOR_VER ||
		    migf->data.nr_ports != mdev_state->nr_ports) {
			dev_dbg(migf->mdev_state->vdev.dev,
				"%s failed validation\n", __func__);
			ret = -EFAULT;
		} else {
			dev_dbg(migf->mdev_state->vdev.dev,
				"%s header validated\n", __func__);
		}
	}

	migf->filled_size += len;

out_unlock:
	mutex_unlock(&migf->lock);
	return ret;
}

static const struct file_operations mtty_resume_fops = {
	.owner = THIS_MODULE,
	.write = mtty_resume_write,
	.release = mtty_release_migf,
	.llseek = no_llseek,
};

static struct mtty_migration_file *
mtty_resume_device_data(struct mdev_state *mdev_state)
{
	struct mtty_migration_file *migf;
	int ret;

	migf = kzalloc(sizeof(*migf), GFP_KERNEL_ACCOUNT);
	if (!migf)
		return ERR_PTR(-ENOMEM);

	migf->filp = anon_inode_getfile("mtty_mig", &mtty_resume_fops,
					migf, O_WRONLY);
	if (IS_ERR(migf->filp)) {
		ret = PTR_ERR(migf->filp);
		kfree(migf);
		return ERR_PTR(ret);
	}

	stream_open(migf->filp->f_inode, migf->filp);
	mutex_init(&migf->lock);
	migf->mdev_state = mdev_state;

	mdev_state->resuming_migf = migf;

	return migf;
}

static struct file *mtty_step_state(struct mdev_state *mdev_state,
				     enum vfio_device_mig_state new)
{
	enum vfio_device_mig_state cur = mdev_state->state;

	dev_dbg(mdev_state->vdev.dev, "%s: %d -> %d\n", __func__, cur, new);

	/*
	 * The following state transitions are no-op considering
	 * mtty does not do DMA nor require any explicit start/stop.
	 *
	 *         RUNNING -> RUNNING_P2P
	 *         RUNNING_P2P -> RUNNING
	 *         RUNNING_P2P -> STOP
	 *         PRE_COPY -> PRE_COPY_P2P
	 *         PRE_COPY_P2P -> PRE_COPY
	 *         STOP -> RUNNING_P2P
	 */
	if ((cur == VFIO_DEVICE_STATE_RUNNING &&
	     new == VFIO_DEVICE_STATE_RUNNING_P2P) ||
	    (cur == VFIO_DEVICE_STATE_RUNNING_P2P &&
	     (new == VFIO_DEVICE_STATE_RUNNING ||
	      new == VFIO_DEVICE_STATE_STOP)) ||
	    (cur == VFIO_DEVICE_STATE_PRE_COPY &&
	     new == VFIO_DEVICE_STATE_PRE_COPY_P2P) ||
	    (cur == VFIO_DEVICE_STATE_PRE_COPY_P2P &&
	     new == VFIO_DEVICE_STATE_PRE_COPY) ||
	    (cur == VFIO_DEVICE_STATE_STOP &&
	     new == VFIO_DEVICE_STATE_RUNNING_P2P))
		return NULL;

	/*
	 * The following state transitions simply close migration files,
	 * with the exception of RESUMING -> STOP, which needs to load
	 * the state first.
	 *
	 *         RESUMING -> STOP
	 *         PRE_COPY -> RUNNING
	 *         PRE_COPY_P2P -> RUNNING_P2P
	 *         STOP_COPY -> STOP
	 */
	if (cur == VFIO_DEVICE_STATE_RESUMING &&
	    new == VFIO_DEVICE_STATE_STOP) {
		int ret;

		ret = mtty_load_state(mdev_state);
		if (ret)
			return ERR_PTR(ret);
		mtty_disable_files(mdev_state);
		return NULL;
	}

	if ((cur == VFIO_DEVICE_STATE_PRE_COPY &&
	     new == VFIO_DEVICE_STATE_RUNNING) ||
	    (cur == VFIO_DEVICE_STATE_PRE_COPY_P2P &&
	     new == VFIO_DEVICE_STATE_RUNNING_P2P) ||
	    (cur == VFIO_DEVICE_STATE_STOP_COPY &&
	     new == VFIO_DEVICE_STATE_STOP)) {
		mtty_disable_files(mdev_state);
		return NULL;
	}

	/*
	 * The following state transitions return migration files.
	 *
	 *         RUNNING -> PRE_COPY
	 *         RUNNING_P2P -> PRE_COPY_P2P
	 *         STOP -> STOP_COPY
	 *         STOP -> RESUMING
	 *         PRE_COPY_P2P -> STOP_COPY
	 */
	if ((cur == VFIO_DEVICE_STATE_RUNNING &&
	     new == VFIO_DEVICE_STATE_PRE_COPY) ||
	    (cur == VFIO_DEVICE_STATE_RUNNING_P2P &&
	     new == VFIO_DEVICE_STATE_PRE_COPY_P2P) ||
	    (cur == VFIO_DEVICE_STATE_STOP &&
	     new == VFIO_DEVICE_STATE_STOP_COPY) ||
	    (cur == VFIO_DEVICE_STATE_PRE_COPY_P2P &&
	     new == VFIO_DEVICE_STATE_STOP_COPY)) {
		struct mtty_migration_file *migf;

		migf = mtty_save_device_data(mdev_state, new);
		if (IS_ERR(migf))
			return ERR_CAST(migf);

		if (migf) {
			get_file(migf->filp);

			return migf->filp;
		}
		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_STOP &&
	    new == VFIO_DEVICE_STATE_RESUMING) {
		struct mtty_migration_file *migf;

		migf = mtty_resume_device_data(mdev_state);
		if (IS_ERR(migf))
			return ERR_CAST(migf);

		get_file(migf->filp);

		return migf->filp;
	}

	/* vfio_mig_get_next_state() does not use arcs other than the above */
	WARN_ON(true);
	return ERR_PTR(-EINVAL);
}

static struct file *mtty_set_state(struct vfio_device *vdev,
				   enum vfio_device_mig_state new_state)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);
	struct file *ret = NULL;

	dev_dbg(vdev->dev, "%s -> %d\n", __func__, new_state);

	mutex_lock(&mdev_state->state_mutex);
	while (mdev_state->state != new_state) {
		enum vfio_device_mig_state next_state;
		int rc = vfio_mig_get_next_state(vdev, mdev_state->state,
						 new_state, &next_state);
		if (rc) {
			ret = ERR_PTR(rc);
			break;
		}

		ret = mtty_step_state(mdev_state, next_state);
		if (IS_ERR(ret))
			break;

		mdev_state->state = next_state;

		if (WARN_ON(ret && new_state != next_state)) {
			fput(ret);
			ret = ERR_PTR(-EINVAL);
			break;
		}
	}
	mtty_state_mutex_unlock(mdev_state);
	return ret;
}

static int mtty_get_state(struct vfio_device *vdev,
			  enum vfio_device_mig_state *current_state)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);

	mutex_lock(&mdev_state->state_mutex);
	*current_state = mdev_state->state;
	mtty_state_mutex_unlock(mdev_state);
	return 0;
}

static int mtty_get_data_size(struct vfio_device *vdev,
			      unsigned long *stop_copy_length)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);

	*stop_copy_length = mtty_data_size(mdev_state);
	return 0;
}

static const struct vfio_migration_ops mtty_migration_ops = {
	.migration_set_state = mtty_set_state,
	.migration_get_state = mtty_get_state,
	.migration_get_data_size = mtty_get_data_size,
};

static int mtty_log_start(struct vfio_device *vdev,
			  struct rb_root_cached *ranges,
			  u32 nnodes, u64 *page_size)
{
	return 0;
}

static int mtty_log_stop(struct vfio_device *vdev)
{
	return 0;
}

static int mtty_log_read_and_clear(struct vfio_device *vdev,
				   unsigned long iova, unsigned long length,
				   struct iova_bitmap *dirty)
{
	return 0;
}

static const struct vfio_log_ops mtty_log_ops = {
	.log_start = mtty_log_start,
	.log_stop = mtty_log_stop,
	.log_read_and_clear = mtty_log_read_and_clear,
};

static int mtty_init_dev(struct vfio_device *vdev)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);
	struct mdev_device *mdev = to_mdev_device(vdev->dev);
	struct mtty_type *type =
		container_of(mdev->type, struct mtty_type, type);
	int avail_ports = atomic_read(&mdev_avail_ports);
	int ret;

	do {
		if (avail_ports < type->nr_ports)
			return -ENOSPC;
	} while (!atomic_try_cmpxchg(&mdev_avail_ports,
				     &avail_ports,
				     avail_ports - type->nr_ports));

	mdev_state->nr_ports = type->nr_ports;
	mdev_state->irq_index = -1;
	mdev_state->s[0].max_fifo_size = MAX_FIFO_SIZE;
	mdev_state->s[1].max_fifo_size = MAX_FIFO_SIZE;
	mutex_init(&mdev_state->rxtx_lock);

	mdev_state->vconfig = kzalloc(MTTY_CONFIG_SPACE_SIZE, GFP_KERNEL);
	if (!mdev_state->vconfig) {
		ret = -ENOMEM;
		goto err_nr_ports;
	}

	mutex_init(&mdev_state->ops_lock);
	mdev_state->mdev = mdev;
	mtty_create_config_space(mdev_state);

	mutex_init(&mdev_state->state_mutex);
	mutex_init(&mdev_state->reset_mutex);
	vdev->migration_flags = VFIO_MIGRATION_STOP_COPY |
				VFIO_MIGRATION_P2P |
				VFIO_MIGRATION_PRE_COPY;
	vdev->mig_ops = &mtty_migration_ops;
	vdev->log_ops = &mtty_log_ops;
	mdev_state->state = VFIO_DEVICE_STATE_RUNNING;

	return 0;

err_nr_ports:
	atomic_add(type->nr_ports, &mdev_avail_ports);
	return ret;
}

static int mtty_probe(struct mdev_device *mdev)
{
	struct mdev_state *mdev_state;
	int ret;

	mdev_state = vfio_alloc_device(mdev_state, vdev, &mdev->dev,
				       &mtty_dev_ops);
	if (IS_ERR(mdev_state))
		return PTR_ERR(mdev_state);

	ret = vfio_register_emulated_iommu_dev(&mdev_state->vdev);
	if (ret)
		goto err_put_vdev;
	dev_set_drvdata(&mdev->dev, mdev_state);
	return 0;

err_put_vdev:
	vfio_put_device(&mdev_state->vdev);
	return ret;
}

static void mtty_release_dev(struct vfio_device *vdev)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);

	mutex_destroy(&mdev_state->reset_mutex);
	mutex_destroy(&mdev_state->state_mutex);
	atomic_add(mdev_state->nr_ports, &mdev_avail_ports);
	kfree(mdev_state->vconfig);
}

static void mtty_remove(struct mdev_device *mdev)
{
	struct mdev_state *mdev_state = dev_get_drvdata(&mdev->dev);

	vfio_unregister_group_dev(&mdev_state->vdev);
	vfio_put_device(&mdev_state->vdev);
}

static int mtty_reset(struct mdev_state *mdev_state)
{
	pr_info("%s: called\n", __func__);

	mutex_lock(&mdev_state->reset_mutex);
	mdev_state->deferred_reset = true;
	if (!mutex_trylock(&mdev_state->state_mutex)) {
		mutex_unlock(&mdev_state->reset_mutex);
		return 0;
	}
	mutex_unlock(&mdev_state->reset_mutex);
	mtty_state_mutex_unlock(mdev_state);

	return 0;
}

static ssize_t mtty_read(struct vfio_device *vdev, char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);
	unsigned int done = 0;
	int ret;

	while (count) {
		size_t filled;

		if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			ret =  mdev_access(mdev_state, (u8 *)&val, sizeof(val),
					   *ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 4;
		} else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			ret = mdev_access(mdev_state, (u8 *)&val, sizeof(val),
					  *ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 2;
		} else {
			u8 val;

			ret = mdev_access(mdev_state, (u8 *)&val, sizeof(val),
					  *ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 1;
		}

		count -= filled;
		done += filled;
		*ppos += filled;
		buf += filled;
	}

	return done;

read_err:
	return -EFAULT;
}

static ssize_t mtty_write(struct vfio_device *vdev, const char __user *buf,
		   size_t count, loff_t *ppos)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);
	unsigned int done = 0;
	int ret;

	while (count) {
		size_t filled;

		if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = mdev_access(mdev_state, (u8 *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 4;
		} else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = mdev_access(mdev_state, (u8 *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 2;
		} else {
			u8 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = mdev_access(mdev_state, (u8 *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 1;
		}
		count -= filled;
		done += filled;
		*ppos += filled;
		buf += filled;
	}

	return done;
write_err:
	return -EFAULT;
}

static void mtty_disable_intx(struct mdev_state *mdev_state)
{
	if (mdev_state->intx_evtfd) {
		eventfd_ctx_put(mdev_state->intx_evtfd);
		mdev_state->intx_evtfd = NULL;
		mdev_state->intx_mask = false;
		mdev_state->irq_index = -1;
	}
}

static void mtty_disable_msi(struct mdev_state *mdev_state)
{
	if (mdev_state->msi_evtfd) {
		eventfd_ctx_put(mdev_state->msi_evtfd);
		mdev_state->msi_evtfd = NULL;
		mdev_state->irq_index = -1;
	}
}

static int mtty_set_irqs(struct mdev_state *mdev_state, uint32_t flags,
			 unsigned int index, unsigned int start,
			 unsigned int count, void *data)
{
	int ret = 0;

	mutex_lock(&mdev_state->ops_lock);
	switch (index) {
	case VFIO_PCI_INTX_IRQ_INDEX:
		switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
		case VFIO_IRQ_SET_ACTION_MASK:
			if (!is_intx(mdev_state) || start != 0 || count != 1) {
				ret = -EINVAL;
				break;
			}

			if (flags & VFIO_IRQ_SET_DATA_NONE) {
				mdev_state->intx_mask = true;
			} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
				uint8_t mask = *(uint8_t *)data;

				if (mask)
					mdev_state->intx_mask = true;
			} else if (flags &  VFIO_IRQ_SET_DATA_EVENTFD) {
				ret = -ENOTTY; /* No support for mask fd */
			}
			break;
		case VFIO_IRQ_SET_ACTION_UNMASK:
			if (!is_intx(mdev_state) || start != 0 || count != 1) {
				ret = -EINVAL;
				break;
			}

			if (flags & VFIO_IRQ_SET_DATA_NONE) {
				mdev_state->intx_mask = false;
			} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
				uint8_t mask = *(uint8_t *)data;

				if (mask)
					mdev_state->intx_mask = false;
			} else if (flags &  VFIO_IRQ_SET_DATA_EVENTFD) {
				ret = -ENOTTY; /* No support for unmask fd */
			}
			break;
		case VFIO_IRQ_SET_ACTION_TRIGGER:
			if (is_intx(mdev_state) && !count &&
			    (flags & VFIO_IRQ_SET_DATA_NONE)) {
				mtty_disable_intx(mdev_state);
				break;
			}

			if (!(is_intx(mdev_state) || is_noirq(mdev_state)) ||
			    start != 0 || count != 1) {
				ret = -EINVAL;
				break;
			}

			if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
				int fd = *(int *)data;
				struct eventfd_ctx *evt;

				mtty_disable_intx(mdev_state);

				if (fd < 0)
					break;

				evt = eventfd_ctx_fdget(fd);
				if (IS_ERR(evt)) {
					ret = PTR_ERR(evt);
					break;
				}
				mdev_state->intx_evtfd = evt;
				mdev_state->irq_index = index;
				break;
			}

			if (!is_intx(mdev_state)) {
				ret = -EINVAL;
				break;
			}

			if (flags & VFIO_IRQ_SET_DATA_NONE) {
				mtty_trigger_interrupt(mdev_state);
			} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
				uint8_t trigger = *(uint8_t *)data;

				if (trigger)
					mtty_trigger_interrupt(mdev_state);
			}
			break;
		}
		break;
	case VFIO_PCI_MSI_IRQ_INDEX:
		switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
		case VFIO_IRQ_SET_ACTION_MASK:
		case VFIO_IRQ_SET_ACTION_UNMASK:
			ret = -ENOTTY;
			break;
		case VFIO_IRQ_SET_ACTION_TRIGGER:
			if (is_msi(mdev_state) && !count &&
			    (flags & VFIO_IRQ_SET_DATA_NONE)) {
				mtty_disable_msi(mdev_state);
				break;
			}

			if (!(is_msi(mdev_state) || is_noirq(mdev_state)) ||
			    start != 0 || count != 1) {
				ret = -EINVAL;
				break;
			}

			if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
				int fd = *(int *)data;
				struct eventfd_ctx *evt;

				mtty_disable_msi(mdev_state);

				if (fd < 0)
					break;

				evt = eventfd_ctx_fdget(fd);
				if (IS_ERR(evt)) {
					ret = PTR_ERR(evt);
					break;
				}
				mdev_state->msi_evtfd = evt;
				mdev_state->irq_index = index;
				break;
			}

			if (!is_msi(mdev_state)) {
				ret = -EINVAL;
				break;
			}

			if (flags & VFIO_IRQ_SET_DATA_NONE) {
				mtty_trigger_interrupt(mdev_state);
			} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
				uint8_t trigger = *(uint8_t *)data;

				if (trigger)
					mtty_trigger_interrupt(mdev_state);
			}
			break;
		}
		break;
	case VFIO_PCI_MSIX_IRQ_INDEX:
		dev_dbg(mdev_state->vdev.dev, "%s: MSIX_IRQ\n", __func__);
		ret = -ENOTTY;
		break;
	case VFIO_PCI_ERR_IRQ_INDEX:
		dev_dbg(mdev_state->vdev.dev, "%s: ERR_IRQ\n", __func__);
		ret = -ENOTTY;
		break;
	case VFIO_PCI_REQ_IRQ_INDEX:
		dev_dbg(mdev_state->vdev.dev, "%s: REQ_IRQ\n", __func__);
		ret = -ENOTTY;
		break;
	}

	mutex_unlock(&mdev_state->ops_lock);
	return ret;
}

static int mtty_get_region_info(struct mdev_state *mdev_state,
			 struct vfio_region_info *region_info,
			 u16 *cap_type_id, void **cap_type)
{
	unsigned int size = 0;
	u32 bar_index;

	bar_index = region_info->index;
	if (bar_index >= VFIO_PCI_NUM_REGIONS)
		return -EINVAL;

	mutex_lock(&mdev_state->ops_lock);

	switch (bar_index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:
		size = MTTY_CONFIG_SPACE_SIZE;
		break;
	case VFIO_PCI_BAR0_REGION_INDEX:
		size = MTTY_IO_BAR_SIZE;
		break;
	case VFIO_PCI_BAR1_REGION_INDEX:
		if (mdev_state->nr_ports == 2)
			size = MTTY_IO_BAR_SIZE;
		break;
	default:
		size = 0;
		break;
	}

	mdev_state->region_info[bar_index].size = size;
	mdev_state->region_info[bar_index].vfio_offset =
		MTTY_VFIO_PCI_INDEX_TO_OFFSET(bar_index);

	region_info->size = size;
	region_info->offset = MTTY_VFIO_PCI_INDEX_TO_OFFSET(bar_index);
	region_info->flags = VFIO_REGION_INFO_FLAG_READ |
		VFIO_REGION_INFO_FLAG_WRITE;
	mutex_unlock(&mdev_state->ops_lock);
	return 0;
}

static int mtty_get_irq_info(struct vfio_irq_info *irq_info)
{
	if (irq_info->index != VFIO_PCI_INTX_IRQ_INDEX &&
	    irq_info->index != VFIO_PCI_MSI_IRQ_INDEX)
		return -EINVAL;

	irq_info->flags = VFIO_IRQ_INFO_EVENTFD;
	irq_info->count = 1;

	if (irq_info->index == VFIO_PCI_INTX_IRQ_INDEX)
		irq_info->flags |= VFIO_IRQ_INFO_MASKABLE |
				   VFIO_IRQ_INFO_AUTOMASKED;
	else
		irq_info->flags |= VFIO_IRQ_INFO_NORESIZE;

	return 0;
}

static int mtty_get_device_info(struct vfio_device_info *dev_info)
{
	dev_info->flags = VFIO_DEVICE_FLAGS_PCI;
	dev_info->num_regions = VFIO_PCI_NUM_REGIONS;
	dev_info->num_irqs = VFIO_PCI_NUM_IRQS;

	return 0;
}

static long mtty_ioctl(struct vfio_device *vdev, unsigned int cmd,
			unsigned long arg)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);
	int ret = 0;
	unsigned long minsz;

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
	{
		struct vfio_device_info info;

		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		ret = mtty_get_device_info(&info);
		if (ret)
			return ret;

		memcpy(&mdev_state->dev_info, &info, sizeof(info));

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_DEVICE_GET_REGION_INFO:
	{
		struct vfio_region_info info;
		u16 cap_type_id = 0;
		void *cap_type = NULL;

		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		ret = mtty_get_region_info(mdev_state, &info, &cap_type_id,
					   &cap_type);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}

	case VFIO_DEVICE_GET_IRQ_INFO:
	{
		struct vfio_irq_info info;

		minsz = offsetofend(struct vfio_irq_info, count);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if ((info.argsz < minsz) ||
		    (info.index >= mdev_state->dev_info.num_irqs))
			return -EINVAL;

		ret = mtty_get_irq_info(&info);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_DEVICE_SET_IRQS:
	{
		struct vfio_irq_set hdr;
		u8 *data = NULL, *ptr = NULL;
		size_t data_size = 0;

		minsz = offsetofend(struct vfio_irq_set, count);

		if (copy_from_user(&hdr, (void __user *)arg, minsz))
			return -EFAULT;

		ret = vfio_set_irqs_validate_and_prepare(&hdr,
						mdev_state->dev_info.num_irqs,
						VFIO_PCI_NUM_IRQS,
						&data_size);
		if (ret)
			return ret;

		if (data_size) {
			ptr = data = memdup_user((void __user *)(arg + minsz),
						 data_size);
			if (IS_ERR(data))
				return PTR_ERR(data);
		}

		ret = mtty_set_irqs(mdev_state, hdr.flags, hdr.index, hdr.start,
				    hdr.count, data);

		kfree(ptr);
		return ret;
	}
	case VFIO_DEVICE_RESET:
		return mtty_reset(mdev_state);
	}
	return -ENOTTY;
}

static ssize_t
sample_mdev_dev_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	return sprintf(buf, "This is MDEV %s\n", dev_name(dev));
}

static DEVICE_ATTR_RO(sample_mdev_dev);

static struct attribute *mdev_dev_attrs[] = {
	&dev_attr_sample_mdev_dev.attr,
	NULL,
};

static const struct attribute_group mdev_dev_group = {
	.name  = "vendor",
	.attrs = mdev_dev_attrs,
};

static const struct attribute_group *mdev_dev_groups[] = {
	&mdev_dev_group,
	NULL,
};

static unsigned int mtty_get_available(struct mdev_type *mtype)
{
	struct mtty_type *type = container_of(mtype, struct mtty_type, type);

	return atomic_read(&mdev_avail_ports) / type->nr_ports;
}

static void mtty_close(struct vfio_device *vdev)
{
	struct mdev_state *mdev_state =
				container_of(vdev, struct mdev_state, vdev);

	mtty_disable_files(mdev_state);
	mtty_disable_intx(mdev_state);
	mtty_disable_msi(mdev_state);
}

static const struct vfio_device_ops mtty_dev_ops = {
	.name = "vfio-mtty",
	.init = mtty_init_dev,
	.release = mtty_release_dev,
	.read = mtty_read,
	.write = mtty_write,
	.ioctl = mtty_ioctl,
	.bind_iommufd	= vfio_iommufd_emulated_bind,
	.unbind_iommufd	= vfio_iommufd_emulated_unbind,
	.attach_ioas	= vfio_iommufd_emulated_attach_ioas,
	.detach_ioas	= vfio_iommufd_emulated_detach_ioas,
	.close_device	= mtty_close,
};

static struct mdev_driver mtty_driver = {
	.device_api = VFIO_DEVICE_API_PCI_STRING,
	.driver = {
		.name = "mtty",
		.owner = THIS_MODULE,
		.mod_name = KBUILD_MODNAME,
		.dev_groups = mdev_dev_groups,
	},
	.probe = mtty_probe,
	.remove	= mtty_remove,
	.get_available = mtty_get_available,
};

static void mtty_device_release(struct device *dev)
{
	dev_dbg(dev, "mtty: released\n");
}

static int __init mtty_dev_init(void)
{
	int ret = 0;

	pr_info("mtty_dev: %s\n", __func__);

	memset(&mtty_dev, 0, sizeof(mtty_dev));

	idr_init(&mtty_dev.vd_idr);

	ret = alloc_chrdev_region(&mtty_dev.vd_devt, 0, MINORMASK + 1,
				  MTTY_NAME);

	if (ret < 0) {
		pr_err("Error: failed to register mtty_dev, err:%d\n", ret);
		return ret;
	}

	cdev_init(&mtty_dev.vd_cdev, &vd_fops);
	cdev_add(&mtty_dev.vd_cdev, mtty_dev.vd_devt, MINORMASK + 1);

	pr_info("major_number:%d\n", MAJOR(mtty_dev.vd_devt));

	ret = mdev_register_driver(&mtty_driver);
	if (ret)
		goto err_cdev;

	mtty_dev.vd_class = class_create(MTTY_CLASS_NAME);

	if (IS_ERR(mtty_dev.vd_class)) {
		pr_err("Error: failed to register mtty_dev class\n");
		ret = PTR_ERR(mtty_dev.vd_class);
		goto err_driver;
	}

	mtty_dev.dev.class = mtty_dev.vd_class;
	mtty_dev.dev.release = mtty_device_release;
	dev_set_name(&mtty_dev.dev, "%s", MTTY_NAME);

	ret = device_register(&mtty_dev.dev);
	if (ret)
		goto err_put;

	ret = mdev_register_parent(&mtty_dev.parent, &mtty_dev.dev,
				   &mtty_driver, mtty_mdev_types,
				   ARRAY_SIZE(mtty_mdev_types));
	if (ret)
		goto err_device;
	return 0;

err_device:
	device_del(&mtty_dev.dev);
err_put:
	put_device(&mtty_dev.dev);
	class_destroy(mtty_dev.vd_class);
err_driver:
	mdev_unregister_driver(&mtty_driver);
err_cdev:
	cdev_del(&mtty_dev.vd_cdev);
	unregister_chrdev_region(mtty_dev.vd_devt, MINORMASK + 1);
	return ret;
}

static void __exit mtty_dev_exit(void)
{
	mtty_dev.dev.bus = NULL;
	mdev_unregister_parent(&mtty_dev.parent);

	device_unregister(&mtty_dev.dev);
	idr_destroy(&mtty_dev.vd_idr);
	mdev_unregister_driver(&mtty_driver);
	cdev_del(&mtty_dev.vd_cdev);
	unregister_chrdev_region(mtty_dev.vd_devt, MINORMASK + 1);
	class_destroy(mtty_dev.vd_class);
	mtty_dev.vd_class = NULL;
	pr_info("mtty_dev: Unloaded!\n");
}

module_init(mtty_dev_init)
module_exit(mtty_dev_exit)

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Test driver that simulate serial port over PCI");
MODULE_VERSION(VERSION_STRING);
MODULE_AUTHOR(DRIVER_AUTHOR);
