#
#  Local Makefile includes for tool and kernel source paths
#  Copyright 2004-2006 Atheros Communications, Inc.
#
# When creating a new build type, use the following template:
#  	ifeq ($(CT_BUILD_TYPE),<platform name>) 
#       CT_ARCH_CPU_TYPE :=  < cpu architecture >
#       CT_CROSS_COMPILE_TYPE := < cross compiler path >
#       CT_LINUXPATH := < kernel source path >
#       < Add HCD flags to compile specific HCDs for this platform >
