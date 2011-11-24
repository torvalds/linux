# Copyright 2004-2006 Atheros Communications, Inc.
# $ATH_LICENSE_SDIOSTACK0_MK$
#
# Local Makefile includes for tool and kernel source paths
#
# When creating a new build type, use the following template:
#  	//ifeq ($(CT_BUILD_TYPE),<platform name>) 
#       //CT_ARCH_CPU_TYPE :=  < cpu architecture >
#       //CT_CROSS_COMPILE_TYPE := < cross compiler path >
#       //CT_LINUXPATH := < kernel source path >
#       //< Add HCD flags to compile specific HCDs for this platform >
#       //endif 
#
#

DEV_BASE :=/usr/dev

ifeq ($(CT_BUILD_TYPE),LOCAL_i686) 
# compiling locally on x86 for i686
CT_ARCH_CPU_TYPE :=  
CT_CROSS_COMPILE_TYPE :=
#CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/fc3-i686-linux-2.6.9
CT_LINUXPATH :=$(ATH_LINUXPATH)
CT_HC_DRIVERS :=pci_ellen/ pci_std/
CT_OUTPUT_FLAGS := 
endif  
   
ifeq ($(CT_BUILD_TYPE),LOCAL_i586) 
# compiling locally on x86 for i586
CT_ARCH_CPU_TYPE := 
CT_CROSS_COMPILE_TYPE :=
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/fc3-i586-linux-2.6.9
CT_HC_DRIVERS :=pci_ellen/ pci_std/
CT_OUTPUT_FLAGS := 
endif

ifeq ($(CT_BUILD_TYPE),GUMSTIX_ARM) 
# cross compiling to gumstix arm
CT_ARCH_CPU_TYPE :=arm  
CT_CROSS_COMPILE_TYPE :=$(DEV_BASE)/linux/tools/gcc/arm/3.4.2/bin/arm-unknown-linux-gnu-
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/gumstix-linux-2.6.9-rc1
CT_OUTPUT_FLAGS :=   
CT_HC_DRIVERS :=pxa255/
endif

ifeq ($(CT_BUILD_TYPE),OMAP1610_ARM) 
# cross compiling to OMAP
CT_ARCH_CPU_TYPE :=arm  
CT_CROSS_COMPILE_TYPE :=$(DEV_BASE)/linux/tools/gcc/arm/3.3.2/bin/arm-linux-
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/omap-1610-linux-2.6.9
CT_OUTPUT_FLAGS :=  
CT_HC_DRIVERS :=omap/
endif

ifeq ($(CT_BUILD_TYPE),MV_OMAP1610_ARM) 
# cross compiling to OMAP
CT_ARCH_CPU_TYPE :=arm  
CT_CROSS_COMPILE_TYPE :=$(DEV_BASE)/linux/tools/gcc/arm/mv_3.3.1_v4t/bin/arm_v4t_le-
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/mv-omap-1610-linux-2.4.20
CT_OUTPUT_FLAGS :=  
CT_OS_SUB_TYPE :=linux_2_4
CT_HC_DRIVERS :=omap/
endif
 
ifeq ($(CT_BUILD_TYPE),MV_OMAP5912_ARM) 
# cross compiling to OMAP
CT_ARCH_CPU_TYPE :=arm  
CT_CROSS_COMPILE_TYPE :=$(DEV_BASE)/linux/tools/gcc/arm/mv_3.3.1_v4t/bin/arm_v4t_le-
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/mv-omap-5912-linux-2.4.20
CT_OUTPUT_FLAGS :=
CT_OS_SUB_TYPE :=linux_2_4
CT_HC_DRIVERS :=omap_raw_spi/ omap/
endif

ifeq ($(CT_BUILD_TYPE),SANDGATEII_ARM) 
# cross compiling to SandGateII arm
CT_ARCH_CPU_TYPE :=arm  
CT_CROSS_COMPILE_TYPE :=$(DEV_BASE)/linux/tools/gcc/arm/3.4.3/bin/arm-linux-
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/pxa270-sg2-linux-2.6.9
CT_OUTPUT_FLAGS := 
CT_HC_DRIVERS :=pxa270/
endif

ifeq ($(CT_BUILD_TYPE),MX21_ARM) 
# cross compiling to MX21
CT_ARCH_CPU_TYPE :=arm  
CT_CROSS_COMPILE_TYPE :=$(DEV_BASE)/linux/tools/gcc/arm/3.3.2/bin/arm-linux-
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/freescale-mx21-celinux-2.4.20
CT_OUTPUT_FLAGS :=
CT_OS_SUB_TYPE :=linux_2_4
CT_HC_DRIVERS :=mx21/
endif

ifeq ($(CT_BUILD_TYPE),MV_OMAP2420_ARM) 
# cross compiling to OMAP
CT_ARCH_CPU_TYPE :=arm  
CT_CROSS_COMPILE_TYPE :=$(DEV_BASE)/linux/tools/gcc/arm/mv_3.3.1_v5t/bin/arm_v5t_le-
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/mv-omap-2420-linux-2.4.20
CT_OUTPUT_FLAGS :=
CT_OS_SUB_TYPE :=linux_2_4
CT_HC_DRIVERS :=omap2420_raw_spi/ omap_2420/
endif

ifeq ($(CT_BUILD_TYPE),OMAP2420_ARM)
CT_ARCH_CPU_TYPE :=arm  
CT_CROSS_COMPILE_TYPE :=$(DEV_BASE)/linux/tools/gcc/arm/3.4.2/bin/arm-unknown-linux-gnu-
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/omap-2420-linux-2.6.10
CT_OUTPUT_FLAGS :=
CT_HC_DRIVERS :=omap_2420/


ifeq ($(CT_BUILD_TYPE),SOFTWINNER_ARM)
CT_ARCH_CPU_TYPE :=arm  
CT_CROSS_COMPILE_TYPE :=arm-none-linux-gnueabi-
CT_LINUXPATH :=~/workdir/lichee/linux-2.6.36
CT_OUTPUT_FLAGS :=
CT_HC_DRIVERS :=winner/
endif
