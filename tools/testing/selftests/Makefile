TARGETS = breakpoints
TARGETS += cpu-hotplug
TARGETS += efivarfs
TARGETS += kcmp
TARGETS += memory-hotplug
TARGETS += mqueue
TARGETS += mount
TARGETS += net
TARGETS += ptrace
TARGETS += timers
TARGETS += vm
TARGETS += powerpc
TARGETS += user
TARGETS += sysctl

all:
	for TARGET in $(TARGETS); do \
		make -C $$TARGET; \
	done;

run_tests: all
	for TARGET in $(TARGETS); do \
		make -C $$TARGET run_tests; \
	done;

clean:
	for TARGET in $(TARGETS); do \
		make -C $$TARGET clean; \
	done;
