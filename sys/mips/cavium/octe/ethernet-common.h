/*************************************************************************
SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2003-2007  Cavium Networks (support@cavium.com). All rights
reserved.


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Cavium Networks nor the names of
      its contributors may be used to endorse or promote products
      derived from this software without specific prior written
      permission.

This Software, including technical data, may be subject to U.S. export  control laws, including the U.S. Export Administration Act and its  associated regulations, and may be subject to export or import  regulations in other countries.

TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.

*************************************************************************/
/* $FreeBSD$ */

int cvm_oct_common_open(struct ifnet *ifp);
int cvm_oct_common_stop(struct ifnet *ifp);
void cvm_oct_common_poll(struct ifnet *ifp);
int cvm_oct_common_init(struct ifnet *ifp);
void cvm_oct_common_uninit(struct ifnet *ifp);

int cvm_oct_common_change_mtu(struct ifnet *ifp, int new_mtu);
void cvm_oct_common_set_multicast_list(struct ifnet *ifp);
void cvm_oct_common_set_mac_address(struct ifnet *ifp, const void *);
int cvm_assign_mac_address(uint64_t *, uint8_t *);

int cvm_oct_init_module(device_t);
void cvm_oct_cleanup_module(device_t);

/*
 * XXX/juli
 * These belong elsewhere but we can't stomach the nested extern.
 */
int cvm_oct_rgmii_init(struct ifnet *ifp);
void cvm_oct_rgmii_uninit(struct ifnet *ifp);
int cvm_oct_sgmii_init(struct ifnet *ifp);
int cvm_oct_spi_init(struct ifnet *ifp);
void cvm_oct_spi_uninit(struct ifnet *ifp);
int cvm_oct_xaui_init(struct ifnet *ifp);

