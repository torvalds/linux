
ccflags-$(CONFIG_PM_DEBUG)	:= -DDEBUG

obj-$(CONFIG_PM)		+= main.o
obj-$(CONFIG_PM_SLEEP)		+= console.o
obj-$(CONFIG_FREEZER)		+= process.o
obj-$(CONFIG_SUSPEND)		+= suspend.o
obj-$(CONFIG_PM_TEST_SUSPEND)	+= suspend_test.o
obj-$(CONFIG_HIBERNATION)	+= hibernate.o snapshot.o swap.o user.o \
				   block_io.o
obj-$(CONFIG_WAKELOCK)		+= wakelock.o
obj-$(CONFIG_USER_WAKELOCK)	+= userwakelock.o
obj-$(CONFIG_EARLYSUSPEND)	+= earlysuspend.o
obj-$(CONFIG_CONSOLE_EARLYSUSPEND)	+= consoleearlysuspend.o

obj-$(CONFIG_MAGIC_SYSRQ)	+= poweroff.o
