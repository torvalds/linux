/*
 * upd64031a - NEC Electronics Ghost Reduction input defines
 *
 * 2006 by Hans Verkuil (hverkuil@xs4all.nl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _UPD64031A_H_
#define _UPD64031A_H_

/* Ghost reduction modes */
#define UPD64031A_GR_ON 	0
#define UPD64031A_GR_OFF 	1
#define UPD64031A_GR_THROUGH 	3

/* Direct 3D/YCS Connection */
#define UPD64031A_3DYCS_DISABLE   (0 << 2)
#define UPD64031A_3DYCS_COMPOSITE (2 << 2)
#define UPD64031A_3DYCS_SVIDEO    (3 << 2)

/* Composite sync digital separation circuit */
#define UPD64031A_COMPOSITE_EXTERNAL (1 << 4)

/* Vertical sync digital separation circuit */
#define UPD64031A_VERTICAL_EXTERNAL (1 << 5)

#endif

