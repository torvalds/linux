/*
 * include/asm-mips/mach-au1x00/au1xxx_ide.h  version 01.30.00   Aug. 02 2005
 *
 * BRIEF MODULE DESCRIPTION
 * AMD Alchemy Au1xxx IDE interface routines over the Static Bus
 *
 * Copyright (c) 2003-2005 AMD, Personal Connectivity Solutions
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Note: for more information, please refer "AMD Alchemy Au1200/Au1550 IDE
 *       Interface and Linux Device Driver" Application Note.
 */
#include <linux/config.h>

#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
        #define DMA_WAIT_TIMEOUT        100
        #define NUM_DESCRIPTORS         PRD_ENTRIES
#else /* CONFIG_BLK_DEV_IDE_AU1XXX_PIO_DBDMA */
        #define NUM_DESCRIPTORS         2
#endif

#ifndef AU1XXX_ATA_RQSIZE
        #define AU1XXX_ATA_RQSIZE       128
#endif

/* Disable Burstable-Support for DBDMA */
#ifndef CONFIG_BLK_DEV_IDE_AU1XXX_BURSTABLE_ON
        #define CONFIG_BLK_DEV_IDE_AU1XXX_BURSTABLE_ON  0
#endif

#ifdef CONFIG_PM
/*
* This will enable the device to be powered up when write() or read()
* is called. If this is not defined, the driver will return -EBUSY.
*/
#define WAKE_ON_ACCESS 1

typedef struct
{
        spinlock_t         lock;       /* Used to block on state transitions */
        au1xxx_power_dev_t *dev;       /* Power Managers device structure */
        unsigned	   stopped;    /* USed to signaling device is stopped */
} pm_state;
#endif


typedef struct
{
        u32                     tx_dev_id, rx_dev_id, target_dev_id;
        u32                     tx_chan, rx_chan;
        void                    *tx_desc_head, *rx_desc_head;
        ide_hwif_t              *hwif;
#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
        ide_drive_t             *drive;
        u8                      white_list, black_list;
        struct dbdma_cmd        *dma_table_cpu;
        dma_addr_t              dma_table_dma;
        struct scatterlist      *sg_table;
        int                     sg_nents;
        int                     sg_dma_direction;
#endif
        struct device           *dev;
	int			irq;
	u32			regbase;
#ifdef CONFIG_PM
        pm_state                pm;
#endif
} _auide_hwif;

#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
struct drive_list_entry {
        const char * id_model;
        const char * id_firmware;
};

/* HD white list */
static const struct drive_list_entry dma_white_list [] = {
/*
 * Hitachi
 */
        { "HITACHI_DK14FA-20"    ,       "ALL"           },
        { "HTS726060M9AT00"      ,       "ALL"           },
/*
 * Maxtor
 */
        { "Maxtor 6E040L0"      ,       "ALL"           },
        { "Maxtor 6Y080P0"      ,       "ALL"           },
        { "Maxtor 6Y160P0"      ,       "ALL"           },
/*
 * Seagate
 */
        { "ST3120026A"          ,       "ALL"           },
        { "ST320014A"           ,       "ALL"           },
        { "ST94011A"            ,       "ALL"           },
        { "ST340016A"           ,       "ALL"           },
/*
 * Western Digital
 */
        { "WDC WD400UE-00HCT0"  ,       "ALL"           },
        { "WDC WD400JB-00JJC0"  ,       "ALL"           },
        { NULL                  ,       NULL            }
};

/* HD black list */
static const struct drive_list_entry dma_black_list [] = {
/*
 * Western Digital
 */
        { "WDC WD100EB-00CGH0"  ,       "ALL"           },
        { "WDC WD200BB-00AUA1"  ,       "ALL"           },
        { "WDC AC24300L"        ,       "ALL"           },
        { NULL                  ,       NULL            }
};
#endif

/* function prototyping */
u8 auide_inb(unsigned long port);
u16 auide_inw(unsigned long port);
u32 auide_inl(unsigned long port);
void auide_insw(unsigned long port, void *addr, u32 count);
void auide_insl(unsigned long port, void *addr, u32 count);
void auide_outb(u8 addr, unsigned long port);
void auide_outbsync(ide_drive_t *drive, u8 addr, unsigned long port);
void auide_outw(u16 addr, unsigned long port);
void auide_outl(u32 addr, unsigned long port);
void auide_outsw(unsigned long port, void *addr, u32 count);
void auide_outsl(unsigned long port, void *addr, u32 count);
static void auide_tune_drive(ide_drive_t *drive, byte pio);
static int auide_tune_chipset (ide_drive_t *drive, u8 speed);
static int auide_ddma_init( _auide_hwif *auide );
static void auide_setup_ports(hw_regs_t *hw, _auide_hwif *ahwif);
int __init auide_probe(void);

#ifdef CONFIG_PM
        int au1200ide_pm_callback( au1xxx_power_dev_t *dev,
                                   au1xxx_request_t request, void *data);
        static int au1xxxide_pm_standby( au1xxx_power_dev_t *dev );
        static int au1xxxide_pm_sleep( au1xxx_power_dev_t *dev );
        static int au1xxxide_pm_resume( au1xxx_power_dev_t *dev );
        static int au1xxxide_pm_getstatus( au1xxx_power_dev_t *dev );
        static int au1xxxide_pm_access( au1xxx_power_dev_t *dev );
        static int au1xxxide_pm_idle( au1xxx_power_dev_t *dev );
        static int au1xxxide_pm_cleanup( au1xxx_power_dev_t *dev );
#endif


/*
 * Multi-Word DMA + DbDMA functions
 */
#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA

        static int in_drive_list(struct hd_driveid *id,
                                 const struct drive_list_entry *drive_table);
        static int auide_build_sglist(ide_drive_t *drive,  struct request *rq);
        static int auide_build_dmatable(ide_drive_t *drive);
        static int auide_dma_end(ide_drive_t *drive);
        static void auide_dma_start(ide_drive_t *drive );
        ide_startstop_t auide_dma_intr (ide_drive_t *drive);
        static void auide_dma_exec_cmd(ide_drive_t *drive, u8 command);
        static int auide_dma_setup(ide_drive_t *drive);
        static int auide_dma_check(ide_drive_t *drive);
        static int auide_dma_test_irq(ide_drive_t *drive);
        static int auide_dma_host_off(ide_drive_t *drive);
        static int auide_dma_host_on(ide_drive_t *drive);
        static int auide_dma_lostirq(ide_drive_t *drive);
        static int auide_dma_on(ide_drive_t *drive);
        static void auide_ddma_tx_callback(int irq, void *param,
                                           struct pt_regs *regs);
        static void auide_ddma_rx_callback(int irq, void *param,
                                           struct pt_regs *regs);
        static int auide_dma_off_quietly(ide_drive_t *drive);
        static int auide_dma_timeout(ide_drive_t *drive);

#endif /* end CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA */

/*******************************************************************************
* PIO Mode timing calculation :                                                *
*                                                                              *
* Static Bus Spec   ATA Spec                                                   *
*      Tcsoe      =   t1                                                       *
*      Toecs      =   t9                                                       *
*      Twcs       =   t9                                                       *
*      Tcsh       =   t2i | t2                                                 *
*      Tcsoff     =   t2i | t2                                                 *
*      Twp        =   t2                                                       *
*      Tcsw       =   t1                                                       *
*      Tpm        =   0                                                        *
*      Ta         =   t1+t2                                                    *
*******************************************************************************/

#define TCSOE_MASK            (0x07<<29)
#define TOECS_MASK            (0x07<<26)
#define TWCS_MASK             (0x07<<28)
#define TCSH_MASK             (0x0F<<24)
#define TCSOFF_MASK           (0x07<<20)
#define TWP_MASK              (0x3F<<14)
#define TCSW_MASK             (0x0F<<10)
#define TPM_MASK              (0x0F<<6)
#define TA_MASK               (0x3F<<0)
#define TS_MASK               (1<<8)

/* Timing parameters PIO mode 0 */
#define SBC_IDE_PIO0_TCSOE    (0x04<<29)
#define SBC_IDE_PIO0_TOECS    (0x01<<26)
#define SBC_IDE_PIO0_TWCS     (0x02<<28)
#define SBC_IDE_PIO0_TCSH     (0x08<<24)
#define SBC_IDE_PIO0_TCSOFF   (0x07<<20)
#define SBC_IDE_PIO0_TWP      (0x10<<14)
#define SBC_IDE_PIO0_TCSW     (0x04<<10)
#define SBC_IDE_PIO0_TPM      (0x0<<6)
#define SBC_IDE_PIO0_TA       (0x15<<0)
/* Timing parameters PIO mode 1 */
#define SBC_IDE_PIO1_TCSOE    (0x03<<29)
#define SBC_IDE_PIO1_TOECS    (0x01<<26)
#define SBC_IDE_PIO1_TWCS     (0x01<<28)
#define SBC_IDE_PIO1_TCSH     (0x06<<24)
#define SBC_IDE_PIO1_TCSOFF   (0x06<<20)
#define SBC_IDE_PIO1_TWP      (0x08<<14)
#define SBC_IDE_PIO1_TCSW     (0x03<<10)
#define SBC_IDE_PIO1_TPM      (0x00<<6)
#define SBC_IDE_PIO1_TA       (0x0B<<0)
/* Timing parameters PIO mode 2 */
#define SBC_IDE_PIO2_TCSOE    (0x05<<29)
#define SBC_IDE_PIO2_TOECS    (0x01<<26)
#define SBC_IDE_PIO2_TWCS     (0x01<<28)
#define SBC_IDE_PIO2_TCSH     (0x07<<24)
#define SBC_IDE_PIO2_TCSOFF   (0x07<<20)
#define SBC_IDE_PIO2_TWP      (0x1F<<14)
#define SBC_IDE_PIO2_TCSW     (0x05<<10)
#define SBC_IDE_PIO2_TPM      (0x00<<6)
#define SBC_IDE_PIO2_TA       (0x22<<0)
/* Timing parameters PIO mode 3 */
#define SBC_IDE_PIO3_TCSOE    (0x05<<29)
#define SBC_IDE_PIO3_TOECS    (0x01<<26)
#define SBC_IDE_PIO3_TWCS     (0x01<<28)
#define SBC_IDE_PIO3_TCSH     (0x0D<<24)
#define SBC_IDE_PIO3_TCSOFF   (0x0D<<20)
#define SBC_IDE_PIO3_TWP      (0x15<<14)
#define SBC_IDE_PIO3_TCSW     (0x05<<10)
#define SBC_IDE_PIO3_TPM      (0x00<<6)
#define SBC_IDE_PIO3_TA       (0x1A<<0)
/* Timing parameters PIO mode 4 */
#define SBC_IDE_PIO4_TCSOE    (0x04<<29)
#define SBC_IDE_PIO4_TOECS    (0x01<<26)
#define SBC_IDE_PIO4_TWCS     (0x01<<28)
#define SBC_IDE_PIO4_TCSH     (0x04<<24)
#define SBC_IDE_PIO4_TCSOFF   (0x04<<20)
#define SBC_IDE_PIO4_TWP      (0x0D<<14)
#define SBC_IDE_PIO4_TCSW     (0x03<<10)
#define SBC_IDE_PIO4_TPM      (0x00<<6)
#define SBC_IDE_PIO4_TA       (0x12<<0)
/* Timing parameters MDMA mode 0 */
#define SBC_IDE_MDMA0_TCSOE   (0x03<<29)
#define SBC_IDE_MDMA0_TOECS   (0x01<<26)
#define SBC_IDE_MDMA0_TWCS    (0x01<<28)
#define SBC_IDE_MDMA0_TCSH    (0x07<<24)
#define SBC_IDE_MDMA0_TCSOFF  (0x07<<20)
#define SBC_IDE_MDMA0_TWP     (0x0C<<14)
#define SBC_IDE_MDMA0_TCSW    (0x03<<10)
#define SBC_IDE_MDMA0_TPM     (0x00<<6)
#define SBC_IDE_MDMA0_TA      (0x0F<<0)
/* Timing parameters MDMA mode 1 */
#define SBC_IDE_MDMA1_TCSOE   (0x05<<29)
#define SBC_IDE_MDMA1_TOECS   (0x01<<26)
#define SBC_IDE_MDMA1_TWCS    (0x01<<28)
#define SBC_IDE_MDMA1_TCSH    (0x05<<24)
#define SBC_IDE_MDMA1_TCSOFF  (0x05<<20)
#define SBC_IDE_MDMA1_TWP     (0x0F<<14)
#define SBC_IDE_MDMA1_TCSW    (0x05<<10)
#define SBC_IDE_MDMA1_TPM     (0x00<<6)
#define SBC_IDE_MDMA1_TA      (0x15<<0)
/* Timing parameters MDMA mode 2 */
#define SBC_IDE_MDMA2_TCSOE   (0x04<<29)
#define SBC_IDE_MDMA2_TOECS   (0x01<<26)
#define SBC_IDE_MDMA2_TWCS    (0x01<<28)
#define SBC_IDE_MDMA2_TCSH    (0x04<<24)
#define SBC_IDE_MDMA2_TCSOFF  (0x04<<20)
#define SBC_IDE_MDMA2_TWP     (0x0D<<14)
#define SBC_IDE_MDMA2_TCSW    (0x04<<10)
#define SBC_IDE_MDMA2_TPM     (0x00<<6)
#define SBC_IDE_MDMA2_TA      (0x12<<0)

