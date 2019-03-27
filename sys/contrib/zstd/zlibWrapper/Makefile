# Makefile for example of using zstd wrapper for zlib
#
# make - compiles examples
# make MOREFLAGS=-DZWRAP_USE_ZSTD=1 - compiles examples with zstd compression turned on
# make test - runs examples


# Paths to static and dynamic zlib and zstd libraries
# Use "make ZLIB_PATH=path/to/zlib ZLIB_LIBRARY=path/to/libz.a" to select a path to library
ZLIB_LIBRARY ?= -lz
ZLIB_PATH ?= .

ZSTDLIBDIR = ../lib
ZSTDLIBRARY = $(ZSTDLIBDIR)/libzstd.a
ZLIBWRAPPER_PATH = .
GZFILES = gzclose.o gzlib.o gzread.o gzwrite.o
EXAMPLE_PATH = examples
PROGRAMS_PATH = ../programs
TEST_FILE = ../doc/zstd_compression_format.md

CPPFLAGS = -DXXH_NAMESPACE=ZSTD_ -I$(ZLIB_PATH) -I$(PROGRAMS_PATH)       \
           -I$(ZSTDLIBDIR) -I$(ZSTDLIBDIR)/common -I$(ZLIBWRAPPER_PATH)
CFLAGS  ?= $(MOREFLAGS) -O3 -std=gnu99
CFLAGS  += -Wall -Wextra -Wcast-qual -Wcast-align -Wshadow -Wswitch-enum \
           -Wdeclaration-after-statement -Wstrict-prototypes -Wundef     \
           -Wstrict-aliasing=1


# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
else
EXT =
endif


all: fitblk example zwrapbench minigzip

test: example fitblk example_zstd fitblk_zstd zwrapbench minigzip minigzip_zstd
	./example
	./example_zstd
	./fitblk 10240 <$(TEST_FILE)
	./fitblk 40960 <$(TEST_FILE)
	./fitblk_zstd 10240 <$(TEST_FILE)
	./fitblk_zstd 40960 <$(TEST_FILE)
	@echo ---- minigzip start ----
	./minigzip_zstd example$(EXT)
	#cp example$(EXT).gz example$(EXT)_zstd.gz
	./minigzip_zstd -d example$(EXT).gz
	./minigzip example$(EXT)
	#cp example$(EXT).gz example$(EXT)_gz.gz
	./minigzip_zstd -d example$(EXT).gz
	@echo ---- minigzip end ----
	./zwrapbench -qi1b3B1K $(TEST_FILE)
	./zwrapbench -rqi1b1e5 ../lib ../programs ../tests

#valgrindTest: ZSTDLIBRARY = $(ZSTDLIBDIR)/libzstd.so
valgrindTest: VALGRIND = LD_LIBRARY_PATH=$(ZSTDLIBDIR) valgrind --track-origins=yes --leak-check=full --error-exitcode=1
valgrindTest: clean example fitblk example_zstd fitblk_zstd zwrapbench
	@echo "\n ---- valgrind tests ----"
	$(VALGRIND) ./example
	$(VALGRIND) ./example_zstd
	$(VALGRIND) ./fitblk 10240 <$(TEST_FILE)
	$(VALGRIND) ./fitblk 40960 <$(TEST_FILE)
	$(VALGRIND) ./fitblk_zstd 10240 <$(TEST_FILE)
	$(VALGRIND) ./fitblk_zstd 40960 <$(TEST_FILE)
	$(VALGRIND) ./zwrapbench -qi1b3B1K $(TEST_FILE)
	$(VALGRIND) ./zwrapbench -rqi1b1e5 ../lib ../programs ../tests

#.c.o:
#	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

minigzip: $(EXAMPLE_PATH)/minigzip.o $(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.o $(GZFILES) $(ZSTDLIBRARY)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(ZSTDLIBRARY) $(ZLIB_LIBRARY) -o $@

minigzip_zstd: $(EXAMPLE_PATH)/minigzip.o $(ZLIBWRAPPER_PATH)/zstdTurnedOn_zlibwrapper.o $(GZFILES) $(ZSTDLIBRARY)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(ZSTDLIBRARY) $(ZLIB_LIBRARY) -o $@

example: $(EXAMPLE_PATH)/example.o $(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.o $(GZFILES) $(ZSTDLIBRARY)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(ZLIB_LIBRARY) -o $@

example_zstd: $(EXAMPLE_PATH)/example.o $(ZLIBWRAPPER_PATH)/zstdTurnedOn_zlibwrapper.o $(GZFILES) $(ZSTDLIBRARY)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(ZLIB_LIBRARY) -o $@

fitblk: $(EXAMPLE_PATH)/fitblk.o $(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.o $(ZSTDLIBRARY)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(ZLIB_LIBRARY) -o $@

fitblk_zstd: $(EXAMPLE_PATH)/fitblk.o $(ZLIBWRAPPER_PATH)/zstdTurnedOn_zlibwrapper.o $(ZSTDLIBRARY)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(ZLIB_LIBRARY) -o $@

zwrapbench: $(EXAMPLE_PATH)/zwrapbench.o $(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.o $(PROGRAMS_PATH)/util.o $(PROGRAMS_PATH)/datagen.o $(ZSTDLIBRARY)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(ZLIB_LIBRARY) -o $@


$(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.o: $(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.c $(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -I. -c -o $@ $(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.c

$(ZLIBWRAPPER_PATH)/zstdTurnedOn_zlibwrapper.o: $(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.c $(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DZWRAP_USE_ZSTD=1 -I. -c -o $@ $(ZLIBWRAPPER_PATH)/zstd_zlibwrapper.c

$(ZSTDLIBDIR)/libzstd.a:
	$(MAKE) -C $(ZSTDLIBDIR) libzstd.a

$(ZSTDLIBDIR)/libzstd.so:
	$(MAKE) -C $(ZSTDLIBDIR) libzstd


clean:
	-$(RM) $(ZLIBWRAPPER_PATH)/*.o $(EXAMPLE_PATH)/*.o *.o foo.gz example$(EXT) example_zstd$(EXT) fitblk$(EXT) fitblk_zstd$(EXT) zwrapbench$(EXT) minigzip$(EXT) minigzip_zstd$(EXT)
	@echo Cleaning completed
