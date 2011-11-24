
calmake.linux.inc，参考如下：

#ATH_BUILD_TYPE := <Platform (Eg: LOCAL_i686)>
#ATH_BUS_TYPE := <Interconnect type (Eg: SDIO, SPI)>
#ATH_LINUXPATH := <Pointer to kernel source (Eg: /<path>/fc3-i686-2.6.9)>
#ATH_ARCH_CPU_TYPE := <Type of architecture (Eg: arm)>
#ATH_CROSS_COMPILE_TYPE := <Path to the compiler toolchain - Eg: (/<path>/arm_v4t_le-)
#ATH_OS_SUB_TYPE := <Kernel version - (Eg: linux_2_4, linux_2_6)>
#ATH_CFG80211_ENV := <yes or no>

ATH_BUILD_TYPE := LOCAL_i686
ATH_BUS_TYPE := SDIO
ATH_LINUXPATH := /usr/src/linux-headers-2.6.32-21-generic
ATH_ARCH_CPU_TYPE := arm
ATH_OS_SUB_TYPE := linux_2_6
ATH_HC_DRIVERS := 'pci_ellen/ pci_std/'
ATH_BUS_SUBTYPE := linux_sdio
#export ATH_EEPROM_FILE_USED:=yes
export ATH_ANDROID_ENV := yes
#export ATH_UPDATA_MAC_EEPROM := yes
ATH_DEBUG_DRIVER := yes
ATH_AR6K_BUILTIN_HCI_TRANSPORT := yes

2.驱动包编译成功后，将附件固件放在默认路径ath6k/AR6003/hw2.0/下，
（该目录也可根据自己的需要在驱动代码AR6kSDK.3.0_RC.205\host\os\linux\include\ar6000_drv.h
中进行修改。）然后insmod ar6000.ko即可。


