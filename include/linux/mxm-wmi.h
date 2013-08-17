/*
 * MXM WMI driver
 *
 * Copyright(C) 2010 Red Hat.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
