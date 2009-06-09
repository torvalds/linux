
ifeq ($(CONFIG_PM_DEBUG),y)
EXTRA_CFLAGS	+=	-DDEBUG
endif

obj-$(CONFIG_PM)		+= main.o
obj-$(CONFIG_PM_SLEEP)		+= console.o
obj-$(CONFIG_FREEZER)		+= process.o
obj-$(CONFIG_SUSPEND)		+= suspend.o
obj-$(CONFIG_PM_TEST_SUSPEND)	+= suspend_test.o
obj-$(CONFIG_HIBERNATION)	+= swsusp.o hibernate.o snapshot.o swap.o user.o

obj-$(CONFIG_MAGIC_SYSRQ)	+= poweroff.o
