#
#  Local Makefile includes for tool and kernel source paths
#  ***** This is only a sample, modify the paths for your specific build environment *****
# 
#  Copyright 2004-2010 Atheros Communications, Inc.
#
# When creating a new build type, use the following template:
#  	ifeq ($(ATH_BUILD_TYPE),<platform name>) 
#       ATH_ARCH_CPU_TYPE :=  < cpu architecture >
#       ATH_CROSS_COMPILE_TYPE := < cross compiler path >
#       ATH_LINUXPATH := < kernel source path >

ATH_CROSS_COMPILE_TYPE:=${CROSS_COMPILE}
ATH_BUILD_TYPE:=LOCAL_arm
ATH_BUS_TYPE:=SDIO
ATH_LINUXPATH:=${KERNEL_DIR}
ATH_ARCH_CPU_TYPE:=arm
ATH_OS_SUB_TYPE:=linux_2_6
ATH_BUS_SUBTYPE=linux_sdio
ATH_ANDROID_ENV=yes
ATH_SOFTMAC_FILE_USED=yes
ATH_EEPROM_FILE_USED=yes
ATH_CFG80211_ENV:=no
ATH_DEBUG_DRIVER:=yes

COMPILED_IMAGE_OBJECTS_PATH:=${INSTALL_DIR}

export ATH_CROSS_COMPILE_TYPE
export ATH_BUILD_TYPE
export ATH_BUS_TYPE
export ATH_LINUXPATH
export ATH_ARCH_CPU_TYPE
export ATH_OS_SUB_TYPE
export ATH_BUS_SUBTYPE
export ATH_ANDROID_ENV
export ATH_SOFTMAC_FILE_USED
export ATH_EEPROM_FILE_USED
export ATH_CFG80211_ENV
export COMPILED_IMAGE_OBJECTS_PATH
