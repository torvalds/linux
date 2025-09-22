/* $OpenBSD: dsdt.h,v 1.82 2024/05/13 01:15:50 jsg Exp $ */
/*
 * Copyright (c) 2005 Marco Peereboom <marco@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __DEV_ACPI_DSDT_H__
#define __DEV_ACPI_DSDT_H__

struct aml_scope {
	struct acpi_softc	*sc;
	uint8_t			*pos;
	uint8_t			*start;
	uint8_t			*end;
	struct aml_node		*node;
	struct aml_scope	*parent;
	struct aml_value	*locals;
	struct aml_value	*args;
	struct aml_value	*retv;
	int			type;
	int			depth;
};


struct aml_opcode {
	uint32_t		opcode;
	const char		*mnem;
	const char		*args;
};

const char		*aml_eisaid(uint32_t);
const char		*aml_mnem(int, uint8_t *);
int64_t			aml_val2int(struct aml_value *);
struct aml_node		*aml_searchname(struct aml_node *, const void *);
struct aml_node		*aml_searchrel(struct aml_node *, const void *);
const char		*aml_getname(const char *);

struct aml_value	*aml_getstack(struct aml_scope *, int);
struct aml_value	*aml_allocvalue(int, int64_t, const void *);
void			aml_freevalue(struct aml_value *);
void			aml_notify(struct aml_node *, int);
void			aml_showvalue(struct aml_value *);

void			aml_find_node(struct aml_node *, const char *,
			    int (*)(struct aml_node *, void *), void *);
int			acpi_parse_aml(struct acpi_softc *, const char *,
			    u_int8_t *, uint32_t);
void			aml_register_notify(struct aml_node *, const char *,
			    int (*)(struct aml_node *, int, void *), void *,
			    int);
void			aml_register_regionspace(struct aml_node *, int, void *,
			    int (*)(void *, int, uint64_t, int, uint64_t *));

int			aml_evalnode(struct acpi_softc *, struct aml_node *,
			    int, struct aml_value *, struct aml_value *);
int			aml_node_setval(struct acpi_softc *, struct aml_node *,
			    int64_t);
int			aml_evalname(struct acpi_softc *, struct aml_node *,
			    const char *, int, struct aml_value *,
			    struct aml_value *);
int			aml_evalinteger(struct acpi_softc *, struct aml_node *,
                            const char *, int, struct aml_value *, int64_t *);

void			aml_create_defaultobjects(void);

const char		*aml_nodename(struct aml_node *);

#define SRT_IRQ2		0x22
#define SRT_IRQ3		0x23
#define SRT_DMA			0x2A
#define SRT_STARTDEP0		0x30
#define SRT_STARTDEP1		0x31
#define SRT_ENDDEP		0x38
#define SRT_IOPORT		0x47
#define SRT_FIXEDPORT		0x4B
#define SRT_ENDTAG		0x79

#define SR_IRQ			0x04
#define SR_DMA			0x05
#define SR_STARTDEP		0x06
#define SR_ENDDEP		0x07
#define SR_IOPORT		0x08
#define SR_FIXEDPORT		0x09
#define SR_ENDTAG		0x0F
/* byte zero of small resources combines the tag above a length [1..7] */
#define	SR_TAG(tag,len)		((tag << 3) + (len))

#define LR_MEM24		0x81
#define LR_GENREGISTER		0x82
#define LR_MEM32		0x85
#define LR_MEM32FIXED		0x86
#define LR_DWORD		0x87
#define LR_WORD			0x88
#define LR_EXTIRQ		0x89
#define LR_QWORD		0x8A
#define LR_GPIO			0x8C
#define LR_SERBUS		0x8E

#define __amlflagbit(v,s,l)
union acpi_resource {
	struct {
		uint8_t  typecode;
		uint16_t length;
	}  __packed hdr;

	/* Small resource structures
	 * format of typecode is: tttttlll, t = type, l = length
	 */
	struct {
		uint8_t  typecode;
		uint16_t irq_mask;
		uint8_t  irq_flags;
#define SR_IRQ_SHR		(1L << 4)
#define SR_IRQ_POLARITY		(1L << 3)
#define SR_IRQ_MODE		(1L << 0)
	}  __packed sr_irq;
	struct {
		uint8_t  typecode;
		uint8_t  channel;
		uint8_t  flags;
#define SR_DMA_TYP_MASK		0x3
#define SR_DMA_TYP_SHIFT 	5
#define SR_DMA_BM		(1L << 2)
#define SR_DMA_SIZE_MASK	0x3
#define SR_DMA_SIZE_SHIFT	0
	}  __packed sr_dma;
	struct {
		uint8_t  typecode;
		uint8_t  flags;
#define SR_IOPORT_DEC		(1L << 0)
		uint16_t _min;
		uint16_t _max;
		uint8_t  _aln;
		uint8_t  _len;
	}  __packed sr_ioport;
	struct {
		uint8_t  typecode;
		uint16_t _bas;
		uint8_t  _len;
	}  __packed sr_fioport;

	/* Large resource structures */
	struct {
		uint8_t  typecode;
		uint16_t length;
		uint8_t  _info;
		uint16_t _min;
		uint16_t _max;
		uint16_t _aln;
		uint16_t _len;
	}  __packed lr_m24;
	struct {
		uint8_t  typecode;
		uint16_t length;
		uint8_t  _info;
		uint32_t _min;
		uint32_t _max;
		uint32_t _aln;
		uint32_t _len;
	}  __packed lr_m32;
	struct {
		uint8_t  typecode;
		uint16_t length;
		uint8_t  _info;
		uint32_t _bas;
		uint32_t _len;
	}  __packed lr_m32fixed;
	struct {
		uint8_t  typecode;
		uint16_t length;
		uint8_t  flags;
#define LR_EXTIRQ_SHR		(1L << 3)
#define LR_EXTIRQ_POLARITY	(1L << 2)
#define LR_EXTIRQ_MODE		(1L << 1)
		uint8_t  irq_count;
		uint32_t irq[1];
	} __packed lr_extirq;
	struct {
		uint8_t		typecode;
		uint16_t	length;
		uint8_t		type;
#define LR_TYPE_MEMORY		0
#define LR_TYPE_IO		1
#define LR_TYPE_BUS		2
		uint8_t		flags;
		uint8_t		tflags;
#define LR_MEMORY_TTP		(1L << 5)
#define LR_IO_TTP		(1L << 4)
		uint16_t	_gra;
		uint16_t	_min;
		uint16_t	_max;
		uint16_t	_tra;
		uint16_t	_len;
		uint8_t		src_index;
		char		src[1];
	} __packed lr_word;
	struct {
		uint8_t		typecode;
		uint16_t	length;
		uint8_t		type;
		uint8_t		flags;
		uint8_t		tflags;
		uint32_t	_gra;
		uint32_t	_min;
		uint32_t	_max;
		uint32_t	_tra;
		uint32_t	_len;
		uint8_t		src_index;
		char		src[1];
	} __packed lr_dword;
	struct {
		uint8_t		typecode;
		uint16_t	length;
		uint8_t		type;
		uint8_t		flags;
		uint8_t		tflags;
		uint64_t	_gra;
		uint64_t	_min;
		uint64_t	_max;
		uint64_t	_tra;
		uint64_t	_len;
		uint8_t		src_index;
		char		src[1];
	} __packed lr_qword;
	struct {
		uint8_t		typecode;
		uint16_t	length;
		uint8_t		revid;
		uint8_t		type;
#define LR_GPIO_INT	0x00
#define LR_GPIO_IO	0x01
		uint16_t	flags;
		uint16_t	tflags;
#define LR_GPIO_SHR		(3L << 3)
#define LR_GPIO_POLARITY	(3L << 1)
#define  LR_GPIO_ACTHI		(0L << 1)
#define  LR_GPIO_ACTLO		(1L << 1)
#define  LR_GPIO_ACTBOTH	(2L << 1)
#define LR_GPIO_MODE		(1L << 0)
#define  LR_GPIO_LEVEL		(0L << 0)
#define  LR_GPIO_EDGE		(1L << 0)
		uint8_t		_ppi;
		uint16_t	_drs;
		uint16_t	_dbt;
		uint16_t	pin_off;
		uint8_t		residx;
		uint16_t	res_off;
		uint16_t	vd_off;
		uint16_t	vd_len;
	} __packed lr_gpio;
	struct {
		uint8_t		typecode;
		uint16_t	length;
		uint8_t		revid;
		uint8_t		residx;
		uint8_t		type;
#define LR_SERBUS_I2C	1
		uint8_t		flags;
		uint16_t	tflags;
		uint8_t		trevid;
		uint16_t	tlength;
		uint8_t		tdata[1];
	} __packed lr_serbus;
	struct {
		uint8_t		typecode;
		uint16_t	length;
		uint8_t		revid;
		uint8_t		residx;
		uint8_t		type;
		uint8_t		flags;
		uint16_t	tflags;
		uint8_t		trevid;
		uint16_t	tlength;
		uint32_t	_spe;
		uint16_t	_adr;
		uint8_t		vdata[1];
	} __packed lr_i2cbus;
	uint8_t		pad[64];
} __packed;

#define AML_CRSTYPE(x)	((x)->hdr.typecode & 0x80 ? \
			    (x)->hdr.typecode : (x)->hdr.typecode >> 3)
#define AML_CRSLEN(x)	((x)->hdr.typecode & 0x80 ? \
			    3+(x)->hdr.length : 1+((x)->hdr.typecode & 0x7))

int			aml_print_resource(union acpi_resource *, void *);
int			aml_parse_resource(struct aml_value *,
			    int (*)(int, union acpi_resource *, void *),
			    void *);

#define ACPI_E_NOERROR   0x00
#define ACPI_E_BADVALUE  0x01

#define AML_MAX_ARG	 7
#define AML_MAX_LOCAL	 8

#define AML_WALK_PRE 0x00
#define AML_WALK_POST 0x01

void			aml_walknodes(struct aml_node *, int,
			    int (*)(struct aml_node *, void *), void *);

void			aml_postparse(void);

void			aml_hashopcodes(void);

void			aml_foreachpkg(struct aml_value *, int,
			    void (*fn)(struct aml_value *, void *), void *);

const char		*aml_val_to_string(const struct aml_value *);

void			aml_disasm(struct aml_scope *scope, int lvl,
			    void (*dbprintf)(void *, const char *, ...),
			    void *arg);
int			aml_evalhid(struct aml_node *, struct aml_value *);

int			acpi_walkmem(int, const char *);

#define aml_get8(p)    *(uint8_t *)(p)
#define aml_get16(p)   *(uint16_t *)(p)
#define aml_get32(p)   *(uint32_t *)(p)
#define aml_get64(p)   *(uint64_t *)(p)

union amlpci_t {
	uint64_t addr;
	struct {
		uint16_t reg;
		uint16_t fun;
		uint8_t dev;
		uint8_t bus;
		uint16_t seg;
	};
};
int			aml_rdpciaddr(struct aml_node *pcidev,
			    union amlpci_t *);

#ifndef SMALL_KERNEL
void			acpi_getdevlist(struct acpi_devlist_head *,
			    struct aml_node *, struct aml_value *, int);
#endif
void			aml_notify_dev(const char *, int);

void			acpi_freedevlist(struct acpi_devlist_head *);

void			acpi_glk_enter(void);
void			acpi_glk_leave(void);

/* https://docs.microsoft.com/en-us/windows-hardware/drivers/acpi/winacpi-osi */

enum acpi_osi {
	OSI_UNKNOWN = -1,
	OSI_WIN_2000,
	OSI_WIN_XP,
	OSI_WIN_2003,
	OSI_WIN_2003_SP1,
	OSI_WIN_XP_SP0,
	OSI_WIN_XP_SP1,
	OSI_WIN_XP_SP2,
	OSI_WIN_XP_SP3,
	OSI_WIN_XP_SP4,
	OSI_WIN_VISTA,
	OSI_WIN_2008,
	OSI_WIN_VISTA_SP1,
	OSI_WIN_VISTA_SP2,
	OSI_WIN_7,
	OSI_WIN_8,
	OSI_WIN_8_1,
	OSI_WIN_10,
	OSI_WIN_10_1607,
	OSI_WIN_10_1703,
	OSI_WIN_10_1709,
	OSI_WIN_10_1803,
	OSI_WIN_10_1809,
	OSI_WIN_10_1903,
	OSI_WIN_10_2004,
	OSI_WIN_11,
	OSI_WIN_11_22H2
};

#define AML_VALID_OSI		\
	"Windows 2000",		\
	"Windows 2001",		\
	"Windows 2001.1",	\
	"Windows 2001.1 SP1",	\
	"Windows 2001 SP0",	\
	"Windows 2001 SP1",	\
	"Windows 2001 SP2",	\
	"Windows 2001 SP3",	\
	"Windows 2001 SP4",	\
	"Windows 2006",		\
	"Windows 2006.1",	\
	"Windows 2006 SP1",	\
	"Windows 2006 SP2",	\
	"Windows 2009",		\
	"Windows 2012",		\
	"Windows 2013",		\
	"Windows 2015",		\
	"Windows 2016",		\
	"Windows 2017",		\
	"Windows 2017.2",	\
	"Windows 2018",		\
	"Windows 2018.2",	\
	"Windows 2019",		\
	"Windows 2020",		\
	"Windows 2021",		\
	"Windows 2022"

extern enum acpi_osi acpi_max_osi;	/* most recent Win version FW knows */

#endif /* __DEV_ACPI_DSDT_H__ */
