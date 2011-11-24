Atheros SPI protocol hardware driver sample.

Copyright 2006-2007, Atheros Communications Inc.
All rights reserved.


The SPI host driver uses the Atheros SDIO stack to reuse some key features:
  - host controller and bus abstraction model
  - bus I/O request model (sync/async I/O, host configurations)
  - interrupt processing model

The Atheros SPI host driver uses a common layer (src\hcd\athspi_common) and a hardware layer
to form the host controller driver binary. These two layers are tightly coupled using a common device
structure and a set of fixed functions (no callback pointers are employed). 


        -----------------------------
        | AR6K WLAN Function Driver |
        -----------------------------
                    ^
                    |
                    v
        -----------------------------
        | SDIO bus driver (RAW mode)| 
        -----------------------------          
                    ^
                    |
                    v
        -----------------------------
        |    ATH-SPI Common Layer   |
        -----------------------------
        |    ATH-SPI HW layer       |
        -----------------------------
        
The SPI common layer handles the AR6K SPI protocol specifics and provides a interface the WLAN
function            


