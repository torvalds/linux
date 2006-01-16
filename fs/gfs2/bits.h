/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __BITS_DOT_H__
#define __BITS_DOT_H__

#define BFITNOENT 0xFFFFFFFF

void gfs2_setbit(struct gfs2_rgrpd *rgd,
		unsigned char *buffer, unsigned int buflen,
		uint32_t block, unsigned char new_state);
unsigned char gfs2_testbit(struct gfs2_rgrpd *rgd,
			  unsigned char *buffer, unsigned int buflen,
			  uint32_t block);
uint32_t gfs2_bitfit(struct gfs2_rgrpd *rgd,
		    unsigned char *buffer, unsigned int buflen,
		    uint32_t goal, unsigned char old_state);
uint32_t gfs2_bitcount(struct gfs2_rgrpd *rgd,
		      unsigned char *buffer, unsigned int buflen,
		      unsigned char state);

#endif /* __BITS_DOT_H__ */
