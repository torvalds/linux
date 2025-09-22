#----------------------------------------------------------------------
# Fill in the source files to build
#----------------------------------------------------------------------
# Uncomment line below for debugging shell commands
# SHELL = /bin/sh -x

#----------------------------------------------------------------------
# Change any build/tool options needed
#----------------------------------------------------------------------
ARCH ?= x86_64
CFLAGS ?=-arch $(ARCH) -gdwarf-2 -O0
CXX ?= $(shell xcrun -find clang++)
EXE ?= libheap.dylib
DSYM ?= $(EXE).dSYM

#----------------------------------------------------------------------
# Compile the executable from all the objects (default rule) with no
# dsym file.
#----------------------------------------------------------------------
$(EXE) : heap_find.cpp
	$(CXX) $(CFLAGS) -install_name "@executable_path/libheap.dylib" -dynamiclib -lobjc heap_find.cpp -o "$(EXE)"

#----------------------------------------------------------------------
# Include all of the makefiles for each source file so we don't have
# to manually track all of the prerequisites for each source file.
#----------------------------------------------------------------------
.PHONY: clean
all:	$(EXE)
clean:
	rm -rf "$(EXE)" "$(DSYM)"



