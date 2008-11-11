/*
 *  include/linux/amba/mmci.h
 */
#ifndef AMBA_MMCI_H
#define AMBA_MMCI_H

#include <linux/mmc/host.h>

/* Just some dummy forwarding */
struct dma_chan;

/**
 * struct mmci_platform_data - platform configuration for the MMCI
 * (also known as PL180) block.
 * @f_max: the maximum operational frequency for this host in this
 * platform configuration. When this is specified it takes precedence
 * over the module parameter for the same frequency.
 * @ocr_mask: available voltages on the 4 pins from the block, this
 * is ignored if a regulator is used, see the MMC_VDD_* masks in
 * mmc/host.h
 * @vdd_handler: a callback function to translate a MMC_VDD_*
 * mask into a value to be binary (or set some other custom bits
 * in MMCIPWR) or:ed and written into the MMCIPWR register of the
 * block.  May also control external power based on the power_mode.
 * @status: if no GPIO read function was given to the block in
 * gpio_wp (below) this function will be called to determine
 * whether a card is present in the MMC slot or not
 * @gpio_wp: read this GPIO pin to see if the card is write protected
 * @gpio_cd: read this GPIO pin to detect card insertion
 * @cd_invert: true if the gpio_cd pin value is active low
 * @capabilities: the capabilities of the block as implemented in
 * this platform, signify anything MMC_CAP_* from mmc/host.h
 * @dma_filter: function used to select an appropriate RX and TX
 * DMA channel to be used for DMA, if and only if you're deploying the
 * generic DMA engine
 * @dma_rx_param: parameter passed to the DMA allocation
 * filter in order to select an appropriate RX channel. If
 * there is a bidirectional RX+TX channel, then just specify
 * this and leave dma_tx_param set to NULL
 * @dma_tx_param: parameter passed to the DMA allocation
 * filter in order to select an appropriate TX channel. If this
 * is NULL the driver will attempt to use the RX channel as a
 * bidirectional channel
 */
struct mmci_platform_data {
	unsigned int f_max;
	unsigned int ocr_mask;
	u32 (*vdd_handler)(struct device *, unsigned int vdd,
			   unsigned char power_mode);
	unsigned int (*status)(struct device *);
	int	gpio_wp;
	int	gpio_cd;
	bool	cd_invert;
	unsigned long capabilities;
	bool (*dma_filter)(struct dma_chan *chan, void *filter_param);
	void *dma_rx_param;
	void *dma_tx_param;
	unsigned int status_irq;
	int (*register_status_notify)(void (*callback)(int card_present, void *dev_id), void *dev_id);
};

#endif
