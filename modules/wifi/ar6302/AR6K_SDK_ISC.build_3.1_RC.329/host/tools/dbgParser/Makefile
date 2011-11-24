CC := $(ATH_CROSS_COMPILE_TYPE)gcc
KERNEL_SRC :=$(ATH_LINUXPATH)

all:
	$(CC) -Wall -I./ -I$(KERNEL_SRC)/include -I../../include -I../../../include -I../../wlan/include -I../../os/linux/include dbgParser.c -o dbgParser
