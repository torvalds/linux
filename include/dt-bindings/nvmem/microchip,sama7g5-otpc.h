/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */

#ifndef _DT_BINDINGS_NVMEM_MICROCHIP_OTPC_H
#define _DT_BINDINGS_NVMEM_MICROCHIP_OTPC_H

/*
 * Need to have it as a multiple of 4 as NVMEM memory is registered with
 * stride = 4.
 */
#define OTP_PKT(id)			((id) * 4)

#endif
