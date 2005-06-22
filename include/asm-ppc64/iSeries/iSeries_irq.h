#ifndef	__ISERIES_IRQ_H__
#define	__ISERIES_IRQ_H__

extern void iSeries_init_IRQ(void);
extern int  iSeries_allocate_IRQ(HvBusNumber, HvSubBusNumber, HvAgentId);
extern void iSeries_activate_IRQs(void);

#endif /* __ISERIES_IRQ_H__ */
