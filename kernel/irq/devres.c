#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/gfp.h>

/*
 * Device resource management aware IRQ request/free implementation.
 */
struct irq_devres {
	unsigned int irq;
	void *dev_id;
};

static void devm_irq_release(struct device *dev, void *res)
{
	struct irq_devres *this = res;

	free_irq(this->irq, this->dev_id);
}

static int devm_irq_match(struct device *dev, void *res, void *data)
{
	struct irq_devres *this = res, *match = data;

	return this->irq == match->irq && this->dev_id == match->dev_id;
}

/**
 *	devm_request_threaded_irq - allocate an interrupt line for a managed device
 *	@dev: device to request interrupt for
 *	@irq: Interrupt line to allocate
 *	@handler: Function to be called when the IRQ occurs
 *	@thread_fn: function to be called in a threaded interrupt context. NULL
 *		    for devices which handle everything in @handler
 *	@irqflags: Interrupt type flags
 *	@devname: An ascii name for the claiming device
 *	@dev_id: A cookie passed back to the handler function
 *
 *	Except for the extra @dev argument, this function takes the
 *	same arguments and performs the same function as
 *	request_irq().  IRQs requested with this function will be
 *	automatically freed on driver detach.
 *
 *	If an IRQ allocated with this function needs to be freed
 *	separately, devm_free_irq() must be used.
 */
int devm_request_threaded_irq(struct device *dev, unsigned int irq,
			      irq_handler_t handler, irq_handler_t thread_fn,
			      unsigned long irqflags, const char *devname,
			      void *dev_id)
{
	struct irq_devres *dr;
	int rc;

	dr = devres_alloc(devm_irq_release, sizeof(struct irq_devres),
			  GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	rc = request_threaded_irq(irq, handler, thread_fn, irqflags, devname,
				  dev_id);
	if (rc) {
		devres_free(dr);
		return rc;
	}

	dr->irq = irq;
	dr->dev_id = dev_id;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL(devm_request_threaded_irq);

/**
 *	devm_free_irq - free an interrupt
 *	@dev: device to free interrupt for
 *	@irq: Interrupt line to free
 *	@dev_id: Device identity to free
 *
 *	Except for the extra @dev argument, this function takes the
 *	same arguments and performs the same function as free_irq().
 *	This function instead of free_irq() should be used to manually
 *	free IRQs allocated with devm_request_irq().
 */
void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id)
{
	struct irq_devres match_data = { irq, dev_id };

	free_irq(irq, dev_id);
	WARN_ON(devres_destroy(dev, devm_irq_release, devm_irq_match,
			       &match_data));
}
EXPORT_SYMBOL(devm_free_irq);
