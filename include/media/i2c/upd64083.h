/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * upd6408x - NEC Electronics 3-Dimensional Y/C separation input defines
 *
 * 2006 by Hans Verkuil (hverkuil@xs4all.nl)
 */

#ifndef _UPD64083_H_
#define _UPD64083_H_

/* There are two bits of information that the driver needs in order
   to select the correct routing: the operating mode and the selection
   of the Y input (external or internal).

   The first two operating modes expect a composite signal on the Y input,
   the second two operating modes use both the Y and C inputs.

   Normally YCS_MODE is used for tuner and composite inputs, and the
   YCNR mode is used for S-Video inputs.

   The external Y-ADC is selected when the composite input comes from a
   upd64031a ghost reduction device. If this device is not present, or
   the input is a S-Video signal, then the internal Y-ADC input should
   be used. */

/* Operating modes: */

/* YCS mode: Y/C separation (burst locked clocking) */
#define UPD64083_YCS_MODE      0
/* YCS+ mode: 2D Y/C separation and YCNR (burst locked clocking) */
#define UPD64083_YCS_PLUS_MODE 1

/* Note: the following two modes cannot be used in combination with the
   external Y-ADC. */
/* MNNR mode: frame comb type YNR+C delay (line locked clocking) */
#define UPD64083_MNNR_MODE     2
/* YCNR mode: frame recursive YCNR (burst locked clocking) */
#define UPD64083_YCNR_MODE     3

/* Select external Y-ADC: this should be set if this device is used in
   combination with the upd64031a ghost reduction device.
   Otherwise leave at 0 (use internal Y-ADC). */
#define UPD64083_EXT_Y_ADC     (1 << 2)

#endif
