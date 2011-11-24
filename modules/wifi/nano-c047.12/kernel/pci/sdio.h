// sdio.h
//
// SDIO specific definitions
// 
//
#ifndef __sdio_h__
#define __sdio_h__

typedef unsigned int UINT;
#define UWORD unsigned short int
#define SDIO_USE_SINGLE_BLOCK 0
#define SDIO_USE_MULTI_BLOCK 1
#define SDIO_PROTOCOL_HEADER_SIZE 6
#define JTAG_REG_OE_OFFSET                 36
#define JTAG_REG_OUT_OFFSET                32
#define JTAG_REG_IN_OFFSET                 32
#define JTAG_REG_LENGTH                     1

//Common DMA buffer offsets
#define NUMBER_OF_CHANNELS      8

#define OUTPUT_CHANNEL_1_OFFSET 2048
#define OUTPUT_CHANNEL_2_OFFSET 4096
#define OUTPUT_CHANNEL_3_OFFSET 6144
#define OUTPUT_CHANNEL_4_OFFSET 8192
#define OUTPUT_CHANNEL_5_OFFSET 10240
#define OUTPUT_CHANNEL_6_OFFSET 12288
#define OUTPUT_CHANNEL_7_OFFSET 14336

#define INPUT_CHANNEL_0_OFFSET 16384
#define INPUT_CHANNEL_1_OFFSET 18432
#define INPUT_CHANNEL_2_OFFSET 20480
#define INPUT_CHANNEL_3_OFFSET 22528
#define INPUT_CHANNEL_4_OFFSET 24576
#define INPUT_CHANNEL_5_OFFSET 26624
#define INPUT_CHANNEL_6_OFFSET 28672
#define INPUT_CHANNEL_7_OFFSET 30720

// DMA address definitions - PCI bar1
#define INIT_SLAVE_OFFSET        0
#define OUTPUT_DMA_PTR_OFFSET    4
#define INPUT_DMA_PTR_OFFSET     8
#define OUTPUT_DMA_ADR_0_OFFSET  128
#define OUTPUT_DMA_SIZE_0_OFFSET 132
#define INPUT_DMA_ADR_0_OFFSET   192
#define INIT_SLAVE               0x0001
#define CLOSE_SLAVE              0x0000
#define ENABLE_BOOT              0x0003
#define DISABLE_BOOT             0x0000
#define ENABLE_SLEEP             0x0005
#define DISABLE_SLEEP            0x0001
#define CLOCK_CONTROL_OFFSET     0x18
#define SDIO_CLOCK_ENABLE        0x01
#define SDIO_CLOCK_DISABLE       0x00

/* XXX this conflicts with CLOCK_CONTROL_OFFSET */
#define HOST_ATTENTION_OFFSET    0x18
#define HOST_ATTENTION_SW        0x00
#define HOST_ATTENTION_GPIO1     0x01 

#define FPGA_VERSION_OFFSET      0x1C


//SD Host register map offsets and length in bytes
// Interrupt registers - bar0
#define REG_INT_OFFSET  0x00
#define INT_ENABLE             0x01
#define INT_DISABLE            0x00

#define REG_INT_GATE_OFFSET           0x04
#define REG_INT_UNGATE_OFFSET         0x08
#define REG_INT_ACK_OFFSET            0x0C
#define REG_GPIO_OUT_PIN              0x10
#define REG_GPIO_DIRECTION            0x14
#define REG_GPIO_IN_PIN               0x18
#define REG_RESET_FPGA                0x1C

#define REG_INT_UNGATE                0x03
#define REG_INT_GATE                  0x00
#define RESET_TOGGLE_LOW              0x00
#define RESET_TOGGLE_HIGH             0x01
#define GPIO_DIRECTION_OUTPUT         0x01
#define GPIO_DIRECTION_INPUT          0x00
#define SET_CLOCK_20_MHZ              0x03



//Interrupt types
#define SDIO_DMA_OUTPUT_INT    0x0000
#define SDIO_DMA_INPUT_INT     0x0001


// Interrupts status bits 
#define SDIO_OUTPUT_GATE   0x0001
#define SDIO_INPUT_GATE    0x0002
#define SDIO_OUTPUT_INT    0x0001
#define SDIO_INPUT_INT     0x0002





typedef enum
{
	SDIO_SPI,
	SDIO_ONE_BIT_DATA,
	SDIO_FOUR_BIT_DATA
}sdio_mode_t;

// This structure defines default values for SDIO
typedef struct
{
	UINT multi_block; // Default value false => multi-byte (1-512 byte)
    UINT   block_size;  // Default vale 512 if multi_block == true
    sdio_mode_t sdio_mode; // Default SPI
}sdio_cb_t;

#endif
