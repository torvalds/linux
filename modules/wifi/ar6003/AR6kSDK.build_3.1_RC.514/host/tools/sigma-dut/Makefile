all: sigma_dut

ifndef CC
CC=gcc
endif

ifndef LDO
LDO=$(CC)
endif

ifndef CFLAGS
CFLAGS = -MMD -O2 -Wall -g
endif

OBJS=sigma_dut.o
OBJS += wpa_ctrl.o
OBJS += wpa_helpers.o

OBJS += cmds_reg.o
OBJS += basic.o
OBJS += sta.o
OBJS += traffic.o
OBJS += p2p.o
OBJS += ap.o
OBJS += powerswitch.o
OBJS += atheros.o

ifndef NO_TRAFFIC_AGENT
CFLAGS += -DCONFIG_TRAFFIC_AGENT
OBJS += traffic_agent.o
LIBS += -lpthread
endif

ifndef NO_WLANTEST
CFLAGS += -DCONFIG_WLANTEST
OBJS += wlantest.o
endif

sigma_dut: $(OBJS)
	$(LDO) $(LDFLAGS) -o sigma_dut $(OBJS) $(LIBS)

clean:
	rm -f core *~ *.o *.d sigma_dut

-include $(OBJS:%.o=%.d)
