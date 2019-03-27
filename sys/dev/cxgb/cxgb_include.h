/*
 *  $FreeBSD$
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <machine/bus.h>
#include <cxgb_osdep.h>
#include <common/cxgb_common.h>
#include <cxgb_ioctl.h>
#include <cxgb_offload.h>
#include <common/cxgb_regs.h>
#include <common/cxgb_t3_cpl.h>
#include <common/cxgb_ctl_defs.h>
#include <common/cxgb_sge_defs.h>
#include <common/cxgb_firmware_exports.h>
#include <common/jhash.h>

SYSCTL_DECL(_hw_cxgb);
