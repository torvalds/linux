default: all

centrino-decode: ../i386/centrino-decode.c
	$(CC) $(CFLAGS) -o $@ $<

powernow-k8-decode: ../i386/powernow-k8-decode.c
	$(CC) $(CFLAGS) -o $@ $<

all: centrino-decode powernow-k8-decode

clean:
	rm -rf centrino-decode powernow-k8-decode

.PHONY: all default clean
