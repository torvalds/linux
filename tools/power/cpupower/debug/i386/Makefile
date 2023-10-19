# SPDX-License-Identifier: GPL-2.0
OUTPUT=./
ifeq ("$(origin O)", "command line")
	OUTPUT := $(O)/
endif

DESTDIR =
bindir  = /usr/bin

INSTALL = /usr/bin/install


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
	rm -rf $(OUTPUT)centrino-decode
	rm -rf $(OUTPUT)dump_psb
	rm -rf $(OUTPUT)intel_gsic
	rm -rf $(OUTPUT)powernow-k8-decode

install:
	$(INSTALL) -d $(DESTDIR)${bindir}
	$(INSTALL) $(OUTPUT)centrino-decode $(DESTDIR)${bindir}
	$(INSTALL) $(OUTPUT)powernow-k8-decode $(DESTDIR)${bindir}
	$(INSTALL) $(OUTPUT)dump_psb $(DESTDIR)${bindir}
	$(INSTALL) $(OUTPUT)intel_gsic $(DESTDIR)${bindir}

.PHONY: all default clean install
