// Copyright (c) 2004, 2005 Atheros Communications Inc.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Portions of this code were developed with information supplied from the 
// SD Card Association Simplified Specifications. The following conditions and disclaimers may apply:
//
//  The following conditions apply to the release of the SD simplified specification (“Simplified
//  Specification”) by the SD Card Association. The Simplified Specification is a subset of the complete 
//  SD Specification which is owned by the SD Card Association. This Simplified Specification is provided 
//  on a non-confidential basis subject to the disclaimers below. Any implementation of the Simplified 
//  Specification may require a license from the SD Card Association or other third parties.
//  Disclaimers:
//  The information contained in the Simplified Specification is presented only as a standard 
//  specification for SD Cards and SD Host/Ancillary products and is provided "AS-IS" without any 
//  representations or warranties of any kind. No responsibility is assumed by the SD Card Association for 
//  any damages, any infringements of patents or other right of the SD Card Association or any third 
//  parties, which may result from its use. No license is granted by implication, estoppel or otherwise 
//  under any patent or other rights of the SD Card Association or any third party. Nothing herein shall 
//  be construed as an obligation by the SD Card Association to disclose or distribute any technical 
//  information, know-how or other confidential information to any third party.
//
//
// The initial developers of the original code are Seung Yi and Paul Lever
//
// sdio@atheros.com
//
//

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: sdio_hcd_linux.h

@abstract: include file for Tokyo Electron PCI Ellen host controller, linux dependent code
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifndef __SDIO_HCD_LINUX_H___
#define __SDIO_HCD_LINUX_H___


#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/device.h>


#include <asm/irq.h>


#define SDHCD_MAX_DEVICE_NAME 12

#define CARD_INSERT_POLARITY   FALSE
#define WP_POLARITY            TRUE
#define HCD_COMMAND_MIN_POLLING_CLOCK 5000000

/* debounce delay for slot */
#define SD_SLOT_DEBOUNCE_MS  1000

/* the config space slot number and start for SD host */
#define PCI_CONFIG_SLOT   0x40
#define GET_SLOT_COUNT(config)\
    ((((config)>>4)& 0x7) +1)
#define GET_SLOT_FIRST(config)\
    ((config) & 0x7)

/* device base name */
#define SDIO_BD_BASE "sdiobd"

/* mapped memory address */
typedef struct _SDHCD_MEMORY {
    ULONG Raw;      /* start of address range */
    ULONG Length;   /* length of range */
    PVOID pMapped;  /* the mapped address */
}SDHCD_MEMORY, *PSDHCD_MEMORY;

typedef enum _SDHCD_TYPE {
    TYPE_CLASS,     /* standard class device */
    TYPE_PCIELLEN,  /* Tokuo Electron PCI Ellen card */
}SDHCD_TYPE, *PSDHCD_TYPE;

/* device data*/
typedef struct _SDHCD_DEVICE {
    struct pci_dev *pBusDevice;    /* our device registered with bus driver */
    SDLIST  List;                  /* linked list */
    SDHCD   Hcd;                   /* HCD description for bus driver */
    char    DeviceName[SDHCD_MAX_DEVICE_NAME]; /* our chr device name */
    SDHCD_MEMORY Address;          /* memory address of this device */ 
    spinlock_t AddressSpinlock;    /* use to protect reghisters when needed */
    SDHCD_MEMORY ControlRegs;      /* memory address of shared control registers */ 
    SDHCD_TYPE Type;               /* type of this device */
    UINT8   InitStateMask;
#define SDIO_BAR_MAPPED            0x01
#define SDIO_LAST_CONTROL_BAR_MAPPED 0x02 /* set on device that will unmap the shared control registers */
#define SDIO_IRQ_INTERRUPT_INIT    0x04
#define SDHC_REGISTERED            0x10
#define SDHC_HW_INIT               0x40
#define TIMER_INIT                 0x80
    spinlock_t   Lock;            /* lock against the ISR */
    BOOL         CardInserted;    /* card inserted flag */
    BOOL         Cancel;
    BOOL         ShuttingDown;    /* indicates shut down of HCD) */
    BOOL         HighSpeed;       /* device supports high speed, 25-50 Mhz */
    UINT32       BaseClock;       /* base clock in hz */ 
    UINT32       TimeOut;         /* timeout setting */ 
    UINT32       ClockSpinLimit;  /* clock limit for command spin loops */
    BOOL         KeepClockOn;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
    struct work_struct iocomplete_work; /* work item definintions */
    struct work_struct carddetect_work; /* work item definintions */
    struct work_struct sdioirq_work; /* work item definintions */
#else
    struct delayed_work iocomplete_work; /* work item definintions */
    struct delayed_work carddetect_work; /* work item definintions */
    struct delayed_work sdioirq_work; /* work item definintions */
#endif
}SDHCD_DEVICE, *PSDHCD_DEVICE;


#define WORK_ITEM_IO_COMPLETE  0
#define WORK_ITEM_CARD_DETECT  1
#define WORK_ITEM_SDIO_IRQ     2

 
#define READ_HOST_REG32(pDevice, OFFSET)  \
    _READ_DWORD_REG((((UINT32)((pDevice)->Address.pMapped))) + (OFFSET))
#define WRITE_HOST_REG32(pDevice, OFFSET, VALUE) \
    _WRITE_DWORD_REG((((UINT32)((pDevice)->Address.pMapped))) + (OFFSET),(VALUE))
#define READ_HOST_REG16(pDevice, OFFSET)  \
    _READ_WORD_REG((((UINT32)((pDevice)->Address.pMapped))) + (OFFSET))
#define WRITE_HOST_REG16(pDevice, OFFSET, VALUE) \
    _WRITE_WORD_REG((((UINT32)((pDevice)->Address.pMapped))) + (OFFSET),(VALUE))
#define READ_HOST_REG8(pDevice, OFFSET)  \
    _READ_BYTE_REG((((UINT32)((pDevice)->Address.pMapped))) + (OFFSET))
#define WRITE_HOST_REG8(pDevice, OFFSET, VALUE) \
    _WRITE_BYTE_REG((((UINT32)((pDevice)->Address.pMapped))) + (OFFSET),(VALUE))

#define READ_CONTROL_REG32(pDevice, OFFSET)  \
    _READ_DWORD_REG((((UINT32)((pDevice)->ControlRegs.pMapped))) + (OFFSET))
#define WRITE_CONTROL_REG32(pDevice, OFFSET, VALUE) \
    _WRITE_DWORD_REG((((UINT32)((pDevice)->ControlRegs.pMapped))) + (OFFSET),(VALUE))
#define READ_CONTROL_REG16(pDevice, OFFSET)  \
    _READ_WORD_REG((((UINT32)((pDevice)->ControlRegs.pMapped))) + (OFFSET))
#define WRITE_CONTROL_REG16(pDevice, OFFSET, VALUE) \
    _WRITE_WORD_REG((((UINT32)((pDevice)->ControlRegs.pMapped))) + (OFFSET),(VALUE))

/* PLX 9030 control registers */
#define INTCSR 0x4C
#define INTCSR_LINTi1ENABLE         (1 << 0)
#define INTCSR_LINTi1STATUS         (1 << 2)
#define INTCSR_LINTi2ENABLE         (1 << 3)
#define INTCSR_LINTi2STATUS         (1 << 5)
#define INTCSR_PCIINTENABLE         (1 << 6)

#define GPIOCTRL 0x54
#define GPIO8_PIN_DIRECTION     (1 << 25)
#define GPIO8_DATA_MASK         (1 << 26)
#define GPIO3_PIN_SELECT        (1 << 9)
#define GPIO3_PIN_DIRECTION     (1 << 10)
#define GPIO3_DATA_MASK         (1 << 11)
#define GPIO2_PIN_SELECT        (1 << 6)
#define GPIO2_PIN_DIRECTION     (1 << 7)
#define GPIO2_DATA_MASK         (1 << 8)
#define GPIO4_PIN_SELECT        (1 << 12)
#define GPIO4_PIN_DIRECTION     (1 << 13)
#define GPIO4_DATA_MASK         (1 << 14)

#define GPIO_CONTROL(pDevice, on,  GpioMask)   \
{   \
     UINT32 gpio_ctrl_temp;   \
     gpio_ctrl_temp = READ_CONTROL_REG32((pDevice),GPIOCTRL);   \
     if (on) gpio_ctrl_temp |= (GpioMask); else gpio_ctrl_temp &= ~(GpioMask);   \
     WRITE_CONTROL_REG32((pDevice),GPIOCTRL, gpio_ctrl_temp);   \
}

//??#define TRACE_SIGNAL_DATA_WRITE(pDevice, on) GPIO_CONTROL((pDevice),(on),GPIO8_DATA_MASK)
//??#define TRACE_SIGNAL_DATA_READ(pDevice, on) GPIO_CONTROL((pDevice),(on),GPIO2_DATA_MASK)
//??#define TRACE_SIGNAL_DATA_ISR(pDevice, on) GPIO_CONTROL((pDevice),(on),GPIO4_DATA_MASK)
//??#define TRACE_SIGNAL_DATA_IOCOMP(pDevice, on) GPIO_CONTROL((pDevice),(on),GPIO3_DATA_MASK)
#define TRACE_SIGNAL_DATA_WRITE(pDevice, on) 
#define TRACE_SIGNAL_DATA_READ(pDevice, on) 
#define TRACE_SIGNAL_DATA_ISR(pDevice, on) 
#define TRACE_SIGNAL_DATA_IOCOMP(pDevice, on) 
#define TRACE_SIGNAL_DATA_TIMEOUT(pDevice, on) GPIO_CONTROL((pDevice),(on),GPIO3_DATA_MASK)

/* prototypes */
#endif /* __SDIO_HCD_LINUX_H___ */
