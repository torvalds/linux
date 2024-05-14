/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2008-2009 Texas Instruments Inc
 *
 **************************************************************************/
#ifndef _CCDC_TYPES_H
#define _CCDC_TYPES_H
enum ccdc_pixfmt {
	CCDC_PIXFMT_RAW,
	CCDC_PIXFMT_YCBCR_16BIT,
	CCDC_PIXFMT_YCBCR_8BIT
};

enum ccdc_frmfmt {
	CCDC_FRMFMT_PROGRESSIVE,
	CCDC_FRMFMT_INTERLACED
};

/* PIXEL ORDER IN MEMORY from LSB to MSB */
/* only applicable for 8-bit input mode  */
enum ccdc_pixorder {
	CCDC_PIXORDER_YCBYCR,
	CCDC_PIXORDER_CBYCRY,
};

enum ccdc_buftype {
	CCDC_BUFTYPE_FLD_INTERLEAVED,
	CCDC_BUFTYPE_FLD_SEPARATED
};
#endif
