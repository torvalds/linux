/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SAA7146__
#define __SAA7146__

#include <linux/delay.h>	/* for delay-stuff */
#include <linux/slab.h>		/* for kmalloc/kfree */
#include <linux/pci.h>		/* for pci-config-stuff, vendor ids etc. */
#include <linux/init.h>		/* for "__init" */
#include <linux/interrupt.h>	/* for IMMEDIATE_BH */
#include <linux/kmod.h>		/* for kernel module loader */
#include <linux/i2c.h>		/* for i2c subsystem */
#include <asm/io.h>		/* for accessing devices */
#include <linux/stringify.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#include <linux/vmalloc.h>	/* for vmalloc() */
#include <linux/mm.h>		/* for vmalloc_to_page() */

#define saa7146_write(sxy,adr,dat)    writel((dat),(sxy->mem+(adr)))
#define saa7146_read(sxy,adr)         readl(sxy->mem+(adr))

extern unsigned int saa7146_debug;

#ifndef DEBUG_VARIABLE
	#define DEBUG_VARIABLE saa7146_debug
#endif

#define ERR(fmt, ...)	pr_err("%s: " fmt, __func__, ##__VA_ARGS__)

#define _DBG(mask, fmt, ...)						\
do {									\
	if (DEBUG_VARIABLE & mask)					\
		pr_debug("%s(): " fmt, __func__, ##__VA_ARGS__);	\
} while (0)

/* simple debug messages */
#define DEB_S(fmt, ...)		_DBG(0x01, fmt, ##__VA_ARGS__)
/* more detailed debug messages */
#define DEB_D(fmt, ...)		_DBG(0x02, fmt, ##__VA_ARGS__)
/* print enter and exit of functions */
#define DEB_EE(fmt, ...)	_DBG(0x04, fmt, ##__VA_ARGS__)
/* i2c debug messages */
#define DEB_I2C(fmt, ...)	_DBG(0x08, fmt, ##__VA_ARGS__)
/* vbi debug messages */
#define DEB_VBI(fmt, ...)	_DBG(0x10, fmt, ##__VA_ARGS__)
/* interrupt debug messages */
#define DEB_INT(fmt, ...)	_DBG(0x20, fmt, ##__VA_ARGS__)
/* capture debug messages */
#define DEB_CAP(fmt, ...)	_DBG(0x40, fmt, ##__VA_ARGS__)

#define SAA7146_ISR_CLEAR(x,y) \
	saa7146_write(x, ISR, (y));

struct module;

struct saa7146_dev;
struct saa7146_extension;
struct saa7146_vv;

/* saa7146 page table */
struct saa7146_pgtable {
	unsigned int	size;
	__le32		*cpu;
	dma_addr_t	dma;
	/* used for offsets for u,v planes for planar capture modes */
	unsigned long	offset;
	/* used for custom pagetables (used for example by budget dvb cards) */
	struct scatterlist *slist;
	int		nents;
};

struct saa7146_pci_extension_data {
	struct saa7146_extension *ext;
	void *ext_priv;			/* most likely a name string */
};

#define MAKE_EXTENSION_PCI(x_var, x_vendor, x_device)		\
	{							\
		.vendor    = PCI_VENDOR_ID_PHILIPS,		\
		.device	   = PCI_DEVICE_ID_PHILIPS_SAA7146,	\
		.subvendor = x_vendor,				\
		.subdevice = x_device,				\
		.driver_data = (unsigned long)& x_var,		\
	}

struct saa7146_extension
{
	char	name[32];		/* name of the device */
#define SAA7146_USE_I2C_IRQ	0x1
#define SAA7146_I2C_SHORT_DELAY	0x2
	int	flags;

	/* pairs of subvendor and subdevice ids for
	   supported devices, last entry 0xffff, 0xfff */
	struct module *module;
	struct pci_driver driver;
	const struct pci_device_id *pci_tbl;

	/* extension functions */
	int (*probe)(struct saa7146_dev *);
	int (*attach)(struct saa7146_dev *, struct saa7146_pci_extension_data *);
	int (*detach)(struct saa7146_dev*);

	u32	irq_mask;	/* mask to indicate, which irq-events are handled by the extension */
	void	(*irq_func)(struct saa7146_dev*, u32* irq_mask);
};

struct saa7146_dma
{
	dma_addr_t	dma_handle;
	__le32		*cpu_addr;
};

struct saa7146_dev
{
	struct module			*module;

	struct v4l2_device		v4l2_dev;
	struct v4l2_ctrl_handler	ctrl_handler;

	/* different device locks */
	spinlock_t			slock;
	struct mutex			v4l2_lock;

	unsigned char			__iomem *mem;		/* pointer to mapped IO memory */
	u32				revision;	/* chip revision; needed for bug-workarounds*/

	/* pci-device & irq stuff*/
	char				name[32];
	struct pci_dev			*pci;
	u32				int_todo;
	spinlock_t			int_slock;

	/* extension handling */
	struct saa7146_extension	*ext;		/* indicates if handled by extension */
	void				*ext_priv;	/* pointer for extension private use (most likely some private data) */
	struct saa7146_ext_vv		*ext_vv_data;

	/* per device video/vbi information (if available) */
	struct saa7146_vv	*vv_data;
	void (*vv_callback)(struct saa7146_dev *dev, unsigned long status);

	/* i2c-stuff */
	struct mutex			i2c_lock;

	u32				i2c_bitrate;
	struct saa7146_dma		d_i2c;	/* pointer to i2c memory */
	wait_queue_head_t		i2c_wq;
	int				i2c_op;

	/* memories */
	struct saa7146_dma		d_rps0;
	struct saa7146_dma		d_rps1;
};

static inline struct saa7146_dev *to_saa7146_dev(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct saa7146_dev, v4l2_dev);
}

/* from saa7146_i2c.c */
int saa7146_i2c_adapter_prepare(struct saa7146_dev *dev, struct i2c_adapter *i2c_adapter, u32 bitrate);

/* from saa7146_core.c */
int saa7146_register_extension(struct saa7146_extension*);
int saa7146_unregister_extension(struct saa7146_extension*);
struct saa7146_format* saa7146_format_by_fourcc(struct saa7146_dev *dev, int fourcc);
int saa7146_pgtable_alloc(struct pci_dev *pci, struct saa7146_pgtable *pt);
void saa7146_pgtable_free(struct pci_dev *pci, struct saa7146_pgtable *pt);
int saa7146_pgtable_build_single(struct pci_dev *pci, struct saa7146_pgtable *pt, struct scatterlist *list, int length );
void *saa7146_vmalloc_build_pgtable(struct pci_dev *pci, long length, struct saa7146_pgtable *pt);
void saa7146_vfree_destroy_pgtable(struct pci_dev *pci, void *mem, struct saa7146_pgtable *pt);
void saa7146_setgpio(struct saa7146_dev *dev, int port, u32 data);
int saa7146_wait_for_debi_done(struct saa7146_dev *dev, int nobusyloop);

/* some memory sizes */
#define SAA7146_I2C_MEM		( 1*PAGE_SIZE)
#define SAA7146_RPS_MEM		( 1*PAGE_SIZE)

/* some i2c constants */
#define SAA7146_I2C_TIMEOUT	100	/* i2c-timeout-value in ms */
#define SAA7146_I2C_RETRIES	3	/* how many times shall we retry an i2c-operation? */
#define SAA7146_I2C_DELAY	5	/* time we wait after certain i2c-operations */

/* unsorted defines */
#define ME1    0x0000000800
#define PV1    0x0000000008

/* gpio defines */
#define SAA7146_GPIO_INPUT 0x00
#define SAA7146_GPIO_IRQHI 0x10
#define SAA7146_GPIO_IRQLO 0x20
#define SAA7146_GPIO_IRQHL 0x30
#define SAA7146_GPIO_OUTLO 0x40
#define SAA7146_GPIO_OUTHI 0x50

/* debi defines */
#define DEBINOSWAP 0x000e0000

/* define for the register programming sequencer (rps) */
#define CMD_NOP		0x00000000  /* No operation */
#define CMD_CLR_EVENT	0x00000000  /* Clear event */
#define CMD_SET_EVENT	0x10000000  /* Set signal event */
#define CMD_PAUSE	0x20000000  /* Pause */
#define CMD_CHECK_LATE	0x30000000  /* Check late */
#define CMD_UPLOAD	0x40000000  /* Upload */
#define CMD_STOP	0x50000000  /* Stop */
#define CMD_INTERRUPT	0x60000000  /* Interrupt */
#define CMD_JUMP	0x80000000  /* Jump */
#define CMD_WR_REG	0x90000000  /* Write (load) register */
#define CMD_RD_REG	0xa0000000  /* Read (store) register */
#define CMD_WR_REG_MASK	0xc0000000  /* Write register with mask */

#define CMD_OAN		MASK_27
#define CMD_INV		MASK_26
#define CMD_SIG4	MASK_25
#define CMD_SIG3	MASK_24
#define CMD_SIG2	MASK_23
#define CMD_SIG1	MASK_22
#define CMD_SIG0	MASK_21
#define CMD_O_FID_B	MASK_14
#define CMD_E_FID_B	MASK_13
#define CMD_O_FID_A	MASK_12
#define CMD_E_FID_A	MASK_11

/* some events and command modifiers for rps1 squarewave generator */
#define EVT_HS          (1<<15)     // Source Line Threshold reached
#define EVT_VBI_B       (1<<9)      // VSYNC Event
#define RPS_OAN         (1<<27)     // 1: OR events, 0: AND events
#define RPS_INV         (1<<26)     // Invert (compound) event
#define GPIO3_MSK       0xFF000000  // GPIO #3 control bits

/* Bit mask constants */
#define MASK_00   0x00000001    /* Mask value for bit 0 */
#define MASK_01   0x00000002    /* Mask value for bit 1 */
#define MASK_02   0x00000004    /* Mask value for bit 2 */
#define MASK_03   0x00000008    /* Mask value for bit 3 */
#define MASK_04   0x00000010    /* Mask value for bit 4 */
#define MASK_05   0x00000020    /* Mask value for bit 5 */
#define MASK_06   0x00000040    /* Mask value for bit 6 */
#define MASK_07   0x00000080    /* Mask value for bit 7 */
#define MASK_08   0x00000100    /* Mask value for bit 8 */
#define MASK_09   0x00000200    /* Mask value for bit 9 */
#define MASK_10   0x00000400    /* Mask value for bit 10 */
#define MASK_11   0x00000800    /* Mask value for bit 11 */
#define MASK_12   0x00001000    /* Mask value for bit 12 */
#define MASK_13   0x00002000    /* Mask value for bit 13 */
#define MASK_14   0x00004000    /* Mask value for bit 14 */
#define MASK_15   0x00008000    /* Mask value for bit 15 */
#define MASK_16   0x00010000    /* Mask value for bit 16 */
#define MASK_17   0x00020000    /* Mask value for bit 17 */
#define MASK_18   0x00040000    /* Mask value for bit 18 */
#define MASK_19   0x00080000    /* Mask value for bit 19 */
#define MASK_20   0x00100000    /* Mask value for bit 20 */
#define MASK_21   0x00200000    /* Mask value for bit 21 */
#define MASK_22   0x00400000    /* Mask value for bit 22 */
#define MASK_23   0x00800000    /* Mask value for bit 23 */
#define MASK_24   0x01000000    /* Mask value for bit 24 */
#define MASK_25   0x02000000    /* Mask value for bit 25 */
#define MASK_26   0x04000000    /* Mask value for bit 26 */
#define MASK_27   0x08000000    /* Mask value for bit 27 */
#define MASK_28   0x10000000    /* Mask value for bit 28 */
#define MASK_29   0x20000000    /* Mask value for bit 29 */
#define MASK_30   0x40000000    /* Mask value for bit 30 */
#define MASK_31   0x80000000    /* Mask value for bit 31 */

#define MASK_B0   0x000000ff    /* Mask value for byte 0 */
#define MASK_B1   0x0000ff00    /* Mask value for byte 1 */
#define MASK_B2   0x00ff0000    /* Mask value for byte 2 */
#define MASK_B3   0xff000000    /* Mask value for byte 3 */

#define MASK_W0   0x0000ffff    /* Mask value for word 0 */
#define MASK_W1   0xffff0000    /* Mask value for word 1 */

#define MASK_PA   0xfffffffc    /* Mask value for physical address */
#define MASK_PR   0xfffffffe	/* Mask value for protection register */
#define MASK_ER   0xffffffff    /* Mask value for the entire register */

#define MASK_NONE 0x00000000    /* No mask */

/* register aliases */
#define BASE_ODD1         0x00  /* Video DMA 1 registers  */
#define BASE_EVEN1        0x04
#define PROT_ADDR1        0x08
#define PITCH1            0x0C
#define BASE_PAGE1        0x10  /* Video DMA 1 base page */
#define NUM_LINE_BYTE1    0x14

#define BASE_ODD2         0x18  /* Video DMA 2 registers */
#define BASE_EVEN2        0x1C
#define PROT_ADDR2        0x20
#define PITCH2            0x24
#define BASE_PAGE2        0x28  /* Video DMA 2 base page */
#define NUM_LINE_BYTE2    0x2C

#define BASE_ODD3         0x30  /* Video DMA 3 registers */
#define BASE_EVEN3        0x34
#define PROT_ADDR3        0x38
#define PITCH3            0x3C
#define BASE_PAGE3        0x40  /* Video DMA 3 base page */
#define NUM_LINE_BYTE3    0x44

#define PCI_BT_V1         0x48  /* Video/FIFO 1 */
#define PCI_BT_V2         0x49  /* Video/FIFO 2 */
#define PCI_BT_V3         0x4A  /* Video/FIFO 3 */
#define PCI_BT_DEBI       0x4B  /* DEBI */
#define PCI_BT_A          0x4C  /* Audio */

#define DD1_INIT          0x50  /* Init setting of DD1 interface */

#define DD1_STREAM_B      0x54  /* DD1 B video data stream handling */
#define DD1_STREAM_A      0x56  /* DD1 A video data stream handling */

#define BRS_CTRL          0x58  /* BRS control register */
#define HPS_CTRL          0x5C  /* HPS control register */
#define HPS_V_SCALE       0x60  /* HPS vertical scale */
#define HPS_V_GAIN        0x64  /* HPS vertical ACL and gain */
#define HPS_H_PRESCALE    0x68  /* HPS horizontal prescale   */
#define HPS_H_SCALE       0x6C  /* HPS horizontal scale */
#define BCS_CTRL          0x70  /* BCS control */
#define CHROMA_KEY_RANGE  0x74
#define CLIP_FORMAT_CTRL  0x78  /* HPS outputs formats & clipping */

#define DEBI_CONFIG       0x7C
#define DEBI_COMMAND      0x80
#define DEBI_PAGE         0x84
#define DEBI_AD           0x88

#define I2C_TRANSFER      0x8C
#define I2C_STATUS        0x90

#define BASE_A1_IN        0x94	/* Audio 1 input DMA */
#define PROT_A1_IN        0x98
#define PAGE_A1_IN        0x9C

#define BASE_A1_OUT       0xA0  /* Audio 1 output DMA */
#define PROT_A1_OUT       0xA4
#define PAGE_A1_OUT       0xA8

#define BASE_A2_IN        0xAC  /* Audio 2 input DMA */
#define PROT_A2_IN        0xB0
#define PAGE_A2_IN        0xB4

#define BASE_A2_OUT       0xB8  /* Audio 2 output DMA */
#define PROT_A2_OUT       0xBC
#define PAGE_A2_OUT       0xC0

#define RPS_PAGE0         0xC4  /* RPS task 0 page register */
#define RPS_PAGE1         0xC8  /* RPS task 1 page register */

#define RPS_THRESH0       0xCC  /* HBI threshold for task 0 */
#define RPS_THRESH1       0xD0  /* HBI threshold for task 1 */

#define RPS_TOV0          0xD4  /* RPS timeout for task 0 */
#define RPS_TOV1          0xD8  /* RPS timeout for task 1 */

#define IER               0xDC  /* Interrupt enable register */

#define GPIO_CTRL         0xE0  /* GPIO 0-3 register */

#define EC1SSR            0xE4  /* Event cnt set 1 source select */
#define EC2SSR            0xE8  /* Event cnt set 2 source select */
#define ECT1R             0xEC  /* Event cnt set 1 thresholds */
#define ECT2R             0xF0  /* Event cnt set 2 thresholds */

#define ACON1             0xF4
#define ACON2             0xF8

#define MC1               0xFC   /* Main control register 1 */
#define MC2               0x100  /* Main control register 2  */

#define RPS_ADDR0         0x104  /* RPS task 0 address register */
#define RPS_ADDR1         0x108  /* RPS task 1 address register */

#define ISR               0x10C  /* Interrupt status register */
#define PSR               0x110  /* Primary status register */
#define SSR               0x114  /* Secondary status register */

#define EC1R              0x118  /* Event counter set 1 register */
#define EC2R              0x11C  /* Event counter set 2 register */

#define PCI_VDP1          0x120  /* Video DMA pointer of FIFO 1 */
#define PCI_VDP2          0x124  /* Video DMA pointer of FIFO 2 */
#define PCI_VDP3          0x128  /* Video DMA pointer of FIFO 3 */
#define PCI_ADP1          0x12C  /* Audio DMA pointer of audio out 1 */
#define PCI_ADP2          0x130  /* Audio DMA pointer of audio in 1 */
#define PCI_ADP3          0x134  /* Audio DMA pointer of audio out 2 */
#define PCI_ADP4          0x138  /* Audio DMA pointer of audio in 2 */
#define PCI_DMA_DDP       0x13C  /* DEBI DMA pointer */

#define LEVEL_REP         0x140,
#define A_TIME_SLOT1      0x180,  /* from 180 - 1BC */
#define A_TIME_SLOT2      0x1C0,  /* from 1C0 - 1FC */

/* isr masks */
#define SPCI_PPEF       0x80000000  /* PCI parity error */
#define SPCI_PABO       0x40000000  /* PCI access error (target or master abort) */
#define SPCI_PPED       0x20000000  /* PCI parity error on 'real time data' */
#define SPCI_RPS_I1     0x10000000  /* Interrupt issued by RPS1 */
#define SPCI_RPS_I0     0x08000000  /* Interrupt issued by RPS0 */
#define SPCI_RPS_LATE1  0x04000000  /* RPS task 1 is late */
#define SPCI_RPS_LATE0  0x02000000  /* RPS task 0 is late */
#define SPCI_RPS_E1     0x01000000  /* RPS error from task 1 */
#define SPCI_RPS_E0     0x00800000  /* RPS error from task 0 */
#define SPCI_RPS_TO1    0x00400000  /* RPS timeout task 1 */
#define SPCI_RPS_TO0    0x00200000  /* RPS timeout task 0 */
#define SPCI_UPLD       0x00100000  /* RPS in upload */
#define SPCI_DEBI_S     0x00080000  /* DEBI status */
#define SPCI_DEBI_E     0x00040000  /* DEBI error */
#define SPCI_IIC_S      0x00020000  /* I2C status */
#define SPCI_IIC_E      0x00010000  /* I2C error */
#define SPCI_A2_IN      0x00008000  /* Audio 2 input DMA protection / limit */
#define SPCI_A2_OUT     0x00004000  /* Audio 2 output DMA protection / limit */
#define SPCI_A1_IN      0x00002000  /* Audio 1 input DMA protection / limit */
#define SPCI_A1_OUT     0x00001000  /* Audio 1 output DMA protection / limit */
#define SPCI_AFOU       0x00000800  /* Audio FIFO over- / underflow */
#define SPCI_V_PE       0x00000400  /* Video protection address */
#define SPCI_VFOU       0x00000200  /* Video FIFO over- / underflow */
#define SPCI_FIDA       0x00000100  /* Field ID video port A */
#define SPCI_FIDB       0x00000080  /* Field ID video port B */
#define SPCI_PIN3       0x00000040  /* GPIO pin 3 */
#define SPCI_PIN2       0x00000020  /* GPIO pin 2 */
#define SPCI_PIN1       0x00000010  /* GPIO pin 1 */
#define SPCI_PIN0       0x00000008  /* GPIO pin 0 */
#define SPCI_ECS        0x00000004  /* Event counter 1, 2, 4, 5 */
#define SPCI_EC3S       0x00000002  /* Event counter 3 */
#define SPCI_EC0S       0x00000001  /* Event counter 0 */

/* i2c */
#define	SAA7146_I2C_ABORT	(1<<7)
#define	SAA7146_I2C_SPERR	(1<<6)
#define	SAA7146_I2C_APERR	(1<<5)
#define	SAA7146_I2C_DTERR	(1<<4)
#define	SAA7146_I2C_DRERR	(1<<3)
#define	SAA7146_I2C_AL		(1<<2)
#define	SAA7146_I2C_ERR		(1<<1)
#define	SAA7146_I2C_BUSY	(1<<0)

#define	SAA7146_I2C_START	(0x3)
#define	SAA7146_I2C_CONT	(0x2)
#define	SAA7146_I2C_STOP	(0x1)
#define	SAA7146_I2C_NOP		(0x0)

#define SAA7146_I2C_BUS_BIT_RATE_6400	(0x500)
#define SAA7146_I2C_BUS_BIT_RATE_3200	(0x100)
#define SAA7146_I2C_BUS_BIT_RATE_480	(0x400)
#define SAA7146_I2C_BUS_BIT_RATE_320	(0x600)
#define SAA7146_I2C_BUS_BIT_RATE_240	(0x700)
#define SAA7146_I2C_BUS_BIT_RATE_120	(0x000)
#define SAA7146_I2C_BUS_BIT_RATE_80	(0x200)
#define SAA7146_I2C_BUS_BIT_RATE_60	(0x300)

static inline void SAA7146_IER_DISABLE(struct saa7146_dev *x, unsigned y)
{
	unsigned long flags;
	spin_lock_irqsave(&x->int_slock, flags);
	saa7146_write(x, IER, saa7146_read(x, IER) & ~y);
	spin_unlock_irqrestore(&x->int_slock, flags);
}

static inline void SAA7146_IER_ENABLE(struct saa7146_dev *x, unsigned y)
{
	unsigned long flags;
	spin_lock_irqsave(&x->int_slock, flags);
	saa7146_write(x, IER, saa7146_read(x, IER) | y);
	spin_unlock_irqrestore(&x->int_slock, flags);
}

#endif
