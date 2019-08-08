/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MXM WMI driver
 *
 * Copyright(C) 2010 Red Hat.
 */

#ifndef MXM_WMI_H
#define MXM_WMI_H

/* discrete adapters */
#define MXM_MXDS_ADAPTER_0 0x0
#define MXM_MXDS_ADAPTER_1 0x0
/* integrated adapter */
#define MXM_MXDS_ADAPTER_IGD 0x10
int mxm_wmi_call_mxds(int adapter);
int mxm_wmi_call_mxmx(int adapter);
bool mxm_wmi_supported(void);

#endif
