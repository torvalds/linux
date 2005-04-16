/*
 * linux/include/asm-arm26/ecard.h
 *
 * definitions for expansion cards
 *
 * This is a new system as from Linux 1.2.3
 *
 * Changelog:
 *  11-12-1996	RMK	Further minor improvements
 *  12-09-1997	RMK	Added interrupt enable/disable for card level
 *  18-05-2003  IM      Adjusted for ARM26
 *
 * Reference: Acorns Risc OS 3 Programmers Reference Manuals.
 */

#ifndef __ASM_ECARD_H
#define __ASM_ECARD_H

/*
 * Currently understood cards (but not necessarily
 * supported):
 *                        Manufacturer  Product ID
 */
#define MANU_ACORN		0x0000
#define PROD_ACORN_SCSI			0x0002
#define PROD_ACORN_ETHER1		0x0003
#define PROD_ACORN_MFM			0x000b

#define MANU_CCONCEPTS		0x0009
#define PROD_CCONCEPTS_COLOURCARD	0x0050

#define MANU_ANT2		0x0011
#define PROD_ANT_ETHER3			0x00a4

#define MANU_ATOMWIDE		0x0017
#define PROD_ATOMWIDE_3PSERIAL		0x0090

#define MANU_IRLAM_INSTRUMENTS	0x001f
#define MANU_IRLAM_INSTRUMENTS_ETHERN	0x5678

#define MANU_OAK		0x0021
#define PROD_OAK_SCSI			0x0058

#define MANU_MORLEY		0x002b
#define PROD_MORLEY_SCSI_UNCACHED	0x0067

#define MANU_CUMANA		0x003a
#define PROD_CUMANA_SCSI_2		0x003a
#define PROD_CUMANA_SCSI_1		0x00a0

#define MANU_ICS		0x003c
#define PROD_ICS_IDE			0x00ae

#define MANU_ICS2		0x003d
#define PROD_ICS2_IDE			0x00ae

#define MANU_SERPORT		0x003f
#define PROD_SERPORT_DSPORT		0x00b9

#define MANU_ARXE		0x0041
#define PROD_ARXE_SCSI			0x00be

#define MANU_I3			0x0046
#define PROD_I3_ETHERLAN500		0x00d4
#define PROD_I3_ETHERLAN600		0x00ec
#define PROD_I3_ETHERLAN600A		0x011e

#define MANU_ANT		0x0053
#define PROD_ANT_ETHERM			0x00d8
#define PROD_ANT_ETHERB			0x00e4

#define MANU_ALSYSTEMS		0x005b
#define PROD_ALSYS_SCSIATAPI		0x0107

#define MANU_MCS		0x0063
#define PROD_MCS_CONNECT32		0x0125

#define MANU_EESOX		0x0064
#define PROD_EESOX_SCSI2		0x008c

#define MANU_YELLOWSTONE	0x0096
#define PROD_YELLOWSTONE_RAPIDE32	0x0120

#define MANU_SIMTEC             0x005f
#define PROD_SIMTEC_IDE8                0x0130
#define PROD_SIMTEC_IDE16               0x0131


#ifdef ECARD_C
#define CONST
#else
#define CONST const
#endif

#define MAX_ECARDS	4

typedef enum {				/* Cards address space		*/
	ECARD_IOC,
	ECARD_MEMC,
	ECARD_EASI
} card_type_t;

typedef enum {				/* Speed for ECARD_IOC space	*/
	ECARD_SLOW	 = 0,
	ECARD_MEDIUM	 = 1,
	ECARD_FAST	 = 2,
	ECARD_SYNC	 = 3
} card_speed_t;

struct ecard_id {			/* Card ID structure		*/
	unsigned short	manufacturer;
	unsigned short	product;
	void		*data;
};

struct in_ecid {			/* Packed card ID information	*/
	unsigned short	product;	/* Product code			*/
	unsigned short	manufacturer;	/* Manufacturer code		*/
	unsigned char	id:4;		/* Simple ID			*/
	unsigned char	cd:1;		/* Chunk dir present		*/
	unsigned char	is:1;		/* Interrupt status pointers	*/
	unsigned char	w:2;		/* Width			*/
	unsigned char	country;	/* Country			*/
	unsigned char	irqmask;	/* IRQ mask			*/
	unsigned char	fiqmask;	/* FIQ mask			*/
	unsigned long	irqoff;		/* IRQ offset			*/
	unsigned long	fiqoff;		/* FIQ offset			*/
};

typedef struct expansion_card ecard_t;
typedef unsigned long *loader_t;

typedef struct {			/* Card handler routines	*/
	void (*irqenable)(ecard_t *ec, int irqnr);
	void (*irqdisable)(ecard_t *ec, int irqnr);
	int  (*irqpending)(ecard_t *ec);
	void (*fiqenable)(ecard_t *ec, int fiqnr);
	void (*fiqdisable)(ecard_t *ec, int fiqnr);
	int  (*fiqpending)(ecard_t *ec);
} expansioncard_ops_t;

#define ECARD_NUM_RESOURCES	(6)

#define ECARD_RES_IOCSLOW	(0)
#define ECARD_RES_IOCMEDIUM	(1)
#define ECARD_RES_IOCFAST	(2)
#define ECARD_RES_IOCSYNC	(3)
#define ECARD_RES_MEMC		(4)
#define ECARD_RES_EASI		(5)

#define ecard_resource_start(ec,nr)	((ec)->resource[nr].start)
#define ecard_resource_end(ec,nr)	((ec)->resource[nr].end)
#define ecard_resource_len(ec,nr)	((ec)->resource[nr].end - \
					 (ec)->resource[nr].start + 1)

/*
 * This contains all the info needed on an expansion card
 */
struct expansion_card {
	struct expansion_card  *next;

	struct device		dev;
	struct resource		resource[ECARD_NUM_RESOURCES];

	/* Public data */
	volatile unsigned char *irqaddr;     /* address of IRQ register */
	volatile unsigned char *fiqaddr;     /* address of FIQ register */
	unsigned char		irqmask;     /* IRQ mask */
	unsigned char		fiqmask;     /* FIQ mask */
	unsigned char  		claimed;     /* Card claimed? */

	void			*irq_data;   /* Data for use for IRQ by card */
	void			*fiq_data;   /* Data for use for FIQ by card */
	const expansioncard_ops_t *ops;	     /* Enable/Disable Ops for card */

	CONST unsigned int	slot_no;     /* Slot number */
	CONST unsigned int	dma;         /* DMA number (for request_dma) */
	CONST unsigned int	irq;         /* IRQ number (for request_irq) */
	CONST unsigned int	fiq;         /* FIQ number (for request_irq) */
	CONST card_type_t	type;        /* Type of card */
	CONST struct in_ecid	cid;         /* Card Identification */

	/* Private internal data */
	const char		*card_desc;  /* Card description */
	CONST unsigned int	podaddr;     /* Base Linux address for card */
	CONST loader_t		loader;      /* loader program */
	u64			dma_mask;
};

struct in_chunk_dir {
	unsigned int start_offset;
	union {
		unsigned char string[256];
		unsigned char data[1];
	} d;
};

/*
 * ecard_claim: claim an expansion card entry
 * FIXME - are these atomic / called with interrupts off ?
 */
#define ecard_claim(ec) ((ec)->claimed = 1)

/*
 * ecard_release: release an expansion card entry
 */
#define ecard_release(ec) ((ec)->claimed = 0)

/*
 * Read a chunk from an expansion card
 * cd : where to put read data
 * ec : expansion card info struct
 * id : id number to find
 * num: (n+1)'th id to find.
 */
extern int ecard_readchunk (struct in_chunk_dir *cd, struct expansion_card *ec, int id, int num);

/*
 * Obtain the address of a card
 */
extern unsigned int ecard_address (struct expansion_card *ec, card_type_t card_type, card_speed_t speed);

#ifdef ECARD_C
/* Definitions internal to ecard.c - for it's use only!!
 *
 * External expansion card header as read from the card
 */
struct ex_ecid {
	unsigned char	r_irq:1;
	unsigned char	r_zero:1;
	unsigned char	r_fiq:1;
	unsigned char	r_id:4;
	unsigned char	r_a:1;

	unsigned char	r_cd:1;
	unsigned char	r_is:1;
	unsigned char	r_w:2;
	unsigned char	r_r1:4;

	unsigned char	r_r2:8;

	unsigned char	r_prod[2];

	unsigned char	r_manu[2];

	unsigned char	r_country;

	unsigned char	r_irqmask;
	unsigned char	r_irqoff[3];

	unsigned char	r_fiqmask;
	unsigned char	r_fiqoff[3];
};

/*
 * Chunk directory entry as read from the card
 */
struct ex_chunk_dir {
	unsigned char r_id;
	unsigned char r_len[3];
	unsigned long r_start;
	union {
		char string[256];
		char data[1];
	} d;
#define c_id(x)		((x)->r_id)
#define c_len(x)	((x)->r_len[0]|((x)->r_len[1]<<8)|((x)->r_len[2]<<16))
#define c_start(x)	((x)->r_start)
};

#endif

extern struct bus_type ecard_bus_type;

#define ECARD_DEV(_d)	container_of((_d), struct expansion_card, dev)

struct ecard_driver {
	int			(*probe)(struct expansion_card *, const struct ecard_id *id);
	void			(*remove)(struct expansion_card *);
	void			(*shutdown)(struct expansion_card *);
	const struct ecard_id	*id_table;
	unsigned int		id;
	struct device_driver	drv;
};

#define ECARD_DRV(_d)	container_of((_d), struct ecard_driver, drv)

#define ecard_set_drvdata(ec,data)	dev_set_drvdata(&(ec)->dev, (data))
#define ecard_get_drvdata(ec)		dev_get_drvdata(&(ec)->dev)

int ecard_register_driver(struct ecard_driver *);
void ecard_remove_driver(struct ecard_driver *);

#endif
