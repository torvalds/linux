
Atheros SPI Host Controller Driver for S3C6400 (SMDK6400) ReadMe

Copyright 2008, Atheros Communications

This driver supports the SMDK6400 SPI interface (SC36400 SPI controller) under the control of the
Atheros SPI stack and WLAN networking client driver.  This driver supports the SMDK6400 BSP for Windows CE
operating systems.

This driver can only be compiled within the build environment for SMDK6400-based projects.

The SPI interface (CLK, CS, MISO, MOSI) are provide through the SMDK's CON6 header.  This header provides
access to the SPI1 controller.  An additional GPIO pin is required to implement the SPI-Interrupt line 
(level-triggered input) from the Atheros WLAN card.  PIN4 of SW67 (push-button near the power button)
has been selected for this function.  This input is connected to interrupt group EINT10 in the SMDK6400 BSP.


In order to use SPI the following additioanl SMDK6400 board configuration must be made:

CFG6 - 1-Off, 2-Off
J3 - Jumper 2-3 

#define USE_SPI_ID 0 in ath_spi_hw_drv.c when you want to use SPI 0 instead of SPI 1 
#define SPI_CLOCK to select from EPLL, USB, PCLK source. 

Debug option (define)
    ENABLE_SPI_DEBUG will check if the tx/rx operation is done 
    ENABLE_MASTER_CS will enable the CS manually. 
    ENABLE_INT_MODE will use interrupt to wait for rx polling data
    HCD_EMULATE_DMA will use polling instead of DMA 

Default value is
	USE_SPI_ID 1 
	SPI_CLOCK  EPLL_CLOCK
  All debug options are disabled /* undefined */	

