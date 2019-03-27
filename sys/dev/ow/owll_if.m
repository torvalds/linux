#-
# Copyright (c) 2015 M. Warner Losh <imp@freebsd.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#include <sys/bus.h>
#include <dev/ow/owll.h>

INTERFACE owll;

#
# Dallas Semiconductor 1-Wire bus Link Layer (owll)
#
# See Maxim Application Note AN937: Book of iButton Standards for the
# 1-Wire protocol specification.
# http://pdfserv.maximintegrated.com/en/an/AN937.pdf
#
# Note: 1-Wire is a registered trademark of Maxim Integrated Products, Inc.
#
# This file provides an interface to the logical layer of the protocol.
# Although the first implementation is done with GPIO bit banging, some
# SoCs have a 1-Wire controller with more smarts or hardware offload.
# Maxim datasheets also describe how to use UARTs to generate timing,
# as well as both usb and i2c 1-Wire controllers.
#
# Chapter 4 has all the electrical timing diagrams that make up the link
# layer of this protocol.
#
# Two speed classes are defined: Regular speed and Overdrive speed.
# It is the responsibility of a device implementing the owll(9) interface
# to ensure that the timings are met:
#
# 	Regular				Overdrive
#
#	60us <= tSLOT < 120us		6us <= tSLOT <= 16us
#	60us <= tLOW0 < tSLOT < 120us	6us <= tLOW0 < tSLOT < 16us
#	1us <= tLOW1 < 15us		1us <= tLOW < 2us
#	1us < tLOWR < 15us		1us <= tLOWR < 2us
#	0 <= tRELEASE < 45us		0 <= tRELEASE < 4us
#	1us <= tREC < inf		1us <= tREC < inf
#	tRDV = 15us			tRDV = 2us
#	480us <= tRSTL < inf		48us <= tRSTL < 80us
#	480us <= tRSTH < inf		48us <= tRSTH < inf
#	15us < tPDH < 60us		2us <= tPDH < 6us
#	60us < tPDL < 240us		8us <= tPDL < 24us
#
# In the diagrams below, R is driven by the resistor pullup, M is driven by
# the master, and S is driven by the slave / target.
#
# All of these methods are expected to be called from the "network"/bus layer
# for doing its operations. See 1wn_if.m for those.
#
# Note: This is the polling / busy-wait interface. An interrupt-based interface
# may be different. But an interrupt-based, non-blocking interface can be tricky.
#
# Only the owbus should talk to this interface.
#

# WRITE-ONE (see above for timings) From Figure 4-1 AN-937
#
#		       |<---------tSLOT---->|<-tREC->|
#	High	RRRRM  | 	RRRRRRRRRRRR|RRRRRRRRM
#		     M |       R |     |  |	      M
#		      M|      R	 |     |  |	       M
#	Low	       MMMMMMM	 |     |  |    	        MMMMMM...
#      	       	       |<-tLOW1->|     |  |
#    		       |<------15us--->|  |
#                      |<--------60us---->|
#
#
METHOD int write_one {
	device_t	lldev;		/* Link Level device (eg bridge) */
	struct ow_timing *timing;	/* timing values */
};


# WRITE-ZERO (see above for timings) From Figure 4-2 AN-937
#
#		       |<---------tSLOT------>|<-tREC->|
#	High	RRRRM  | 	            | |RRRRRRRM
#		     M |                    | R	       M
#		      M|       	 |     |    |R 	        M
#	Low	       MMMMMMMMMMMMMMMMMMMMMR  	         MMMMMM...
#      	       	       |<--15us->|     |    |
#      	       	       |<------60us--->|    |
#                      |<-------tLOW0------>|
#
#
METHOD int write_zero {
	device_t	lldev;		/* Link Level device (eg bridge) */
	struct ow_timing *timing;	/* timing values */
};

# READ-DATA (see above for timings) From Figure 4-3 AN-937
#
#		       |<---------tSLOT------>|<-tREC->|
#	High	RRRRM  |        rrrrrrrrrrrrrrrRRRRRRRM
#		     M |       r            | R	       M
#		      M|      r	        |   |R 	        M
#	Low	       MMMMMMMSSSSSSSSSSSSSSR  	         MMMMMM...
#      	       	       |<tLOWR>< sample	>   |
#      	       	       |<------tRDV---->|   |
#                                     ->|   |<-tRELEASE
#
# r -- allowed to pull high via the resistor when slave writes a 1-bit
#
METHOD int read_data {
	device_t	lldev;		/* Link Level device (eg bridge) */
	struct ow_timing *timing;	/* timing values */
	int		*bit;		/* Bit we sampled */
};

# RESET AND PRESENCE PULSE (see above for timings) From Figure 4-4 AN-937
#
#				    |<---------tRSTH------------>|
#	High RRRM  |		  | RRRRRRRS	       |  RRRR RRM
#		 M |		  |R|  	   |S  	       | R	  M
#		  M|		  R |	   | S	       |R	   M
#	Low	   MMMMMMMM MMMMMM| |	   |  SSSSSSSSSS	    MMMMMM
#      	       	   |<----tRSTL--->| |  	   |<-tPDL---->|
#		   |   	       	->| |<-tR  |	       |
#				    |<tPDH>|
#
# Note: for Regular Speed operations, tRSTL + tR should be less than 960us to
# avoid interfering with other devives on the bus.
#
# Returns errors associating with acquiring the bus, or EIO to indicate
# that the bus was low during the RRRR time where it should have been
# pulled high. The present field is always updated, even on error.
#
METHOD int reset_and_presence {
	device_t	lldev;		/* Link level device (eg bridge) */ 
	struct ow_timing *timing;	/* timing values */
	int		*present;	/* 0 = slave 1 = no slave -1 = bus error */
};
