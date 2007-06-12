ifneq ($(KERNELRELEASE),)
# kbuild part of makefile

obj-m  := btrfs.o
btrfs-y := super.o ctree.o extent-tree.o print-tree.o root-tree.o dir-item.o \
	   hash.o file-item.o inode-item.o inode-map.o disk-io.o \
	   transaction.o bit-radix.o inode.o file.o

#btrfs-y := ctree.o disk-io.o radix-tree.o extent-tree.o print-tree.o \
#	  root-tree.o dir-item.o hash.o file-item.o inode-item.o \
#	  inode-map.o \

else

# Normal Makefile

KERNELDIR := /lib/modules/`uname -r`/build
all::
	$(MAKE) -C $(KERNELDIR) M=`pwd` modules
clean::
	rm *.o btrfs.ko
endif
