/*
 * Header file for the i2c/i2s based TA3001c sound chip used
 * on some Apple hardware. Also known as "tumbler".
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 *
 * Written by Christopher C. Chimelis <chris@debian.org>
 */

#ifndef _TAS3001C_H_
#define _TAS3001C_H_

#include <linux/types.h>

#include "tas_common.h"
#include "tas_eq_prefs.h"

/*
 * Macros that correspond to the registers that we write to
 * when setting the various values.
 */

#define TAS3001C_VERSION	"0.3"
#define TAS3001C_DATE	        "20011214"

#define I2C_DRIVERNAME_TAS3001C "TAS3001c driver V " TAS3001C_VERSION
#define I2C_DRIVERID_TAS3001C   (I2C_DRIVERID_TAS_BASE+0)

extern  struct tas_driver_hooks_t tas3001c_hooks;
extern struct tas_gain_t tas3001c_gain;
extern struct tas_eq_pref_t *tas3001c_eq_prefs[];

enum tas3001c_reg_t {
  TAS3001C_REG_MCR                    = 0x01,
  TAS3001C_REG_DRC                    = 0x02,

  TAS3001C_REG_VOLUME                 = 0x04,
  TAS3001C_REG_TREBLE                 = 0x05,
  TAS3001C_REG_BASS                   = 0x06,
  TAS3001C_REG_MIXER1                 = 0x07,
  TAS3001C_REG_MIXER2                 = 0x08,

  TAS3001C_REG_LEFT_BIQUAD0           = 0x0a,
  TAS3001C_REG_LEFT_BIQUAD1           = 0x0b,
  TAS3001C_REG_LEFT_BIQUAD2           = 0x0c,
  TAS3001C_REG_LEFT_BIQUAD3           = 0x0d,
  TAS3001C_REG_LEFT_BIQUAD4           = 0x0e,
  TAS3001C_REG_LEFT_BIQUAD5           = 0x0f,
  TAS3001C_REG_LEFT_BIQUAD6           = 0x10,
  
  TAS3001C_REG_RIGHT_BIQUAD0          = 0x13,
  TAS3001C_REG_RIGHT_BIQUAD1          = 0x14,
  TAS3001C_REG_RIGHT_BIQUAD2          = 0x15,
  TAS3001C_REG_RIGHT_BIQUAD3          = 0x16,
  TAS3001C_REG_RIGHT_BIQUAD4          = 0x17,
  TAS3001C_REG_RIGHT_BIQUAD5          = 0x18,
  TAS3001C_REG_RIGHT_BIQUAD6          = 0x19,

  TAS3001C_REG_MAX                    = 0x20
};

#endif /* _TAS3001C_H_ */
