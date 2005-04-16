/*
 * Header file for the i2c/i2s based TA3004 sound chip used
 * on some Apple hardware. Also known as "tumbler".
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 *
 * Written by Christopher C. Chimelis <chris@debian.org>
 */

#ifndef _TAS3004_H_
#define _TAS3004_H_

#include <linux/types.h>

#include "tas_common.h"
#include "tas_eq_prefs.h"

/*
 * Macros that correspond to the registers that we write to
 * when setting the various values.
 */

#define TAS3004_VERSION	        "0.3"
#define TAS3004_DATE	        "20011214"

#define I2C_DRIVERNAME_TAS3004 "TAS3004 driver V " TAS3004_VERSION
#define I2C_DRIVERID_TAS3004    (I2C_DRIVERID_TAS_BASE+1)

extern  struct tas_driver_hooks_t tas3004_hooks;
extern struct tas_gain_t tas3004_gain;
extern struct tas_eq_pref_t *tas3004_eq_prefs[];

enum tas3004_reg_t {
  TAS3004_REG_MCR                    = 0x01,
  TAS3004_REG_DRC                    = 0x02,

  TAS3004_REG_VOLUME                 = 0x04,
  TAS3004_REG_TREBLE                 = 0x05,
  TAS3004_REG_BASS                   = 0x06,
  TAS3004_REG_LEFT_MIXER             = 0x07,
  TAS3004_REG_RIGHT_MIXER            = 0x08,

  TAS3004_REG_LEFT_BIQUAD0           = 0x0a,
  TAS3004_REG_LEFT_BIQUAD1           = 0x0b,
  TAS3004_REG_LEFT_BIQUAD2           = 0x0c,
  TAS3004_REG_LEFT_BIQUAD3           = 0x0d,
  TAS3004_REG_LEFT_BIQUAD4           = 0x0e,
  TAS3004_REG_LEFT_BIQUAD5           = 0x0f,
  TAS3004_REG_LEFT_BIQUAD6           = 0x10,
  
  TAS3004_REG_RIGHT_BIQUAD0          = 0x13,
  TAS3004_REG_RIGHT_BIQUAD1          = 0x14,
  TAS3004_REG_RIGHT_BIQUAD2          = 0x15,
  TAS3004_REG_RIGHT_BIQUAD3          = 0x16,
  TAS3004_REG_RIGHT_BIQUAD4          = 0x17,
  TAS3004_REG_RIGHT_BIQUAD5          = 0x18,
  TAS3004_REG_RIGHT_BIQUAD6          = 0x19,

  TAS3004_REG_LEFT_LOUD_BIQUAD       = 0x21,
  TAS3004_REG_RIGHT_LOUD_BIQUAD      = 0x22,

  TAS3004_REG_LEFT_LOUD_BIQUAD_GAIN  = 0x23,
  TAS3004_REG_RIGHT_LOUD_BIQUAD_GAIN = 0x24,

  TAS3004_REG_TEST                   = 0x29,

  TAS3004_REG_ANALOG_CTRL            = 0x40,
  TAS3004_REG_TEST1                  = 0x41,
  TAS3004_REG_TEST2                  = 0x42,
  TAS3004_REG_MCR2                   = 0x43,

  TAS3004_REG_MAX                    = 0x44
};

#endif /* _TAS3004_H_ */
