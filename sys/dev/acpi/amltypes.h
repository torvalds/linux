/* $OpenBSD: amltypes.h,v 1.51 2025/06/11 09:57:01 kettenis Exp $ */
/*
 * Copyright (c) 2005 Jordan Hargrave <jordan@openbsd.org>
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

#ifndef __DEV_ACPI_AMLTYPES_H__
#define __DEV_ACPI_AMLTYPES_H__

/* AML Opcodes */
#define AMLOP_ZERO		0x00
#define AMLOP_ONE		0x01
#define AMLOP_ALIAS		0x06
#define AMLOP_NAME		0x08
#define AMLOP_BYTEPREFIX	0x0A
#define AMLOP_WORDPREFIX	0x0B
#define AMLOP_DWORDPREFIX	0x0C
#define AMLOP_STRINGPREFIX	0x0D
#define AMLOP_QWORDPREFIX	0x0E
#define AMLOP_SCOPE		0x10
#define AMLOP_BUFFER		0x11
#define AMLOP_PACKAGE		0x12
#define AMLOP_VARPACKAGE	0x13
#define AMLOP_METHOD		0x14
#define AMLOP_DUALNAMEPREFIX	0x2E
#define AMLOP_MULTINAMEPREFIX	0x2F
#define AMLOP_EXTPREFIX		0x5B
#define AMLOP_MUTEX		0x5B01
#define AMLOP_EVENT		0x5B02
#define AMLOP_CONDREFOF		0x5B12
#define AMLOP_CREATEFIELD	0x5B13
#define AMLOP_LOADTABLE		0x5B1F
#define AMLOP_LOAD		0x5B20
#define AMLOP_STALL		0x5B21
#define AMLOP_SLEEP		0x5B22
#define AMLOP_ACQUIRE		0x5B23
#define AMLOP_SIGNAL		0x5B24
#define AMLOP_WAIT		0x5B25
#define AMLOP_RESET		0x5B26
#define AMLOP_RELEASE		0x5B27
#define AMLOP_FROMBCD		0x5B28
#define AMLOP_TOBCD		0x5B29
#define AMLOP_UNLOAD		0x5B2A
#define AMLOP_REVISION		0x5B30
#define AMLOP_DEBUG		0x5B31
#define AMLOP_FATAL		0x5B32
#define AMLOP_TIMER		0x5B33
#define AMLOP_OPREGION		0x5B80
#define AMLOP_FIELD		0x5B81
#define AMLOP_DEVICE		0x5B82
#define AMLOP_PROCESSOR		0x5B83
#define AMLOP_POWERRSRC		0x5B84
#define AMLOP_THERMALZONE	0x5B85
#define AMLOP_INDEXFIELD	0x5B86
#define AMLOP_BANKFIELD		0x5B87
#define AMLOP_DATAREGION	0x5B88
#define AMLOP_ROOTCHAR		0x5C
#define AMLOP_PARENTPREFIX	0x5E
#define AMLOP_NAMECHAR		0x5F
#define AMLOP_LOCAL0		0x60
#define AMLOP_LOCAL1		0x61
#define AMLOP_LOCAL2		0x62
#define AMLOP_LOCAL3		0x63
#define AMLOP_LOCAL4		0x64
#define AMLOP_LOCAL5		0x65
#define AMLOP_LOCAL6		0x66
#define AMLOP_LOCAL7		0x67
#define AMLOP_ARG0		0x68
#define AMLOP_ARG1		0x69
#define AMLOP_ARG2		0x6A
#define AMLOP_ARG3		0x6B
#define AMLOP_ARG4		0x6C
#define AMLOP_ARG5		0x6D
#define AMLOP_ARG6		0x6E
#define AMLOP_STORE		0x70
#define AMLOP_REFOF		0x71
#define AMLOP_ADD		0x72
#define AMLOP_CONCAT		0x73
#define AMLOP_SUBTRACT		0x74
#define AMLOP_INCREMENT		0x75
#define AMLOP_DECREMENT		0x76
#define AMLOP_MULTIPLY		0x77
#define AMLOP_DIVIDE		0x78
#define AMLOP_SHL		0x79
#define AMLOP_SHR		0x7A
#define AMLOP_AND		0x7B
#define AMLOP_NAND		0x7C
#define AMLOP_OR		0x7D
#define AMLOP_NOR		0x7E
#define AMLOP_XOR		0x7F
#define AMLOP_NOT		0x80
#define AMLOP_FINDSETLEFTBIT	0x81
#define AMLOP_FINDSETRIGHTBIT	0x82
#define AMLOP_DEREFOF		0x83
#define AMLOP_CONCATRES		0x84
#define AMLOP_MOD		0x85
#define AMLOP_NOTIFY		0x86
#define AMLOP_SIZEOF		0x87
#define AMLOP_INDEX		0x88
#define AMLOP_MATCH		0x89
#define AMLOP_CREATEDWORDFIELD	0x8A
#define AMLOP_CREATEWORDFIELD	0x8B
#define AMLOP_CREATEBYTEFIELD	0x8C
#define AMLOP_CREATEBITFIELD	0x8D
#define AMLOP_OBJECTTYPE	0x8E
#define AMLOP_CREATEQWORDFIELD	0x8F
#define AMLOP_LAND		0x90
#define AMLOP_LOR		0x91
#define AMLOP_LNOT		0x92
#define AMLOP_LNOTEQUAL		0x9293
#define AMLOP_LLESSEQUAL	0x9294
#define AMLOP_LGREATEREQUAL	0x9295
#define AMLOP_LEQUAL		0x93
#define AMLOP_LGREATER		0x94
#define AMLOP_LLESS		0x95
#define AMLOP_TOBUFFER		0x96
#define AMLOP_TODECSTRING	0x97
#define AMLOP_TOHEXSTRING	0x98
#define AMLOP_TOINTEGER		0x99
#define AMLOP_TOSTRING		0x9C
#define AMLOP_COPYOBJECT	0x9D
#define AMLOP_MID		0x9E
#define AMLOP_CONTINUE		0x9F
#define AMLOP_IF		0xA0
#define AMLOP_ELSE		0xA1
#define AMLOP_WHILE		0xA2
#define AMLOP_NOP		0xA3
#define AMLOP_RETURN		0xA4
#define AMLOP_BREAK		0xA5
#define AMLOP_BREAKPOINT	0xCC
#define AMLOP_ONES		0xFF

#define AMLOP_FIELDUNIT		0xFE00
#define AML_ANYINT		0xFF00

/*
 * Comparison types for Match()
 *
 *  true,==,<=,<,>=,>
 */
#define AML_MATCH_TR		0
#define AML_MATCH_EQ		1
#define AML_MATCH_LE		2
#define AML_MATCH_LT		3
#define AML_MATCH_GE		4
#define AML_MATCH_GT		5

/* Defined types for ObjectType() */
enum aml_objecttype {
	AML_OBJTYPE_UNINITIALIZED = 0,
	AML_OBJTYPE_INTEGER,
	AML_OBJTYPE_STRING,
	AML_OBJTYPE_BUFFER,
	AML_OBJTYPE_PACKAGE,
	AML_OBJTYPE_FIELDUNIT,
	AML_OBJTYPE_DEVICE,
	AML_OBJTYPE_EVENT,
	AML_OBJTYPE_METHOD,
	AML_OBJTYPE_MUTEX,
	AML_OBJTYPE_OPREGION,
	AML_OBJTYPE_POWERRSRC,
	AML_OBJTYPE_PROCESSOR,
	AML_OBJTYPE_THERMZONE,
	AML_OBJTYPE_BUFFERFIELD,
	AML_OBJTYPE_DDBHANDLE,
	AML_OBJTYPE_DEBUGOBJ,

	AML_OBJTYPE_NAMEREF = 0x100,
	AML_OBJTYPE_OBJREF,
	AML_OBJTYPE_SCOPE,
	AML_OBJTYPE_NOTARGET,
	AML_OBJTYPE_HEXSTRING,
	AML_OBJTYPE_DECSTRING,
};

/* AML Opcode Arguments */
#define AML_ARG_INTEGER		'i'
#define AML_ARG_BYTE		'b'
#define AML_ARG_WORD		'w'
#define AML_ARG_DWORD		'd'
#define AML_ARG_QWORD		'q'
#define AML_ARG_IMPBYTE		'!'
#define AML_ARG_OBJLEN		'p'
#define AML_ARG_STRING		'a'
#define AML_ARG_BYTELIST	'B'
#define AML_ARG_REVISION	'R'

#define AML_ARG_METHOD		'M'
#define AML_ARG_NAMESTRING	'N'
#define AML_ARG_NAMEREF		'n'
#define AML_ARG_FIELDLIST	'F'
#define AML_ARG_FLAG		'f'

#define AML_ARG_DATAOBJLIST	'O'
#define AML_ARG_DATAOBJ		'o'

#define AML_ARG_SIMPLENAME	's'
#define AML_ARG_SUPERNAME	'S'

#define AML_ARG_TERMOBJLIST	'T'
#define AML_ARG_TERMOBJ		't'

#define AML_ARG_IFELSE          'I'
#define AML_ARG_BUFFER          'B'
#define AML_ARG_SEARCHNAME      'n'
#define AML_ARG_CREATENAME      'N'
#define AML_ARG_STKARG          'A'
#define AML_ARG_STKLOCAL        'L'
#define AML_ARG_DEBUG           'D'
#define AML_ARG_CONST           'c'
#define AML_ARG_TARGET          'r'

#define AML_METHOD_ARGCOUNT(v)	 (((v) >> 0) & 0x7)
#define AML_METHOD_SERIALIZED(v) (((v) >> 3) & 0x1)
#define AML_METHOD_SYNCLEVEL(v)	 (((v) >> 4) & 0xF)

#define AML_FIELD_ACCESSMASK	0x0F
#define AML_FIELD_SETATTR(f,t,a) (((f) & 0xF0) | ((t) & 0xF) | ((a)<<8))
#define AML_FIELD_ACCESS(v)	(((v) >> 0) & 0xF)
# define AML_FIELD_ANYACC	0x0
# define AML_FIELD_BYTEACC	0x1
# define AML_FIELD_WORDACC	0x2
# define AML_FIELD_DWORDACC	0x3
# define AML_FIELD_QWORDACC	0x4
# define AML_FIELD_BUFFERACC	0x5
#define AML_FIELD_LOCK(v)	(((v) >> 4) & 0x1)
# define AML_FIELD_LOCK_OFF	0x0
# define AML_FIELD_LOCK_ON	0x1
#define AML_FIELD_UPDATE(v)	(((v) >> 5) & 0x3)
# define AML_FIELD_PRESERVE	0x0
# define AML_FIELD_WRITEASONES	0x1
# define AML_FIELD_WRITEASZEROES 0x2
#define AML_FIELD_ATTR(v)	((v) >> 8)
#define AML_FIELD_RESERVED	0x00
/* XXX fix this name */
#define AML_FIELD_ATTR__		0x01

struct aml_scope;
struct aml_node;

struct aml_waitq {
	struct aml_scope          *scope;
	SIMPLEQ_ENTRY(aml_waitq)   link;
};
SIMPLEQ_HEAD(aml_waitq_head, aml_waitq);

/* AML Object Value */
struct aml_value {
	int	type;
	int	length;
	int	refcnt;
	int	stack;
	struct aml_node *node;
	union {
		int64_t		vinteger;
		char		*vstring;
		uint8_t		*vbuffer;
		struct aml_value **vpackage;
		struct {
			uint8_t		iospace;
			uint64_t	iobase;
			uint32_t	iolen;
			int		flag;
		} vopregion;
		struct {
			int		flags;
			uint8_t		*start;
			uint8_t		*end;
			struct aml_value *(*fneval)(struct aml_scope *, struct aml_value *);
			uint8_t	        *base;
		} vmethod;
		struct {
			uint16_t	 type;
			uint16_t	 flags;
			uint32_t	 bitpos;
			uint32_t	 bitlen;
			struct aml_value *ref1;
			struct aml_value *ref2;
			int		 ref3;
		} vfield;
		struct {
			uint8_t		proc_id;
			uint32_t	proc_addr;
			uint8_t		proc_len;
		} vprocessor;
		struct {
			int		type;
			int		index;
			struct aml_value *ref;
		} vobjref;
		struct {
			uint8_t		pwr_level;
			uint16_t	pwr_order;
		} vpowerrsrc;
		struct acpi_mutex	*vmutex;
		struct {
			uint8_t	         *name;
			struct aml_node  *node;
		} vnameref;
		struct {
			int               synclvl;
			int               savelvl;
			int               count;
			char              ownername[5];
			struct aml_scope *owner;
			struct aml_waitq_head    waiters;
		} Vmutex;
		struct {
			int               state;
			struct aml_waitq_head    waiters;
		} Vevent;
	} _;
};

#define v_nameref		_.vbuffer
#define v_objref		_.vobjref
#define v_integer		_.vinteger
#define v_string		_.vstring
#define v_buffer		_.vbuffer
#define v_package		_.vpackage
#define v_field			_.vfield
#define v_opregion		_.vopregion
#define v_method		_.vmethod
#define v_processor		_.vprocessor
#define v_powerrsrc		_.vpowerrsrc
#define v_mutex			_.vmutex
#define v_mtx                   _.Vmutex
#define v_evt                   _.Vevent

#define xaml_intval(v)		((v)->v_integer)
#define aml_strlen(v)		((v)->length)
#define aml_strval(v)		((v)->v_string ? (v)->v_string : "bad string")
#define aml_buflen(v)		((v)->length)
#define aml_bufval(v)		((v)->v_buffer)
#define aml_pkglen(v)		((v)->length)
#define aml_pkgval(v,i)		(&(v)->v_package[(i)])

struct acpi_pci {
	TAILQ_ENTRY(acpi_pci)		next;

	struct aml_node			*node;
	struct device			*device;

	int				sub;
	int				seg;
	int				bus;
	int				dev;
	int				fun;

	int				_s0w;
	int				_s3d;
	int				_s3w;
	int				_s4d;
	int				_s4w;
};

struct acpi_gpio {
	void	*cookie;
	int	(*read_pin)(void *, int);
	void	(*write_pin)(void *, int, int);
	void	(*intr_establish)(void *, int, int, int,
		    int (*)(void *), void *);
	void	(*intr_enable)(void *, int);
	void	(*intr_disable)(void *, int);
};

struct i2c_controller;

struct aml_node {
	struct aml_node *parent;

	SIMPLEQ_HEAD(,aml_node)	son;
	SIMPLEQ_ENTRY(aml_node)	sib;

	int		attached;

	char		name[5];
	uint16_t	opcode;
	uint8_t		*start;
	uint8_t		*end;

	struct aml_value *value;
	struct acpi_pci *pci;
	struct acpi_gpio *gpio;
	struct i2c_controller *i2c;
};

#define aml_bitmask(n)		(1L << ((n) & 0x7))
#define aml_bitpos(n)		((n)&0x7)
#define aml_bytepos(n)		((n)>>3)
#define aml_bytelen(n)		(((n)+7)>>3)
#define aml_bytealigned(x)	!((x)&0x7)

#define AML_NO_TIMEOUT		0xffff

#endif /* __DEV_ACPI_AMLTYPES_H__ */
