
#ifndef _atari_stdma_h
#define _atari_stdma_h


#include <linux/interrupt.h>


/***************************** Prototypes *****************************/

void stdma_lock(irqreturn_t (*handler)(int, void *, struct pt_regs *),
		void *data);
void stdma_release( void );
int stdma_others_waiting( void );
int stdma_islocked( void );
void *stdma_locked_by( void );
void stdma_init( void );

/************************* End of Prototypes **************************/



#endif  /* _atari_stdma_h */
