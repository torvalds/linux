#
# Makefile for the linux BeOS filesystem routines.
#
 
obj-$(CONFIG_BEFS_FS) += befs.o
ccflags-$(CONFIG_BEFS_DE)    += -DDE
befs-objs := datastream.o btree.o super.o inode.o de.o io.o linuxvfs.o
