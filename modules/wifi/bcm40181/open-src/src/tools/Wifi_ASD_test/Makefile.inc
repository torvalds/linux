ifndef  SRCBASE
        SRCBASE = ../..
endif

#include ../../Makerules

LIB=lib
CA=ca
CLI=cli
UCC=ucc
MAKE=make

CFLAGS = -g -O2 -D_REENTRANT -DWFA_WMM_EXT -DWFA_WMM_AC -DWFA_WMM_VOICE  -DWFA_WMM_PS_EXT -Wall -I../inc -I$(SRCBASE)/include -I$(SRCBASE)/wl/exe
CFLAGS += $(GCFLAGS)

ifeq ($(TARGETENV), android)
CFLAGS += -DTARGETENV_android
endif

CALIBS = ../lib/$(TARGETARCH)/libwfa_ca.a -lpthread 
RANLIB = ranlib

LIBWFA_NAME_CA =./$(TARGETARCH)/libwfa_ca.a
LIBWFA_NAME =./$(TARGETARCH)/libwfa.a


LIB_OBJS = ./$(TARGETARCH)/wfa_sock.o ./$(TARGETARCH)/wfa_tg.o ./$(TARGETARCH)/wfa_miscs_linux.o ./$(TARGETARCH)/wfa_cs.o ./$(TARGETARCH)/wfa_ca_resp.o ./$(TARGETARCH)/wfa_tlv.o ./$(TARGETARCH)/wfa_cmdtbl.o ./$(TARGETARCH)/wfa_cmdproc.o ./$(TARGETARCH)/wfa_miscs.o ./$(TARGETARCH)/wfa_thr.o ./$(TARGETARCH)/wfa_wmmps.o ./$(TARGETARCH)/wfa_wmmps_linux.o ../dut/$(TARGETARCH)/wfa_dut.o ../dut/$(TARGETARCH)/wfa_dut_init.o ./$(TARGETARCH)/wfa_typestr.o 


LIB_OBJS_CA = wfa_sock.o wfa_tlv.o wfa_ca.o wfa_ca_resp.o wfa_cmdproc.o wfa_miscs.o wfa_miscs_linux.o wfa_cmdtbl.o wfa_thr.o wfa_wmmps.o wfa_wmmps_linux.o wfa_typestr.o 


CLEANFILES = core core.* *.core.* *.o *.a
