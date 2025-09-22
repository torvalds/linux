/*	$OpenBSD: partition_map.h,v 1.42 2016/01/31 23:00:11 krw Exp $	*/

/*
 * partition_map.h - partition map routines
 *
 * Written by Eryk Vershen
 */

/*
 * Copyright 1996,1998 by Apple Computer, Inc.
 *              All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * APPLE COMPUTER DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL APPLE COMPUTER BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __partition_map__
#define __partition_map__

#define	BLOCK0_SIGNATURE	0x4552	/* 'ER' */
#define	DPME_SIGNATURE		0x504D	/* 'PM' */

#define	DPISTRLEN	32

struct ddmap {
    uint32_t	ddBlock;	/* 1st driver's starting sbBlkSize block */
    uint16_t	ddSize;		/* size of 1st driver (512-byte blks) */
    uint16_t	ddType;		/* system type (1 for Mac+) */
};

struct entry;

struct partition_map {
    LIST_HEAD(, entry)	disk_order;
    LIST_HEAD(, entry)	base_order;
    char	       *name;
    int			fd;
    int			changed;
    int			blocks_in_map;
    int			maximum_in_map;
    unsigned long	media_size;	/* in physical blocks */

    /* On-disk block 0 data. */
    uint16_t		sbSig;		/* "ER" */
    uint16_t		sbBlkSize;	/* physical block size of device */
    uint32_t		sbBlkCount;	/* # of physical blocks on device */
    uint16_t		sbDevType;	/* device type */
    uint16_t		sbDevId;	/* device id */
    uint32_t		sbData;		/* not used */
    uint16_t		sbDrvrCount;	/* driver descriptor count */
    struct ddmap	sbDDMap[8];	/* driver descriptor map*/
    uint8_t		sbReserved[430];
};

struct entry {
    LIST_ENTRY(entry)		disk_entry;
    LIST_ENTRY(entry)		base_entry;
    struct partition_map       *the_map;
    long			disk_address;

    /* On-disk dpme block data.*/
    uint16_t	dpme_signature;		/* "PM" */
    uint8_t	dpme_reserved_1[2];
    uint32_t	dpme_map_entries;	/* # of partition entries */
    uint32_t	dpme_pblock_start;	/* physical block start of partition */
    uint32_t	dpme_pblocks;		/* physical block count of partition */
    char	dpme_name[DPISTRLEN+1];	/* name of partition + NUL */
    char	dpme_type[DPISTRLEN+1];	/* type of partition + NUL */
    uint32_t	dpme_lblock_start;	/* logical block start of partition */
    uint32_t	dpme_lblocks;		/* logical block count of partition */
    uint32_t	dpme_flags;
#define	DPME_OS_SPECIFIC_1	(1<<8)
#define	DPME_OS_SPECIFIC_2	(1<<7)
#define	DPME_OS_PIC_CODE	(1<<6)
#define	DPME_WRITABLE		(1<<5)
#define	DPME_READABLE		(1<<4)
#define	DPME_BOOTABLE		(1<<3)
#define	DPME_IN_USE		(1<<2)
#define	DPME_ALLOCATED		(1<<1)
#define	DPME_VALID		(1<<0)
    uint32_t	dpme_boot_block;	/* logical block start of boot code */
    uint32_t	dpme_boot_bytes;	/* byte count of boot code */
    uint16_t	dpme_load_addr;		/* memory address of boot code */
    uint8_t	dpme_reserved_2[4];
    uint32_t	dpme_goto_addr;		/* memory jump address of boot code */
    uint8_t	dpme_reserved_3[4];
    uint32_t	dpme_checksum;		/* of the boot code. */
    char	dpme_processor_id[17];	/* processor type + NUL */
    uint8_t	dpme_reserved_4[376];
};

extern const char *kFreeType;
extern const char *kMapType;
extern const char *kUnixType;
extern const char *kHFSType;

extern int lflag;
extern int rflag;

struct partition_map	*create_partition_map(int, char *, uint64_t, uint32_t);
struct partition_map	*open_partition_map(int, char *, uint64_t, uint32_t);

struct entry	*find_entry_by_disk_address(long, struct partition_map *);
struct entry	*find_entry_by_type(const char *, struct partition_map *);
struct entry	*find_entry_by_base(uint32_t, struct partition_map *);

int	add_partition_to_map(const char *, const char *, uint32_t, uint32_t,
    struct partition_map *);

int	contains_driver(struct entry *);
void	free_partition_map(struct partition_map *);
void	delete_partition_from_map(struct entry *);
void	move_entry_in_map(long, long, struct partition_map *);
void	resize_map(long new_size, struct partition_map *);
void	write_partition_map(struct partition_map *);
void	dpme_init_flags(struct entry *);

#endif /* __partition_map__ */
