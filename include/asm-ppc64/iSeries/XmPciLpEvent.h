
#ifndef	__XMPCILPEVENT_H__
#define	__XMPCILPEVENT_H__


#ifdef __cplusplus
extern "C" {
#endif

int XmPciLpEvent_init(void);
void ppc_irq_dispatch_handler(struct pt_regs *regs, int irq);


#ifdef __cplusplus
}
#endif

#endif /* __XMPCILPEVENT_H__ */
