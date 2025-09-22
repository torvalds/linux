/*	$OpenBSD: twereg.h,v 1.10 2023/04/11 00:45:08 jsg Exp $	*/

/*
 * Copyright (c) 2000 Michael Shalayeff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * most of the meaning for registers were taken from
 * freebsd driver, which in turn got 'em from linux.
 * it seems those got 'em from windows driver, in turn.
 */


/* general parameters */
#define	TWE_MAX_UNITS		16
#define	TWE_MAXOFFSETS		62
#define	TWE_MAXCMDS		255
#define	TWE_SECTOR_SIZE		512
#define	TWE_ALIGN		512
#define	TWE_MAXFER		(TWE_MAXOFFSETS * PAGE_SIZE)

/* registers */
#define	TWE_CONTROL		0x00
#define		TWE_CTRL_CHOSTI	0x00080000	/* clear host int */
#define		TWE_CTRL_CATTNI	0x00040000	/* clear attention int */
#define		TWE_CTRL_MCMDI	0x00020000	/* mask cmd int */
#define		TWE_CTRL_MRDYI	0x00010000	/* mask ready int */
#define		TWE_CTRL_ECMDI	0x00008000	/* enable cmd int */
#define		TWE_CTRL_ERDYI	0x00004000	/* enable ready int */
#define		TWE_CTRL_CERR	0x00000200	/* clear error status */
#define		TWE_CTRL_SRST	0x00000100	/* soft reset */
#define		TWE_CTRL_EINT	0x00000080	/* enable ints */
#define		TWE_CTRL_MINT	0x00000040	/* mask ints */
#define		TWE_CTRL_HOSTI	0x00000020	/* generate host int */
#define	TWE_STATUS		0x04
#define		TWE_STAT_MAJV	0xf0000000
#define		TWE_MAJV(st)	(((st) >> 28) & 0xf)
#define		TWE_STAT_MINV	0x0f000000
#define		TWE_MINV(st)	(((st) >> 24) & 0xf)
#define		TWE_STAT_PCIPAR	0x00800000
#define		TWE_STAT_QUEUEE	0x00400000
#define		TWE_STAT_CPUERR	0x00200000
#define		TWE_STAT_PCIABR	0x00100000
#define		TWE_STAT_HOSTI	0x00080000
#define		TWE_STAT_ATTNI	0x00040000
#define		TWE_STAT_CMDI	0x00020000
#define		TWE_STAT_RDYI	0x00010000
#define		TWE_STAT_CQF	0x00008000	/* cmd queue full */
#define		TWE_STAT_RQE	0x00004000	/* ready queue empty */
#define		TWE_STAT_CPURDY	0x00002000	/* cpu ready */
#define		TWE_STAT_CQR	0x00001000	/* cmd queue ready */
#define		TWE_STAT_FLAGS	0x00fff000	/* mask out other stuff */
#define		TWE_STAT_BITS	"\020\015cqr\016cpurdy\017rqe\20cqf"	\
    "\021rdyi\022cmdi\023attni\024hosti\025pciabr\026cpuerr\027queuee\030pcipar"

#define	TWE_COMMANDQUEUE	0x08
	/*
	 * the segs offset is encoded into upper 3 bits of the opcode.
	 * i bet other bits mean something too
	 * upper 8 bits is the command size in 32bit words.
	 */
#define		TWE_CMD_NOP	0x0200
#define		TWE_CMD_INIT	0x0301
#define		TWE_CMD_READ	0x0362
#define		TWE_CMD_WRITE	0x0363
#define		TWE_CMD_RDVRFY	0x0364
#define		TWE_CMD_VERIFY	0x0365
#define		TWE_CMD_ZRFUNIT	0x0208
#define		TWE_CMD_RPLUNIT	0x0209
#define		TWE_CMD_HOTSWAP	0x020a
#define		TWE_CMD_SETATA	0x020c
#define		TWE_CMD_FLUSH	0x020e
#define		TWE_CMD_ABORT	0x020f
#define		TWE_CMD_QSTAT	0x0210
#define		TWE_CMD_GPARAM	0x0252
#define		TWE_CMD_SPARAM	0x0253
#define		TWE_CMD_NEWUNIT	0x0214
#define		TWE_CMD_DELUNIT	0x0215
#define		TWE_CMD_RBLUNIT	0x0217	/* rebuild */
#define		TWE_CMD_SECINF	0x021a
#define		TWE_CMD_AEN	0x021c
#define		TWE_CMD_CMDPK	0x021d
#define	TWE_READYQUEUE		0x0c
#define		TWE_READYID(u)	(((u) >> 4) & 0xff)

/*
 * From 3ware's documentation:
 *
 *   All parameters maintained by the controller are grouped into related
 *   tables.  Tables are accessed indirectly via get and set parameter
 *   commands.  To access a specific parameter in a table, the table ID and
 *   parameter index are used to uniquely identify a parameter.  Table
 *   0xffff is the directory table and provides a list of the table IDs and
 *   sizes of all other tables.  Index zero in each table specifies the
 *   entire table, and index one specifies the size of the table.  An entire
 *   table can be read or set by using index zero.
 */

/* get/set param table ids */
#define	TWE_PARAM_ALL	0x000	/* everything */
#define	TWE_PARAM_DSUM	0x002	/* drive summary */
#define	TWE_PARAM_UC	0x003	/* unit config */
#define	TWE_PARAM_DC	0x200	/* + 15 -- drive config (doc says 0x100) */
#define	TWE_PARAM_UI	0x300	/* + 16 -- unit information */
#define	TWE_PARAM_AEN	0x401
#define	TWE_PARAM_VER	0x402	/* version info */
#define	TWE_PARAM_CTRL	0x403	/* controller info */
#define	TWE_PARAM_FTRS	0x404	/* features */
#define	TWE_PARAM_DIR	0xffff	/* param table directory */

#define	TWE_AEN_QEMPTY	0x0000
#define	TWE_AEN_SRST	0x0001	/* soft reset */
#define	TWE_AEN_DMIRROR	0x0002	/* degraded mirror */
#define	TWE_AEN_CERROR	0x0003	/* controller error */
#define	TWE_AEN_RBFAIL	0x0004	/* rebuild failed */
#define	TWE_AEN_RBDONE	0x0005	/* rebuild done */
#define	TWE_AEN_ILLUN	0x0006	/* incompatible unit */
#define	TWE_AEN_INDONE	0x0007	/* init done */
#define	TWE_AEN_DSHUT	0x0008	/* unclean shutdown */
#define	TWE_AEN_APORT	0x0009	/* aport timeout */
#define	TWE_AEN_DRVERR	0x000a	/* drive error */
#define	TWE_AEN_RBSTART	0x000b	/* rebuild start */
#define	TWE_AEN_ISTART	0x000c	/* init started */
#define	TWE_AEN_TUN	0x0015	/* table undefined */
/*	TWE_AEN_	0x0000	 * dunno what this is (yet) */
#define	TWE_AEN_QFULL	0x00ff

/* struct definitions */
struct twe_param {
	u_int16_t	table_id;
	u_int8_t	param_id;
	u_int8_t	param_size;
	u_int8_t	data[1];
} __packed;

struct twe_segs {
	u_int32_t twes_addr;
	u_int32_t twes_len;
} __packed;

struct twe_cmd {
	u_int16_t	cmd_op;
	u_int8_t	cmd_index;
	u_int8_t	cmd_unit_host;
#define	TWE_UNITHOST(u, h)	(((u) & 0xf) | ((h) << 4))
	u_int8_t	cmd_status;
	u_int8_t	cmd_flags;
#define	TWE_FLAGS_CACHEDISABLE		0x01
	union {
		struct {
			u_int16_t	count;
			u_int32_t	lba;
			struct twe_segs	segs[TWE_MAXOFFSETS];
			u_int32_t	pad;
		} __packed _cmd_io;
#define	cmd_io		_._cmd_io
		struct {
			u_int16_t	count;
			struct twe_segs	segs[TWE_MAXOFFSETS];
		} __packed _cmd_param;
#define	cmd_param	_._cmd_param
		struct {
			u_int16_t	msgcr;
			u_int32_t	rdy_q_ptr;
		} __packed _cmd_init;
#define	cmd_init	_._cmd_init
		struct {
			u_int8_t	action;
#define	TWE_HSWAP_REMOVE	0
#define	TWE_HSWAP_ADDCBOD	1
#define	TWE_HSWAP_ADDSPARE	2
			u_int8_t	port;
		} __packed _cmd_aport;
#define	cmd_hswap	_._cmd_hswap
		struct {
			u_int8_t	feature;
#define	TWE_ATA_WCE	0x02
#define	TWE_ATA_NWCE	0x82
			u_int8_t	mode;
			u_int16_t	units;
			u_int16_t	persist;
		} __packed _cmd_ata;
#define	cmd_ata		_._cmd_ata
		u_int16_t	cmd_status;
		struct {
			u_int8_t	action;
#define	TWE_REBUILD_NOP		0
#define	TWE_REBUILD_STOP	2
#define	TWE_REBUILD_START	4
#define	TWE_REBUILD_STARTU	5
#define	TWE_REBUILD_CS		0x80
			u_int8_t	subunit;	/* raid10 lu rebuild */
		} __packed _cmd_rebuild;
#define	cmd_rebuild	_._cmd_rebuild
	} _;
} __packed;	/* 512 bytes */
