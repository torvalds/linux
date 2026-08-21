obj-$(CONFIG_SECURITY_LANDLOCK) := landlock.o

landlock-y := \
	setup.o \
	syscalls.o \
	object.o \
	ruleset.o \
	cred.o \
	task.o \
	fs.o \
	tsync.o \
	domain.o

landlock-$(CONFIG_INET) += net.o

landlock-$(CONFIG_SECURITY_LANDLOCK_LOG) += \
	id.o \
	log.o

landlock-$(CONFIG_AUDIT) += audit.o

landlock-$(CONFIG_TRACEPOINTS) += trace.o
