OUTPUT=./
ifeq ("$(origin O)", "command line")
	OUTPUT := $(O)/
endif


default: all

$(OUTPUT)centrino-decode: ../i386/centrino-decode.c
	$(CC) $(CFLAGS) -o $@ $<

$(OUTPUT)powernow-k8-decode: ../i386/powernow-k8-decode.c
	$(CC) $(CFLAGS) -o $@ $<

all: $(OUTPUT)centrino-decode $(OUTPUT)powernow-k8-decode

clean:
	rm -rf $(OUTPUT)centrino-decode $(OUTPUT)powernow-k8-decode

.PHONY: all default clean
