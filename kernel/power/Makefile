
ifeq ($(CONFIG_PM_DEBUG),y)
EXTRA_CFLAGS	+=	-DDEBUG
endif

obj-y				:= main.o process.o console.o pm.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= swsusp.o disk.o

obj-$(CONFIG_SUSPEND_SMP)	+= smp.o

obj-$(CONFIG_MAGIC_SYSRQ)	+= poweroff.o
