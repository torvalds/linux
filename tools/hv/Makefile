# Makefile for Hyper-V tools

CC = $(CROSS_COMPILE)gcc
PTHREAD_LIBS = -lpthread
WARNINGS = -Wall -Wextra
CFLAGS = $(WARNINGS) -g $(PTHREAD_LIBS)

all: hv_kvp_daemon hv_vss_daemon hv_fcopy_daemon
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) hv_kvp_daemon hv_vss_daemon hv_fcopy_daemon
