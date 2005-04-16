#ifndef _ASM_ATARI_ACSI_H
#define _ASM_ATARI_ACSI_H

/* Functions exported by drivers/block/acsi.c */

void acsi_delay_start( void );
void acsi_delay_end( long usec );
int acsi_wait_for_IRQ( unsigned timeout );
int acsi_wait_for_noIRQ( unsigned timeout );
int acsicmd_nodma( const char *cmd, int enable);
int acsi_getstatus( void );
int acsi_extstatus( char *buffer, int cnt );
void acsi_end_extstatus( void );
int acsi_extcmd( unsigned char *buffer, int cnt );

/* The ACSI buffer is guarantueed to reside in ST-RAM and may be used by other
 * drivers that work on the ACSI bus, too. It's data are valid only as long as
 * the ST-DMA is locked. */
extern char *acsi_buffer;
extern unsigned long phys_acsi_buffer;

/* Utility macros */

/* Send one data byte over the bus and set mode for next operation
 * with one move.l -- Atari recommends this...
 */

#define DMA_LONG_WRITE(data,mode)							\
    do {													\
		*((unsigned long *)&dma_wd.fdc_acces_seccount) =	\
			((data)<<16) | (mode);							\
	} while(0)

#define ENABLE_IRQ()	atari_turnon_irq( IRQ_MFP_ACSI )
#define DISABLE_IRQ()	atari_turnoff_irq( IRQ_MFP_ACSI )

#endif /* _ASM_ATARI_ACSI_H */
