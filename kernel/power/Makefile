
ifeq ($(CONFIG_PM_DEBUG),y)
EXTRA_CFLAGS	+=	-DDEBUG
endif

swsusp-smp-$(CONFIG_SMP)	+= smp.o

obj-y				:= main.o process.o console.o pm.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= swsusp.o $(swsusp-smp-y) disk.o

obj-$(CONFIG_MAGIC_SYSRQ)	+= poweroff.o
