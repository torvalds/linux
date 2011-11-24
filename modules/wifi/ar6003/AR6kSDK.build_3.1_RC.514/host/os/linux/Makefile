#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2005-2010 Atheros Corporation.  All rights reserved.
# 
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
#
#------------------------------------------------------------------------------
#==============================================================================
# Author(s): ="Atheros"
#==============================================================================
#######################################################################################
#  AR6K Kernel Module makefile.
#
#    This makefile is invoked by the master makefile in the linux kernel, the linux kernel
#    source must be initially compiled.  The top level make will invoke the main linux kernel
#    makefile which will subsequently pick up this makefile.
#
#######################################################################################


REV ?= 2

EXTRA_CFLAGS += -I$(ATH_SRC_BASE)/include
EXTRA_CFLAGS += -I$(ATH_SRC_BASE)/../include
EXTRA_CFLAGS += -I$(ATH_SRC_BASE)/wlan/include
EXTRA_CFLAGS += -I$(ATH_SRC_BASE)/os/linux/include
EXTRA_CFLAGS += -I$(ATH_SRC_BASE)/os/
EXTRA_CFLAGS += -I$(ATH_SRC_BASE)/bmi/include
EXTRA_CFLAGS += -I$(ATH_SRC_BASE)/../include/AR6002

EXTRA_CFLAGS += -DLINUX -D__KERNEL__ \
                -DTCMD -DSEND_EVENT_TO_APP -DUSER_KEYS \
                -DNO_SYNC_FLUSH  -DHTC_EP_STAT_PROFILING -DWLAN_HEADERS\
                -DATH_AR6K_11N_SUPPORT \
                -DATH_SUPPORT_DFS \
		-DWAPI_ENABLE    #\
                -DMULTIPLE_FRAMES_PER_INTERRUPT -DAR6000REV$(REV) \
                -DBLOCK_TX_PATH_FLAG -DCHECKSUM_OFFLOAD \


ifeq ($(ATH_CFG80211_ENV),yes)
   EXTRA_CFLAGS += -DATH6K_CONFIG_CFG80211
 #  EXTRA_CFLAGS += -DCFG80211_WAPI_ENABLE
endif

ifeq ($(ATH_INIT_MODE_DRV_ENABLED), yes)
ifeq ($(ATH_AR600x_WB31_XXX),yes)
EXTRA_CFLAGS += -DAR600x_WB31_XXX
else
ifeq ($(ATH_AR600x_SD32_XXX),yes)
EXTRA_CFLAGS += -DAR600x_SD32_XXX
else
EXTRA_CFLAGS += -DAR600x_SD31_XXX
endif
endif
EXTRA_CFLAGS += -DINIT_MODE_DRV_ENABLED -DBMIENABLE_SET
endif

ifeq ($(ATH_ANDROID_ENV),yes)
    EXTRA_CFLAGS += -DANDROID_ENV -D__linux__ $(ATH_ANDROID_BUILD_FLAGS)
    EXTRA_CFLAGS += -DINIT_MODE_DRV_ENABLED -DBMIENABLE_SET -DAR600x_SD31_XXX \
                    -DATH6KL_CONFIG_HIF_VIRTUAL_SCATTER \
                    -DCONFIG_AP_VIRTUAL_ADAPTER_SUPPORT
ifeq ($(ATH_AR6K_HCI_BRIDGE),yes)
    EXTRA_CFLAGS += -DSETUPHCI_ENABLED -DSETUPBTDEV_ENABLED \
                    -DATH6KL_CONFIG_GPIO_BT_RESET -DAR600x_BT_AR3001
endif
endif

EXTRA_CFLAGS += -DCONFIG_BT
ifeq ($(ATH_AR6K_HCI_PAL),yes)
    EXTRA_CFLAGS += -DSETUPHCIPAL_ENABLED
ifeq ($(ATH_AR6K_DEBUG_HCI_PAL),yes)
    EXTRA_CFLAGS += -DHCIPAL_DEBUG
endif
endif

ifeq ($(ATH_SOFTMAC_FILE_USED),yes)
    EXTRA_CFLAGS += -DSOFTMAC_FILE_USED
endif

ifeq ($(ATH_AR6K_OTA_TEST_MODE),yes)
    EXTRA_CFLAGS += -DATH6K_CONFIG_OTA_MODE
endif

ifeq ($(ATH_AR6K_HCI_BRIDGE),yes)
EXTRA_CFLAGS += -I$(ATH_SRC_BASE)/miscdrv/ar3kps
EXTRA_CFLAGS += -I$(ATH_SRC_BASE)/../include/AR6002/hw4.0

EXTRA_CFLAGS += -DATH_AR6K_ENABLE_GMBOX \
                -DHCI_TRANSPORT_SDIO

ifeq ($(ATH_AR6K_BUILTIN_HCI_TRANSPORT),no)
EXTRA_CFLAGS += -DEXPORT_HCI_BRIDGE_INTERFACE
endif
endif


ifeq ($(ATH_HTC_RAW_INT_ENV),yes)
  EXTRA_CFLAGS += -DHTC_RAW_INTERFACE
endif

ifeq ($(ATH_DEBUG_DRIVER),yes)
   EXTRA_CFLAGS += -DDEBUG -DATH_DEBUG_MODULE
endif

ifeq ($(ATH_BUILD_P2P),yes)
   EXTRA_CFLAGS += -DP2P
endif

ifeq ($(AR6002_REV),4)
   EXTRA_CFLAGS += -DAR6002_REV4
endif

ifneq ("$(wildcard $(ATH_SRC_BASE)/../include/AR6002/hw2.0)","")
   EXTRA_CFLAGS += -DAR6002_HEADERS_DEF
endif

ifneq ("$(wildcard $(ATH_SRC_BASE)/../include/AR6002/hw4.0)","")
   EXTRA_CFLAGS += -DAR6003_HEADERS_DEF
endif

ifneq ("$(wildcard $(ATH_SRC_BASE)/../include/AR6002/hw6.0)","")
   EXTRA_CFLAGS += -DMCKINLEY_HEADERS_DEF
endif

ifneq ($(ATH_OS_SUB_TYPE),linux_2_4)
# linux 2.6 and higher kernel module build settings
EXTRA_CFLAGS += -DKERNEL_2_6
obj-m += ar6000.o
ifeq ($(ATH_AR6K_BUILTIN_HCI_TRANSPORT),no)
obj-m += bt_hci_sdio.o
endif
-include $(ATH_SRC_BASE)/hif/$(ATH_HIF_TYPE)/Makefile

ifneq ($(ATH_HIF_MESSAGE_BASED),yes)
ar6000-objs += ../../htc2/AR6000/ar6k.o        \
			   ../../htc2/AR6000/ar6k_events.o \
               ../../htc2/htc_send.o       \
               ../../htc2/htc_recv.o       \
               ../../htc2/htc_services.o   \
               ../../htc2/htc.o     	   
endif

ifeq ($(ATH_HIF_MESSAGE_BASED),yes)
ar6000-objs += ../../htc_thin/htc_send.o       \
               ../../htc_thin/htc_recv.o       \
               ../../htc_thin/htc_services.o   \
               ../../htc_thin/htc.o     	   
endif               
                        
               
ar6000-objs += ../../bmi/src/bmi.o              \
               ar6000_drv.o           			\
               ar6000_pm.o           			\
               ar6000_raw_if.o					\
               netbuf.o							\
               wireless_ext.o					\
               ioctl.o							\
               ../../miscdrv/common_drv.o		\
               ../../miscdrv/credit_dist.o		\
               ../../wmi/wmi.o                       \
               ../../reorder/rcv_aggr.o              \
               ../../dfs/dfs_ar.o \
               ../../dfs/dfs_bindetects.o \
               ../../dfs/dfs.o \
               ../../dfs/dfs_debug.o \
               ../../dfs/dfs_fcc_bin5.o \
               ../../dfs/dfs_init.o \
               ../../dfs/dfs_misc.o \
               ../../dfs/dfs_process_phyerr.o \
               ../../dfs/dfs_process_radarevent.o \
               ../../dfs/dfs_staggered.o \
               ../../dfs/dfs_host_project.o \
               ../../wlan/src/wlan_node.o            \
               ../../wlan/src/wlan_recv_beacon.o     \
               ../../wlan/src/wlan_utils.o \
               ../../regtable/regtable.o  \
               ../../regtable/AR6002def.o \
               ../../regtable/AR6003def.o \
               ../../regtable/MCKINLEYdef.o 

ifeq ($(ATH_BUILD_P2P),yes)
ar6000-objs += ../../p2p/p2p_main.o
endif

ifeq ($(ATH_HIF_TYPE),sdio)
ar6000-objs += ../../hif/common/hif_bmi_reg_access.o  \
			   ../../hif/common/hif_diag_reg_access.o

endif

ifeq ($(ATH_CFG80211_ENV),yes)
ar6000-objs += cfg80211.o
endif

ifneq ($(ATH_AR6K_BUILTIN_HCI_TRANSPORT),no)
ar6000-objs += hci_bridge.o
endif

ifneq ($(ATH_AR6K_BUILTIN_HCI_PAL),no)
ar6000-objs += ar6k_pal.o
else
obj-m += ar6k_pal.o
endif

ifeq ($(ATH_AR6K_HCI_BRIDGE),yes)
ar6000-objs += ../../htc2/AR6000/ar6k_gmbox.o          \
			   ../../htc2/AR6000/ar6k_gmbox_hciuart.o
ifneq ($(ATH_AR6K_BUILTIN_HCI_TRANSPORT),no)
ar6000-objs += ../../miscdrv/ar3kconfig.o				\
               ../../miscdrv/ar3kps/ar3kpsconfig.o		\
               ../../miscdrv/ar3kps/ar3kpsparser.o
else
ar6000-objs += export_hci_transport.o
endif

ifeq ($(ATH_AR6K_BUILTIN_HCI_TRANSPORT),no)
bt_hci_sdio-objs += hci_bridge.o        \
                ../../miscdrv/ar3kconfig.o				\
                ../../miscdrv/ar3kps/ar3kpsconfig.o		\
                ../../miscdrv/ar3kps/ar3kpsparser.o
endif
endif

ifeq ($(ATH_ANDROID_ENV),yes)
   ar6000-objs += ar6000_android.o
endif

ifeq ($(ATH_AR6K_DEBUG_ALLOC),yes)
    EXTRA_CFLAGS += -DAR6K_ALLOC_DEBUG
    ar6000-objs += ar6k_mem_debug.o
endif

endif

ifeq ($(ATH_OS_SUB_TYPE),linux_2_4)
# linux 2.4 kernel module build settings
EXTRA_CFLAGS += -DKERNEL_2_4

O_TARGET := ar6000.o
# need export line for module export
export-objs := ar6000_drv.o
-include $(ATH_SRC_BASE)/hif/$(ATH_HIF_TYPE)/Makefile
obj-y += $(ATH_SRC_BASE)/htc2/htc.o \
         $(ATH_SRC_BASE)/htc2/htc_send.o \
         $(ATH_SRC_BASE)/htc2/htc_recv.o  \
         $(ATH_SRC_BASE)/htc2/htc_services.o \
         $(ATH_SRC_BASE)/htc2/AR6000/ar6k.o  \
         $(ATH_SRC_BASE)/htc2/AR6000/ar6k_events.o  \
         $(ATH_SRC_BASE)/bmi/src/bmi.o \
         $(ATH_SRC_BASE)/wmi/wmi.o \
         $(ATH_SRC_BASE)/reorder/rcv_aggr.o \
         $(ATH_SRC_BASE)/dfs/dfs_ar.o \
         $(ATH_SRC_BASE)/dfs/dfs_bindetects.o \
         $(ATH_SRC_BASE)/dfs/dfs.o \
         $(ATH_SRC_BASE)/dfs/dfs_debug.o \
         $(ATH_SRC_BASE)/dfs/dfs_fcc_bin5.o \
         $(ATH_SRC_BASE)/dfs/dfs_init.o \
         $(ATH_SRC_BASE)/dfs/dfs_misc.o \
         $(ATH_SRC_BASE)/dfs/dfs_process_phyerr.o \
         $(ATH_SRC_BASE)/dfs/dfs_process_radarevent.o \
         $(ATH_SRC_BASE)/dfs/dfs_staggered.o \
         $(ATH_SRC_BASE)/dfs/dfs_host_project.o \
         $(ATH_SRC_BASE)/wlan/src/wlan_node.o \
         $(ATH_SRC_BASE)/wlan/src/wlan_recv_beacon.o \
         $(ATH_SRC_BASE)/wlan/src/wlan_utils.o \
         $(ATH_SRC_BASE)/os/linux/ar6000_drv.o \
         $(ATH_SRC_BASE)/os/linux/ar6000_raw_if.o \
         $(ATH_SRC_BASE)/os/linux/wireless_ext.o \
         $(ATH_SRC_BASE)/os/linux/ioctl.o 		\
         $(ATH_SRC_BASE)/os/linux/netbuf.o     \
         $(ATH_SRC_BASE)/os/linux/hci_bridge.o     \
         $(ATH_SRC_BASE)/miscdrv/common_drv.o   \
         $(ATH_SRC_BASE)/miscdrv/credit_dist.o

ifeq ($(ATH_HIF_TYPE),SPI2)
obj-y += $(ATH_SRC_BASE)/hif/common/hif_bmi_reg_access.o \
         $(ATH_SRC_BASE)/hif/common/hif_diag_reg_access.o
         
endif

obj-m += htc.o \
         htc_send.o \
         htc_recv.o \
         htc_services.o  \
         ar6k_events.o \
         ar6k.o \
         ar6k_gmbox.o  \
         ar6k_gmbox_hciuart.o \
         hif.o  \
         bmi.o  \
         wmi.o  \
         dfs_ar.o \
         dfs_bindetects.o \
         dfs.o \
         dfs_debug.o \
         dfs_fcc_bin5.o \
         dfs_init.o \
         dfs_misc.o \
         dfs_process_phyerr.o \
         dfs_process_radarevent.o \
         dfs_staggered.o \
         dfs_host_project.o \
         wlan_node.o \
         wlan_recv_beacon.o \
         wlan_utils.o \
         ar6000_drv.o \
         ar6000_raw_if.o \
         common_drv.o \
         credit_dist.o \
         netbuf.o     \
         wireless_ext.o \
         rcv_aggr.o        \
         ioctl.o        \
         hci_bridge.o    \
         ar6000.o

ifeq ($(ATH_HIF_TYPE),SPI2)
obj-m += hif_bmi_reg_access.o \
		 hif_diag_reg_access.o

endif
             
include $(ATH_LINUXPATH)/Rules.make
endif
