#
# Makefile for the linux ipc.
#

obj-$(CONFIG_SYSVIPC_COMPAT) += compat.o
obj_mem-$(CONFIG_MEMORY_HOTPLUG) += ipcns_notifier.o
obj-$(CONFIG_SYSVIPC) += util.o msgutil.o msg.o sem.o shm.o $(obj_mem-y)
obj-$(CONFIG_SYSVIPC_SYSCTL) += ipc_sysctl.o
obj_mq-$(CONFIG_COMPAT) += compat_mq.o
obj-$(CONFIG_POSIX_MQUEUE) += mqueue.o msgutil.o $(obj_mq-y)
obj-$(CONFIG_IPC_NS) += namespace.o

