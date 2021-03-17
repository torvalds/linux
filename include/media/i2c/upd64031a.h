/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * upd64031a - NEC Electronics Ghost Reduction input defines
 *
 * 2006 by Hans Verkuil (hverkuil@xs4all.nl)
 */

#ifndef _UPD64031A_H_
#define _UPD64031A_H_

/* Ghost reduction modes */
#define UPD64031A_GR_ON		0
#define UPD64031A_GR_OFF	1
#define UPD64031A_GR_THROUGH	3

/* Direct 3D/YCS Connection */
#define UPD64031A_3DYCS_DISABLE   (0 << 2)
#define UPD64031A_3DYCS_COMPOSITE (2 << 2)
#define UPD64031A_3DYCS_SVIDEO    (3 << 2)

/* Composite sync digital separation circuit */
#define UPD64031A_COMPOSITE_EXTERNAL (1 << 4)

/* Vertical sync digital separation circuit */
#define UPD64031A_VERTICAL_EXTERNAL (1 << 5)

#endif
