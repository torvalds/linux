#
# Makefile for mfgapi_test.cpp
# $Id: Makefile,v 1.6 2009/10/02 21:38:14 Exp $
#
#	There are total five options for make:
#
#		make		: This is to make mfgapi_test.exe, wlmSampleTests.exe, and wlm.lib
#		make exe	: This is to make mfgapi_test.exe and wlmSampleTests.exe
#		make lib	: This is to make wlm.lib only
#		make clean	: This is to remove mfgtest_api.exe and wlmSampleTests.exe
#		make cleanall	: This is to remove mfgtest_api.exe, wlmSampleTests.exe, wlm.lib and all of its object files
#
#

UNAME = $(shell uname)

SRCBASE = ../..
WLM_DIR = wl/exe

ifeq ($(findstring CYGWIN,$(UNAME)),CYGWIN)
TARGET_TEST = mfgapi_test.exe
TARGET_SAMPLE = wlmSampleTests.exe
WLM_LIB = wlm.lib
WLM_DLL = wlm.dll
REL_WLM_DIR = $(WLM_DIR)/obj/wlm/free
WLM_MAKE = GNUmakefile.wlm_dll
C_DEFINES += -mno-cygwin -DWIN32
else
TARGET_TEST = mfgapi_test
TARGET_SAMPLE = wlmSampleTests
WLM_LIB = wlm.so
REL_WLM_DIR = $(WLM_DIR)/wlm/x86
WLM_MAKE = GNUmakefile
C_DEFINES += -DLINUX
endif

SOURCE = mfgapi_test.cpp
SOURCE_SAMPLE = wlmSampleTests.c

vpath %.h $(SRCBASE)/$(WLM_DIR)

CC = gcc
CXX = g++

C_DEFINES += -Wall -Wextra
ANSI_C90 = -ansi -pedantic

INCLUDES = $(SRCBASE)/$(WLM_DIR) $(SRCBASE)/include
CPPFLAGS = $(patsubst %,-I%,$(subst ;, ,$(INCLUDES)))

all: lib $(TARGET_SAMPLE)

test: lib $(TARGET_TEST)

lib: $(SRCBASE)/$(REL_WLM_DIR)/$(WLM_LIB)

$(TARGET_TEST): $(SOURCE) wlm.h
	@echo "Compiling - $(TARGET_TEST)"
	$(CXX) $(C_DEFINES) $(CPPFLAGS) $(SOURCE) $(SRCBASE)/$(REL_WLM_DIR)/$(WLM_LIB) -o $(TARGET_TEST)

$(SRCBASE)/$(REL_WLM_DIR)/$(WLM_LIB):
ifeq ($(findstring CYGWIN,$(UNAME)),CYGWIN)
	$(MAKE) -C $(SRCBASE)/$(WLM_DIR) OTHER_SOURCES="" debug
	$(MAKE) -C $(SRCBASE)/$(WLM_DIR) -f $(WLM_MAKE)
	cp -f $(SRCBASE)/$(REL_WLM_DIR)/$(WLM_DLL) .
else
	$(MAKE) -C $(SRCBASE)/$(WLM_DIR) -f $(WLM_MAKE) wlm
	cp -f $(SRCBASE)/$(REL_WLM_DIR)/$(WLM_LIB) .
endif

$(TARGET_SAMPLE): $(SOURCE_SAMPLE) wlm.h
	@echo "Compiling - $(TARGET_SAMPLE)"
	$(CC) $(C_DEFINES) $(CPPFLAGS) $(ANSI_C90) $(SOURCE_SAMPLE) $(SRCBASE)/$(REL_WLM_DIR)/$(WLM_LIB) -o $(TARGET_SAMPLE)

clean:
	rm -f $(TARGET_TEST) $(TARGET_SAMPLE)

cleanall:
	rm -f $(TARGET_TEST) $(TARGET_SAMPLE) $(WLM_LIB)
ifeq ($(findstring CYGWIN,$(UNAME)),CYGWIN)
	rm -rf $(SRCBASE)/$(REL_WLM_DIR)
else
	rm -f $(SRCBASE)/$(REL_WLM_DIR)/*.o $(SRCBASE)/$(REL_WLM_DIR)/$(WLM_LIB)
endif

.PHONY : all exe lib clean cleanall
