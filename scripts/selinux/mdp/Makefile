hostprogs-y	:= mdp
HOST_EXTRACFLAGS += \
	-I$(srctree)/include/uapi -I$(srctree)/include \
	-I$(srctree)/security/selinux/include

always		:= $(hostprogs-y)
clean-files	:= policy.* file_contexts
