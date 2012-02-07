OUTPUT=./
ifeq ("$(origin O)", "command line")
	OUTPUT := $(O)/
endif


default: all

$(OUTPUT)centrino-decode: centrino-decode.c
	$(CC) $(CFLAGS) -o $@ centrino-decode.c

$(OUTPUT)dump_psb: dump_psb.c
	$(CC) $(CFLAGS) -o $@ dump_psb.c

$(OUTPUT)intel_gsic: intel_gsic.c
	$(CC) $(CFLAGS) -o $@ -llrmi intel_gsic.c

$(OUTPUT)powernow-k8-decode: powernow-k8-decode.c
	$(CC) $(CFLAGS) -o $@ powernow-k8-decode.c

all: $(OUTPUT)centrino-decode $(OUTPUT)dump_psb $(OUTPUT)intel_gsic $(OUTPUT)powernow-k8-decode

clean:
	rm -rf $(OUTPUT){centrino-decode,dump_psb,intel_gsic,powernow-k8-decode}

.PHONY: all default clean
