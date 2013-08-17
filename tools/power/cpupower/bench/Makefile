OUTPUT := ./
ifeq ("$(origin O)", "command line")
ifneq ($(O),)
	OUTPUT := $(O)/
endif
endif

LIBS = -L../ -L$(OUTPUT) -lm -lcpupower

OBJS = $(OUTPUT)main.o $(OUTPUT)parse.o $(OUTPUT)system.o $(OUTPUT)benchmark.o
CFLAGS += -D_GNU_SOURCE -I../lib -DDEFAULT_CONFIG_FILE=\"$(confdir)/cpufreq-bench.conf\"

$(OUTPUT)%.o : %.c
	$(ECHO) "  CC      " $@
	$(QUIET) $(CC) -c $(CFLAGS) $< -o $@

$(OUTPUT)cpufreq-bench: $(OBJS)
	$(ECHO) "  CC      " $@
	$(QUIET) $(CC) -o $@ $(CFLAGS) $(OBJS) $(LIBS)

all: $(OUTPUT)cpufreq-bench

install:
	mkdir -p $(DESTDIR)/$(sbindir)
	mkdir -p $(DESTDIR)/$(bindir)
	mkdir -p $(DESTDIR)/$(docdir)
	mkdir -p $(DESTDIR)/$(confdir)
	install -m 755 $(OUTPUT)cpufreq-bench $(DESTDIR)/$(sbindir)/cpufreq-bench
	install -m 755 cpufreq-bench_plot.sh $(DESTDIR)/$(bindir)/cpufreq-bench_plot.sh
	install -m 644 README-BENCH $(DESTDIR)/$(docdir)/README-BENCH
	install -m 755 cpufreq-bench_script.sh $(DESTDIR)/$(docdir)/cpufreq-bench_script.sh
	install -m 644 example.cfg $(DESTDIR)/$(confdir)/cpufreq-bench.conf

clean:
	rm -f $(OUTPUT)*.o
	rm -f $(OUTPUT)cpufreq-bench
