OMAP2420 SPI Driver
Copyright 2007, Atheros Communications Inc.
All rights reserved.

OMAP SDIO-Raw driver implementing Atheros 2nd generation SPI protocol.

General Notes:
-  Operates with SDIO stack using the RAW Host Controller Driver (HCD) model.
-  Atheros SPI 2 Protocol.
-  Uses OMAP DMA controller for all DMA read/write operations that are properly aligned.
-  Atheros Module Interrupt detection (GPIO16 pin).
    
Linux Notes:
    
Module Parameters:

	"base_clock" = the base clock rate (in Hz) of the module system clock, default is 12Mhz.
	"op_clock" = the clock rate of the SPI interface, default is to run the SPI bus
	             clock at 12 Mhz
	"powerupdelay" = power up delay (in MS) before Atheros module is initialized (default is
	                 1000 milliseconds.
    "MaxBytesPerDMARequest" = max number of bytes allowed in a DMA transfer, default is 4K.
    "gpiodebug" = special GPIO pin assertion, do not use unless GPIO pins are properly
                  assigned for debug.              
    "debuglevel" =  set module debug level (default = 4).
    
    "spimodule" = sets which spi module block in the OMAP controller, use only 1. Defaults to 1
    "spichan"   = sets which channel in the spi module block to use, defaults to 2
    "d0swap"    = swap spi module D0 functionality with D1, default: D0 is RX and D1 is TX.
                  If set to 1, D0 becomes TX, D1 becomes RX.
    "int_gpio"  = gpio pin used for SPI interrupts, default is 98.
    "int_gpio_pad_conf_offset" = gpio pad configuration offset (see TRM) for SPI interrupt
                                 default is 272 (0x110) for pad N19.
    "int_gpio_pad_conf_byte" = gpio pad configuration byte number for SPI interrupt
                               default is offset 0 for GPIO98 on pad N19.
    "int_gpio_pad_mode_value" = gpio pin mode value. Default is 3, for configurating pad N19
                                as GPIO98
    "reset_spi_on_shutdown" = reset Atheros module spi interface on shutdown
    "dump_state"            = dump Atheros module spi state
    
    Module Debug Level:      Description of Kernel Prints (each level is cummalative):                  
  		8					     SPI module interrupts 
        9                        SPI request processing  
       10                        SPI request with DATA
       11                        SPI request DATA dumps
       12                        OMAP-SPI DMA info
       13                        OMAP-SPI transactions (very verbose).


          

