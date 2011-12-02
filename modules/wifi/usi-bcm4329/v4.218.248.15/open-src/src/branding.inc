# Copyright (C) 1999-2010, Broadcom Corporation
# 
#      Unless you and Broadcom execute a separate written software license
# agreement governing use of this software, this software is licensed to you
# under the terms of the GNU General Public License version 2 (the "GPL"),
# available at http://www.broadcom.com/licenses/GPLv2.php, with the
# following added to such license:
# 
#      As a special exception, the copyright holders of this software give you
# permission to link this software with independent modules, and to copy and
# distribute the resulting executable under terms of your choice, provided that
# you also meet, for each linked independent module, the terms and conditions of
# the license of that module.  An independent module is a module which is not
# derived from this software.  The special exception does not apply to any
# modifications of the software.
# 
#      Notwithstanding the above, under no circumstances may you combine this
# software in any way with any other Broadcom software provided under a license
# other than the GPL, without Broadcom's express prior written consent.
#  
ifeq ($(origin BRAND), undefined)
# undefine BRAND
BRAND=
endif

DIAG_FILE_BASE	    = bcm42dia
DIAG_A0_NIC_BASE    = bcm42da0
DIAG_B0_NIC_BASE    = bcm42db0
DIAG_HWACCESS_BASE  = bcm42ioa
DIAG_HWDEV_BASE	    = bcm42xhw
DIAG_CTLDLL_BASE    = bcm42ctl

export RELAY_FILE_BASE=bcm42rly
export NIC_NDIS30_DRIVER=bcm42xx3
export NIC_NDIS40_DRIVER=bcm42xx4
export NIC_NDIS50_DRIVER=bcm42xx5
export USB_NDIS50_DRIVER=bcm42u
export NDI_PCI_DLL=bcm42ndi
export NDI_USB_DLL=bcmndiu
