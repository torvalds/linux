TARGETS = breakpoints

all:
	for TARGET in $(TARGETS); do \
		make -C $$TARGET; \
	done;

run_tests:
	for TARGET in $(TARGETS); do \
		make -C $$TARGET run_tests; \
	done;

clean:
	for TARGET in $(TARGETS); do \
		make -C $$TARGET clean; \
	done;
