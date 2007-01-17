obj-$(CONFIG_DLM) +=		dlm.o
dlm-y :=			ast.o \
				config.o \
				dir.o \
				lock.o \
				lockspace.o \
				main.o \
				member.o \
				memory.o \
				midcomms.o \
				rcom.o \
				recover.o \
				recoverd.o \
				requestqueue.o \
				user.o \
				util.o
dlm-$(CONFIG_DLM_DEBUG) +=	debug_fs.o

dlm-$(CONFIG_DLM_TCP)   += lowcomms-tcp.o

dlm-$(CONFIG_DLM_SCTP)  += lowcomms-sctp.o