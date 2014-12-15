#ifndef _LINUX_AMLOGIC_OF_LM_H
#define _LINUX_AMLOGIC_OF_LM_H

extern int of_lm_populate(struct device_node *root,
			const struct of_device_id *matches,
			const struct of_dev_auxdata *lookup,
			struct device *parent);

extern int of_lm_bus_probe(struct device_node *root,
			  const struct of_device_id *matches,
			  struct device *parent);
			  
extern const struct of_device_id *of_lm_match_node(
			  const struct of_device_id *matches, 
			  const struct device_node *node);	
			  
#endif	/* _LINUX_AMLOGIC_OF_LM_H */
