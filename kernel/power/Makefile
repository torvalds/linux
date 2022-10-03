# SPDX-License-Identifier: GPL-2.0

ifeq ($(CONFIG_DYNAMIC_DEBUG), y)
CFLAGS_swap.o                   := -DDEBUG
CFLAGS_snapshot.o               := -DDEBUG
CFLAGS_energy_model.o           := -DDEBUG
endif

KASAN_SANITIZE_snapshot.o	:= n

obj-y				+= qos.o
obj-$(CONFIG_PM)		+= main.o
obj-$(CONFIG_VT_CONSOLE_SLEEP)	+= console.o
obj-$(CONFIG_FREEZER)		+= process.o
obj-$(CONFIG_SUSPEND)		+= suspend.o
obj-$(CONFIG_PM_TEST_SUSPEND)	+= suspend_test.o
obj-$(CONFIG_HIBERNATION)	+= hibernate.o snapshot.o swap.o
obj-$(CONFIG_HIBERNATION_SNAPSHOT_DEV) += user.o
obj-$(CONFIG_PM_AUTOSLEEP)	+= autosleep.o
obj-$(CONFIG_PM_WAKELOCKS)	+= wakelock.o

obj-$(CONFIG_MAGIC_SYSRQ)	+= poweroff.o

obj-$(CONFIG_ENERGY_MODEL)	+= energy_model.o
