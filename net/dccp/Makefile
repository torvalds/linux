obj-$(CONFIG_IP_DCCP) += dccp.o

dccp-y := ccid.o input.o ipv4.o minisocks.o options.o output.o proto.o \
	  timer.o

dccp-$(CONFIG_IP_DCCP_ACKVEC) += ackvec.o

obj-$(CONFIG_INET_DCCP_DIAG) += dccp_diag.o

dccp_diag-y := diag.o

obj-y += ccids/
