obj-$(CONFIG_SECURITY_LANDLOCK) := landlock.o

landlock-y := setup.o syscalls.o object.o ruleset.o \
	cred.o ptrace.o fs.o

landlock-$(CONFIG_INET) += net.o
