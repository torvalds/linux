ifneq ($(KERNELRELEASE),)
# kbuild part of makefile

obj-m  := btrfs.o
btrfs-y := super.o ctree.o extent-tree.o print-tree.o root-tree.o dir-item.o \
	   hash.o file-item.o inode-item.o inode-map.o disk-io.o \
	   transaction.o bit-radix.o inode.o file.o tree-defrag.o \
	   extent_map.o sysfs.o struct-funcs.o xattr.o acl.o

#btrfs-y := ctree.o disk-io.o radix-tree.o extent-tree.o print-tree.o \
#	  root-tree.o dir-item.o hash.o file-item.o inode-item.o \
#	  inode-map.o \

else

# Normal Makefile

KERNELDIR := /lib/modules/`uname -r`/build
all:
	$(MAKE) -C $(KERNELDIR) M=`pwd` modules
modules_install:
	$(MAKE) -C $(KERNELDIR) M=`pwd` modules_install
clean:
	$(MAKE) -C $(KERNELDIR) M=`pwd` clean

tester:
	$(MAKE) -C $(KERNELDIR) M=`pwd` tree-defrag.o transaction.o sysfs.o super.o root-tree.o inode-map.o inode-item.o inode.o file-item.o file.o extent_map.o disk-io.o ctree.o dir-item.o extent-tree.o
endif
