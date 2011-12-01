default: all

centrino-decode: centrino-decode.c
	$(CC) $(CFLAGS) -o centrino-decode centrino-decode.c

dump_psb: dump_psb.c
	$(CC) $(CFLAGS) -o dump_psb dump_psb.c

intel_gsic: intel_gsic.c
	$(CC) $(CFLAGS) -o intel_gsic -llrmi intel_gsic.c

powernow-k8-decode: powernow-k8-decode.c
	$(CC) $(CFLAGS) -o powernow-k8-decode powernow-k8-decode.c

all: centrino-decode dump_psb intel_gsic powernow-k8-decode

clean:
	rm -rf centrino-decode dump_psb intel_gsic powernow-k8-decode

.PHONY: all default clean
