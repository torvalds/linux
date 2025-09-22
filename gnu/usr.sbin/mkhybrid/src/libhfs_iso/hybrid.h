/*
**	hybrid.h: extra info needed by libhfs and mkisofs
**
**	James Pearson 15/9/97
*/

#ifndef _HYBRID_H

#define	CTC	2		/* factor to increase initial Catalog file
				   size to prevent the file growing */
#define CTC_LOOP 4		/* number of attemps before we give up
				   trying to create the volume */

#define HCE_ERROR -9999		/* dummy errno value for Catalog file
				   size problems */

#define HFS_MAP_SIZE	16	/* size of HFS partition maps (8Kb) */

typedef struct {
  int hfs_ce_size;		/* extents/catalog size in HFS blks */
  int hfs_hdr_size;		/* vol header size in HFS blks */
  int hfs_dt_size;		/* Desktop file size in HFS blks */
  int hfs_tot_size;		/* extents/catalog/dt size in HFS blks */
  int hfs_map_size;		/* size of partition maps in HFS blks */
  int hfs_vol_size;		/* size of volume in bytes */
  unsigned char *hfs_ce;	/* mem copy of extents/catalog files */
  unsigned char *hfs_hdr;	/* mem copy of vol header */
  unsigned char *hfs_alt_mdb;	/* location of alternate MDB */
  unsigned char *hfs_map;	/* location of partiton_maps */
  int Csize;			/* size of allocation unit */
  int XTCsize;			/* default size of catalog/extents files */
  int ctc_size;			/* factor to increase Catalog file size */
  char *error;			/* HFS error message */
} hce_mem;

#define ERROR_SIZE	1024

#define _HYBRID_H
#endif /* _HYBRID_H */
