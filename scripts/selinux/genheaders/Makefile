hostprogs-y	:= genheaders
HOST_EXTRACFLAGS += \
	-I$(srctree)/include/uapi -I$(srctree)/include \
	-I$(srctree)/security/selinux/include

always		:= $(hostprogs-y)
