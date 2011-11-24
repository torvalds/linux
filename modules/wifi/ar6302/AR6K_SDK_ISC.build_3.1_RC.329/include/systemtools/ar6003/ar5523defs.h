
// set a one in the bit position "s"
#define _SET_ONE_(s)            (1<<(s))
//
// // create a string of "w" ones, starting at bit position "s"
// // will not support a width of 0 (but then why would you want a width of
// 0?)


#define WMAC0_INTERRUPT_A      0
#define AR5523_WMAC0_BASE_ADDRESS           0xa0900000 
#define AR5523_USB_BASE_ADDRESS             0xa0A00000  
#define AR5523_UART0_BASE_ADDRESS           0xa0B00000
#define AR5523_GPIO_BASE_ADDRESS            0xa0B10000
#define AR5523_APB_DMA_BASE_ADDRESS         0xa0B20000
#define AR5523_FLASH_CONTROL_BASE_ADDRESS   0xa0B30000


#define EEPROM_CONFIG_REG   (AR5523_FLASH_CONTROL_BASE_ADDRESS + 0x01C)
#define SPI_SELECT_OFFSET      _SET_ONE_(0)
#define I2C     0
#define SPI		1
#define AR5523_CONFIG_BASE_ADDRESS          0xa0C00000
#define AR5523_RESET              (AR5523_CONFIG_BASE_ADDRESS + 0x20)
#define RESET_FL_CNTL	0x80000
#define RESET_GPIO		0x40000
#define RESET_UART		0x20000
#define RESET_APB_DMA	0x10000
#define RESET_USB		0x1000
#define RESET_WLAN_CAL	0x200
#define RESET_WLAN		0x100
#define RESET_NMI		0x40
#define RESET_PROC_WARM	0x20
#define RESET_PROC_COLD	0x10
#define RESET_SYSTEM		0x1

#define AR5523_CLK_CONTROL            (AR5523_CONFIG_BASE_ADDRESS + 0x60)

#define AR5523_INTERFACE              (AR5523_CONFIG_BASE_ADDRESS + 0x90)
#define WLAN_ENABLE		0x1
#define USB_ENABLE		0x2
#define APB_ENABLE		0x3

#define AR5523_REV              (AR5523_CONFIG_BASE_ADDRESS + 0xA0)
#define AR5523_FLASH_ADDRESS                0xbfc00000
#define EEPROM_CAL_OFFSET 0xf000
#define SPI_CAL_OFFSET 0x10000
#define AUTO_LOADER_CAL_OFFSET  0x40000
#define AUTO_LOADER_VERSION     2


#define AR5523_EMULATION_MAJOR_REV0	5
#define AR5523_EMULATION_MINOR_REV0	1  
#define AR5523_MAJOR_REV0	6
#define AR5523_MINOR_REV0	0  
#define AR5523_MINOR_REV1	1  

#define REV_MIN_W 4
#define REV_MAJ_W 4
#define REV_MIN_S 0
#define REV_MAJ_S 4
#define REV_MIN_M 0xf
#define REV_MAJ_M 0xf0

// EEPROM I2C interface
#define     EEPROM_ADDR      (AR5523_FLASH_CONTROL_BASE_ADDRESS + 0x0010)
#define     EEPROM_DATA  (AR5523_FLASH_CONTROL_BASE_ADDRESS + 0x0014)
#define     EEPROM_CMD   (AR5523_FLASH_CONTROL_BASE_ADDRESS + 0x0018)
#define     WR_CMD              _SET_ONE_(0)
#define     EEPROM_STS   (AR5523_FLASH_CONTROL_BASE_ADDRESS + 0x0018)
#define EEPROM_WRITE_ERROR   _SET_ONE_(6)
#define     EEPROM_CFG   (AR5523_FLASH_CONTROL_BASE_ADDRESS + 0x001c)
#define     MAC_EEPROM_WRITE_ACK    _SET_ONE_(14)   
#define     MAC_EEPROM_ADDR_HIT_TRACK   _SET_ONE_(15)   


//////////////////////////////////////////////////////////////////////////////
////// SPI address mapping
//////////////////////////////////////////////////////////////////////////////

#define SPI_CONTROL_STATUS         (AR5523_FLASH_CONTROL_BASE_ADDRESS + 0x0)

#define SPI_CS_TXBC_S              0
#define SPI_CS_TXBC_W              4
#define SPI_CS_TXBC_M              0xf
#define SPI_CS_RXBC_S              4
#define SPI_CS_RXBC_W              4
#define SPI_CS_RXBC_M              0xf0
#define SPI_CS_TRANS_START		   0x100
#define SPI_CS_BUSY_INDICATION	   0x10000
#define SPI_CS_SPI_ADDR_SIZE	   0x60000
#define SPI_CS_AUTO_SIZE_OVERRIDE  0x180000

#define SPI_ADDR_OP                (AR5523_FLASH_CONTROL_BASE_ADDRESS + 0x4)
#define SPI_ADDR_S				   8
#define SPI_DATA                   (AR5523_FLASH_CONTROL_BASE_ADDRESS + 0x8)


//////////////////////////////////////////////////////////////////////////////
//// SPI op code
////////////////////////////////////////////////////////////////////////////////

#define SPI_WRITE_SR_OP			0x01
#define SPI_SST_ENABLE_WRITE_SR_OP          0x50
#define SPI_WRITE_OP				0x02
#define SPI_WRITE_EN_OP			0x06

#define SPI_READ_OP				0x03
#define SPI_DISABLE_OP				0x04
#define SPI_READ_SR_OP				0x05
#define SPI_FAST_READ				0x0b

#define SPI_SECTOR_ERASE_OP		0xd8
#define SPI_BULK_ERASE_OP			0xc7

#define SPI_SST_SECTOR_ERASE_OP		0x52
#define SPI_SST_BULK_ERASE_OP		0x60

#define SPI_POWER_DOWN_OP			0xb9
#define SPI_RELEASE_POWER_DOWN_OP	0xab

#define SPI_DEVICE_ID_OP			0xab
#define SPI_SST_DEVICE_ID           0x49

#define NEXFLASH_STATUS_BUSY   0x1
#define NEXFLASH_WE_LATCH      0x2

#define NEX_FLASH_PAGE_SIZE		256

#define NEXFLASH_MFG_ID		0xef
#define NEXFLASH_P10_DEVICE_ID  0x10
#define NEXFLASH_P20_DEVICE_ID  0x11
#define NEXFLASH_P40_DEVICE_ID  0x12



