#ifndef	__XMPCILPEVENT_H__
#define	__XMPCILPEVENT_H__

extern int XmPciLpEvent_init(void);
extern void ppc_irq_dispatch_handler(struct pt_regs *regs, int irq);

#endif /* __XMPCILPEVENT_H__ */
