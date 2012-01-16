TARGETS = breakpoints

all:
	for TARGET in $(TARGETS); do \
		make -C $$TARGET; \
	done;

clean:
	for TARGET in $(TARGETS); do \
		make -C $$TARGET clean; \
	done;
