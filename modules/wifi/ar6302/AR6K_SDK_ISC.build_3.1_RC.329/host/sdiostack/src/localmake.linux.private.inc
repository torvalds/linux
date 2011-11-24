
ifeq ($(CT_BUILD_TYPE),LOCAL_i686_ATHEROS) 
# compiling locally on X86
CT_ARCH_CPU_TYPE :=
CT_CROSS_COMPILE_TYPE :=
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/fc3-i686-linux-2.6.9
CT_HC_DRIVERS :=pci_ellen/ pci_std/
EXTRA_CFLAGS += -DCT_MAN_CODE_CHECK=0x271
endif  


ifeq ($(CT_BUILD_TYPE),MV_OMAP5912_ATHEROS) 
# cross compiling to OMAP
CT_ARCH_CPU_TYPE :=arm  
CT_CROSS_COMPILE_TYPE :=$(DEV_BASE)/linux/tools/gcc/arm/mv_3.3.1/bin/arm_v4t_le-
CT_LINUXPATH :=$(DEV_BASE)/linux/kernels/mv-omap-5912-linux-2.4.20
CT_OS_SUB_TYPE :=linux_2_4
CT_HC_DRIVERS :=omap_raw_spi/
EXTRA_CFLAGS += -DCT_MAN_CODE_CHECK=0x271
endif


 
 
