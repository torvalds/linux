/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Derived from IRIX <sys/SN/klconfig.h>.
 *
 * Copyright (C) 1992-1997,1999,2001-2004 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (C) 1999 by Ralf Baechle
 */
#ifndef _ASM_IA64_SN_KLCONFIG_H
#define _ASM_IA64_SN_KLCONFIG_H

/*
 * The KLCONFIG structures store info about the various BOARDs found
 * during Hardware Discovery. In addition, it stores info about the
 * components found on the BOARDs.
 */

typedef s32 klconf_off_t;


/* Functions/macros needed to use this structure */

typedef struct kl_config_hdr {
	char		pad[20];
	klconf_off_t	ch_board_info;	/* the link list of boards */
	char		pad0[88];
} kl_config_hdr_t;


#define NODE_OFFSET_TO_LBOARD(nasid,off)        (lboard_t*)(GLOBAL_CAC_ADDR((nasid), (off)))

/*
 * The KLCONFIG area is organized as a LINKED LIST of BOARDs. A BOARD
 * can be either 'LOCAL' or 'REMOTE'. LOCAL means it is attached to 
 * the LOCAL/current NODE. REMOTE means it is attached to a different
 * node.(TBD - Need a way to treat ROUTER boards.)
 *
 * There are 2 different structures to represent these boards -
 * lboard - Local board, rboard - remote board. These 2 structures
 * can be arbitrarily mixed in the LINKED LIST of BOARDs. (Refer
 * Figure below). The first byte of the rboard or lboard structure
 * is used to find out its type - no unions are used.
 * If it is a lboard, then the config info of this board will be found
 * on the local node. (LOCAL NODE BASE + offset value gives pointer to 
 * the structure.
 * If it is a rboard, the local structure contains the node number
 * and the offset of the beginning of the LINKED LIST on the remote node.
 * The details of the hardware on a remote node can be built locally,
 * if required, by reading the LINKED LIST on the remote node and 
 * ignoring all the rboards on that node.
 *
 * The local node uses the REMOTE NODE NUMBER + OFFSET to point to the 
 * First board info on the remote node. The remote node list is 
 * traversed as the local list, using the REMOTE BASE ADDRESS and not
 * the local base address and ignoring all rboard values.
 *
 * 
 KLCONFIG

 +------------+      +------------+      +------------+      +------------+
 |  lboard    |  +-->|   lboard   |  +-->|   rboard   |  +-->|   lboard   |
 +------------+  |   +------------+  |   +------------+  |   +------------+
 | board info |  |   | board info |  |   |errinfo,bptr|  |   | board info |
 +------------+  |   +------------+  |   +------------+  |   +------------+
 | offset     |--+   |  offset    |--+   |  offset    |--+   |offset=NULL |
 +------------+      +------------+      +------------+      +------------+


 +------------+
 | board info |
 +------------+       +--------------------------------+
 | compt 1    |------>| type, rev, diaginfo, size ...  |  (CPU)
 +------------+       +--------------------------------+
 | compt 2    |--+
 +------------+  |    +--------------------------------+
 |  ...       |  +--->| type, rev, diaginfo, size ...  |  (MEM_BANK)
 +------------+       +--------------------------------+
 | errinfo    |--+
 +------------+  |    +--------------------------------+
                 +--->|r/l brd errinfo,compt err flags |
                      +--------------------------------+

 *
 * Each BOARD consists of COMPONENTs and the BOARD structure has 
 * pointers (offsets) to its COMPONENT structure.
 * The COMPONENT structure has version info, size and speed info, revision,
 * error info and the NIC info. This structure can accommodate any
 * BOARD with arbitrary COMPONENT composition.
 *
 * The ERRORINFO part of each BOARD has error information
 * that describes errors about the BOARD itself. It also has flags to
 * indicate the COMPONENT(s) on the board that have errors. The error 
 * information specific to the COMPONENT is present in the respective 
 * COMPONENT structure.
 *
 * The ERRORINFO structure is also treated like a COMPONENT, ie. the 
 * BOARD has pointers(offset) to the ERRORINFO structure. The rboard
 * structure also has a pointer to the ERRORINFO structure. This is 
 * the place to store ERRORINFO about a REMOTE NODE, if the HUB on
 * that NODE is not working or if the REMOTE MEMORY is BAD. In cases where 
 * only the CPU of the REMOTE NODE is disabled, the ERRORINFO pointer can
 * be a NODE NUMBER, REMOTE OFFSET combination, pointing to error info 
 * which is present on the REMOTE NODE.(TBD)
 * REMOTE ERRINFO can be stored on any of the nearest nodes 
 * or on all the nearest nodes.(TBD)
 * Like BOARD structures, REMOTE ERRINFO structures can be built locally
 * using the rboard errinfo pointer.
 *
 * In order to get useful information from this Data organization, a set of
 * interface routines are provided (TBD). The important thing to remember while
 * manipulating the structures, is that, the NODE number information should
 * be used. If the NODE is non-zero (remote) then each offset should
 * be added to the REMOTE BASE ADDR else it should be added to the LOCAL BASE ADDR. 
 * This includes offsets for BOARDS, COMPONENTS and ERRORINFO.
 * 
 * Note that these structures do not provide much info about connectivity.
 * That info will be part of HWGRAPH, which is an extension of the cfg_t
 * data structure. (ref IP27prom/cfg.h) It has to be extended to include
 * the IO part of the Network(TBD).
 *
 * The data structures below define the above concepts.
 */


/*
 * BOARD classes
 */

#define KLCLASS_MASK	0xf0   
#define KLCLASS_NONE	0x00
#define KLCLASS_NODE	0x10             /* CPU, Memory and HUB board */
#define KLCLASS_CPU	KLCLASS_NODE	
#define KLCLASS_IO	0x20             /* BaseIO, 4 ch SCSI, ethernet, FDDI 
					    and the non-graphics widget boards */
#define KLCLASS_ROUTER	0x30             /* Router board */
#define KLCLASS_MIDPLANE 0x40            /* We need to treat this as a board
                                            so that we can record error info */
#define KLCLASS_IOBRICK	0x70		/* IP35 iobrick */
#define KLCLASS_MAX	8		/* Bump this if a new CLASS is added */

#define KLCLASS(_x) ((_x) & KLCLASS_MASK)


/*
 * board types
 */

#define KLTYPE_MASK	0x0f
#define KLTYPE(_x)      ((_x) & KLTYPE_MASK)

#define KLTYPE_SNIA	(KLCLASS_CPU | 0x1)
#define KLTYPE_TIO	(KLCLASS_CPU | 0x2)

#define KLTYPE_ROUTER     (KLCLASS_ROUTER | 0x1)
#define KLTYPE_META_ROUTER (KLCLASS_ROUTER | 0x3)
#define KLTYPE_REPEATER_ROUTER (KLCLASS_ROUTER | 0x4)

#define KLTYPE_IOBRICK_XBOW	(KLCLASS_MIDPLANE | 0x2)

#define KLTYPE_IOBRICK		(KLCLASS_IOBRICK | 0x0)
#define KLTYPE_NBRICK		(KLCLASS_IOBRICK | 0x4)
#define KLTYPE_PXBRICK		(KLCLASS_IOBRICK | 0x6)
#define KLTYPE_IXBRICK		(KLCLASS_IOBRICK | 0x7)
#define KLTYPE_CGBRICK		(KLCLASS_IOBRICK | 0x8)
#define KLTYPE_OPUSBRICK	(KLCLASS_IOBRICK | 0x9)
#define KLTYPE_SABRICK          (KLCLASS_IOBRICK | 0xa)
#define KLTYPE_IABRICK		(KLCLASS_IOBRICK | 0xb)
#define KLTYPE_PABRICK          (KLCLASS_IOBRICK | 0xc)
#define KLTYPE_GABRICK		(KLCLASS_IOBRICK | 0xd)


/* 
 * board structures
 */

#define MAX_COMPTS_PER_BRD 24

typedef struct lboard_s {
	klconf_off_t 	brd_next_any;     /* Next BOARD */
	unsigned char 	struct_type;      /* type of structure, local or remote */
	unsigned char 	brd_type;         /* type+class */
	unsigned char 	brd_sversion;     /* version of this structure */
        unsigned char 	brd_brevision;    /* board revision */
        unsigned char 	brd_promver;      /* board prom version, if any */
 	unsigned char 	brd_flags;        /* Enabled, Disabled etc */
	unsigned char 	brd_slot;         /* slot number */
	unsigned short	brd_debugsw;      /* Debug switches */
	geoid_t		brd_geoid;	  /* geo id */
	partid_t 	brd_partition;    /* Partition number */
        unsigned short 	brd_diagval;      /* diagnostic value */
        unsigned short 	brd_diagparm;     /* diagnostic parameter */
        unsigned char 	brd_inventory;    /* inventory history */
        unsigned char 	brd_numcompts;    /* Number of components */
        nic_t         	brd_nic;          /* Number in CAN */
	nasid_t		brd_nasid;        /* passed parameter */
	klconf_off_t 	brd_compts[MAX_COMPTS_PER_BRD]; /* pointers to COMPONENTS */
	klconf_off_t 	brd_errinfo;      /* Board's error information */
	struct lboard_s *brd_parent;	  /* Logical parent for this brd */
	char            pad0[4];
	unsigned char	brd_confidence;	  /* confidence that the board is bad */
	nasid_t		brd_owner;        /* who owns this board */
	unsigned char 	brd_nic_flags;    /* To handle 8 more NICs */
	char		pad1[24];	  /* future expansion */
	char		brd_name[32];
	nasid_t		brd_next_same_host; /* host of next brd w/same nasid */
	klconf_off_t	brd_next_same;    /* Next BOARD with same nasid */
} lboard_t;

#define KLCF_NUM_COMPS(_brd)	((_brd)->brd_numcompts)
#define NODE_OFFSET_TO_KLINFO(n,off)    ((klinfo_t*) TO_NODE_CAC(n,off))
#define KLCF_NEXT(_brd)         \
        ((_brd)->brd_next_same ?     \
         (NODE_OFFSET_TO_LBOARD((_brd)->brd_next_same_host, (_brd)->brd_next_same)): NULL)
#define KLCF_NEXT_ANY(_brd)         \
        ((_brd)->brd_next_any ?     \
         (NODE_OFFSET_TO_LBOARD(NASID_GET(_brd), (_brd)->brd_next_any)): NULL)
#define KLCF_COMP(_brd, _ndx)   \
                ((((_brd)->brd_compts[(_ndx)]) == 0) ? 0 : \
			(NODE_OFFSET_TO_KLINFO(NASID_GET(_brd), (_brd)->brd_compts[(_ndx)])))


/*
 * Generic info structure. This stores common info about a 
 * component.
 */
 
typedef struct klinfo_s {                  /* Generic info */
        unsigned char   struct_type;       /* type of this structure */
        unsigned char   struct_version;    /* version of this structure */
        unsigned char   flags;            /* Enabled, disabled etc */
        unsigned char   revision;         /* component revision */
        unsigned short  diagval;          /* result of diagnostics */
        unsigned short  diagparm;         /* diagnostic parameter */
        unsigned char   inventory;        /* previous inventory status */
        unsigned short  partid;		   /* widget part number */
	nic_t 		nic;              /* MUst be aligned properly */
        unsigned char   physid;           /* physical id of component */
        unsigned int    virtid;           /* virtual id as seen by system */
	unsigned char	widid;	          /* Widget id - if applicable */
	nasid_t		nasid;            /* node number - from parent */
	char		pad1;		  /* pad out structure. */
	char		pad2;		  /* pad out structure. */
	void		*data;
        klconf_off_t	errinfo;          /* component specific errors */
        unsigned short  pad3;             /* pci fields have moved over to */
        unsigned short  pad4;             /* klbri_t */
} klinfo_t ;


static inline lboard_t *find_lboard_any(lboard_t * start, unsigned char brd_type)
{
        /* Search all boards stored on this node. */

        while (start) {
                if (start->brd_type == brd_type)
                        return start;
                start = KLCF_NEXT_ANY(start);
        }
        /* Didn't find it. */
        return (lboard_t *) NULL;
}


/* external declarations of Linux kernel functions. */

extern lboard_t *root_lboard[];
extern klinfo_t *find_component(lboard_t *brd, klinfo_t *kli, unsigned char type);
extern klinfo_t *find_first_component(lboard_t *brd, unsigned char type);

#endif /* _ASM_IA64_SN_KLCONFIG_H */
