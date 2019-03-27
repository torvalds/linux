/***********************license start***************
 * Copyright (c) 2011 Cavium Inc. (support@cavium.com). All rights 
 * reserved.
 * 
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 * 
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 * 
 *     * Neither the name of Cavium Inc. nor the names of
 *       its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.  
 * 
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS" 
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS 
 * OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH 
 * RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY 
 * REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT 
 * DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES 
 * OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR 
 * PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET 
 * POSSESSION OR CORRESPONDENCE TO DESCRIPTION.  THE ENTIRE RISK ARISING OUT 
 * OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.  
 * 
 * 
 **********************license end****************/

/* Version information is made available at compile time in two forms:
** 1) a version string for printing
** 2) a combined SDK version and build number, suitable for comparisons
**    to determine what SDK version is being used.
**    SDK 1.2.3 build 567 => 102030567
**    Note that 2 digits are used for each version number, so that:
**     1.9.0  == 01.09.00 < 01.10.00 == 1.10.0 
**     10.9.0 == 10.09.00 > 09.10.00 == 9.10.0 
** 
*/ 
#define OCTEON_SDK_VERSION_NUM  203000427ull
#define OCTEON_SDK_VERSION_STRING   "Cavium Inc. OCTEON SDK version 2.3.0, build 427"
