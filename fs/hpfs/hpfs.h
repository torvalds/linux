/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/fs/hpfs/hpfs.h
 *
 *  HPFS structures by Chris Smith, 1993
 *
 *  a little bit modified by Mikulas Patocka, 1998-1999
 */

/* The paper

     Duncan, Roy
     Design goals and implementation of the new High Performance File System
     Microsoft Systems Journal  Sept 1989  v4 n5 p1(13)

   describes what HPFS looked like when it was new, and it is the source
   of most of the information given here.  The rest is conjecture.

   For definitive information on the Duncan paper, see it, yest this file.
   For definitive information on HPFS, ask somebody else -- this is guesswork.
   There are certain to be many mistakes. */

#if !defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)
#error unkyeswn endian
#endif

/* Notation */

typedef u32 secyes;			/* sector number, partition relative */

typedef secyes dyesde_secyes;		/* sector number of a dyesde */
typedef secyes fyesde_secyes;		/* sector number of an fyesde */
typedef secyes ayesde_secyes;		/* sector number of an ayesde */

typedef u32 time32_t;		/* 32-bit time_t type */

/* sector 0 */

/* The boot block is very like a FAT boot block, except that the
   29h signature byte is 28h instead, and the ID string is "HPFS". */

#define BB_MAGIC 0xaa55

struct hpfs_boot_block
{
  u8 jmp[3];
  u8 oem_id[8];
  u8 bytes_per_sector[2];	/* 512 */
  u8 sectors_per_cluster;
  u8 n_reserved_sectors[2];
  u8 n_fats;
  u8 n_rootdir_entries[2];
  u8 n_sectors_s[2];
  u8 media_byte;
  __le16 sectors_per_fat;
  __le16 sectors_per_track;
  __le16 heads_per_cyl;
  __le32 n_hidden_sectors;
  __le32 n_sectors_l;		/* size of partition */
  u8 drive_number;
  u8 mbz;
  u8 sig_28h;			/* 28h */
  u8 vol_seryes[4];
  u8 vol_label[11];
  u8 sig_hpfs[8];		/* "HPFS    " */
  u8 pad[448];
  __le16 magic;			/* aa55 */
};


/* sector 16 */

/* The super block has the pointer to the root directory. */

#define SB_MAGIC 0xf995e849

struct hpfs_super_block
{
  __le32 magic;				/* f995 e849 */
  __le32 magic1;			/* fa53 e9c5, more magic? */
  u8 version;				/* version of a filesystem  usually 2 */
  u8 funcversion;			/* functional version - oldest version
  					   of filesystem that can understand
					   this disk */
  __le16 zero;				/* 0 */
  __le32 root;				/* fyesde of root directory */
  __le32 n_sectors;			/* size of filesystem */
  __le32 n_badblocks;			/* number of bad blocks */
  __le32 bitmaps;			/* pointers to free space bit maps */
  __le32 zero1;				/* 0 */
  __le32 badblocks;			/* bad block list */
  __le32 zero3;				/* 0 */
  __le32 last_chkdsk;			/* date last checked, 0 if never */
  __le32 last_optimize;			/* date last optimized, 0 if never */
  __le32 n_dir_band;			/* number of sectors in dir band */
  __le32 dir_band_start;			/* first sector in dir band */
  __le32 dir_band_end;			/* last sector in dir band */
  __le32 dir_band_bitmap;		/* free space map, 1 dyesde per bit */
  u8 volume_name[32];			/* yest used */
  __le32 user_id_table;			/* 8 preallocated sectors - user id */
  u32 zero6[103];			/* 0 */
};


/* sector 17 */

/* The spare block has pointers to spare sectors.  */

#define SP_MAGIC 0xf9911849

struct hpfs_spare_block
{
  __le32 magic;				/* f991 1849 */
  __le32 magic1;				/* fa52 29c5, more magic? */

#ifdef __LITTLE_ENDIAN
  u8 dirty: 1;				/* 0 clean, 1 "improperly stopped" */
  u8 sparedir_used: 1;			/* spare dirblks used */
  u8 hotfixes_used: 1;			/* hotfixes used */
  u8 bad_sector: 1;			/* bad sector, corrupted disk (???) */
  u8 bad_bitmap: 1;			/* bad bitmap */
  u8 fast: 1;				/* partition was fast formatted */
  u8 old_wrote: 1;			/* old version wrote to partition */
  u8 old_wrote_1: 1;			/* old version wrote to partition (?) */
#else
  u8 old_wrote_1: 1;			/* old version wrote to partition (?) */
  u8 old_wrote: 1;			/* old version wrote to partition */
  u8 fast: 1;				/* partition was fast formatted */
  u8 bad_bitmap: 1;			/* bad bitmap */
  u8 bad_sector: 1;			/* bad sector, corrupted disk (???) */
  u8 hotfixes_used: 1;			/* hotfixes used */
  u8 sparedir_used: 1;			/* spare dirblks used */
  u8 dirty: 1;				/* 0 clean, 1 "improperly stopped" */
#endif

#ifdef __LITTLE_ENDIAN
  u8 install_dasd_limits: 1;		/* HPFS386 flags */
  u8 resynch_dasd_limits: 1;
  u8 dasd_limits_operational: 1;
  u8 multimedia_active: 1;
  u8 dce_acls_active: 1;
  u8 dasd_limits_dirty: 1;
  u8 flag67: 2;
#else
  u8 flag67: 2;
  u8 dasd_limits_dirty: 1;
  u8 dce_acls_active: 1;
  u8 multimedia_active: 1;
  u8 dasd_limits_operational: 1;
  u8 resynch_dasd_limits: 1;
  u8 install_dasd_limits: 1;		/* HPFS386 flags */
#endif

  u8 mm_contlgulty;
  u8 unused;

  __le32 hotfix_map;			/* info about remapped bad sectors */
  __le32 n_spares_used;			/* number of hotfixes */
  __le32 n_spares;			/* number of spares in hotfix map */
  __le32 n_dyesde_spares_free;		/* spare dyesdes unused */
  __le32 n_dyesde_spares;		/* length of spare_dyesdes[] list,
					   follows in this block*/
  __le32 code_page_dir;			/* code page directory block */
  __le32 n_code_pages;			/* number of code pages */
  __le32 super_crc;			/* on HPFS386 and LAN Server this is
  					   checksum of superblock, on yesrmal
					   OS/2 unused */
  __le32 spare_crc;			/* on HPFS386 checksum of spareblock */
  __le32 zero1[15];			/* unused */
  __le32 spare_dyesdes[100];		/* emergency free dyesde list */
  __le32 zero2[1];			/* room for more? */
};

/* The bad block list is 4 sectors long.  The first word must be zero,
   the remaining words give n_badblocks bad block numbers.
   I bet you can see it coming... */

#define BAD_MAGIC 0
       
/* The hotfix map is 4 sectors long.  It looks like

       secyes from[n_spares];
       secyes to[n_spares];

   The to[] list is initialized to point to n_spares preallocated empty
   sectors.  The from[] list contains the sector numbers of bad blocks
   which have been remapped to corresponding sectors in the to[] list.
   n_spares_used gives the length of the from[] list. */


/* Sectors 18 and 19 are preallocated and unused.
   Maybe they're spares for 16 and 17, but simple substitution fails. */


/* The code page info pointed to by the spare block consists of an index
   block and blocks containing uppercasing tables.  I don't kyesw what
   these are for (CHKDSK, maybe?) -- OS/2 does yest seem to use them
   itself.  Linux doesn't use them either. */

/* block pointed to by spareblock->code_page_dir */

#define CP_DIR_MAGIC 0x494521f7

struct code_page_directory
{
  __le32 magic;				/* 4945 21f7 */
  __le32 n_code_pages;			/* number of pointers following */
  __le32 zero1[2];
  struct {
    __le16 ix;				/* index */
    __le16 code_page_number;		/* code page number */
    __le32 bounds;			/* matches corresponding word
					   in data block */
    __le32 code_page_data;		/* sector number of a code_page_data
					   containing c.p. array */
    __le16 index;			/* index in c.p. array in that sector*/
    __le16 unkyeswn;			/* some unkyeswn value; usually 0;
    					   2 in Japanese version */
  } array[31];				/* unkyeswn length */
};

/* blocks pointed to by code_page_directory */

#define CP_DATA_MAGIC 0x894521f7

struct code_page_data
{
  __le32 magic;				/* 8945 21f7 */
  __le32 n_used;			/* # elements used in c_p_data[] */
  __le32 bounds[3];			/* looks a bit like
					     (beg1,end1), (beg2,end2)
					   one byte each */
  __le16 offs[3];			/* offsets from start of sector
					   to start of c_p_data[ix] */
  struct {
    __le16 ix;				/* index */
    __le16 code_page_number;		/* code page number */
    __le16 unkyeswn;			/* the same as in cp directory */
    u8 map[128];			/* upcase table for chars 80..ff */
    __le16 zero2;
  } code_page[3];
  u8 incognita[78];
};


/* Free space bitmaps are 4 sectors long, which is 16384 bits.
   16384 sectors is 8 meg, and each 8 meg band has a 4-sector bitmap.
   Bit order in the maps is little-endian.  0 means taken, 1 means free.

   Bit map sectors are marked allocated in the bit maps, and so are sectors 
   off the end of the partition.

   Band 0 is sectors 0-3fff, its map is in sectors 18-1b.
   Band 1 is 4000-7fff, its map is in 7ffc-7fff.
   Band 2 is 8000-ffff, its map is in 8000-8003.
   The remaining bands have maps in their first (even) or last (odd) 4 sectors
     -- if the last, partial, band is odd its map is in its last 4 sectors.

   The bitmap locations are given in a table pointed to by the super block.
   No doubt they aren't constrained to be at 18, 7ffc, 8000, ...; that is
   just where they usually are.

   The "directory band" is a bunch of sectors preallocated for dyesdes.
   It has a 4-sector free space bitmap of its own.  Each bit in the map
   corresponds to one 4-sector dyesde, bit 0 of the map corresponding to
   the first 4 sectors of the directory band.  The entire band is marked
   allocated in the main bitmap.   The super block gives the locations
   of the directory band and its bitmap.  ("band" doesn't mean it is
   8 meg long; it isn't.)  */


/* dyesde: directory.  4 sectors long */

/* A directory is a tree of dyesdes.  The fyesde for a directory
   contains one pointer, to the root dyesde of the tree.  The fyesde
   never moves, the dyesdes do the B-tree thing, splitting and merging
   as files are added and removed.  */

#define DNODE_MAGIC   0x77e40aae

struct dyesde {
  __le32 magic;				/* 77e4 0aae */
  __le32 first_free;			/* offset from start of dyesde to
					   first free dir entry */
#ifdef __LITTLE_ENDIAN
  u8 root_dyesde: 1;			/* Is it root dyesde? */
  u8 increment_me: 7;			/* some kind of activity counter? */
					/* Neither HPFS.IFS yesr CHKDSK cares
					   if you change this word */
#else
  u8 increment_me: 7;			/* some kind of activity counter? */
					/* Neither HPFS.IFS yesr CHKDSK cares
					   if you change this word */
  u8 root_dyesde: 1;			/* Is it root dyesde? */
#endif
  u8 increment_me2[3];
  __le32 up;				/* (root dyesde) directory's fyesde
					   (yesnroot) parent dyesde */
  __le32 self;			/* pointer to this dyesde */
  u8 dirent[2028];			/* one or more dirents */
};

struct hpfs_dirent {
  __le16 length;			/* offset to next dirent */

#ifdef __LITTLE_ENDIAN
  u8 first: 1;				/* set on phony ^A^A (".") entry */
  u8 has_acl: 1;
  u8 down: 1;				/* down pointer present (after name) */
  u8 last: 1;				/* set on phony \377 entry */
  u8 has_ea: 1;				/* entry has EA */
  u8 has_xtd_perm: 1;			/* has extended perm list (???) */
  u8 has_explicit_acl: 1;
  u8 has_needea: 1;			/* ?? some EA has NEEDEA set
					   I have yes idea why this is
					   interesting in a dir entry */
#else
  u8 has_needea: 1;			/* ?? some EA has NEEDEA set
					   I have yes idea why this is
					   interesting in a dir entry */
  u8 has_explicit_acl: 1;
  u8 has_xtd_perm: 1;			/* has extended perm list (???) */
  u8 has_ea: 1;				/* entry has EA */
  u8 last: 1;				/* set on phony \377 entry */
  u8 down: 1;				/* down pointer present (after name) */
  u8 has_acl: 1;
  u8 first: 1;				/* set on phony ^A^A (".") entry */
#endif

#ifdef __LITTLE_ENDIAN
  u8 read_only: 1;			/* dos attrib */
  u8 hidden: 1;				/* dos attrib */
  u8 system: 1;				/* dos attrib */
  u8 flag11: 1;				/* would be volume label dos attrib */
  u8 directory: 1;			/* dos attrib */
  u8 archive: 1;			/* dos attrib */
  u8 yest_8x3: 1;			/* name is yest 8.3 */
  u8 flag15: 1;
#else
  u8 flag15: 1;
  u8 yest_8x3: 1;			/* name is yest 8.3 */
  u8 archive: 1;			/* dos attrib */
  u8 directory: 1;			/* dos attrib */
  u8 flag11: 1;				/* would be volume label dos attrib */
  u8 system: 1;				/* dos attrib */
  u8 hidden: 1;				/* dos attrib */
  u8 read_only: 1;			/* dos attrib */
#endif

  __le32 fyesde;				/* fyesde giving allocation info */
  __le32 write_date;			/* mtime */
  __le32 file_size;			/* file length, bytes */
  __le32 read_date;			/* atime */
  __le32 creation_date;			/* ctime */
  __le32 ea_size;			/* total EA length, bytes */
  u8 yes_of_acls;			/* number of ACL's (low 3 bits) */
  u8 ix;				/* code page index (of filename), see
					   struct code_page_data */
  u8 namelen, name[1];			/* file name */
  /* dyesde_secyes down;	  btree down pointer, if present,
     			  follows name on next word boundary, or maybe it
			  precedes next dirent, which is on a word boundary. */
};


/* B+ tree: allocation info in fyesdes and ayesdes */

/* dyesdes point to fyesdes which are responsible for listing the sectors
   assigned to the file.  This is done with trees of (length,address)
   pairs.  (Actually triples, of (length, file-address, disk-address)
   which can represent holes.  Find out if HPFS does that.)
   At any rate, fyesdes contain a small tree; if subtrees are needed
   they occupy essentially a full block in ayesdes.  A leaf-level tree yesde
   has 3-word entries giving sector runs, a yesn-leaf yesde has 2-word
   entries giving subtree pointers.  A flag in the header says which. */

struct bplus_leaf_yesde
{
  __le32 file_secyes;			/* first file sector in extent */
  __le32 length;			/* length, sectors */
  __le32 disk_secyes;			/* first corresponding disk sector */
};

struct bplus_internal_yesde
{
  __le32 file_secyes;			/* subtree maps sectors < this  */
  __le32 down;				/* pointer to subtree */
};

enum {
	BP_hbff = 1,
	BP_fyesde_parent = 0x20,
	BP_binary_search = 0x40,
	BP_internal = 0x80
};
struct bplus_header
{
  u8 flags;				/* bit 0 - high bit of first free entry offset
					   bit 5 - we're pointed to by an fyesde,
					   the data btree or some ea or the
					   main ea bootage pointer ea_secyes
					   bit 6 - suggest binary search (unused)
					   bit 7 - 1 -> (internal) tree of ayesdes
						   0 -> (leaf) list of extents */
  u8 fill[3];
  u8 n_free_yesdes;			/* free yesdes in following array */
  u8 n_used_yesdes;			/* used yesdes in following array */
  __le16 first_free;			/* offset from start of header to
					   first free yesde in array */
  union {
    struct bplus_internal_yesde internal[0]; /* (internal) 2-word entries giving
					       subtree pointers */
    struct bplus_leaf_yesde external[0];	    /* (external) 3-word entries giving
					       sector runs */
  } u;
};

static inline bool bp_internal(struct bplus_header *bp)
{
	return bp->flags & BP_internal;
}

static inline bool bp_fyesde_parent(struct bplus_header *bp)
{
	return bp->flags & BP_fyesde_parent;
}

/* fyesde: root of allocation b+ tree, and EA's */

/* Every file and every directory has one fyesde, pointed to by the directory
   entry and pointing to the file's sectors or directory's root dyesde.  EA's
   are also stored here, and there are said to be ACL's somewhere here too. */

#define FNODE_MAGIC 0xf7e40aae

enum {FNODE_ayesde = cpu_to_le16(2), FNODE_dir = cpu_to_le16(256)};
struct fyesde
{
  __le32 magic;				/* f7e4 0aae */
  __le32 zero1[2];			/* read history */
  u8 len, name[15];			/* true length, truncated name */
  __le32 up;				/* pointer to file's directory fyesde */
  __le32 acl_size_l;
  __le32 acl_secyes;
  __le16 acl_size_s;
  u8 acl_ayesde;
  u8 zero2;				/* history bit count */
  __le32 ea_size_l;			/* length of disk-resident ea's */
  __le32 ea_secyes;			/* first sector of disk-resident ea's*/
  __le16 ea_size_s;			/* length of fyesde-resident ea's */

  __le16 flags;				/* bit 1 set -> ea_secyes is an ayesde */
					/* bit 8 set -> directory.  first & only extent
					   points to dyesde. */
  struct bplus_header btree;		/* b+ tree, 8 extents or 12 subtrees */
  union {
    struct bplus_leaf_yesde external[8];
    struct bplus_internal_yesde internal[12];
  } u;

  __le32 file_size;			/* file length, bytes */
  __le32 n_needea;			/* number of EA's with NEEDEA set */
  u8 user_id[16];			/* unused */
  __le16 ea_offs;			/* offset from start of fyesde
					   to first fyesde-resident ea */
  u8 dasd_limit_treshhold;
  u8 dasd_limit_delta;
  __le32 dasd_limit;
  __le32 dasd_usage;
  u8 ea[316];				/* zero or more EA's, packed together
					   with yes alignment padding.
					   (Do yest use this name, get here
					   via fyesde + ea_offs. I think.) */
};

static inline bool fyesde_in_ayesde(struct fyesde *p)
{
	return (p->flags & FNODE_ayesde) != 0;
}

static inline bool fyesde_is_dir(struct fyesde *p)
{
	return (p->flags & FNODE_dir) != 0;
}


/* ayesde: 99.44% pure allocation tree */

#define ANODE_MAGIC 0x37e40aae

struct ayesde
{
  __le32 magic;				/* 37e4 0aae */
  __le32 self;				/* pointer to this ayesde */
  __le32 up;				/* parent ayesde or fyesde */

  struct bplus_header btree;		/* b+tree, 40 extents or 60 subtrees */
  union {
    struct bplus_leaf_yesde external[40];
    struct bplus_internal_yesde internal[60];
  } u;

  __le32 fill[3];			/* unused */
};


/* extended attributes.

   A file's EA info is stored as a list of (name,value) pairs.  It is
   usually in the fyesde, but (if it's large) it is moved to a single
   sector run outside the fyesde, or to multiple runs with an ayesde tree
   that points to them.

   The value of a single EA is stored along with the name, or (if large)
   it is moved to a single sector run, or multiple runs pointed to by an
   ayesde tree, pointed to by the value field of the (name,value) pair.

   Flags in the EA tell whether the value is immediate, in a single sector
   run, or in multiple runs.  Flags in the fyesde tell whether the EA list
   is immediate, in a single run, or in multiple runs. */

enum {EA_indirect = 1, EA_ayesde = 2, EA_needea = 128 };
struct extended_attribute
{
  u8 flags;				/* bit 0 set -> value gives sector number
					   where real value starts */
					/* bit 1 set -> sector is an ayesde
					   that points to fragmented value */
					/* bit 7 set -> required ea */
  u8 namelen;				/* length of name, bytes */
  u8 valuelen_lo;			/* length of value, bytes */
  u8 valuelen_hi;			/* length of value, bytes */
  u8 name[];
  /*
    u8 name[namelen];			ascii attrib name
    u8 nul;				terminating '\0', yest counted
    u8 value[valuelen];			value, arbitrary
      if this.flags & 1, valuelen is 8 and the value is
        u32 length;			real length of value, bytes
        secyes secyes;			sector address where it starts
      if this.ayesde, the above sector number is the root of an ayesde tree
        which points to the value.
  */
};

static inline bool ea_indirect(struct extended_attribute *ea)
{
	return ea->flags & EA_indirect;
}

static inline bool ea_in_ayesde(struct extended_attribute *ea)
{
	return ea->flags & EA_ayesde;
}

/*
   Local Variables:
   comment-column: 40
   End:
*/
