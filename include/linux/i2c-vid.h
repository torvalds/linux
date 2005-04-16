/*
    i2c-vid.h - Part of lm_sensors, Linux kernel modules for hardware
                monitoring
    Copyright (c) 2002 Mark D. Studebaker <mdsxyz123@yahoo.com>
    With assistance from Trent Piepho <xyzzy@speakeasy.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
    This file contains common code for decoding VID pins.
    This file is #included in various chip drivers in this directory.
    As the user is unlikely to load more than one driver which
    includes this code we don't worry about the wasted space.
    Reference: VRM x.y DC-DC Converter Design Guidelines,
    available at http://developer.intel.com
*/

/*
    AMD Opteron processors don't follow the Intel VRM spec.
    I'm going to "make up" 2.4 as the VRM spec for the Opterons.
    No good reason just a mnemonic for the 24x Opteron processor
    series

    Opteron VID encoding is:

       00000  =  1.550 V
       00001  =  1.525 V
        . . . .
       11110  =  0.800 V
       11111  =  0.000 V (off)
 */

/*
    Legal val values 0x00 - 0x1f; except for VRD 10.0, 0x00 - 0x3f.
    vrm is the Intel VRM document version.
    Note: vrm version is scaled by 10 and the return value is scaled by 1000
    to avoid floating point in the kernel.
*/

int i2c_which_vrm(void);

#define DEFAULT_VRM	82

static inline int vid_from_reg(int val, int vrm)
{
	int vid;

	switch(vrm) {

	case  0:
		return 0;

	case 100:               /* VRD 10.0 */
		if((val & 0x1f) == 0x1f)
			return 0;
		if((val & 0x1f) <= 0x09 || val == 0x0a)
			vid = 10875 - (val & 0x1f) * 250;
		else
			vid = 18625 - (val & 0x1f) * 250;
		if(val & 0x20)
			vid -= 125;
		vid /= 10;      /* only return 3 dec. places for now */
		return vid;

	case 24:                /* Opteron processor */
		return(val == 0x1f ? 0 : 1550 - val * 25);

	case 91:		/* VRM 9.1 */
	case 90:		/* VRM 9.0 */
		return(val == 0x1f ? 0 :
		                       1850 - val * 25);

	case 85:		/* VRM 8.5 */
		return((val & 0x10  ? 25 : 0) +
		       ((val & 0x0f) > 0x04 ? 2050 : 1250) -
		       ((val & 0x0f) * 50));

	case 84:		/* VRM 8.4 */
		val &= 0x0f;
				/* fall through */
	default:		/* VRM 8.2 */
		return(val == 0x1f ? 0 :
		       val & 0x10  ? 5100 - (val) * 100 :
		                     2050 - (val) * 50);
	}
}
