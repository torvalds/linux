Quick Design Document for 1-wire bus

In new bus terms, 1-wire devices are attached to 1-wire buses (ow)
which are attached to a one wire bridge (owc).

The implementation follows the terminology used in the Maxim AN927
Application note which defines the 1-wire bus as implemented for the
iButton product. This is considered to be the canonical definition of
the 1-wire bus. This means that the 1-wire bridge will implement the
owll(9) interface. ow is one wire. ll is for Link Level to mirror the ISO
stack terminology used by AN927. The 1-wire bus is implemented in the ow(4)
device, which implements the own(9) interface (n for network, the layer
described in the AN927). The presentation layer and above is the
responsibility of the client device drivers to implement.

Client drivers may only call the own(9) interface. The ow(4) driver
calls the owll(9) interface and implements the own(9).

$FreeBSD$
