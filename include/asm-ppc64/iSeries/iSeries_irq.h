#ifndef	__ISERIES_IRQ_H__
#define	__ISERIES_IRQ_H__

extern void iSeries_init_IRQ(void);
extern int  iSeries_allocate_IRQ(HvBusNumber, HvSubBusNumber, HvAgentId);
extern int  iSeries_assign_IRQ(int, HvBusNumber, HvSubBusNumber, HvAgentId);
extern void iSeries_activate_IRQs(void);

extern int XmPciLpEvent_init(void);

#endif /* __ISERIES_IRQ_H__ */
