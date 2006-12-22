#
# Makefile for the linux kernel.
#

obj-y				:= main.o version.o mounts.o initramfs.o
obj-$(CONFIG_GENERIC_CALIBRATE_DELAY) += calibrate.o

mounts-y			:= do_mounts.o
mounts-$(CONFIG_BLK_DEV_RAM)	+= do_mounts_rd.o
mounts-$(CONFIG_BLK_DEV_INITRD)	+= do_mounts_initrd.o
mounts-$(CONFIG_BLK_DEV_MD)	+= do_mounts_md.o

# files to be removed upon make clean
clean-files := ../include/linux/compile.h

# dependencies on generated files need to be listed explicitly

$(obj)/main.o: include/linux/compile.h
$(obj)/version.o: include/linux/compile.h

