# Rules for making the NTFS driver.

obj-$(CONFIG_NTFS_FS) += ntfs.o

ntfs-objs := aops.o attrib.o collate.o compress.o debug.o dir.o file.o \
	     index.o inode.o mft.o mst.o namei.o runlist.o super.o sysctl.o \
	     unistr.o upcase.o

EXTRA_CFLAGS = -DNTFS_VERSION=\"2.1.29\"

ifeq ($(CONFIG_NTFS_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

ifeq ($(CONFIG_NTFS_RW),y)
EXTRA_CFLAGS += -DNTFS_RW

ntfs-objs += bitmap.o lcnalloc.o logfile.o quota.o usnjrnl.o
endif
