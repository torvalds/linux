Atheros SPI sample hcd driver readme.txt

Copyright 2008, Atheros Communications

Introduction:
=============

This sample driver can be used as a template to support new SPI hardware and platforms.  The spi
bus driver and host controller are built as a single DLL (SPI host driver) comprising of the
following layered components:

        -----------------------------
        |  Serial Bus core library  |   ----
        -----------------------------       |
        |  ATH-SPI Protocol library |       |--------  athspi_sample.dll
        -----------------------------       |
        |    SPI Hardware library   |   ----
        -----------------------------

Serial Bus Core:
----------------
This contains the core I/O APIs to issue/process SPI bus requests, handle insertion/removal
events and handle IRQ events.  The SPI client driver interfaces to the SPI host driver at this layer.
This allows the SPI client driver to remain platform independent.

ATH-SPI Protocol:
-----------------
This contains the SPI hardware independent-code that implements the proprietary SPI command set
and data framing and flow control protocols.  This layer handles AR6002 bus-level sleep/awake protocols.
This layer insulates the SPI client driver from changes to the SPI protocol between revisions of
chips.

SPI Hardware:
-------------
This layer implements the SPI hardware control code which is unique for each platform and spi controller
hardware.  This layer insulates all upper layers from hardware specifics which maximizes code-reuse.

The SPI bus architecture currently supports only a single client driver.

The sample driver provides these features:

 - Basic skeleton for all hardware-specific APIs required by the ATH-SPI protocol layer.
 - Serial bus core initialization and tear down.
 - Stream device interface for loading and custom IOCTLs.
 - SPI protocol layer initialization and HCD registration.
 - Client driver loading/unloading (via bus core APIs).
 - Sample SPI IRQ IST thread and event creation/cleanup
 - Sample DMA handling code (w/ Comments)
 - Areas that require hardware specific coding are marked with "TODO" comments.

DMA:
----
 All bus requests flow into the SPI protocol layer from the client. The SPI protocol layer breaks
 up the request into primitive SPI tokens that are issued by the hardware layer directly onto the bus.
 The SPI protocol layer also prepares DMA transfers which can be implemented at the hardware layer to
 accelerate data transfers.  Non-DMA capable SPI host controllers can implement "emulated" DMA
 functions as long as the hardware specific layer performs the proper DMA completion indications.

SPI IRQ:
--------
 The SPI IRQ must be a level-sensitive interrupt input to the host platform (typically a GPIO line).
 Thie interrupt is detected at the hardware layer and indicated to the SPI protocol layer.  The SPI
 protocol layer processes IRQs requiring no action in part from the hardware later;  The SPI protocol
 layer will enable/disable this interrupt signal during IRQ processing.

Load Model:
===========
The SPI host driver loads as a stream-interface device.  The stream device can be loaded as a
built-in device or loaded manually.  The build-in device is the most common and convenient loading
methods.  The hardware layer automatically instructs the bus core to load any clients which are
also loaded as a stream device.

Debugging Aids:
===============

The SPI common layer implements many debug prints using debug levels.  The default debug level is 7.
The level is set in the ath_spi_hw_drv.c sample file (see DBG_DECLARE macro). This can be increased
to debug every SPI transaction. The driver must be compiled as DEBUG to view prints.

The stack code provides a convenient debug tool that can unload any streams driver and subsequently
reload that driver.  This is useful for shutdown the spi host driver and AR6K client driver without
rebooting.  This can be used to invoke cleanup code and check for memory leaks. The tool is called
cedeviceload.exe.

To unload the SPI sample host driver (if it was already loaded):

   Windows CE> s cedeviceloader -x Drivers\BuiltIn\ATHSPI_SAMPLE

To load the SPI sample host driver (if it was unloaded)

   Windows CE> s cedeviceloader -l Drivers\BuiltIn\ATHSPI_SAMPLE

All that is required is the streams driver registry path.


