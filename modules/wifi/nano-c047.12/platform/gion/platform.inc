export ARCH=arm

KERNEL_DIR = /home/nanotest/gion-kernel/msm

PLATFORM_FLAGS += -D__linux__
PLATFORM_FLAGS += -DKSDIO_NO_BLOCK_MODE
PLATFORM_FLAGS += -DFIRMWARE_PADDING
#fix for broken kernel vs. gcc 4.4:
PLATFORM_FLAGS += -DMODULE_ARCH_INIT={.foo=0}

CROSS_COMPILE = /home/nanotest/mydroid/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/arm-eabi-
KERNEL_MAJOR_VERSION=2.6
TRANSPORT?=ksdio

