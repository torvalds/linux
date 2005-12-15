EXTRA_CFLAGS += -Ifs/ocfs2

obj-$(CONFIG_OCFS2_FS) += ocfs2_dlm.o ocfs2_dlmfs.o

ocfs2_dlm-objs := dlmdomain.o dlmdebug.o dlmthread.o dlmrecovery.o \
	dlmmaster.o dlmast.o dlmconvert.o dlmlock.o dlmunlock.o dlmver.o

ocfs2_dlmfs-objs := userdlm.o dlmfs.o dlmfsver.o
