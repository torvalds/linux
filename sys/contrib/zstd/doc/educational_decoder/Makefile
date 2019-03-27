HARNESS_FILES=*.c

MULTITHREAD_LDFLAGS = -pthread
DEBUGFLAGS= -g -DZSTD_DEBUG=1
CPPFLAGS += -I$(ZSTDDIR) -I$(ZSTDDIR)/common -I$(ZSTDDIR)/compress \
            -I$(ZSTDDIR)/dictBuilder -I$(ZSTDDIR)/deprecated -I$(PRGDIR)
CFLAGS   ?= -O3
CFLAGS   += -Wall -Wextra -Wcast-qual -Wcast-align -Wshadow                 \
            -Wstrict-aliasing=1 -Wswitch-enum -Wdeclaration-after-statement \
            -Wstrict-prototypes -Wundef -Wformat-security                   \
            -Wvla -Wformat=2 -Winit-self -Wfloat-equal -Wwrite-strings      \
            -Wredundant-decls
CFLAGS   += $(DEBUGFLAGS)
CFLAGS   += $(MOREFLAGS)
FLAGS     = $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(MULTITHREAD_LDFLAGS)

harness: $(HARNESS_FILES)
	$(CC) $(FLAGS) $^ -o $@

clean:
	@$(RM) -f harness
	@$(RM) -rf harness.dSYM

test: harness
	@zstd README.md -o tmp.zst
	@./harness tmp.zst tmp
	@diff -s tmp README.md
	@$(RM) -f tmp*
	@zstd --train harness.c zstd_decompress.c zstd_decompress.h README.md
	@zstd -D dictionary README.md -o tmp.zst
	@./harness tmp.zst tmp dictionary
	@diff -s tmp README.md
	@$(RM) -f tmp* dictionary
	@make clean
