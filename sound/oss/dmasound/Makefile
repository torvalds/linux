#
# Makefile for the DMA sound driver
#

dmasound_pmac-y			+= dmasound_awacs.o \
				   trans_16.o dac3550a.o tas_common.o \
				   tas3001c.o tas3001c_tables.o \
				   tas3004.o tas3004_tables.o

obj-$(CONFIG_DMASOUND_ATARI)	+= dmasound_core.o dmasound_atari.o
obj-$(CONFIG_DMASOUND_PMAC)	+= dmasound_core.o dmasound_pmac.o
obj-$(CONFIG_DMASOUND_PAULA)	+= dmasound_core.o dmasound_paula.o
obj-$(CONFIG_DMASOUND_Q40)	+= dmasound_core.o dmasound_q40.o
