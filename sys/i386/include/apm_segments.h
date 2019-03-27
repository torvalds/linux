/*-
 * LP (Laptop Package)
 *
 * Copyright (C) 1994 by HOSOKAWA Tatsumi <hosokawa@mt.cs.keio.ac.jp>
 *
 * This software may be used, modified, copied, and distributed, in
 * both source and binary form provided that the above copyright and
 * these terms are retained. Under no circumstances is the author
 * responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its
 * use.
 *
 * Sep., 1994	Implemented on FreeBSD 1.1.5.1R (Toshiba AVS001WD)
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_APM_SEGMENTS_H
#define _MACHINE_APM_SEGMENTS_H

#define SIZEOF_GDT		8
#define BOOTSTRAP_GDT_NUM	32

#define APM_INIT_CS_INDEX	(BOOTSTRAP_GDT_NUM - 4)
#define APM_INIT_DS_INDEX	(BOOTSTRAP_GDT_NUM - 3)
#define APM_INIT_CS16_INDEX	(BOOTSTRAP_GDT_NUM - 2)
#define APM_INIT_DS16_INDEX	(BOOTSTRAP_GDT_NUM - 1)
#define APM_INIT_CS_SEL		(APM_INIT_CS_INDEX << 3)
#define APM_INIT_DS_SEL		(APM_INIT_DS_INDEX << 3)
#define APM_INIT_CS16_SEL	(APM_INIT_CS16_INDEX << 3)
#define APM_INIT_DS16_SEL	(APM_INIT_DS16_INDEX << 3)

#define CS32_ATTRIB		0x409e
#define DS32_ATTRIB		0x4092
#define CS16_ATTRIB		0x009e
#define DS16_ATTRIB		0x0092

#endif
