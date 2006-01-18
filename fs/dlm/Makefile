obj-$(CONFIG_DLM) +=		dlm.o
obj-$(CONFIG_DLM_DEVICE) +=	dlm_device.o

dlm-y :=			ast.o \
				config.o \
				dir.o \
				lock.o \
				lockspace.o \
				lowcomms.o \
				main.o \
				member.o \
				memory.o \
				midcomms.o \
				rcom.o \
				recover.o \
				recoverd.o \
				requestqueue.o \
				util.o
dlm-$(CONFIG_DLM_DEBUG) +=	debug_fs.o

dlm_device-y :=			device.o
