#-
# Copyright (c) 2003 Marcel Moolenaar
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>

# The UART hardware interface. The core UART code is hardware independent.
# The details of the hardware are abstracted by the UART hardware interface.

INTERFACE uart;

# attach() - attach hardware.
# This method is called when the device is being attached. All resources
# have been allocated. The transmit and receive buffers exist, but no
# high-level (ie tty) initialization has been done yet.
# The intend of this method is to setup the hardware for normal operation.
METHOD int attach {
	struct uart_softc *this;
};

# detach() - detach hardware.
# This method is called when a device is being detached from its bus. It
# is the first action performed, so even the high-level (ie tty) interface
# is still operational.
# The intend of this method is to disable the hardware.
METHOD int detach {
	struct uart_softc *this;
};

# flush() - flush FIFOs.
# This method is called to flush the transmitter and/or the receiver as
# specified by the what argument. Characters are expected to be lost.
METHOD int flush {
	struct uart_softc *this;
	int	what;
};

# getsig() - get line and modem signals.
# This method retrieves the DTE and DCE signals and their corresponding
# delta bits. The delta bits include those corresponding to DTE signals
# when they were changed by a call to setsig. The delta bits maintained
# by the hardware driver are cleared as a side-effect. A second call to
# this function will not have any delta bits set, unless there was a
# change in the signals in the mean time.
METHOD int getsig {
	struct uart_softc *this;
};

# ioctl() - get or set miscellaneous parameters.
# This method is the bitbucket method. It can (and will) be used when there's
# something we need to set or get for which a new method is overkill. It's
# used for example to set HW or SW flow-control.
METHOD int ioctl {
	struct uart_softc *this;
	int request;
	intptr_t data;
};

# ipend() - query UART for pending interrupts.
# When an interrupt is signalled, the handler will call this method to find
# out which of the interrupt sources needs attention. The handler will use
# this information to dispatch service routines that deal with each of the
# interrupt sources. An advantage of this approach is that it allows multi-
# port drivers (like puc(4)) to query multiple devices concurrently and
# service them on an interrupt priority basis. If the hardware cannot provide
# the information reliably, it is free to service the interrupt and return 0,
# meaning that no attention is required.
METHOD int ipend {
	struct uart_softc *this;
}

# param() - set communication parameters.
# This method is called to change the communication parameters.
METHOD int param {
	struct uart_softc *this;
	int	baudrate;
	int	databits;
	int	stopbits;
	int	parity;
};

# probe() - detect hardware.
# This method is called as part of the bus probe to make sure the
# hardware exists. This function should also set the device description
# to something that represents the hardware.
METHOD int probe {
	struct uart_softc *this;
};

# receive() - move data from the receive FIFO to the receive buffer.
# This method is called to move received data to the receive buffer and
# additionally should make sure the receive interrupt should be cleared.
METHOD int receive {
	struct uart_softc *this;
};

# setsig() - set line and modem signals.
# This method allows changing DTE signals. The DTE delta bits indicate which
# signals are to be changed and the DTE bits themselves indicate whether to
# set or clear the signals. A subsequent call to getsig will return with the
# DTE delta bits set of those DTE signals that did change by this method.
METHOD int setsig {
	struct uart_softc *this;
	int	sig;
};

# transmit() - move data from the transmit buffer to the transmit FIFO.
# This method is responsible for writing the Tx buffer to the UART and
# additionally should make sure that a transmit interrupt is generated
# when transmission is complete.
METHOD int transmit {
	struct uart_softc *this;
};

# grab() - Up call from the console to the upper layers of the driver when
# the kernel asks to grab the console. This is valid only for console
# drivers. This method is responsible for transitioning the hardware
# from an interrupt driven state to a polled state that works with the
# low-level console interface defined for this device. The kernel
# currently only calls this when it wants to grab input from the
# console. Output can still happen asyncrhonously to these calls.
METHOD void grab {
	struct uart_softc *this;
};

# ungrab() - Undoes the effects of grab().
METHOD void ungrab {
	struct uart_softc *this;
};
