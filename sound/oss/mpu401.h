
/*	From uart401.c */
int probe_uart401 (struct address_info *hw_config, struct module *owner);
void unload_uart401 (struct address_info *hw_config);

irqreturn_t uart401intr (int irq, void *dev_id, struct pt_regs * dummy);

/*	From mpu401.c */
int probe_mpu401(struct address_info *hw_config, struct resource *ports);
int attach_mpu401(struct address_info * hw_config, struct module *owner);
void unload_mpu401(struct address_info *hw_info);

int intchk_mpu401(void *dev_id);
irqreturn_t mpuintr(int irq, void *dev_id, struct pt_regs * dummy);
