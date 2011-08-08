default: all

centrino-decode: centrino-decode.c
	$(CC) $(CFLAGS) -o centrino-decode centrino-decode.c

powernow-k8-decode: powernow-k8-decode.c
	$(CC) $(CFLAGS) -o powernow-k8-decode powernow-k8-decode.c

all: centrino-decode powernow-k8-decode

clean:
	rm -rf centrino-decode powernow-k8-decode

.PHONY: all default clean
