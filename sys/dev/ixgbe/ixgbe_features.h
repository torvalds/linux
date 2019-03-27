/******************************************************************************

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/


#ifndef _IXGBE_FEATURES_H_
#define _IXGBE_FEATURES_H_

/*
 * Feature defines.  Eventually, we'd like to get to a point where we
 * can remove MAC/Phy type checks scattered throughout the code in
 * favor of checking these feature flags. If the feature expects OS
 * support, make sure to add an #undef below if expected to run on
 * OSs that don't support said feature.
 */
#define IXGBE_FEATURE_VF                        (u32)(1 << 0)
#define IXGBE_FEATURE_SRIOV                     (u32)(1 << 1)
#define IXGBE_FEATURE_RSS                       (u32)(1 << 2)
#define IXGBE_FEATURE_NETMAP                    (u32)(1 << 3)
#define IXGBE_FEATURE_FAN_FAIL                  (u32)(1 << 4)
#define IXGBE_FEATURE_TEMP_SENSOR               (u32)(1 << 5)
#define IXGBE_FEATURE_BYPASS                    (u32)(1 << 6)
#define IXGBE_FEATURE_LEGACY_TX                 (u32)(1 << 7)
#define IXGBE_FEATURE_FDIR                      (u32)(1 << 8)
#define IXGBE_FEATURE_MSI                       (u32)(1 << 9)
#define IXGBE_FEATURE_MSIX                      (u32)(1 << 10)
#define IXGBE_FEATURE_EEE                       (u32)(1 << 11)
#define IXGBE_FEATURE_LEGACY_IRQ                (u32)(1 << 12)
#define IXGBE_FEATURE_NEEDS_CTXD                (u32)(1 << 13)

/* Check for OS support.  Undefine features if not included in the OS */
#ifndef PCI_IOV
#undef  IXGBE_FEATURE_SRIOV
#define IXGBE_FEATURE_SRIOV                     0
#endif

#ifndef RSS
#undef  IXGBE_FEATURE_RSS
#define IXGBE_FEATURE_RSS                       0
#endif

#ifndef DEV_NETMAP
#undef  IXGBE_FEATURE_NETMAP
#define IXGBE_FEATURE_NETMAP                    0
#endif

#endif /* _IXGBE_FEATURES_H_ */
