/* ------------------------
 *  This file is created as a "front" for chip specific hw headers. Since hw headers change from
 *  chip to chip, ART/MDK code needs a "constant" hw header file.
 *  The thinking is that this header file will hide all chip specific hw headers from the application
 *  code.
 *  This is really work-in-progress. For now we just want to get McKinley built. We don't really care
 *  what's in the bb_lc_reg.h which may have equivalent in McKinley.
 *  In that case, there are several options. 
 *  Option 1: mCal uses ART/MDK defined registers, which are mapped to the hw headers. This is basically
 *  the current ART/MDK code, which has its flaws. For example, mapping is wrong and it takes time to
 *  detect.
 *  Option 2: front hw header file will include all hw files. This will force the new chip to expose
 *  registers that need to be changed. In the McKinley case, mCal.c.
 *
 *  Currently option 2 is used. We'll see how it goes.
 *
 * -----------------------
 */

#ifndef _BB_LC_REG_REG_H_
#define _BB_LC_REG_REG_H_

#if defined(_MCKINLEY)
#include "bb_lc_reg_tbd.h"
#endif /* #if defined(AR6002_REV6)  */

#endif /* #ifndef _BB_LC_REG_REG_H__ */
