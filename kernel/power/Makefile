
ifeq ($(CONFIG_PM_DEBUG),y)
EXTRA_CFLAGS	+=	-DDEBUG
endif

obj-y				:= main.o process.o console.o
obj-$(CONFIG_PM_LEGACY)		+= pm.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= swsusp.o disk.o snapshot.o swap.o user.o

obj-$(CONFIG_SUSPEND_SMP)	+= smp.o

obj-$(CONFIG_MAGIC_SYSRQ)	+= poweroff.o
