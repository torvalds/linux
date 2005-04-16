###########################################################################
# Makefile for the Linux 802.2 LLC (fully-functional) layer.
#
# Copyright (c) 1997 by Procom Technology,Inc.
#		2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
#
# This program can be redistributed or modified under the terms of the 
# GNU General Public License as published by the Free Software Foundation.
# This program is distributed without any warranty or implied warranty
# of merchantability or fitness for a particular purpose.
#
# See the GNU General Public License for more details.
###########################################################################

obj-$(CONFIG_LLC) += llc.o

llc-y := llc_core.o llc_input.o llc_output.o

obj-$(CONFIG_LLC2) += llc2.o

llc2-y := llc_if.o llc_c_ev.o llc_c_ac.o llc_conn.o llc_c_st.o llc_pdu.o \
	  llc_sap.o llc_s_ac.o llc_s_ev.o llc_s_st.o af_llc.o llc_station.o

llc2-$(CONFIG_PROC_FS) += llc_proc.o
