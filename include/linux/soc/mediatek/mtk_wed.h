#ifndef __MTK_WED_H
#define __MTK_WED_H

#include <linux/kernel.h>
#include <linux/rcupdate.h>
#include <linux/regmap.h>
#include <linux/pci.h>

#define MTK_WED_TX_QUEUES		2

struct mtk_wed_hw;
struct mtk_wdma_desc;

struct mtk_wed_ring {
	struct mtk_wdma_desc *desc;
	dma_addr_t desc_phys;
	int size;

	u32 reg_base;
	void __iomem *wpdma;
};

struct mtk_wed_device {
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	const struct mtk_wed_ops *ops;
	struct device *dev;
	struct mtk_wed_hw *hw;
	bool init_done, running;
	int wdma_idx;
	int irq;

	struct mtk_wed_ring tx_ring[MTK_WED_TX_QUEUES];
	struct mtk_wed_ring txfree_ring;
	struct mtk_wed_ring tx_wdma[MTK_WED_TX_QUEUES];

	struct {
		int size;
		void **pages;
		struct mtk_wdma_desc *desc;
		dma_addr_t desc_phys;
	} buf_ring;

	/* filled by driver: */
	struct {
		struct pci_dev *pci_dev;

		u32 wpdma_phys;

		u16 token_start;
		unsigned int nbuf;

		u32 (*init_buf)(void *ptr, dma_addr_t phys, int token_id);
		int (*offload_enable)(struct mtk_wed_device *wed);
		void (*offload_disable)(struct mtk_wed_device *wed);
	} wlan;
#endif
};

struct mtk_wed_ops {
	int (*attach)(struct mtk_wed_device *dev);
	int (*tx_ring_setup)(struct mtk_wed_device *dev, int ring,
			     void __iomem *regs);
	int (*txfree_ring_setup)(struct mtk_wed_device *dev,
				 void __iomem *regs);
	void (*detach)(struct mtk_wed_device *dev);

	void (*stop)(struct mtk_wed_device *dev);
	void (*start)(struct mtk_wed_device *dev, u32 irq_mask);
	void (*reset_dma)(struct mtk_wed_device *dev);

	u32 (*reg_read)(struct mtk_wed_device *dev, u32 reg);
	void (*reg_write)(struct mtk_wed_device *dev, u32 reg, u32 val);

	u32 (*irq_get)(struct mtk_wed_device *dev, u32 mask);
	void (*irq_set_mask)(struct mtk_wed_device *dev, u32 mask);
};

extern const struct mtk_wed_ops __rcu *mtk_soc_wed_ops;

static inline int
mtk_wed_device_attach(struct mtk_wed_device *dev)
{
	int ret = -ENODEV;

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	rcu_read_lock();
	dev->ops = rcu_dereference(mtk_soc_wed_ops);
	if (dev->ops)
		ret = dev->ops->attach(dev);
	else
		rcu_read_unlock();

	if (ret)
		dev->ops = NULL;
#endif

	return ret;
}

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
#define mtk_wed_device_active(_dev) !!(_dev)->ops
#define mtk_wed_device_detach(_dev) (_dev)->ops->detach(_dev)
#define mtk_wed_device_start(_dev, _mask) (_dev)->ops->start(_dev, _mask)
#define mtk_wed_device_tx_ring_setup(_dev, _ring, _regs) \
	(_dev)->ops->tx_ring_setup(_dev, _ring, _regs)
#define mtk_wed_device_txfree_ring_setup(_dev, _regs) \
	(_dev)->ops->txfree_ring_setup(_dev, _regs)
#define mtk_wed_device_reg_read(_dev, _reg) \
	(_dev)->ops->reg_read(_dev, _reg)
#define mtk_wed_device_reg_write(_dev, _reg, _val) \
	(_dev)->ops->reg_write(_dev, _reg, _val)
#define mtk_wed_device_irq_get(_dev, _mask) \
	(_dev)->ops->irq_get(_dev, _mask)
#define mtk_wed_device_irq_set_mask(_dev, _mask) \
	(_dev)->ops->irq_set_mask(_dev, _mask)
#else
static inline bool mtk_wed_device_active(struct mtk_wed_device *dev)
{
	return false;
}
#define mtk_wed_device_detach(_dev) do {} while (0)
#define mtk_wed_device_start(_dev, _mask) do {} while (0)
#define mtk_wed_device_tx_ring_setup(_dev, _ring, _regs) -ENODEV
#define mtk_wed_device_txfree_ring_setup(_dev, _ring, _regs) -ENODEV
#define mtk_wed_device_reg_read(_dev, _reg) 0
#define mtk_wed_device_reg_write(_dev, _reg, _val) do {} while (0)
#define mtk_wed_device_irq_get(_dev, _mask) 0
#define mtk_wed_device_irq_set_mask(_dev, _mask) do {} while (0)
#endif

#endif
