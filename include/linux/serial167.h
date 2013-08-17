/*
 * serial167.h
 *
 * Richard Hirst [richard@sleepie.demon.co.uk]
 *
 * Based on cyclades.h
 */

struct cyclades_monitor {
        unsigned long           int_count;
        unsigned long           char_count;
        unsigned long           char_max;
        unsigned long           char_last;
};

/*
 * This is our internal structure for each serial port's state.
 * 
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

struct cyclades_port {
	int                     magic;
	int                     type;
	int			card;
	int			line;
	int			flags; 		/* defined in tty.h */
	struct tty_struct 	*tty;
	int			read_status_mask;
	int			timeout;
	int			xmit_fifo_size;
	int                     cor1,cor2,cor3,cor4,cor5,cor6,cor7;
	int                     tbpr,tco,rbpr,rco;
	int			ignore_status_mask;
	int			close_delay;
	int			IER; 	/* Interrupt Enable Register */
	unsigned long		last_active;
	int			count;	/* # of fd on device */
	int                     x_char; /* to be pushed out ASAP */
	int                     x_break;
	int			blocked_open; /* # of blocked opens */
	unsigned char 		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
        int                     default_threshold;
        int                     default_timeout;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
        struct cyclades_monitor mon;
};

#define CYCLADES_MAGIC  0x4359

#define CYGETMON                0x435901
#define CYGETTHRESH             0x435902
#define CYSETTHRESH             0x435903
#define CYGETDEFTHRESH          0x435904
#define CYSETDEFTHRESH          0x435905
#define CYGETTIMEOUT            0x435906
#define CYSETTIMEOUT            0x435907
#define CYGETDEFTIMEOUT         0x435908
#define CYSETDEFTIMEOUT         0x435909

#define CyMaxChipsPerCard 1

/**** cd2401 registers ****/

#define CyGFRCR         (0x81)
#define CyCCR		(0x13)
#define      CyCLR_CHAN		(0x40)
#define      CyINIT_CHAN	(0x20)
#define      CyCHIP_RESET	(0x10)
#define      CyENB_XMTR		(0x08)
#define      CyDIS_XMTR		(0x04)
#define      CyENB_RCVR		(0x02)
#define      CyDIS_RCVR		(0x01)
#define CyCAR		(0xee)
#define CyIER		(0x11)
#define      CyMdmCh		(0x80)
#define      CyRxExc		(0x20)
#define      CyRxData		(0x08)
#define      CyTxMpty		(0x02)
#define      CyTxRdy		(0x01)
#define CyLICR		(0x26)
#define CyRISR		(0x89)
#define      CyTIMEOUT		(0x80)
#define      CySPECHAR		(0x70)
#define      CyOVERRUN		(0x08)
#define      CyPARITY		(0x04)
#define      CyFRAME		(0x02)
#define      CyBREAK		(0x01)
#define CyREOIR		(0x84)
#define CyTEOIR		(0x85)
#define CyMEOIR		(0x86)
#define      CyNOTRANS		(0x08)
#define CyRFOC		(0x30)
#define CyRDR		(0xf8)
#define CyTDR		(0xf8)
#define CyMISR		(0x8b)
#define CyRISR		(0x89)
#define CyTISR		(0x8a)
#define CyMSVR1		(0xde)
#define CyMSVR2		(0xdf)
#define      CyDSR		(0x80)
#define      CyDCD		(0x40)
#define      CyCTS		(0x20)
#define      CyDTR		(0x02)
#define      CyRTS		(0x01)
#define CyRTPRL		(0x25)
#define CyRTPRH		(0x24)
#define CyCOR1		(0x10)
#define      CyPARITY_NONE	(0x00)
#define      CyPARITY_E		(0x40)
#define      CyPARITY_O		(0xC0)
#define      Cy_5_BITS		(0x04)
#define      Cy_6_BITS		(0x05)
#define      Cy_7_BITS		(0x06)
#define      Cy_8_BITS		(0x07)
#define CyCOR2		(0x17)
#define      CyETC		(0x20)
#define      CyCtsAE		(0x02)
#define CyCOR3		(0x16)
#define      Cy_1_STOP		(0x02)
#define      Cy_2_STOP		(0x04)
#define CyCOR4		(0x15)
#define      CyREC_FIFO		(0x0F)  /* Receive FIFO threshold */
#define CyCOR5		(0x14)
#define CyCOR6		(0x18)
#define CyCOR7		(0x07)
#define CyRBPR		(0xcb)
#define CyRCOR		(0xc8)
#define CyTBPR		(0xc3)
#define CyTCOR		(0xc0)
#define CySCHR1		(0x1f)
#define CySCHR2 	(0x1e)
#define CyTPR		(0xda)
#define CyPILR1		(0xe3)
#define CyPILR2		(0xe0)
#define CyPILR3		(0xe1)
#define CyCMR		(0x1b)
#define      CyASYNC		(0x02)
#define CyLICR          (0x26)
#define CyLIVR          (0x09)
#define CySCRL		(0x23)
#define CySCRH		(0x22)
#define CyTFTC		(0x80)


/* max number of chars in the FIFO */

#define CyMAX_CHAR_FIFO	12

/***************************************************************************/
