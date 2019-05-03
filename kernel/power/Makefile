# SPDX-License-Identifier: GPL-2.0

ccflags-$(CONFIG_PM_DEBUG)	:= -DDEBUG

KASAN_SANITIZE_snapshot.o	:= n

obj-y				+= qos.o
obj-$(CONFIG_PM)		+= main.o
obj-$(CONFIG_VT_CONSOLE_SLEEP)	+= console.o
obj-$(CONFIG_FREEZER)		+= process.o
obj-$(CONFIG_SUSPEND)		+= suspend.o
obj-$(CONFIG_PM_TEST_SUSPEND)	+= suspend_test.o
obj-$(CONFIG_HIBERNATION)	+= hibernate.o snapshot.o swap.o user.o
obj-$(CONFIG_PM_AUTOSLEEP)	+= autosleep.o
obj-$(CONFIG_PM_WAKELOCKS)	+= wakelock.o

obj-$(CONFIG_MAGIC_SYSRQ)	+= poweroff.o

obj-$(CONFIG_SUSPEND)		+= wakeup_reason.o
obj-$(CONFIG_ENERGY_MODEL)	+= energy_model.o
