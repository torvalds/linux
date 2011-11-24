#
#  Local Makefile includes for tool and kernel source paths
# 
#  Copyright 2004-2010 Atheros Communications, Inc.
#
# When creating a new build type, use the following template:
#  	ifeq ($(ATH_BUILD_TYPE),<platform name>) 
#       ATH_ARCH_CPU_TYPE :=  < cpu architecture >
#       ATH_CROSS_COMPILE_TYPE := < cross compiler path >
#       ATH_LINUXPATH := < kernel source path >

#ATH_BSP_TYPE=msm7627a
#ATH_ANDROID_ROOT=/local/mnt/workspace/ssajjan/gbtip/

ATH_BSP_TYPE=sun4i
ATH_CROSS_COMPILE_TYPE:=${CROSS_COMPILE}
#ATH_BUILD_TYPE:=LOCAL_arm
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

  # Kernel path
# [WIFI] Wi-Fi Support ++
# ifeq ($(ATH_BSP_TYPE),$(filter $(ATH_BSP_TYPE),qsd8250_surf qsd8250_ffa msm7627_surf msm7627_ffa msm7627a msm7625_ffa msm7625_surf msm7630_surf))
# Use it always on our platform. Driver select by device/[VENDOR]/[PRODUCT]/BoardConfig.mk
# [WIFI] Wi-Fi Support --
#ATH_ARCH_CPU_TYPE := arm
#ATH_LINUXPATH := $(ATH_ANDROID_ROOT)/out/target/product/$(ATH_BSP_TYPE)/obj/KERNEL_OBJ
#ATH_CROSS_COMPILE_TYPE := $(ATH_ANDROID_ROOT)/prebuilt/linux-x86/toolchain/arm-eabi-4.3.1/bin/arm-eabi-
# [WIFI] Wi-Fi Support ++
#endif
# [WIFI] Wi-Fi Support --

ifndef ATH_LINUXPATH
# Check for Nvidia-base platform
ifeq ($(ATH_BSP_TYPE),$(filter $(ATH_BSP_TYPE),harmony ventana))
ATH_ARCH_CPU_TYPE := arm
ATH_LINUXPATH := $(ATH_ANDROID_ROOT)/out/target/product/$(ATH_BSP_TYPE)/obj/KERNEL
ifeq ($(ATH_BSP_TYPE),$(filter $(ATH_BSP_TYPE),harmony))
ATH_CROSS_COMPILE_TYPE := $(ATH_ANDROID_ROOT)/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/arm-eabi-
endif
ifeq ($(ATH_BSP_TYPE),$(filter $(ATH_BSP_TYPE),ventana))
ATH_CROSS_COMPILE_TYPE := $(ATH_ANDROID_ROOT)/prebuilt/linux-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi-
endif
endif
endif

ifndef ATH_LINUXPATH
# Check for IMX51-base platform
ifeq ($(ATH_BSP_TYPE),$(filter $(ATH_BSP_TYPE),imx51_bbg))
ATH_ARCH_CPU_TYPE := arm
ATH_LINUXPATH := $(ATH_ANDROID_ROOT)/out/target/product/$(ATH_BSP_TYPE)/obj/KERNEL
ATH_CROSS_COMPILE_TYPE := $(ATH_ANDROID_ROOT)/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/arm-eabi-
endif
endif


ifndef ATH_LINUXPATH
# Comment out the following variable for your platform 
# Link your kernel into android SDK directory as 'kernel' directory
# export  ATH_LINUXPATH= [Your android/kernel path ]
$(error define your kernel path here for ATH_LINUXPATH)
ATH_CROSS_COMPILE_TYPE := $(ATH_ANDROID_ROOT)/prebuilt/linux-x86/toolchain/arm-eabi-4.3.1/bin/arm-eabi-
endif

  $(info *************************)
  $(info PLATFORM: $(ATH_BUILD_TYPE))
  $(info BSP:      $(ATH_BSP_TYPE))
  $(info *************************)
  # Some environment settings
  ATH_BUILD_TYPE=$(ATH_BSP_TYPE)_ARM_NATIVEMMC
  ATH_ANDROID_ENV := yes
  ATH_SOFTMAC_FILE_USED := no
  ATH_DEBUG_DRIVER := yes
  ATH_HTC_RAW_INT_ENV :=yes
  ATH_AR6K_OTA_TEST_MODE := no

  # Some common variable
#  ATH_BUS_TYPE := SDIO
#  ATH_OS_SUB_TYPE := linux_2_6
#  ATH_BUS_SUBTYPE := linux_sdio
  ATH_BUILD_3RDPARTY := no
  ATH_CFG80211_ENV := no
  ATH_BUILD_SYSTEM_TOOLS := no
  ATH_AR6K_HCI_PAL := no
  ATH_AR6K_DEBUG_ALLOC := no
  ATH_BUILD_P2P := yes
