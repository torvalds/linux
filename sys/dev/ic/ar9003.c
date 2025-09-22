/*	$OpenBSD: ar9003.c,v 1.56 2022/12/27 20:13:03 patrick Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2010 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver for Atheros 802.11a/g/n chipsets.
 * Routines for AR9003 family.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/timeout.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/stdint.h>	/* uintptr_t */
#include <sys/endian.h>

#include <machine/bus.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif
#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_ra.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/athnreg.h>
#include <dev/ic/athnvar.h>

#include <dev/ic/ar9003reg.h>

int	ar9003_attach(struct athn_softc *);
int	ar9003_read_eep_word(struct athn_softc *, uint32_t, uint16_t *);
int	ar9003_read_eep_data(struct athn_softc *, uint32_t, void *, int);
int	ar9003_read_otp_word(struct athn_softc *, uint32_t, uint32_t *);
int	ar9003_read_otp_data(struct athn_softc *, uint32_t, void *, int);
int	ar9003_find_rom(struct athn_softc *);
int	ar9003_restore_rom_block(struct athn_softc *, uint8_t, uint8_t,
	    const uint8_t *, int);
int	ar9003_read_rom(struct athn_softc *);
int	ar9003_gpio_read(struct athn_softc *, int);
void	ar9003_gpio_write(struct athn_softc *, int, int);
void	ar9003_gpio_config_input(struct athn_softc *, int);
void	ar9003_gpio_config_output(struct athn_softc *, int, int);
void	ar9003_rfsilent_init(struct athn_softc *);
int	ar9003_dma_alloc(struct athn_softc *);
void	ar9003_dma_free(struct athn_softc *);
int	ar9003_tx_alloc(struct athn_softc *);
void	ar9003_tx_free(struct athn_softc *);
int	ar9003_rx_alloc(struct athn_softc *, int, int);
void	ar9003_rx_free(struct athn_softc *, int);
void	ar9003_reset_txsring(struct athn_softc *);
void	ar9003_rx_enable(struct athn_softc *);
void	ar9003_rx_radiotap(struct athn_softc *, struct mbuf *,
	    struct ar_rx_status *);
int	ar9003_rx_process(struct athn_softc *, int, struct mbuf_list *);
void	ar9003_rx_intr(struct athn_softc *, int);
int	ar9003_tx_process(struct athn_softc *);
void	ar9003_tx_intr(struct athn_softc *);
int	ar9003_swba_intr(struct athn_softc *);
int	ar9003_intr(struct athn_softc *);
int	ar9003_tx(struct athn_softc *, struct mbuf *, struct ieee80211_node *,
	    int);
void	ar9003_set_rf_mode(struct athn_softc *, struct ieee80211_channel *);
int	ar9003_rf_bus_request(struct athn_softc *);
void	ar9003_rf_bus_release(struct athn_softc *);
void	ar9003_set_phy(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar9003_set_delta_slope(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar9003_enable_antenna_diversity(struct athn_softc *);
void	ar9003_init_baseband(struct athn_softc *);
void	ar9003_disable_phy(struct athn_softc *);
void	ar9003_init_chains(struct athn_softc *);
void	ar9003_set_rxchains(struct athn_softc *);
void	ar9003_read_noisefloor(struct athn_softc *, int16_t *, int16_t *);
void	ar9003_write_noisefloor(struct athn_softc *, int16_t *, int16_t *);
int	ar9003_get_noisefloor(struct athn_softc *);
void	ar9003_apply_noisefloor(struct athn_softc *);
void	ar9003_bb_load_noisefloor(struct athn_softc *);
void	ar9003_do_noisefloor_calib(struct athn_softc *);
void	ar9003_init_noisefloor_calib(struct athn_softc *);
int	ar9003_init_calib(struct athn_softc *);
void	ar9003_do_calib(struct athn_softc *);
void	ar9003_next_calib(struct athn_softc *);
void	ar9003_calib_iq(struct athn_softc *);
int	ar9003_get_iq_corr(struct athn_softc *, int32_t *, int32_t *);
int	ar9003_calib_tx_iq(struct athn_softc *);
void	ar9003_paprd_calib(struct athn_softc *, struct ieee80211_channel *);
int	ar9003_get_desired_txgain(struct athn_softc *, int, int);
void	ar9003_force_txgain(struct athn_softc *, uint32_t);
void	ar9003_set_training_gain(struct athn_softc *, int);
int	ar9003_paprd_tx_tone(struct athn_softc *);
int	ar9003_compute_predistortion(struct athn_softc *, const uint32_t *,
	    const uint32_t *);
void	ar9003_enable_predistorter(struct athn_softc *, int);
void	ar9003_paprd_enable(struct athn_softc *);
void	ar9003_paprd_tx_tone_done(struct athn_softc *);
void	ar9003_write_txpower(struct athn_softc *, int16_t *);
void	ar9003_reset_rx_gain(struct athn_softc *, struct ieee80211_channel *);
void	ar9003_reset_tx_gain(struct athn_softc *, struct ieee80211_channel *);
void	ar9003_hw_init(struct athn_softc *, struct ieee80211_channel *,
	    struct ieee80211_channel *);
void	ar9003_get_lg_tpow(struct athn_softc *, struct ieee80211_channel *,
	    uint8_t, const uint8_t *, const struct ar_cal_target_power_leg *,
	    int, uint8_t *);
void	ar9003_get_ht_tpow(struct athn_softc *, struct ieee80211_channel *,
	    uint8_t, const uint8_t *, const struct ar_cal_target_power_ht *,
	    int, uint8_t *);
void	ar9003_set_noise_immunity_level(struct athn_softc *, int);
void	ar9003_enable_ofdm_weak_signal(struct athn_softc *);
void	ar9003_disable_ofdm_weak_signal(struct athn_softc *);
void	ar9003_set_cck_weak_signal(struct athn_softc *, int);
void	ar9003_set_firstep_level(struct athn_softc *, int);
void	ar9003_set_spur_immunity_level(struct athn_softc *, int);

/* Extern functions. */
void	athn_stop(struct ifnet *, int);
int	athn_interpolate(int, int, int, int, int);
int	athn_txtime(struct athn_softc *, int, int, u_int);
void	athn_inc_tx_trigger_level(struct athn_softc *);
int	athn_tx_pending(struct athn_softc *, int);
void	athn_stop_tx_dma(struct athn_softc *, int);
void	athn_get_delta_slope(uint32_t, uint32_t *, uint32_t *);
void	athn_config_pcie(struct athn_softc *);
void	athn_config_nonpcie(struct athn_softc *);
uint8_t	athn_chan2fbin(struct ieee80211_channel *);


int
ar9003_attach(struct athn_softc *sc)
{
	struct athn_ops *ops = &sc->ops;
	int error;

	/* Set callbacks for AR9003 family. */
	ops->gpio_read = ar9003_gpio_read;
	ops->gpio_write = ar9003_gpio_write;
	ops->gpio_config_input = ar9003_gpio_config_input;
	ops->gpio_config_output = ar9003_gpio_config_output;
	ops->rfsilent_init = ar9003_rfsilent_init;

	ops->dma_alloc = ar9003_dma_alloc;
	ops->dma_free = ar9003_dma_free;
	ops->rx_enable = ar9003_rx_enable;
	ops->intr = ar9003_intr;
	ops->tx = ar9003_tx;

	ops->set_rf_mode = ar9003_set_rf_mode;
	ops->rf_bus_request = ar9003_rf_bus_request;
	ops->rf_bus_release = ar9003_rf_bus_release;
	ops->set_phy = ar9003_set_phy;
	ops->set_delta_slope = ar9003_set_delta_slope;
	ops->enable_antenna_diversity = ar9003_enable_antenna_diversity;
	ops->init_baseband = ar9003_init_baseband;
	ops->disable_phy = ar9003_disable_phy;
	ops->set_rxchains = ar9003_set_rxchains;
	ops->noisefloor_calib = ar9003_do_noisefloor_calib;
	ops->init_noisefloor_calib = ar9003_init_noisefloor_calib;
	ops->get_noisefloor = ar9003_get_noisefloor;
	ops->apply_noisefloor = ar9003_apply_noisefloor;
	ops->do_calib = ar9003_do_calib;
	ops->next_calib = ar9003_next_calib;
	ops->hw_init = ar9003_hw_init;

	ops->set_noise_immunity_level = ar9003_set_noise_immunity_level;
	ops->enable_ofdm_weak_signal = ar9003_enable_ofdm_weak_signal;
	ops->disable_ofdm_weak_signal = ar9003_disable_ofdm_weak_signal;
	ops->set_cck_weak_signal = ar9003_set_cck_weak_signal;
	ops->set_firstep_level = ar9003_set_firstep_level;
	ops->set_spur_immunity_level = ar9003_set_spur_immunity_level;

	/* Set MAC registers offsets. */
	sc->obs_off = AR_OBS;
	sc->gpio_input_en_off = AR_GPIO_INPUT_EN_VAL;

	if (!(sc->flags & ATHN_FLAG_PCIE))
		athn_config_nonpcie(sc);
	else
		athn_config_pcie(sc);

	/* Determine ROM type and location. */
	if ((error = ar9003_find_rom(sc)) != 0) {
		printf("%s: could not find ROM\n", sc->sc_dev.dv_xname);
		return (error);
	}
	/* Read entire ROM content in memory. */
	if ((error = ar9003_read_rom(sc)) != 0) {
		printf("%s: could not read ROM\n", sc->sc_dev.dv_xname);
		return (error);
	}

	/* Determine if it is a non-enterprise AR9003 card. */
	if (AR_READ(sc, AR_ENT_OTP) & AR_ENT_OTP_MPSD)
		sc->flags |= ATHN_FLAG_NON_ENTERPRISE;

	ops->setup(sc);
	return (0);
}

/*
 * Read 16-bit word from EEPROM.
 */
int
ar9003_read_eep_word(struct athn_softc *sc, uint32_t addr, uint16_t *val)
{
	uint32_t reg;
	int ntries;

	reg = AR_READ(sc, AR_EEPROM_OFFSET(addr));
	for (ntries = 0; ntries < 1000; ntries++) {
		reg = AR_READ(sc, AR_EEPROM_STATUS_DATA);
		if (!(reg & (AR_EEPROM_STATUS_DATA_BUSY |
		    AR_EEPROM_STATUS_DATA_PROT_ACCESS))) {
			*val = MS(reg, AR_EEPROM_STATUS_DATA_VAL);
			return (0);
		}
		DELAY(10);
	}
	*val = 0xffff;
	return (ETIMEDOUT);
}

/*
 * Read an arbitrary number of bytes at a specified address in EEPROM.
 * NB: The address may not be 16-bit aligned.
 */
int
ar9003_read_eep_data(struct athn_softc *sc, uint32_t addr, void *buf, int len)
{
	uint8_t *dst = buf;
	uint16_t val;
	int error;

	if (len > 0 && (addr & 1)) {
		/* Deal with non-aligned reads. */
		addr >>= 1;
		error = ar9003_read_eep_word(sc, addr, &val);
		if (error != 0)
			return (error);
		*dst++ = val & 0xff;
		addr--;
		len--;
	} else
		addr >>= 1;
	for (; len >= 2; addr--, len -= 2) {
		error = ar9003_read_eep_word(sc, addr, &val);
		if (error != 0)
			return (error);
		*dst++ = val >> 8;
		*dst++ = val & 0xff;
	}
	if (len > 0) {
		error = ar9003_read_eep_word(sc, addr, &val);
		if (error != 0)
			return (error);
		*dst++ = val >> 8;
	}
	return (0);
}

/*
 * Read 32-bit word from OTPROM.
 */
int
ar9003_read_otp_word(struct athn_softc *sc, uint32_t addr, uint32_t *val)
{
	uint32_t reg;
	int ntries;

	reg = AR_READ(sc, AR_OTP_BASE(addr));
	for (ntries = 0; ntries < 1000; ntries++) {
		reg = AR_READ(sc, AR_OTP_STATUS);
		if (MS(reg, AR_OTP_STATUS_TYPE) == AR_OTP_STATUS_VALID) {
			*val = AR_READ(sc, AR_OTP_READ_DATA);
			return (0);
		}
		DELAY(10);
	}
	return (ETIMEDOUT);
}

/*
 * Read an arbitrary number of bytes at a specified address in OTPROM.
 * NB: The address may not be 32-bit aligned.
 */
int
ar9003_read_otp_data(struct athn_softc *sc, uint32_t addr, void *buf, int len)
{
	uint8_t *dst = buf;
	uint32_t val;
	int error;

	/* NB: not optimal for non-aligned reads, but correct. */
	for (; len > 0; addr--, len--) {
		error = ar9003_read_otp_word(sc, addr >> 2, &val);
		if (error != 0)
			return (error);
		*dst++ = (val >> ((addr & 3) * 8)) & 0xff;
	}
	return (0);
}

/*
 * Determine if the chip has an external EEPROM or an OTPROM and its size.
 */
int
ar9003_find_rom(struct athn_softc *sc)
{
	struct athn_ops *ops = &sc->ops;
	uint32_t hdr;
	int error;

	/* Try EEPROM. */
	ops->read_rom_data = ar9003_read_eep_data;

	sc->eep_size = AR_SREV_9485(sc) ? 4096 : 1024;
	sc->eep_base = sc->eep_size - 1;
	error = ops->read_rom_data(sc, sc->eep_base, &hdr, sizeof(hdr));
	if (error == 0 && hdr != 0 && hdr != 0xffffffff)
		return (0);

	sc->eep_size = 512;
	sc->eep_base = sc->eep_size - 1;
	error = ops->read_rom_data(sc, sc->eep_base, &hdr, sizeof(hdr));
	if (error == 0 && hdr != 0 && hdr != 0xffffffff)
		return (0);

	/* Try OTPROM. */
	ops->read_rom_data = ar9003_read_otp_data;

	sc->eep_size = 1024;
	sc->eep_base = sc->eep_size - 1;
	error = ops->read_rom_data(sc, sc->eep_base, &hdr, sizeof(hdr));
	if (error == 0 && hdr != 0 && hdr != 0xffffffff)
		return (0);

	sc->eep_size = 512;
	sc->eep_base = sc->eep_size - 1;
	error = ops->read_rom_data(sc, sc->eep_base, &hdr, sizeof(hdr));
	if (error == 0 && hdr != 0 && hdr != 0xffffffff)
		return (0);

	return (EIO);	/* Not found. */
}

int
ar9003_restore_rom_block(struct athn_softc *sc, uint8_t alg, uint8_t ref,
    const uint8_t *buf, int len)
{
	const uint8_t *def, *ptr, *end;
	uint8_t *eep = sc->eep;
	int off, clen;

	if (alg == AR_EEP_COMPRESS_BLOCK) {
		/* Block contains chunks that shadow ROM template. */
		def = sc->ops.get_rom_template(sc, ref);
		if (def == NULL) {
			DPRINTF(("unknown template image %d\n", ref));
			return (EINVAL);
		}
		/* Start with template. */
		memcpy(eep, def, sc->eep_size);
		/* Shadow template with chunks. */
		off = 0;	/* Offset in ROM image. */
		ptr = buf;	/* Offset in block. */
		end = buf + len;
		/* Process chunks. */
		while (ptr + 2 <= end) {
			off += *ptr++;	/* Gap with previous chunk. */
			clen = *ptr++;	/* Chunk length. */
			/* Make sure block is large enough. */
			if (ptr + clen > end)
				return (EINVAL);
			/* Make sure chunk fits in ROM image. */
			if (off + clen > sc->eep_size)
				return (EINVAL);
			/* Restore chunk. */
			DPRINTFN(2, ("ROM chunk @%d/%d\n", off, clen));
			memcpy(&eep[off], ptr, clen);
			ptr += clen;
			off += clen;
		}
	} else if (alg == AR_EEP_COMPRESS_NONE) {
		/* Block contains full ROM image. */
		if (len != sc->eep_size) {
			DPRINTF(("block length mismatch %d\n", len));
			return (EINVAL);
		}
		memcpy(eep, buf, len);
	}
	return (0);
}

int
ar9003_read_rom(struct athn_softc *sc)
{
	struct athn_ops *ops = &sc->ops;
	uint8_t *buf, *ptr, alg, ref;
	uint16_t sum, rsum;
	uint32_t hdr;
	int error, addr, len, i, j;

	/* Allocate space to store ROM in host memory. */
	sc->eep = malloc(sc->eep_size, M_DEVBUF, M_NOWAIT);
	if (sc->eep == NULL)
		return (ENOMEM);

	/* Allocate temporary buffer to store ROM blocks. */
	buf = malloc(2048, M_DEVBUF, M_NOWAIT);
	if (buf == NULL)
		return (ENOMEM);

	/* Restore vendor-specified ROM blocks. */
	addr = sc->eep_base;
	for (i = 0; i < 100; i++) {
		/* Read block header. */
		error = ops->read_rom_data(sc, addr, &hdr, sizeof(hdr));
		if (error != 0)
			break;
		if (hdr == 0 || hdr == 0xffffffff)
			break;
		addr -= sizeof(hdr);

		/* Extract bits from header. */
		ptr = (uint8_t *)&hdr;
		alg = (ptr[0] & 0xe0) >> 5;
		ref = (ptr[1] & 0x80) >> 2 | (ptr[0] & 0x1f);
		len = (ptr[1] & 0x7f) << 4 | (ptr[2] & 0xf0) >> 4;
		DPRINTFN(2, ("ROM block %d: alg=%d ref=%d len=%d\n",
		    i, alg, ref, len));

		/* Read block data (len <= 0x7ff). */
		error = ops->read_rom_data(sc, addr, buf, len);
		if (error != 0)
			break;
		addr -= len;

		/* Read block checksum. */
		error = ops->read_rom_data(sc, addr, &sum, sizeof(sum));
		if (error != 0)
			break;
		addr -= sizeof(sum);

		/* Compute block checksum. */
		rsum = 0;
		for (j = 0; j < len; j++)
			rsum += buf[j];
		/* Compare to that in ROM. */
		if (letoh16(sum) != rsum) {
			DPRINTF(("bad block checksum 0x%x/0x%x\n",
			    letoh16(sum), rsum));
			continue;	/* Skip bad block. */
		}
		/* Checksum is correct, restore block. */
		ar9003_restore_rom_block(sc, alg, ref, buf, len);
	}
#if BYTE_ORDER == BIG_ENDIAN
	/* NB: ROM is always little endian. */
	if (error == 0)
		ops->swap_rom(sc);
#endif
	free(buf, M_DEVBUF, 0);
	return (error);
}

/*
 * Access to General Purpose Input/Output ports.
 */
int
ar9003_gpio_read(struct athn_softc *sc, int pin)
{
	KASSERT(pin < sc->ngpiopins);
	return (((AR_READ(sc, AR_GPIO_IN) & AR9300_GPIO_IN_VAL) &
	    (1 << pin)) != 0);
}

void
ar9003_gpio_write(struct athn_softc *sc, int pin, int set)
{
	uint32_t reg;

	KASSERT(pin < sc->ngpiopins);
	reg = AR_READ(sc, AR_GPIO_IN_OUT);
	if (set)
		reg |= 1 << pin;
	else
		reg &= ~(1 << pin);
	AR_WRITE(sc, AR_GPIO_IN_OUT, reg);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_gpio_config_input(struct athn_softc *sc, int pin)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_GPIO_OE_OUT);
	reg &= ~(AR_GPIO_OE_OUT_DRV_M << (pin * 2));
	reg |= AR_GPIO_OE_OUT_DRV_NO << (pin * 2);
	AR_WRITE(sc, AR_GPIO_OE_OUT, reg);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_gpio_config_output(struct athn_softc *sc, int pin, int type)
{
	uint32_t reg;
	int mux, off;

	mux = pin / 6;
	off = pin % 6;

	reg = AR_READ(sc, AR_GPIO_OUTPUT_MUX(mux));
	reg &= ~(0x1f << (off * 5));
	reg |= (type & 0x1f) << (off * 5);
	AR_WRITE(sc, AR_GPIO_OUTPUT_MUX(mux), reg);

	reg = AR_READ(sc, AR_GPIO_OE_OUT);
	reg &= ~(AR_GPIO_OE_OUT_DRV_M << (pin * 2));
	reg |= AR_GPIO_OE_OUT_DRV_ALL << (pin * 2);
	AR_WRITE(sc, AR_GPIO_OE_OUT, reg);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_rfsilent_init(struct athn_softc *sc)
{
	uint32_t reg;

	/* Configure hardware radio switch. */
	AR_SETBITS(sc, AR_GPIO_INPUT_EN_VAL, AR_GPIO_INPUT_EN_VAL_RFSILENT_BB);
	reg = AR_READ(sc, AR_GPIO_INPUT_MUX2);
	reg = RW(reg, AR_GPIO_INPUT_MUX2_RFSILENT, 0);
	AR_WRITE(sc, AR_GPIO_INPUT_MUX2, reg);
	ar9003_gpio_config_input(sc, sc->rfsilent_pin);
	AR_SETBITS(sc, AR_PHY_TEST, AR_PHY_TEST_RFSILENT_BB);
	if (!(sc->flags & ATHN_FLAG_RFSILENT_REVERSED)) {
		AR_SETBITS(sc, AR_GPIO_INTR_POL,
		    AR_GPIO_INTR_POL_PIN(sc->rfsilent_pin));
	}
	AR_WRITE_BARRIER(sc);
}

int
ar9003_dma_alloc(struct athn_softc *sc)
{
	int error;

	error = ar9003_tx_alloc(sc);
	if (error != 0)
		return (error);

	error = ar9003_rx_alloc(sc, ATHN_QID_LP, AR9003_RX_LP_QDEPTH);
	if (error != 0)
		return (error);

	error = ar9003_rx_alloc(sc, ATHN_QID_HP, AR9003_RX_HP_QDEPTH);
	if (error != 0)
		return (error);

	return (0);
}

void
ar9003_dma_free(struct athn_softc *sc)
{
	ar9003_tx_free(sc);
	ar9003_rx_free(sc, ATHN_QID_LP);
	ar9003_rx_free(sc, ATHN_QID_HP);
}

int
ar9003_tx_alloc(struct athn_softc *sc)
{
	struct athn_tx_buf *bf;
	bus_size_t size;
	int error, nsegs, i;

	/*
	 * Allocate Tx status ring.
	 */
	size = AR9003_NTXSTATUS * sizeof(struct ar_tx_status);

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &sc->txsmap);
	if (error != 0)
		goto fail;

	error = bus_dmamem_alloc(sc->sc_dmat, size, 4, 0, &sc->txsseg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0)
		goto fail;

	error = bus_dmamem_map(sc->sc_dmat, &sc->txsseg, 1, size,
	    (caddr_t *)&sc->txsring, BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (error != 0)
		goto fail;

	error = bus_dmamap_load_raw(sc->sc_dmat, sc->txsmap, &sc->txsseg,
	    1, size, BUS_DMA_NOWAIT | BUS_DMA_READ);
	if (error != 0)
		goto fail;

	/*
	 * Allocate a pool of Tx descriptors shared between all Tx queues.
	 */
	size = ATHN_NTXBUFS * sizeof(struct ar_tx_desc);

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &sc->map);
	if (error != 0)
		goto fail;

	error = bus_dmamem_alloc(sc->sc_dmat, size, 4, 0, &sc->seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0)
		goto fail;

	error = bus_dmamem_map(sc->sc_dmat, &sc->seg, 1, size,
	    (caddr_t *)&sc->descs, BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (error != 0)
		goto fail;

	error = bus_dmamap_load_raw(sc->sc_dmat, sc->map, &sc->seg, 1, size,
	    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
	if (error != 0)
		goto fail;

	SIMPLEQ_INIT(&sc->txbufs);
	for (i = 0; i < ATHN_NTXBUFS; i++) {
		bf = &sc->txpool[i];

		error = bus_dmamap_create(sc->sc_dmat, ATHN_TXBUFSZ,
		    AR9003_MAX_SCATTER, ATHN_TXBUFSZ, 0, BUS_DMA_NOWAIT,
		    &bf->bf_map);
		if (error != 0) {
			printf("%s: could not create Tx buf DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		bf->bf_descs = &((struct ar_tx_desc *)sc->descs)[i];
		bf->bf_daddr = sc->map->dm_segs[0].ds_addr +
		    i * sizeof(struct ar_tx_desc);

		SIMPLEQ_INSERT_TAIL(&sc->txbufs, bf, bf_list);
	}
	return (0);
 fail:
	ar9003_tx_free(sc);
	return (error);
}

void
ar9003_tx_free(struct athn_softc *sc)
{
	struct athn_tx_buf *bf;
	int i;

	for (i = 0; i < ATHN_NTXBUFS; i++) {
		bf = &sc->txpool[i];

		if (bf->bf_map != NULL)
			bus_dmamap_destroy(sc->sc_dmat, bf->bf_map);
	}
	/* Free Tx descriptors. */
	if (sc->map != NULL) {
		if (sc->descs != NULL) {
			bus_dmamap_unload(sc->sc_dmat, sc->map);
			bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->descs,
			    ATHN_NTXBUFS * sizeof(struct ar_tx_desc));
			bus_dmamem_free(sc->sc_dmat, &sc->seg, 1);
		}
		bus_dmamap_destroy(sc->sc_dmat, sc->map);
	}
	/* Free Tx status ring. */
	if (sc->txsmap != NULL) {
		if (sc->txsring != NULL) {
			bus_dmamap_unload(sc->sc_dmat, sc->txsmap);
			bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->txsring,
			     AR9003_NTXSTATUS * sizeof(struct ar_tx_status));
			bus_dmamem_free(sc->sc_dmat, &sc->txsseg, 1);
		}
		bus_dmamap_destroy(sc->sc_dmat, sc->txsmap);
	}
}

int
ar9003_rx_alloc(struct athn_softc *sc, int qid, int count)
{
	struct athn_rxq *rxq = &sc->rxq[qid];
	struct athn_rx_buf *bf;
	struct ar_rx_status *ds;
	int error, i;

	rxq->bf = mallocarray(count, sizeof(*bf), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (rxq->bf == NULL)
		return (ENOMEM);

	rxq->count = count;

	for (i = 0; i < rxq->count; i++) {
		bf = &rxq->bf[i];

		error = bus_dmamap_create(sc->sc_dmat, ATHN_RXBUFSZ, 1,
		    ATHN_RXBUFSZ, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &bf->bf_map);
		if (error != 0) {
			printf("%s: could not create Rx buf DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
		/*
		 * Assumes MCLGETL returns cache-line-size aligned buffers.
		 */
		bf->bf_m = MCLGETL(NULL, M_DONTWAIT, ATHN_RXBUFSZ);
		if (bf->bf_m == NULL) {
			printf("%s: could not allocate Rx mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOBUFS;
			goto fail;
		}

		error = bus_dmamap_load(sc->sc_dmat, bf->bf_map,
		    mtod(bf->bf_m, void *), ATHN_RXBUFSZ, NULL,
		    BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: could not DMA map Rx buffer\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		ds = mtod(bf->bf_m, struct ar_rx_status *);
		memset(ds, 0, sizeof(*ds));
		bf->bf_desc = ds;
		bf->bf_daddr = bf->bf_map->dm_segs[0].ds_addr;

		bus_dmamap_sync(sc->sc_dmat, bf->bf_map, 0, ATHN_RXBUFSZ,
		    BUS_DMASYNC_PREREAD);
	}
	return (0);
 fail:
	ar9003_rx_free(sc, qid);
	return (error);
}

void
ar9003_rx_free(struct athn_softc *sc, int qid)
{
	struct athn_rxq *rxq = &sc->rxq[qid];
	struct athn_rx_buf *bf;
	int i;

	if (rxq->bf == NULL)
		return;
	for (i = 0; i < rxq->count; i++) {
		bf = &rxq->bf[i];

		if (bf->bf_map != NULL)
			bus_dmamap_destroy(sc->sc_dmat, bf->bf_map);
		m_freem(bf->bf_m);
	}
	free(rxq->bf, M_DEVBUF, 0);
}

void
ar9003_reset_txsring(struct athn_softc *sc)
{
	sc->txscur = 0;
	memset(sc->txsring, 0, AR9003_NTXSTATUS * sizeof(struct ar_tx_status));
	AR_WRITE(sc, AR_Q_STATUS_RING_START,
	    sc->txsmap->dm_segs[0].ds_addr);
	AR_WRITE(sc, AR_Q_STATUS_RING_END,
	    sc->txsmap->dm_segs[0].ds_addr + sc->txsmap->dm_segs[0].ds_len);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_rx_enable(struct athn_softc *sc)
{
	struct athn_rxq *rxq;
	struct athn_rx_buf *bf;
	struct ar_rx_status *ds;
	uint32_t reg;
	int qid, i;

	reg = AR_READ(sc, AR_RXBP_THRESH);
	reg = RW(reg, AR_RXBP_THRESH_HP, 1);
	reg = RW(reg, AR_RXBP_THRESH_LP, 1);
	AR_WRITE(sc, AR_RXBP_THRESH, reg);

	/* Set Rx buffer size. */
	AR_WRITE(sc, AR_DATABUF_SIZE, ATHN_RXBUFSZ - sizeof(*ds));

	for (qid = 0; qid < 2; qid++) {
		rxq = &sc->rxq[qid];

		/* Setup Rx status descriptors. */
		SIMPLEQ_INIT(&rxq->head);
		for (i = 0; i < rxq->count; i++) {
			bf = &rxq->bf[i];
			ds = bf->bf_desc;

			memset(ds, 0, sizeof(*ds));
			if (qid == ATHN_QID_LP)
				AR_WRITE(sc, AR_LP_RXDP, bf->bf_daddr);
			else
				AR_WRITE(sc, AR_HP_RXDP, bf->bf_daddr);
			AR_WRITE_BARRIER(sc);
			SIMPLEQ_INSERT_TAIL(&rxq->head, bf, bf_list);
		}
	}
	/* Enable Rx. */
	AR_WRITE(sc, AR_CR, 0);
	AR_WRITE_BARRIER(sc);
}

#if NBPFILTER > 0
void
ar9003_rx_radiotap(struct athn_softc *sc, struct mbuf *m,
    struct ar_rx_status *ds)
{
#define IEEE80211_RADIOTAP_F_SHORTGI	0x80	/* XXX from FBSD */

	struct athn_rx_radiotap_header *tap = &sc->sc_rxtap;
	struct ieee80211com *ic = &sc->sc_ic;
	uint64_t tsf;
	uint32_t tstamp;
	uint8_t rate;

	/* Extend the 15-bit timestamp from Rx status to 64-bit TSF. */
	tstamp = ds->ds_status3;
	tsf = AR_READ(sc, AR_TSF_U32);
	tsf = tsf << 32 | AR_READ(sc, AR_TSF_L32);
	if ((tsf & 0x7fff) < tstamp)
		tsf -= 0x8000;
	tsf = (tsf & ~0x7fff) | tstamp;

	tap->wr_flags = IEEE80211_RADIOTAP_F_FCS;
	tap->wr_tsft = htole64(tsf);
	tap->wr_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
	tap->wr_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
	tap->wr_dbm_antsignal = MS(ds->ds_status5, AR_RXS5_RSSI_COMBINED);
	/* XXX noise. */
	tap->wr_antenna = MS(ds->ds_status4, AR_RXS4_ANTENNA);
	tap->wr_rate = 0;	/* In case it can't be found below. */
	rate = MS(ds->ds_status1, AR_RXS1_RATE);
	if (rate & 0x80) {		/* HT. */
		/* Bit 7 set means HT MCS instead of rate. */
		tap->wr_rate = rate;
		if (!(ds->ds_status4 & AR_RXS4_GI))
			tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTGI;

	} else if (rate & 0x10) {	/* CCK. */
		if (rate & 0x04)
			tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
		switch (rate & ~0x14) {
		case 0xb: tap->wr_rate =   2; break;
		case 0xa: tap->wr_rate =   4; break;
		case 0x9: tap->wr_rate =  11; break;
		case 0x8: tap->wr_rate =  22; break;
		}
	} else {			/* OFDM. */
		switch (rate) {
		case 0xb: tap->wr_rate =  12; break;
		case 0xf: tap->wr_rate =  18; break;
		case 0xa: tap->wr_rate =  24; break;
		case 0xe: tap->wr_rate =  36; break;
		case 0x9: tap->wr_rate =  48; break;
		case 0xd: tap->wr_rate =  72; break;
		case 0x8: tap->wr_rate =  96; break;
		case 0xc: tap->wr_rate = 108; break;
		}
	}
	bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_rxtap_len, m, BPF_DIRECTION_IN);
}
#endif

int
ar9003_rx_process(struct athn_softc *sc, int qid, struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct athn_rxq *rxq = &sc->rxq[qid];
	struct athn_rx_buf *bf;
	struct ar_rx_status *ds;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct mbuf *m, *m1;
	int error, len;

	bf = SIMPLEQ_FIRST(&rxq->head);
	if (__predict_false(bf == NULL)) {	/* Should not happen. */
		printf("%s: Rx queue is empty!\n", sc->sc_dev.dv_xname);
		return (ENOENT);
	}
	bus_dmamap_sync(sc->sc_dmat, bf->bf_map, 0, ATHN_RXBUFSZ,
	    BUS_DMASYNC_POSTREAD);

	ds = mtod(bf->bf_m, struct ar_rx_status *);
	if (!(ds->ds_status11 & AR_RXS11_DONE))
		return (EBUSY);

	/* Check that it is a valid Rx status descriptor. */
	if ((ds->ds_info & (AR_RXI_DESC_ID_M | AR_RXI_DESC_TX |
	    AR_RXI_CTRL_STAT)) != SM(AR_RXI_DESC_ID, AR_VENDOR_ATHEROS))
		goto skip;

	if (!(ds->ds_status11 & AR_RXS11_FRAME_OK)) {
		if (ds->ds_status11 & AR_RXS11_CRC_ERR)
			DPRINTFN(6, ("CRC error\n"));
		else if (ds->ds_status11 & AR_RXS11_PHY_ERR)
			DPRINTFN(6, ("PHY error=0x%x\n",
			    MS(ds->ds_status11, AR_RXS11_PHY_ERR_CODE)));
		else if (ds->ds_status11 & AR_RXS11_DECRYPT_CRC_ERR)
			DPRINTFN(6, ("Decryption CRC error\n"));
		else if (ds->ds_status11 & AR_RXS11_MICHAEL_ERR) {
			DPRINTFN(2, ("Michael MIC failure\n"));
			/* Report Michael MIC failures to net80211. */
			ic->ic_stats.is_rx_locmicfail++;
			ieee80211_michael_mic_failure(ic, 0);
			/*
			 * XXX Check that it is not a control frame
			 * (invalid MIC failures on valid ctl frames).
			 */
		}
		ifp->if_ierrors++;
		goto skip;
	}

	len = MS(ds->ds_status2, AR_RXS2_DATA_LEN);
	if (__predict_false(len < IEEE80211_MIN_LEN ||
	    len > ATHN_RXBUFSZ - sizeof(*ds))) {
		DPRINTF(("corrupted descriptor length=%d\n", len));
		ifp->if_ierrors++;
		goto skip;
	}

	/* Allocate a new Rx buffer. */
	m1 = MCLGETL(NULL, M_DONTWAIT, ATHN_RXBUFSZ);
	if (__predict_false(m1 == NULL)) {
		ic->ic_stats.is_rx_nombuf++;
		ifp->if_ierrors++;
		goto skip;
	}

	/* Unmap the old Rx buffer. */
	bus_dmamap_unload(sc->sc_dmat, bf->bf_map);

	/* Map the new Rx buffer. */
	error = bus_dmamap_load(sc->sc_dmat, bf->bf_map, mtod(m1, void *),
	    ATHN_RXBUFSZ, NULL, BUS_DMA_NOWAIT | BUS_DMA_READ);
	if (__predict_false(error != 0)) {
		m_freem(m1);

		/* Remap the old Rx buffer or panic. */
		error = bus_dmamap_load(sc->sc_dmat, bf->bf_map,
		    mtod(bf->bf_m, void *), ATHN_RXBUFSZ, NULL,
		    BUS_DMA_NOWAIT | BUS_DMA_READ);
		KASSERT(error != 0);
		bf->bf_daddr = bf->bf_map->dm_segs[0].ds_addr;
		ifp->if_ierrors++;
		goto skip;
	}
	bf->bf_desc = mtod(m1, struct ar_rx_status *);
	bf->bf_daddr = bf->bf_map->dm_segs[0].ds_addr;

	m = bf->bf_m;
	bf->bf_m = m1;

	/* Finalize mbuf. */
	/* Strip Rx status descriptor from head. */
	m->m_data = (caddr_t)&ds[1];
	m->m_pkthdr.len = m->m_len = len;

	/* Grab a reference to the source node. */
	wh = mtod(m, struct ieee80211_frame *);
	ni = ieee80211_find_rxnode(ic, wh);

	/* Remove any HW padding after the 802.11 header. */
	if (!(wh->i_fc[0] & IEEE80211_FC0_TYPE_CTL)) {
		u_int hdrlen = ieee80211_get_hdrlen(wh);
		if (hdrlen & 3) {
			memmove((caddr_t)wh + 2, wh, hdrlen);
			m_adj(m, 2);
		}
	}
#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL))
		ar9003_rx_radiotap(sc, m, ds);
#endif
	/* Trim 802.11 FCS after radiotap. */
	m_adj(m, -IEEE80211_CRC_LEN);

	/* Send the frame to the 802.11 layer. */
	memset(&rxi, 0, sizeof(rxi));
	rxi.rxi_rssi = MS(ds->ds_status5, AR_RXS5_RSSI_COMBINED);
	rxi.rxi_tstamp = ds->ds_status3;
	ieee80211_inputm(ifp, m, ni, &rxi, ml);

	/* Node is no longer needed. */
	ieee80211_release_node(ic, ni);

 skip:
	/* Unlink this descriptor from head. */
	SIMPLEQ_REMOVE_HEAD(&rxq->head, bf_list);
	memset(bf->bf_desc, 0, sizeof(*ds));

	/* Re-use this descriptor and link it to tail. */
	bus_dmamap_sync(sc->sc_dmat, bf->bf_map, 0, ATHN_RXBUFSZ,
	    BUS_DMASYNC_PREREAD);

	if (qid == ATHN_QID_LP)
		AR_WRITE(sc, AR_LP_RXDP, bf->bf_daddr);
	else
		AR_WRITE(sc, AR_HP_RXDP, bf->bf_daddr);
	AR_WRITE_BARRIER(sc);
	SIMPLEQ_INSERT_TAIL(&rxq->head, bf, bf_list);

	/* Re-enable Rx. */
	AR_WRITE(sc, AR_CR, 0);
	AR_WRITE_BARRIER(sc);
	return (0);
}

void
ar9003_rx_intr(struct athn_softc *sc, int qid)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	while (ar9003_rx_process(sc, qid, &ml) == 0);

	if_input(ifp, &ml);
}

int
ar9003_tx_process(struct athn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct athn_txq *txq;
	struct athn_node *an;
	struct athn_tx_buf *bf;
	struct ar_tx_status *ds;
	uint8_t qid, failcnt;

	ds = &((struct ar_tx_status *)sc->txsring)[sc->txscur];
	if (!(ds->ds_status8 & AR_TXS8_DONE))
		return (EBUSY);

	sc->txscur = (sc->txscur + 1) % AR9003_NTXSTATUS;

	/* Check that it is a valid Tx status descriptor. */
	if ((ds->ds_info & (AR_TXI_DESC_ID_M | AR_TXI_DESC_TX)) !=
	    (SM(AR_TXI_DESC_ID, AR_VENDOR_ATHEROS) | AR_TXI_DESC_TX)) {
		memset(ds, 0, sizeof(*ds));
		return (0);
	}
	/* Retrieve the queue that was used to send this PDU. */
	qid = MS(ds->ds_info, AR_TXI_QCU_NUM);
	txq = &sc->txq[qid];

	bf = SIMPLEQ_FIRST(&txq->head);
	if (bf == NULL || bf == txq->wait) {
		memset(ds, 0, sizeof(*ds));
		return (0);
	}
	SIMPLEQ_REMOVE_HEAD(&txq->head, bf_list);

	sc->sc_tx_timer = 0;

	if (ds->ds_status3 & AR_TXS3_EXCESSIVE_RETRIES)
		ifp->if_oerrors++;

	if (ds->ds_status3 & AR_TXS3_UNDERRUN)
		athn_inc_tx_trigger_level(sc);

	/* Wakeup PA predistortion state machine. */
	if (bf->bf_txflags & ATHN_TXFLAG_PAPRD)
		ar9003_paprd_tx_tone_done(sc);

	an = (struct athn_node *)bf->bf_ni;
	/*
	 * NB: the data fail count contains the number of un-acked tries
	 * for the final series used.  We must add the number of tries for
	 * each series that was fully processed.
	 */
	failcnt  = MS(ds->ds_status3, AR_TXS3_DATA_FAIL_CNT);
	/* NB: Assume two tries per series. */
	failcnt += MS(ds->ds_status8, AR_TXS8_FINAL_IDX) * 2;

	/* Update rate control statistics. */
	an->amn.amn_txcnt++;
	if (failcnt > 0)
		an->amn.amn_retrycnt++;

	DPRINTFN(5, ("Tx done qid=%d status3=%d fail count=%d\n",
	    qid, ds->ds_status3, failcnt));

	/* Reset Tx status descriptor. */
	memset(ds, 0, sizeof(*ds));

	/* Unmap Tx buffer. */
	bus_dmamap_sync(sc->sc_dmat, bf->bf_map, 0, bf->bf_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, bf->bf_map);

	m_freem(bf->bf_m);
	bf->bf_m = NULL;
	ieee80211_release_node(ic, bf->bf_ni);
	bf->bf_ni = NULL;

	/* Link Tx buffer back to global free list. */
	SIMPLEQ_INSERT_TAIL(&sc->txbufs, bf, bf_list);

	/* Queue buffers that are waiting if there is new room. */
	if (--txq->queued < AR9003_TX_QDEPTH && txq->wait != NULL) {
		AR_WRITE(sc, AR_QTXDP(qid), txq->wait->bf_daddr);
		AR_WRITE_BARRIER(sc);
		txq->wait = SIMPLEQ_NEXT(txq->wait, bf_list);
	}
	return (0);
}

void
ar9003_tx_intr(struct athn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	while (ar9003_tx_process(sc) == 0);

	if (!SIMPLEQ_EMPTY(&sc->txbufs)) {
		ifq_clr_oactive(&ifp->if_snd);
		ifp->if_start(ifp);
	}
}

#ifndef IEEE80211_STA_ONLY
/*
 * Process Software Beacon Alert interrupts.
 */
int
ar9003_swba_intr(struct athn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_node *ni = ic->ic_bss;
	struct athn_tx_buf *bf = sc->bcnbuf;
	struct ieee80211_frame *wh;
	struct ar_tx_desc *ds;
	struct mbuf *m;
	uint32_t sum;
	uint8_t ridx, hwrate;
	int error, totlen;

	if (ic->ic_tim_mcast_pending &&
	    mq_empty(&ni->ni_savedq) &&
	    SIMPLEQ_EMPTY(&sc->txq[ATHN_QID_CAB].head))
		ic->ic_tim_mcast_pending = 0;

	if (ic->ic_dtim_count == 0)
		ic->ic_dtim_count = ic->ic_dtim_period - 1;
	else
		ic->ic_dtim_count--;

	/* Make sure previous beacon has been sent. */
	if (athn_tx_pending(sc, ATHN_QID_BEACON)) {
		DPRINTF(("beacon stuck\n"));
		return (EBUSY);
	}
	/* Get new beacon. */
	m = ieee80211_beacon_alloc(ic, ic->ic_bss);
	if (__predict_false(m == NULL))
		return (ENOBUFS);
	/* Assign sequence number. */
	wh = mtod(m, struct ieee80211_frame *);
	*(uint16_t *)&wh->i_seq[0] =
	    htole16(ic->ic_bss->ni_txseq << IEEE80211_SEQ_SEQ_SHIFT);
	ic->ic_bss->ni_txseq++;

	/* Unmap and free old beacon if any. */
	if (__predict_true(bf->bf_m != NULL)) {
		bus_dmamap_sync(sc->sc_dmat, bf->bf_map, 0,
		    bf->bf_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, bf->bf_map);
		m_freem(bf->bf_m);
		bf->bf_m = NULL;
	}
	/* DMA map new beacon. */
	error = bus_dmamap_load_mbuf(sc->sc_dmat, bf->bf_map, m,
	    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
	if (__predict_false(error != 0)) {
		m_freem(m);
		return (error);
	}
	bf->bf_m = m;

	/* Setup Tx descriptor (simplified ar9003_tx()). */
	ds = bf->bf_descs;
	memset(ds, 0, sizeof(*ds));

	ds->ds_info =
	    SM(AR_TXI_DESC_ID, AR_VENDOR_ATHEROS) |
	    SM(AR_TXI_DESC_NDWORDS, 23) |
	    SM(AR_TXI_QCU_NUM, ATHN_QID_BEACON) |
	    AR_TXI_DESC_TX | AR_TXI_CTRL_STAT;

	totlen = m->m_pkthdr.len + IEEE80211_CRC_LEN;
	ds->ds_ctl11 = SM(AR_TXC11_FRAME_LEN, totlen);
	ds->ds_ctl11 |= SM(AR_TXC11_XMIT_POWER, AR_MAX_RATE_POWER);
	ds->ds_ctl12 = SM(AR_TXC12_FRAME_TYPE, AR_FRAME_TYPE_BEACON);
	ds->ds_ctl12 |= AR_TXC12_NO_ACK;
	ds->ds_ctl17 = SM(AR_TXC17_ENCR_TYPE, AR_ENCR_TYPE_CLEAR);

	/* Write number of tries. */
	ds->ds_ctl13 = SM(AR_TXC13_XMIT_DATA_TRIES0, 1);

	/* Write Tx rate. */
	ridx = (ic->ic_curmode == IEEE80211_MODE_11A) ?
	    ATHN_RIDX_OFDM6 : ATHN_RIDX_CCK1;
	hwrate = athn_rates[ridx].hwrate;
	ds->ds_ctl14 = SM(AR_TXC14_XMIT_RATE0, hwrate);

	/* Write Tx chains. */
	ds->ds_ctl18 = SM(AR_TXC18_CHAIN_SEL0, sc->txchainmask);

	ds->ds_segs[0].ds_data = bf->bf_map->dm_segs[0].ds_addr;
	/* Segment length must be a multiple of 4. */
	ds->ds_segs[0].ds_ctl |= SM(AR_TXC_BUF_LEN,
	    (bf->bf_map->dm_segs[0].ds_len + 3) & ~3);
	/* Compute Tx descriptor checksum. */
	sum = ds->ds_info;
	sum += ds->ds_segs[0].ds_data;
	sum += ds->ds_segs[0].ds_ctl;
	sum = (sum >> 16) + (sum & 0xffff);
	ds->ds_ctl10 = SM(AR_TXC10_PTR_CHK_SUM, sum);

	bus_dmamap_sync(sc->sc_dmat, bf->bf_map, 0, bf->bf_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	/* Stop Tx DMA before putting the new beacon on the queue. */
	athn_stop_tx_dma(sc, ATHN_QID_BEACON);

	AR_WRITE(sc, AR_QTXDP(ATHN_QID_BEACON), bf->bf_daddr);

	for(;;) {
		if (SIMPLEQ_EMPTY(&sc->txbufs))
			break;

		m = mq_dequeue(&ni->ni_savedq);
		if (m == NULL)
			break;
		if (!mq_empty(&ni->ni_savedq)) {
			/* more queued frames, set the more data bit */
			wh = mtod(m, struct ieee80211_frame *);
			wh->i_fc[1] |= IEEE80211_FC1_MORE_DATA;
		}
		
		if (sc->ops.tx(sc, m, ni, ATHN_TXFLAG_CAB) != 0) {
			ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			break;
		}
	}

	/* Kick Tx. */
	AR_WRITE(sc, AR_Q_TXE, 1 << ATHN_QID_BEACON);
	AR_WRITE_BARRIER(sc);
	return (0);
}
#endif

int
ar9003_intr(struct athn_softc *sc)
{
	uint32_t intr, intr2, intr5, sync;

	/* Get pending interrupts. */
	intr = AR_READ(sc, AR_INTR_ASYNC_CAUSE);
	if (!(intr & AR_INTR_MAC_IRQ) || intr == AR_INTR_SPURIOUS) {
		intr = AR_READ(sc, AR_INTR_SYNC_CAUSE);
		if (intr == AR_INTR_SPURIOUS || (intr & sc->isync) == 0)
			return (0);	/* Not for us. */
	}

	if ((AR_READ(sc, AR_INTR_ASYNC_CAUSE) & AR_INTR_MAC_IRQ) &&
	    (AR_READ(sc, AR_RTC_STATUS) & AR_RTC_STATUS_M) == AR_RTC_STATUS_ON)
		intr = AR_READ(sc, AR_ISR);
	else
		intr = 0;
	sync = AR_READ(sc, AR_INTR_SYNC_CAUSE) & sc->isync;
	if (intr == 0 && sync == 0)
		return (0);	/* Not for us. */

	if (intr != 0) {
		if (intr & AR_ISR_BCNMISC) {
			intr2 = AR_READ(sc, AR_ISR_S2);
			if (intr2 & AR_ISR_S2_TIM)
				/* TBD */;
			if (intr2 & AR_ISR_S2_TSFOOR)
				/* TBD */;
			if (intr2 & AR_ISR_S2_BB_WATCHDOG)
				/* TBD */;
		}
		intr = AR_READ(sc, AR_ISR_RAC);
		if (intr == AR_INTR_SPURIOUS)
			return (1);

#ifndef IEEE80211_STA_ONLY
		if (intr & AR_ISR_SWBA)
			ar9003_swba_intr(sc);
#endif
		if (intr & (AR_ISR_RXMINTR | AR_ISR_RXINTM))
			ar9003_rx_intr(sc, ATHN_QID_LP);
		if (intr & (AR_ISR_LP_RXOK | AR_ISR_RXERR | AR_ISR_RXEOL))
			ar9003_rx_intr(sc, ATHN_QID_LP);
		if (intr & AR_ISR_HP_RXOK)
			ar9003_rx_intr(sc, ATHN_QID_HP);

		if (intr & (AR_ISR_TXMINTR | AR_ISR_TXINTM))
			ar9003_tx_intr(sc);
		if (intr & (AR_ISR_TXOK | AR_ISR_TXERR | AR_ISR_TXEOL))
			ar9003_tx_intr(sc);

		if (intr & AR_ISR_GENTMR) {
			intr5 = AR_READ(sc, AR_ISR_S5_S);
			DPRINTF(("GENTMR trigger=%d thresh=%d\n",
			    MS(intr5, AR_ISR_S5_GENTIMER_TRIG),
			    MS(intr5, AR_ISR_S5_GENTIMER_THRESH)));
		}
	}
	if (sync != 0) {
		if (sync & AR_INTR_SYNC_RADM_CPL_TIMEOUT) {
			AR_WRITE(sc, AR_RC, AR_RC_HOSTIF);
			AR_WRITE(sc, AR_RC, 0);
		}

		if ((sc->flags & ATHN_FLAG_RFSILENT) &&
		    (sync & AR_INTR_SYNC_GPIO_PIN(sc->rfsilent_pin))) {
			struct ifnet *ifp = &sc->sc_ic.ic_if;

			printf("%s: radio switch turned off\n",
			    sc->sc_dev.dv_xname);
			/* Turn the interface down. */
			athn_stop(ifp, 1);
			return (1);
		}

		AR_WRITE(sc, AR_INTR_SYNC_CAUSE, sync);
		(void)AR_READ(sc, AR_INTR_SYNC_CAUSE);
	}
	return (1);
}

int
ar9003_tx(struct athn_softc *sc, struct mbuf *m, struct ieee80211_node *ni,
    int txflags)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_key *k = NULL;
	struct ieee80211_frame *wh;
	struct athn_series series[4];
	struct ar_tx_desc *ds;
	struct athn_txq *txq;
	struct athn_tx_buf *bf;
	struct athn_node *an = (void *)ni;
	struct mbuf *m1;
	uintptr_t entry;
	uint32_t sum;
	uint16_t qos = 0;
	uint8_t txpower, type, encrtype, tid, ridx[4];
	int i, error, totlen, hasqos, qid;

	/* Grab a Tx buffer from our global free list. */
	bf = SIMPLEQ_FIRST(&sc->txbufs);
	KASSERT(bf != NULL);

	/* Map 802.11 frame type to hardware frame type. */
	wh = mtod(m, struct ieee80211_frame *);
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
	    IEEE80211_FC0_TYPE_MGT) {
		/* NB: Beacons do not use ar9003_tx(). */
		if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
		    IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			type = AR_FRAME_TYPE_PROBE_RESP;
		else if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
		    IEEE80211_FC0_SUBTYPE_ATIM)
			type = AR_FRAME_TYPE_ATIM;
		else
			type = AR_FRAME_TYPE_NORMAL;
	} else if ((wh->i_fc[0] &
	    (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
	    (IEEE80211_FC0_TYPE_CTL  | IEEE80211_FC0_SUBTYPE_PS_POLL)) {
		type = AR_FRAME_TYPE_PSPOLL;
	} else
		type = AR_FRAME_TYPE_NORMAL;

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_get_txkey(ic, wh, ni);
		if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
			return (ENOBUFS);
		wh = mtod(m, struct ieee80211_frame *);
	}

	/* XXX 2-byte padding for QoS and 4-addr headers. */

	/* Select the HW Tx queue to use for this frame. */
	if ((hasqos = ieee80211_has_qos(wh))) {
		qos = ieee80211_get_qos(wh);
		tid = qos & IEEE80211_QOS_TID;
		qid = athn_ac2qid[ieee80211_up_to_ac(ic, tid)];
	} else if (type == AR_FRAME_TYPE_PSPOLL) {
		qid = ATHN_QID_PSPOLL;
	} else if (txflags & ATHN_TXFLAG_CAB) {
		qid = ATHN_QID_CAB;
	} else
		qid = ATHN_QID_AC_BE;
	txq = &sc->txq[qid];

	/* Select the transmit rates to use for this frame. */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
	    IEEE80211_FC0_TYPE_DATA) {
		/* Use lowest rate for all tries. */
		ridx[0] = ridx[1] = ridx[2] = ridx[3] =
		    (ic->ic_curmode == IEEE80211_MODE_11A) ?
			ATHN_RIDX_OFDM6 : ATHN_RIDX_CCK1;
	} else if (ic->ic_fixed_rate != -1) {
		/* Use same fixed rate for all tries. */
		ridx[0] = ridx[1] = ridx[2] = ridx[3] =
		    sc->fixed_ridx;
	} else {
		int txrate = ni->ni_txrate;
		/* Use fallback table of the node. */
		for (i = 0; i < 4; i++) {
			ridx[i] = an->ridx[txrate];
			txrate = an->fallback[txrate];
		}
	}

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct athn_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		/* Use initial transmit rate. */
		tap->wt_rate = athn_rates[ridx[0]].rate;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		if (athn_rates[ridx[0]].phy == IEEE80211_T_DS &&
		    ridx[0] != ATHN_RIDX_CCK1 &&
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			tap->wt_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len, m,
		    BPF_DIRECTION_OUT);
	}
#endif

	/* DMA map mbuf. */
	error = bus_dmamap_load_mbuf(sc->sc_dmat, bf->bf_map, m,
	    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
	if (__predict_false(error != 0)) {
		if (error != EFBIG) {
			printf("%s: can't map mbuf (error %d)\n",
			    sc->sc_dev.dv_xname, error);
			m_freem(m);
			return (error);
		}
		/*
		 * DMA mapping requires too many DMA segments; linearize
		 * mbuf in kernel virtual address space and retry.
		 */
		MGETHDR(m1, M_DONTWAIT, MT_DATA);
		if (m1 == NULL) {
			m_freem(m);
			return (ENOBUFS);
		}
		if (m->m_pkthdr.len > MHLEN) {
			MCLGET(m1, M_DONTWAIT);
			if (!(m1->m_flags & M_EXT)) {
				m_freem(m);
				m_freem(m1);
				return (ENOBUFS);
			}
		}
		m_copydata(m, 0, m->m_pkthdr.len, mtod(m1, caddr_t));
		m1->m_pkthdr.len = m1->m_len = m->m_pkthdr.len;
		m_freem(m);
		m = m1;

		error = bus_dmamap_load_mbuf(sc->sc_dmat, bf->bf_map, m,
		    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
		if (error != 0) {
			printf("%s: can't map mbuf (error %d)\n",
			    sc->sc_dev.dv_xname, error);
			m_freem(m);
			return (error);
		}
	}
	bf->bf_m = m;
	bf->bf_ni = ni;
	bf->bf_txflags = txflags;

	wh = mtod(m, struct ieee80211_frame *);

	totlen = m->m_pkthdr.len + IEEE80211_CRC_LEN;

	/* Setup Tx descriptor. */
	ds = bf->bf_descs;
	memset(ds, 0, sizeof(*ds));

	ds->ds_info =
	    SM(AR_TXI_DESC_ID, AR_VENDOR_ATHEROS) |
	    SM(AR_TXI_DESC_NDWORDS, 23) |
	    SM(AR_TXI_QCU_NUM, qid) |
	    AR_TXI_DESC_TX | AR_TXI_CTRL_STAT;

	ds->ds_ctl11 = AR_TXC11_CLR_DEST_MASK;
	txpower = AR_MAX_RATE_POWER;	/* Get from per-rate registers. */
	ds->ds_ctl11 |= SM(AR_TXC11_XMIT_POWER, txpower);

	ds->ds_ctl12 = SM(AR_TXC12_FRAME_TYPE, type);

	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    (hasqos && (qos & IEEE80211_QOS_ACK_POLICY_MASK) ==
	     IEEE80211_QOS_ACK_POLICY_NOACK))
		ds->ds_ctl12 |= AR_TXC12_NO_ACK;

	if (0 && k != NULL) {
		/*
		 * Map 802.11 cipher to hardware encryption type and
		 * compute MIC+ICV overhead.
		 */
		switch (k->k_cipher) {
		case IEEE80211_CIPHER_WEP40:
		case IEEE80211_CIPHER_WEP104:
			encrtype = AR_ENCR_TYPE_WEP;
			totlen += 4;
			break;
		case IEEE80211_CIPHER_TKIP:
			encrtype = AR_ENCR_TYPE_TKIP;
			totlen += 12;
			break;
		case IEEE80211_CIPHER_CCMP:
			encrtype = AR_ENCR_TYPE_AES;
			totlen += 8;
			break;
		default:
			panic("unsupported cipher");
		}
		/*
		 * NB: The key cache entry index is stored in the key
		 * private field when the key is installed.
		 */
		entry = (uintptr_t)k->k_priv;
		ds->ds_ctl12 |= SM(AR_TXC12_DEST_IDX, entry);
		ds->ds_ctl11 |= AR_TXC11_DEST_IDX_VALID;
	} else
		encrtype = AR_ENCR_TYPE_CLEAR;
	ds->ds_ctl17 = SM(AR_TXC17_ENCR_TYPE, encrtype);

	/* Check if frame must be protected using RTS/CTS or CTS-to-self. */
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
	    IEEE80211_FC0_TYPE_DATA) {
		/* NB: Group frames are sent using CCK in 802.11b/g. */
		if (totlen > ic->ic_rtsthreshold) {
			ds->ds_ctl11 |= AR_TXC11_RTS_ENABLE;
		} else if ((ic->ic_flags & IEEE80211_F_USEPROT) &&
		    athn_rates[ridx[0]].phy == IEEE80211_T_OFDM) {
			if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
				ds->ds_ctl11 |= AR_TXC11_RTS_ENABLE;
			else if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
				ds->ds_ctl11 |= AR_TXC11_CTS_ENABLE;
		}
	}
	/* 
	 * Disable multi-rate retries when protection is used.
	 * The RTS/CTS frame's duration field is fixed and won't be
	 * updated by hardware when the data rate changes.
	 */
	if (ds->ds_ctl11 & (AR_TXC11_RTS_ENABLE | AR_TXC11_CTS_ENABLE)) {
		ridx[1] = ridx[2] = ridx[3] = ridx[0];
	}
	/* Setup multi-rate retries. */
	for (i = 0; i < 4; i++) {
		series[i].hwrate = athn_rates[ridx[i]].hwrate;
		if (athn_rates[ridx[i]].phy == IEEE80211_T_DS &&
		    ridx[i] != ATHN_RIDX_CCK1 &&
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			series[i].hwrate |= 0x04;
		series[i].dur = 0;
	}
	if (!(ds->ds_ctl12 & AR_TXC12_NO_ACK)) {
		/* Compute duration for each series. */
		for (i = 0; i < 4; i++) {
			series[i].dur = athn_txtime(sc, IEEE80211_ACK_LEN,
			    athn_rates[ridx[i]].rspridx, ic->ic_flags);
		}
	}
	/* If this is a PA training frame, select the Tx chain to use. */
	if (__predict_false(txflags & ATHN_TXFLAG_PAPRD)) {
		ds->ds_ctl12 |= SM(AR_TXC12_PAPRD_CHAIN_MASK,
		    1 << sc->paprd_curchain);
	}

	/* Write number of tries for each series. */
	ds->ds_ctl13 =
	    SM(AR_TXC13_XMIT_DATA_TRIES0, 2) |
	    SM(AR_TXC13_XMIT_DATA_TRIES1, 2) |
	    SM(AR_TXC13_XMIT_DATA_TRIES2, 2) |
	    SM(AR_TXC13_XMIT_DATA_TRIES3, 4);

	/* Tell HW to update duration field in 802.11 header. */
	if (type != AR_FRAME_TYPE_PSPOLL)
		ds->ds_ctl13 |= AR_TXC13_DUR_UPDATE_ENA;

	/* Write Tx rate for each series. */
	ds->ds_ctl14 =
	    SM(AR_TXC14_XMIT_RATE0, series[0].hwrate) |
	    SM(AR_TXC14_XMIT_RATE1, series[1].hwrate) |
	    SM(AR_TXC14_XMIT_RATE2, series[2].hwrate) |
	    SM(AR_TXC14_XMIT_RATE3, series[3].hwrate);

	/* Write duration for each series. */
	ds->ds_ctl15 =
	    SM(AR_TXC15_PACKET_DUR0, series[0].dur) |
	    SM(AR_TXC15_PACKET_DUR1, series[1].dur);
	ds->ds_ctl16 =
	    SM(AR_TXC16_PACKET_DUR2, series[2].dur) |
	    SM(AR_TXC16_PACKET_DUR3, series[3].dur);

	if ((sc->flags & ATHN_FLAG_3TREDUCE_CHAIN) &&
	    ic->ic_curmode == IEEE80211_MODE_11A) {
		/*
		 * In order to not exceed PCIe power requirements, we only
		 * use two Tx chains for MCS0~15 on 5GHz band on these chips.
		 */
		ds->ds_ctl18 =
		    SM(AR_TXC18_CHAIN_SEL0,
			(ridx[0] <= ATHN_RIDX_MCS15) ? 0x3 : sc->txchainmask) |
		    SM(AR_TXC18_CHAIN_SEL1,
			(ridx[1] <= ATHN_RIDX_MCS15) ? 0x3 : sc->txchainmask) |
		    SM(AR_TXC18_CHAIN_SEL2,
			(ridx[2] <= ATHN_RIDX_MCS15) ? 0x3 : sc->txchainmask) |
		    SM(AR_TXC18_CHAIN_SEL3,
			(ridx[3] <= ATHN_RIDX_MCS15) ? 0x3 : sc->txchainmask);
	} else {
		/* Use the same Tx chains for all tries. */
		ds->ds_ctl18 =
		    SM(AR_TXC18_CHAIN_SEL0, sc->txchainmask) |
		    SM(AR_TXC18_CHAIN_SEL1, sc->txchainmask) |
		    SM(AR_TXC18_CHAIN_SEL2, sc->txchainmask) |
		    SM(AR_TXC18_CHAIN_SEL3, sc->txchainmask);
	}
#ifdef notyet
	/* Use the same short GI setting for all tries. */
	if (ic->ic_flags & IEEE80211_F_SHGI)
		ds->ds_ctl18 |= AR_TXC18_GI0123;
	/* Use the same channel width for all tries. */
	if (ic->ic_flags & IEEE80211_F_CBW40)
		ds->ds_ctl18 |= AR_TXC18_2040_0123;
#endif

	if (ds->ds_ctl11 & (AR_TXC11_RTS_ENABLE | AR_TXC11_CTS_ENABLE)) {
		uint8_t protridx, hwrate;
		uint16_t dur = 0;

		/* Use the same protection mode for all tries. */
		if (ds->ds_ctl11 & AR_TXC11_RTS_ENABLE) {
			ds->ds_ctl15 |= AR_TXC15_RTSCTS_QUAL01;
			ds->ds_ctl16 |= AR_TXC16_RTSCTS_QUAL23;
		}
		/* Select protection rate (suboptimal but ok). */
		protridx = (ic->ic_curmode == IEEE80211_MODE_11A) ?
		    ATHN_RIDX_OFDM6 : ATHN_RIDX_CCK2;
		if (ds->ds_ctl11 & AR_TXC11_RTS_ENABLE) {
			/* Account for CTS duration. */
			dur += athn_txtime(sc, IEEE80211_ACK_LEN,
			    athn_rates[protridx].rspridx, ic->ic_flags);
		}
		dur += athn_txtime(sc, totlen, ridx[0], ic->ic_flags);
		if (!(ds->ds_ctl12 & AR_TXC12_NO_ACK)) {
			/* Account for ACK duration. */
			dur += athn_txtime(sc, IEEE80211_ACK_LEN,
			    athn_rates[ridx[0]].rspridx, ic->ic_flags);
		}
		/* Write protection frame duration and rate. */
		ds->ds_ctl13 |= SM(AR_TXC13_BURST_DUR, dur);
		hwrate = athn_rates[protridx].hwrate;
		if (protridx == ATHN_RIDX_CCK2 &&
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			hwrate |= 0x04;
		ds->ds_ctl18 |= SM(AR_TXC18_RTSCTS_RATE, hwrate);
	}

	ds->ds_ctl11 |= SM(AR_TXC11_FRAME_LEN, totlen);
	ds->ds_ctl19 = AR_TXC19_NOT_SOUNDING;

	for (i = 0; i < bf->bf_map->dm_nsegs; i++) {
		ds->ds_segs[i].ds_data = bf->bf_map->dm_segs[i].ds_addr;
		ds->ds_segs[i].ds_ctl = SM(AR_TXC_BUF_LEN,
		    bf->bf_map->dm_segs[i].ds_len);
	}
	/* Compute Tx descriptor checksum. */
	sum = ds->ds_info + ds->ds_link;
	for (i = 0; i < 4; i++) {
		sum += ds->ds_segs[i].ds_data;
		sum += ds->ds_segs[i].ds_ctl;
	}
	sum = (sum >> 16) + (sum & 0xffff);
	ds->ds_ctl10 = SM(AR_TXC10_PTR_CHK_SUM, sum);

	bus_dmamap_sync(sc->sc_dmat, bf->bf_map, 0, bf->bf_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(6, ("Tx qid=%d nsegs=%d ctl11=0x%x ctl12=0x%x ctl14=0x%x\n",
	    qid, bf->bf_map->dm_nsegs, ds->ds_ctl11, ds->ds_ctl12,
	    ds->ds_ctl14));

	SIMPLEQ_REMOVE_HEAD(&sc->txbufs, bf_list);
	SIMPLEQ_INSERT_TAIL(&txq->head, bf, bf_list);

	/* Queue buffer unless hardware FIFO is already full. */
	if (++txq->queued <= AR9003_TX_QDEPTH) {
		AR_WRITE(sc, AR_QTXDP(qid), bf->bf_daddr);
		AR_WRITE_BARRIER(sc);
	} else if (txq->wait == NULL)
		txq->wait = bf;
	return (0);
}

void
ar9003_set_rf_mode(struct athn_softc *sc, struct ieee80211_channel *c)
{
	uint32_t reg;

	reg = IEEE80211_IS_CHAN_2GHZ(c) ?
	    AR_PHY_MODE_DYNAMIC : AR_PHY_MODE_OFDM;
	if (IEEE80211_IS_CHAN_5GHZ(c) &&
	    (sc->flags & ATHN_FLAG_FAST_PLL_CLOCK)) {
		reg |= AR_PHY_MODE_DYNAMIC | AR_PHY_MODE_DYN_CCK_DISABLE;
	}
	AR_WRITE(sc, AR_PHY_MODE, reg);
	AR_WRITE_BARRIER(sc);
}

static __inline uint32_t
ar9003_synth_delay(struct athn_softc *sc)
{
	uint32_t delay;

	delay = MS(AR_READ(sc, AR_PHY_RX_DELAY), AR_PHY_RX_DELAY_DELAY);
	if (sc->sc_ic.ic_curmode == IEEE80211_MODE_11B)
		delay = (delay * 4) / 22;
	else
		delay = delay / 10;	/* in 100ns steps */
	return (delay);
}

int
ar9003_rf_bus_request(struct athn_softc *sc)
{
	int ntries;

	/* Request RF Bus grant. */
	AR_WRITE(sc, AR_PHY_RFBUS_REQ, AR_PHY_RFBUS_REQ_EN);
	for (ntries = 0; ntries < 10000; ntries++) {
		if (AR_READ(sc, AR_PHY_RFBUS_GRANT) & AR_PHY_RFBUS_GRANT_EN)
			return (0);
		DELAY(10);
	}
	DPRINTF(("could not kill baseband Rx"));
	return (ETIMEDOUT);
}

void
ar9003_rf_bus_release(struct athn_softc *sc)
{
	/* Wait for the synthesizer to settle. */
	DELAY(AR_BASE_PHY_ACTIVE_DELAY + ar9003_synth_delay(sc));

	/* Release the RF Bus grant. */
	AR_WRITE(sc, AR_PHY_RFBUS_REQ, 0);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_set_phy(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	uint32_t phy;

	phy = AR_READ(sc, AR_PHY_GEN_CTRL);
	phy |= AR_PHY_GC_HT_EN | AR_PHY_GC_SHORT_GI_40 |
	    AR_PHY_GC_SINGLE_HT_LTF1 | AR_PHY_GC_WALSH;
	if (extc != NULL) {
		phy |= AR_PHY_GC_DYN2040_EN;
		if (extc > c)	/* XXX */
			phy |= AR_PHY_GC_DYN2040_PRI_CH;
	}
	/* Turn off Green Field detection for now. */
	phy &= ~AR_PHY_GC_GF_DETECT_EN;
	AR_WRITE(sc, AR_PHY_GEN_CTRL, phy);

	AR_WRITE(sc, AR_2040_MODE,
	    (extc != NULL) ? AR_2040_JOINED_RX_CLEAR : 0);

	/* Set global transmit timeout. */
	AR_WRITE(sc, AR_GTXTO, SM(AR_GTXTO_TIMEOUT_LIMIT, 25));
	/* Set carrier sense timeout. */
	AR_WRITE(sc, AR_CST, SM(AR_CST_TIMEOUT_LIMIT, 15));
	AR_WRITE_BARRIER(sc);
}

void
ar9003_set_delta_slope(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	uint32_t coeff, exp, man, reg;

	/* Set Delta Slope (exponent and mantissa). */
	coeff = (100 << 24) / c->ic_freq;
	athn_get_delta_slope(coeff, &exp, &man);
	DPRINTFN(5, ("delta slope coeff exp=%u man=%u\n", exp, man));

	reg = AR_READ(sc, AR_PHY_TIMING3);
	reg = RW(reg, AR_PHY_TIMING3_DSC_EXP, exp);
	reg = RW(reg, AR_PHY_TIMING3_DSC_MAN, man);
	AR_WRITE(sc, AR_PHY_TIMING3, reg);

	/* For Short GI, coeff is 9/10 that of normal coeff. */
	coeff = (9 * coeff) / 10;
	athn_get_delta_slope(coeff, &exp, &man);
	DPRINTFN(5, ("delta slope coeff exp=%u man=%u\n", exp, man));

	reg = AR_READ(sc, AR_PHY_SGI_DELTA);
	reg = RW(reg, AR_PHY_SGI_DSC_EXP, exp);
	reg = RW(reg, AR_PHY_SGI_DSC_MAN, man);
	AR_WRITE(sc, AR_PHY_SGI_DELTA, reg);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_enable_antenna_diversity(struct athn_softc *sc)
{
	AR_SETBITS(sc, AR_PHY_CCK_DETECT,
	    AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_init_baseband(struct athn_softc *sc)
{
	uint32_t synth_delay;

	synth_delay = ar9003_synth_delay(sc);
	/* Activate the PHY (includes baseband activate and synthesizer on). */
	AR_WRITE(sc, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);
	AR_WRITE_BARRIER(sc);
	DELAY(AR_BASE_PHY_ACTIVE_DELAY + synth_delay);
}

void
ar9003_disable_phy(struct athn_softc *sc)
{
	AR_WRITE(sc, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_init_chains(struct athn_softc *sc)
{
	if (sc->rxchainmask == 0x5 || sc->txchainmask == 0x5)
		AR_SETBITS(sc, AR_PHY_ANALOG_SWAP, AR_PHY_SWAP_ALT_CHAIN);

	/* Setup chain masks. */
	AR_WRITE(sc, AR_PHY_RX_CHAINMASK,  sc->rxchainmask);
	AR_WRITE(sc, AR_PHY_CAL_CHAINMASK, sc->rxchainmask);

	if (sc->flags & ATHN_FLAG_3TREDUCE_CHAIN) {
		/*
		 * All self-generated frames are sent using two Tx chains
		 * on these chips to not exceed PCIe power requirements.
		 */
		AR_WRITE(sc, AR_SELFGEN_MASK, 0x3);
	} else
		AR_WRITE(sc, AR_SELFGEN_MASK, sc->txchainmask);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_set_rxchains(struct athn_softc *sc)
{
	if (sc->rxchainmask == 0x3 || sc->rxchainmask == 0x5) {
		AR_WRITE(sc, AR_PHY_RX_CHAINMASK,  sc->rxchainmask);
		AR_WRITE(sc, AR_PHY_CAL_CHAINMASK, sc->rxchainmask);
		AR_WRITE_BARRIER(sc);
	}
}

void
ar9003_read_noisefloor(struct athn_softc *sc, int16_t *nf, int16_t *nf_ext)
{
/* Sign-extends 9-bit value (assumes upper bits are zeroes). */
#define SIGN_EXT(v)	(((v) ^ 0x100) - 0x100)
	uint32_t reg;
	int i;

	for (i = 0; i < sc->nrxchains; i++) {
		reg = AR_READ(sc, AR_PHY_CCA(i));
		nf[i] = MS(reg, AR_PHY_MINCCA_PWR);
		nf[i] = SIGN_EXT(nf[i]);

		reg = AR_READ(sc, AR_PHY_EXT_CCA(i));
		nf_ext[i] = MS(reg, AR_PHY_EXT_MINCCA_PWR);
		nf_ext[i] = SIGN_EXT(nf_ext[i]);
	}
#undef SIGN_EXT
}

void
ar9003_write_noisefloor(struct athn_softc *sc, int16_t *nf, int16_t *nf_ext)
{
	uint32_t reg;
	int i;

	for (i = 0; i < sc->nrxchains; i++) {
		reg = AR_READ(sc, AR_PHY_CCA(i));
		reg = RW(reg, AR_PHY_MAXCCA_PWR, nf[i]);
		AR_WRITE(sc, AR_PHY_CCA(i), reg);

		reg = AR_READ(sc, AR_PHY_EXT_CCA(i));
		reg = RW(reg, AR_PHY_EXT_MAXCCA_PWR, nf_ext[i]);
		AR_WRITE(sc, AR_PHY_EXT_CCA(i), reg);
	}
	AR_WRITE_BARRIER(sc);
}

int
ar9003_get_noisefloor(struct athn_softc *sc)
{
	int16_t nf[AR_MAX_CHAINS], nf_ext[AR_MAX_CHAINS];
	int i;

	if (AR_READ(sc, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) {
		/* Noisefloor calibration not finished. */
		return 0;
	}
	/* Noisefloor calibration is finished. */
	ar9003_read_noisefloor(sc, nf, nf_ext);

	/* Update noisefloor history. */
	for (i = 0; i < sc->nrxchains; i++) {
		sc->nf_hist[sc->nf_hist_cur].nf[i] = nf[i];
		sc->nf_hist[sc->nf_hist_cur].nf_ext[i] = nf_ext[i];
	}
	if (++sc->nf_hist_cur >= ATHN_NF_CAL_HIST_MAX)
		sc->nf_hist_cur = 0;
	return 1;
}

void
ar9003_bb_load_noisefloor(struct athn_softc *sc)
{
	int16_t nf[AR_MAX_CHAINS], nf_ext[AR_MAX_CHAINS];
	int i, ntries;

	/* Write filtered noisefloor values. */
	for (i = 0; i < sc->nrxchains; i++) {
		nf[i] = sc->nf_priv[i] * 2;
		nf_ext[i] = sc->nf_ext_priv[i] * 2;
	}
	ar9003_write_noisefloor(sc, nf, nf_ext);

	/* Load filtered noisefloor values into baseband. */
	AR_CLRBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
	AR_CLRBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
	/* Wait for load to complete. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(AR_READ(sc, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF))
			break;
		DELAY(10);
	}
	if (ntries == 1000) {
		DPRINTF(("failed to load noisefloor values\n"));
		return;
	}

	/* Restore noisefloor values to initial (max) values. */
	for (i = 0; i < AR_MAX_CHAINS; i++)
		nf[i] = nf_ext[i] = -50 * 2;
	ar9003_write_noisefloor(sc, nf, nf_ext);
}

void
ar9003_apply_noisefloor(struct athn_softc *sc)
{
	uint32_t agc_nfcal;

	agc_nfcal = AR_READ(sc, AR_PHY_AGC_CONTROL) &
	    (AR_PHY_AGC_CONTROL_NF | AR_PHY_AGC_CONTROL_ENABLE_NF |
	    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);

	if (agc_nfcal & AR_PHY_AGC_CONTROL_NF) {
		/* Pause running NF calibration while values are updated. */
		AR_CLRBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
		AR_WRITE_BARRIER(sc);
	}

	ar9003_bb_load_noisefloor(sc);

	if (agc_nfcal & AR_PHY_AGC_CONTROL_NF) {
		/* Restart interrupted NF calibration. */
		AR_SETBITS(sc, AR_PHY_AGC_CONTROL, agc_nfcal);
		AR_WRITE_BARRIER(sc);
	}
}

void
ar9003_do_noisefloor_calib(struct athn_softc *sc)
{
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_init_noisefloor_calib(struct athn_softc *sc)
{
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
	AR_WRITE_BARRIER(sc);
}

int
ar9003_init_calib(struct athn_softc *sc)
{
	uint8_t txchainmask, rxchainmask;
	uint32_t reg;
	int ntries;

	/* Save chains masks. */
	txchainmask = sc->txchainmask;
	rxchainmask = sc->rxchainmask;
	/* Configure hardware before calibration. */
	if (AR_READ(sc, AR_ENT_OTP) & AR_ENT_OTP_CHAIN2_DISABLE)
		txchainmask = rxchainmask = 0x3;
	else
		txchainmask = rxchainmask = 0x7;
	ar9003_init_chains(sc);

	/* Perform Tx IQ calibration. */
	ar9003_calib_tx_iq(sc);
	/* Disable and re-enable the PHY chips. */
	AR_WRITE(sc, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
	AR_WRITE_BARRIER(sc);
	DELAY(5);
	AR_WRITE(sc, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

	/* Calibrate the AGC. */
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL);
	/* Poll for offset calibration completion. */
	for (ntries = 0; ntries < 10000; ntries++) {
		reg = AR_READ(sc, AR_PHY_AGC_CONTROL);
		if (!(reg & AR_PHY_AGC_CONTROL_CAL))
			break;
		DELAY(10);
	}
	if (ntries == 10000)
		return (ETIMEDOUT);

	/* Restore chains masks. */
	sc->txchainmask = txchainmask;
	sc->rxchainmask = rxchainmask;
	ar9003_init_chains(sc);

	return (0);
}

void
ar9003_do_calib(struct athn_softc *sc)
{
	uint32_t reg;

	if (sc->cur_calib_mask & ATHN_CAL_IQ) {
		reg = AR_READ(sc, AR_PHY_TIMING4);
		reg = RW(reg, AR_PHY_TIMING4_IQCAL_LOG_COUNT_MAX, 10);
		AR_WRITE(sc, AR_PHY_TIMING4, reg);
		AR_WRITE(sc, AR_PHY_CALMODE, AR_PHY_CALMODE_IQ);
		AR_SETBITS(sc, AR_PHY_TIMING4, AR_PHY_TIMING4_DO_CAL);
		AR_WRITE_BARRIER(sc);
	} else if (sc->cur_calib_mask & ATHN_CAL_TEMP) {
		AR_SETBITS(sc, AR_PHY_65NM_CH0_THERM,
		    AR_PHY_65NM_CH0_THERM_LOCAL);
		AR_SETBITS(sc, AR_PHY_65NM_CH0_THERM,
		    AR_PHY_65NM_CH0_THERM_START);
		AR_WRITE_BARRIER(sc);
	}
}

void
ar9003_next_calib(struct athn_softc *sc)
{
	/* Check if we have any calibration in progress. */
	if (sc->cur_calib_mask != 0) {
		if (!(AR_READ(sc, AR_PHY_TIMING4) & AR_PHY_TIMING4_DO_CAL)) {
			/* Calibration completed for current sample. */
			ar9003_calib_iq(sc);
		}
	}
}

void
ar9003_calib_iq(struct athn_softc *sc)
{
	struct athn_iq_cal *cal;
	uint32_t reg, i_coff_denom, q_coff_denom;
	int32_t i_coff, q_coff;
	int i, iq_corr_neg;

	for (i = 0; i < AR_MAX_CHAINS; i++) {
		cal = &sc->calib.iq[i];

		/* Read IQ calibration measures (clear on read). */
		cal->pwr_meas_i = AR_READ(sc, AR_PHY_IQ_ADC_MEAS_0_B(i));
		cal->pwr_meas_q = AR_READ(sc, AR_PHY_IQ_ADC_MEAS_1_B(i));
		cal->iq_corr_meas =
		    (int32_t)AR_READ(sc, AR_PHY_IQ_ADC_MEAS_2_B(i));
	}

	for (i = 0; i < sc->nrxchains; i++) {
		cal = &sc->calib.iq[i];

		if (cal->pwr_meas_q == 0)
			continue;

		if ((iq_corr_neg = cal->iq_corr_meas < 0))
			cal->iq_corr_meas = -cal->iq_corr_meas;

		i_coff_denom =
		    (cal->pwr_meas_i / 2 + cal->pwr_meas_q / 2) / 256;
		q_coff_denom = cal->pwr_meas_q / 64;

		if (i_coff_denom == 0 || q_coff_denom == 0)
			continue;	/* Prevents division by zero. */

		i_coff = cal->iq_corr_meas / i_coff_denom;
		q_coff = (cal->pwr_meas_i / q_coff_denom) - 64;

		if (i_coff > 63)
			i_coff = 63;
		else if (i_coff < -63)
			i_coff = -63;
		/* Negate i_coff if iq_corr_meas is positive. */
		if (!iq_corr_neg)
			i_coff = -i_coff;
		if (q_coff > 63)
			q_coff = 63;
		else if (q_coff < -63)
			q_coff = -63;

		DPRINTFN(2, ("IQ calibration for chain %d\n", i));
		reg = AR_READ(sc, AR_PHY_RX_IQCAL_CORR_B(i));
		reg = RW(reg, AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF, i_coff);
		reg = RW(reg, AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF, q_coff);
		AR_WRITE(sc, AR_PHY_RX_IQCAL_CORR_B(i), reg);
	}

	/* Apply new settings. */
	AR_SETBITS(sc, AR_PHY_RX_IQCAL_CORR_B(0),
	    AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE);
	AR_WRITE_BARRIER(sc);

	/* IQ calibration done. */
	sc->cur_calib_mask &= ~ATHN_CAL_IQ;
	memset(&sc->calib, 0, sizeof(sc->calib));
}

#define DELPT	32
int
ar9003_get_iq_corr(struct athn_softc *sc, int32_t res[6], int32_t coeff[2])
{
/* Sign-extends 12-bit value (assumes upper bits are zeroes). */
#define SIGN_EXT(v)	(((v) ^ 0x800) - 0x800)
#define SCALE		(1 << 15)
#define SHIFT		(1 <<  8)
	struct {
		int32_t	m, p, c;
	} val[2][2];
	int32_t mag[2][2], phs[2][2], cos[2], sin[2];
	int32_t min, max, div, f1, f2, f3, m, p, c;
	int32_t txmag, txphs, rxmag, rxphs;
	int32_t q_coff, i_coff;
	int i, j;

	/* Extract our twelve signed 12-bit values from res[] array. */
	val[0][0].m = res[0] & 0xfff;
	val[0][0].p = (res[0] >> 12) & 0xfff;
	val[0][0].c = ((res[0] >> 24) & 0xff) | (res[1] & 0xf) << 8;

	val[0][1].m = (res[1] >> 4) & 0xfff;
	val[0][1].p = res[2] & 0xfff;
	val[0][1].c = (res[2] >> 12) & 0xfff;

	val[1][0].m = ((res[2] >> 24) & 0xff) | (res[3] & 0xf) << 8;
	val[1][0].p = (res[3] >> 4) & 0xfff;
	val[1][0].c = res[4] & 0xfff;

	val[1][1].m = (res[4] >> 12) & 0xfff;
	val[1][1].p = ((res[4] >> 24) & 0xff) | (res[5] & 0xf) << 8;
	val[1][1].c = (res[5] >> 4) & 0xfff;

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 2; j++) {
			m = SIGN_EXT(val[i][j].m);
			p = SIGN_EXT(val[i][j].p);
			c = SIGN_EXT(val[i][j].c);

			if (p == 0)
				return (1);	/* Prevent division by 0. */

			mag[i][j] = (m * SCALE) / p;
			phs[i][j] = (c * SCALE) / p;
		}
		sin[i] = ((mag[i][0] - mag[i][1]) * SHIFT) / DELPT;
		cos[i] = ((phs[i][0] - phs[i][1]) * SHIFT) / DELPT;
		/* Find magnitude by approximation. */
		min = MIN(abs(sin[i]), abs(cos[i]));
		max = MAX(abs(sin[i]), abs(cos[i]));
		div = max - (max / 32) + (min / 8) + (min / 4);
		if (div == 0)
			return (1);	/* Prevent division by 0. */
		/* Normalize sin and cos by magnitude. */
		sin[i] = (sin[i] * SCALE) / div;
		cos[i] = (cos[i] * SCALE) / div;
	}

	/* Compute IQ mismatch (solve 4x4 linear equation). */
	f1 = cos[0] - cos[1];
	f3 = sin[0] - sin[1];
	f2 = (f1 * f1 + f3 * f3) / SCALE;
	if (f2 == 0)
		return (1);	/* Prevent division by 0. */

	/* Compute Tx magnitude mismatch. */
	txmag = (f1 * ( mag[0][0] - mag[1][0]) +
		 f3 * ( phs[0][0] - phs[1][0])) / f2;
	/* Compute Tx phase mismatch. */
	txphs = (f3 * (-mag[0][0] + mag[1][0]) +
		 f1 * ( phs[0][0] - phs[1][0])) / f2;

	if (txmag == SCALE)
		return (1);	/* Prevent division by 0. */

	/* Compute Rx magnitude mismatch. */
	rxmag = mag[0][0] - (cos[0] * txmag + sin[0] * txphs) / SCALE;
	/* Compute Rx phase mismatch. */
	rxphs = phs[0][0] + (sin[0] * txmag - cos[0] * txphs) / SCALE;

	if (-rxmag == SCALE)
		return (1);	/* Prevent division by 0. */

	txmag = (txmag * SCALE) / (SCALE - txmag);
	txphs = -txphs;

	q_coff = (txmag * 128) / SCALE;
	if (q_coff < -63)
		q_coff = -63;
	else if (q_coff > 63)
		q_coff = 63;
	i_coff = (txphs * 256) / SCALE;
	if (i_coff < -63)
		i_coff = -63;
	else if (i_coff > 63)
		i_coff = 63;
	coeff[0] = q_coff * 128 + i_coff;

	rxmag = (-rxmag * SCALE) / (SCALE + rxmag);
	rxphs = -rxphs;

	q_coff = (rxmag * 128) / SCALE;
	if (q_coff < -63)
		q_coff = -63;
	else if (q_coff > 63)
		q_coff = 63;
	i_coff = (rxphs * 256) / SCALE;
	if (i_coff < -63)
		i_coff = -63;
	else if (i_coff > 63)
		i_coff = 63;
	coeff[1] = q_coff * 128 + i_coff;

	return (0);
#undef SHIFT
#undef SCALE
#undef SIGN_EXT
}

int
ar9003_calib_tx_iq(struct athn_softc *sc)
{
	uint32_t reg;
	int32_t res[6], coeff[2];
	int i, j, ntries;

	reg = AR_READ(sc, AR_PHY_TX_IQCAL_CONTROL_1);
	reg = RW(reg, AR_PHY_TX_IQCAQL_CONTROL_1_IQCORR_I_Q_COFF_DELPT, DELPT);
	AR_WRITE(sc, AR_PHY_TX_IQCAL_CONTROL_1, reg);

	/* Start Tx IQ calibration. */
	AR_SETBITS(sc, AR_PHY_TX_IQCAL_START, AR_PHY_TX_IQCAL_START_DO_CAL);
	/* Wait for completion. */
	for (ntries = 0; ntries < 10000; ntries++) {
		reg = AR_READ(sc, AR_PHY_TX_IQCAL_START);
		if (!(reg & AR_PHY_TX_IQCAL_START_DO_CAL))
			break;
		DELAY(10);
	}
	if (ntries == 10000)
		return (ETIMEDOUT);

	for (i = 0; i < sc->ntxchains; i++) {
		/* Read Tx IQ calibration status for this chain. */
		reg = AR_READ(sc, AR_PHY_TX_IQCAL_STATUS_B(i));
		if (reg & AR_PHY_TX_IQCAL_STATUS_FAILED)
			return (EIO);
		/*
		 * Read Tx IQ calibration results for this chain.
		 * This consists in twelve signed 12-bit values.
		 */
		for (j = 0; j < 3; j++) {
			AR_CLRBITS(sc, AR_PHY_CHAN_INFO_MEMORY,
			    AR_PHY_CHAN_INFO_TAB_S2_READ);
			reg = AR_READ(sc, AR_PHY_CHAN_INFO_TAB(i, j));
			res[j * 2 + 0] = reg;

			AR_SETBITS(sc, AR_PHY_CHAN_INFO_MEMORY,
			    AR_PHY_CHAN_INFO_TAB_S2_READ);
			reg = AR_READ(sc, AR_PHY_CHAN_INFO_TAB(i, j));
			res[j * 2 + 1] = reg & 0xffff;
		}

		/* Compute Tx IQ correction. */
		if (ar9003_get_iq_corr(sc, res, coeff) != 0)
			return (EIO);

		/* Write Tx IQ correction coefficients. */
		reg = AR_READ(sc, AR_PHY_TX_IQCAL_CORR_COEFF_01_B(i));
		reg = RW(reg, AR_PHY_TX_IQCAL_CORR_COEFF_01_COEFF_TABLE,
		    coeff[0]);
		AR_WRITE(sc, AR_PHY_TX_IQCAL_CORR_COEFF_01_B(i), reg);

		reg = AR_READ(sc, AR_PHY_RX_IQCAL_CORR_B(i));
		reg = RW(reg, AR_PHY_RX_IQCAL_CORR_LOOPBACK_IQCORR_Q_Q_COFF,
		    coeff[1] >> 7);
		reg = RW(reg, AR_PHY_RX_IQCAL_CORR_LOOPBACK_IQCORR_Q_I_COFF,
		    coeff[1]);
		AR_WRITE(sc, AR_PHY_RX_IQCAL_CORR_B(i), reg);
		AR_WRITE_BARRIER(sc);
	}

	/* Enable Tx IQ correction. */
	AR_SETBITS(sc, AR_PHY_TX_IQCAL_CONTROL_3,
	    AR_PHY_TX_IQCAL_CONTROL_3_IQCORR_EN);
	AR_SETBITS(sc, AR_PHY_RX_IQCAL_CORR_B(0),
	    AR_PHY_RX_IQCAL_CORR_B0_LOOPBACK_IQCORR_EN);
	AR_WRITE_BARRIER(sc);
	return (0);
}
#undef DELPT

/*-
 * The power amplifier predistortion state machine works as follows:
 * 1) Disable digital predistorters for all Tx chains
 * 2) Repeat steps 3~7 for all Tx chains
 * 3)   Force Tx gain to that of training signal
 * 4)   Send training signal (asynchronous)
 * 5)   Wait for training signal to complete (asynchronous)
 * 6)   Read PA measurements (input power, output power, output phase)
 * 7)   Compute the predistortion function that linearizes PA output
 * 8) Write predistortion functions to hardware tables for all Tx chains
 * 9) Enable digital predistorters for all Tx chains
 */
void
ar9003_paprd_calib(struct athn_softc *sc, struct ieee80211_channel *c)
{
	static const int scaling[] = {
		261376, 248079, 233759, 220464,
		208194, 196949, 185706, 175487
	};
	struct athn_ops *ops = &sc->ops;
	uint32_t reg, ht20mask, ht40mask;
	int i;

	/* Read PA predistortion masks from ROM. */
	ops->get_paprd_masks(sc, c, &ht20mask, &ht40mask);

	/* AM-to-AM: amplifier's amplitude characteristic. */
	reg = AR_READ(sc, AR_PHY_PAPRD_AM2AM);
	reg = RW(reg, AR_PHY_PAPRD_AM2AM_MASK, ht20mask);
	AR_WRITE(sc, AR_PHY_PAPRD_AM2AM, reg);

	/* AM-to-PM: amplifier's phase transfer characteristic. */
	reg = AR_READ(sc, AR_PHY_PAPRD_AM2PM);
	reg = RW(reg, AR_PHY_PAPRD_AM2PM_MASK, ht20mask);
	AR_WRITE(sc, AR_PHY_PAPRD_AM2PM, reg);

	reg = AR_READ(sc, AR_PHY_PAPRD_HT40);
	reg = RW(reg, AR_PHY_PAPRD_HT40_MASK, ht40mask);
	AR_WRITE(sc, AR_PHY_PAPRD_HT40, reg);

	for (i = 0; i < AR9003_MAX_CHAINS; i++) {
		AR_SETBITS(sc, AR_PHY_PAPRD_CTRL0_B(i),
		    AR_PHY_PAPRD_CTRL0_USE_SINGLE_TABLE);

		reg = AR_READ(sc, AR_PHY_PAPRD_CTRL1_B(i));
		reg = RW(reg, AR_PHY_PAPRD_CTRL1_PA_GAIN_SCALE_FACT, 181);
		reg = RW(reg, AR_PHY_PAPRD_CTRL1_MAG_SCALE_FACT, 361);
		reg &= ~AR_PHY_PAPRD_CTRL1_ADAPTIVE_SCALING_ENA;
		reg |= AR_PHY_PAPRD_CTRL1_ADAPTIVE_AM2AM_ENA;
		reg |= AR_PHY_PAPRD_CTRL1_ADAPTIVE_AM2PM_ENA;
		AR_WRITE(sc, AR_PHY_PAPRD_CTRL1_B(i), reg);

		reg = AR_READ(sc, AR_PHY_PAPRD_CTRL0_B(i));
		reg = RW(reg, AR_PHY_PAPRD_CTRL0_PAPRD_MAG_THRSH, 3);
		AR_WRITE(sc, AR_PHY_PAPRD_CTRL0_B(i), reg);
	}

	/* Disable all digital predistorters during calibration. */
	for (i = 0; i < AR9003_MAX_CHAINS; i++) {
		AR_CLRBITS(sc, AR_PHY_PAPRD_CTRL0_B(i),
		    AR_PHY_PAPRD_CTRL0_PAPRD_ENABLE);
	}
	AR_WRITE_BARRIER(sc);

	/*
	 * Configure training signal.
	 */
	reg = AR_READ(sc, AR_PHY_PAPRD_TRAINER_CNTL1);
	reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL1_AGC2_SETTLING, 28);
	reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL1_LB_SKIP, 0x30);
	reg &= ~AR_PHY_PAPRD_TRAINER_CNTL1_RX_BB_GAIN_FORCE;
	reg &= ~AR_PHY_PAPRD_TRAINER_CNTL1_IQCORR_ENABLE;
	reg |= AR_PHY_PAPRD_TRAINER_CNTL1_LB_ENABLE;
	reg |= AR_PHY_PAPRD_TRAINER_CNTL1_TX_GAIN_FORCE;
	reg |= AR_PHY_PAPRD_TRAINER_CNTL1_TRAIN_ENABLE;
	AR_WRITE(sc, AR_PHY_PAPRD_TRAINER_CNTL1, reg);

	AR_WRITE(sc, AR_PHY_PAPRD_TRAINER_CNTL2, 147);

	reg = AR_READ(sc, AR_PHY_PAPRD_TRAINER_CNTL3);
	reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL3_FINE_CORR_LEN, 4);
	reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL3_COARSE_CORR_LEN, 4);
	reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL3_NUM_CORR_STAGES, 7);
	reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL3_MIN_LOOPBACK_DEL, 1);
	if (AR_SREV_9485(sc))
		reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL3_QUICK_DROP, -3);
	else
		reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL3_QUICK_DROP, -6);
	reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL3_ADC_DESIRED_SIZE, -15);
	reg |= AR_PHY_PAPRD_TRAINER_CNTL3_BBTXMIX_DISABLE;
	AR_WRITE(sc, AR_PHY_PAPRD_TRAINER_CNTL3, reg);

	reg = AR_READ(sc, AR_PHY_PAPRD_TRAINER_CNTL4);
	reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL4_SAFETY_DELTA, 0);
	reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL4_MIN_CORR, 400);
	reg = RW(reg, AR_PHY_PAPRD_TRAINER_CNTL4_NUM_TRAIN_SAMPLES, 100);
	AR_WRITE(sc, AR_PHY_PAPRD_TRAINER_CNTL4, reg);

	for (i = 0; i < nitems(scaling); i++) {
		reg = AR_READ(sc, AR_PHY_PAPRD_PRE_POST_SCALE_B0(i));
		reg = RW(reg, AR_PHY_PAPRD_PRE_POST_SCALING, scaling[i]);
		AR_WRITE(sc, AR_PHY_PAPRD_PRE_POST_SCALE_B0(i), reg);
	}

	/* Save Tx gain table. */
	for (i = 0; i < AR9003_TX_GAIN_TABLE_SIZE; i++)
		sc->txgain[i] = AR_READ(sc, AR_PHY_TXGAIN_TABLE(i));

	/* Set Tx power of training signal (use setting for MCS0). */
	sc->trainpow = MS(AR_READ(sc, AR_PHY_PWRTX_RATE5),
	    AR_PHY_PWRTX_RATE5_POWERTXHT20_0) - 4;

	/*
	 * Start PA predistortion calibration state machine.
	 */
	/* Find first available Tx chain. */
	sc->paprd_curchain = 0;
	while (!(sc->txchainmask & (1 << sc->paprd_curchain)))
		sc->paprd_curchain++;

	/* Make sure training done bit is clear. */
	AR_CLRBITS(sc, AR_PHY_PAPRD_TRAINER_STAT1,
	    AR_PHY_PAPRD_TRAINER_STAT1_TRAIN_DONE);
	AR_WRITE_BARRIER(sc);

	/* Transmit training signal. */
	ar9003_paprd_tx_tone(sc);
}

int
ar9003_get_desired_txgain(struct athn_softc *sc, int chain, int pow)
{
	int32_t scale, atemp, avolt, tempcal, voltcal, temp, volt;
	int32_t tempcorr, voltcorr;
	uint32_t reg;
	int8_t delta;

	scale = MS(AR_READ(sc, AR_PHY_TPC_12),
	    AR_PHY_TPC_12_DESIRED_SCALE_HT40_5);

	reg = AR_READ(sc, AR_PHY_TPC_19);
	atemp = MS(reg, AR_PHY_TPC_19_ALPHA_THERM);
	avolt = MS(reg, AR_PHY_TPC_19_ALPHA_VOLT);

	reg = AR_READ(sc, AR_PHY_TPC_18);
	tempcal = MS(reg, AR_PHY_TPC_18_THERM_CAL);
	voltcal = MS(reg, AR_PHY_TPC_18_VOLT_CAL);

	reg = AR_READ(sc, AR_PHY_BB_THERM_ADC_4);
	temp = MS(reg, AR_PHY_BB_THERM_ADC_4_LATEST_THERM);
	volt = MS(reg, AR_PHY_BB_THERM_ADC_4_LATEST_VOLT);

	delta = (int8_t)MS(AR_READ(sc, AR_PHY_TPC_11_B(chain)),
	    AR_PHY_TPC_11_OLPC_GAIN_DELTA);

	/* Compute temperature and voltage correction. */
	tempcorr = (atemp * (temp - tempcal) + 128) / 256;
	voltcorr = (avolt * (volt - voltcal) + 64) / 128;

	/* Compute desired Tx gain. */
	return (pow - delta - tempcorr - voltcorr + scale);
}

void
ar9003_force_txgain(struct athn_softc *sc, uint32_t txgain)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_TX_FORCED_GAIN);
	reg = RW(reg, AR_PHY_TX_FORCED_GAIN_TXBB1DBGAIN,
	    MS(txgain, AR_PHY_TXGAIN_TXBB1DBGAIN));
	reg = RW(reg, AR_PHY_TX_FORCED_GAIN_TXBB6DBGAIN,
	    MS(txgain, AR_PHY_TXGAIN_TXBB6DBGAIN));
	reg = RW(reg, AR_PHY_TX_FORCED_GAIN_TXMXRGAIN,
	    MS(txgain, AR_PHY_TXGAIN_TXMXRGAIN));
	reg = RW(reg, AR_PHY_TX_FORCED_GAIN_PADRVGNA,
	    MS(txgain, AR_PHY_TXGAIN_PADRVGNA));
	reg = RW(reg, AR_PHY_TX_FORCED_GAIN_PADRVGNB,
	    MS(txgain, AR_PHY_TXGAIN_PADRVGNB));
	reg = RW(reg, AR_PHY_TX_FORCED_GAIN_PADRVGNC,
	    MS(txgain, AR_PHY_TXGAIN_PADRVGNC));
	reg = RW(reg, AR_PHY_TX_FORCED_GAIN_PADRVGND,
	    MS(txgain, AR_PHY_TXGAIN_PADRVGND));
	reg &= ~AR_PHY_TX_FORCED_GAIN_ENABLE_PAL;
	reg &= ~AR_PHY_TX_FORCED_GAIN_FORCE_TX_GAIN;
	AR_WRITE(sc, AR_PHY_TX_FORCED_GAIN, reg);

	reg = AR_READ(sc, AR_PHY_TPC_1);
	reg = RW(reg, AR_PHY_TPC_1_FORCED_DAC_GAIN, 0);
	reg &= ~AR_PHY_TPC_1_FORCE_DAC_GAIN;
	AR_WRITE(sc, AR_PHY_TPC_1, reg);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_set_training_gain(struct athn_softc *sc, int chain)
{
	int i, gain;

	/*
	 * Get desired gain for training signal power (take into account
	 * current temperature/voltage).
	 */
	gain = ar9003_get_desired_txgain(sc, chain, sc->trainpow);
	/* Find entry in table. */
	for (i = 0; i < AR9003_TX_GAIN_TABLE_SIZE - 1; i++)
		if (MS(sc->txgain[i], AR_PHY_TXGAIN_INDEX) >= gain)
			break;
	ar9003_force_txgain(sc, sc->txgain[i]);
}

int
ar9003_paprd_tx_tone(struct athn_softc *sc)
{
#define TONE_LEN	1800
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct mbuf *m;
	int error;

	/* Build a Null (no data) frame of TONE_LEN bytes. */
	m = MCLGETL(NULL, M_DONTWAIT, TONE_LEN);
	if (m == NULL)
		return (ENOBUFS);
	memset(mtod(m, caddr_t), 0, TONE_LEN);
	wh = mtod(m, struct ieee80211_frame *);
	wh->i_fc[0] = IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_NODATA;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	*(uint16_t *)wh->i_dur = htole16(10);	/* XXX */
	IEEE80211_ADDR_COPY(wh->i_addr1, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, ic->ic_myaddr);
	m->m_pkthdr.len = m->m_len = TONE_LEN;

	/* Set gain of training signal. */
	ar9003_set_training_gain(sc, sc->paprd_curchain);

	/* Transmit training signal. */
	ni = ieee80211_ref_node(ic->ic_bss);
	if ((error = ar9003_tx(sc, m, ni, ATHN_TXFLAG_PAPRD)) != 0)
		ieee80211_release_node(ic, ni);
	return (error);
#undef TONE_LEN
}

static __inline int
get_scale(int val)
{
	int log = 0;

	/* Find the log base 2 (position of highest bit set). */
	while (val >>= 1)
		log++;

	return ((log > 10) ? log - 10 : 0);
}

/*
 * Compute predistortion function to linearize power amplifier output based
 * on feedback from training signal.
 */
int
ar9003_compute_predistortion(struct athn_softc *sc, const uint32_t *lo,
    const uint32_t *hi)
{
#define NBINS	23
	int chain = sc->paprd_curchain;
	int x[NBINS + 1], y[NBINS + 1], t[NBINS + 1];
	int b1[NBINS + 1], b2[NBINS + 1], xtilde[NBINS + 1];
	int nsamples, txsum, rxsum, rosum, maxidx;
	int order, order5x, order5xrem, order3x, order3xrem, y5, y3;
	int icept, G, I, L, M, angle, xnonlin, y2, y4, sumy2, sumy4;
	int alpha, beta, scale, Qalpha, Qbeta, Qscale, Qx, Qb1, Qb2;
	int tavg, ttilde, maxb1abs, maxb2abs, maxxtildeabs, in;
	int tmp, i;

	/* Set values at origin. */
	x[0] = y[0] = t[0] = 0;

#define SCALE	32
	maxidx = 0;
	for (i = 0; i < NBINS; i++) {
		nsamples = lo[i] & 0xffff;
		/* Skip bins that contain 16 or less samples. */
		if (nsamples <= 16) {
			x[i + 1] = y[i + 1] = t[i + 1] = 0;
			continue;
		}
		txsum = (hi[i] & 0x7ff) << 16 | lo[i] >> 16;
		rxsum = (lo[i + NBINS] & 0xffff) << 5 |
		    ((hi[i] >> 11) & 0x1f);
		rosum = (hi[i + NBINS] & 0x7ff) << 16 | hi[i + NBINS] >> 16;
		/* Sign-extend 27-bit value. */
		rosum = (rosum ^ 0x4000000) - 0x4000000;

		txsum *= SCALE;
		rxsum *= SCALE;
		rosum *= SCALE;

		x[i + 1] = ((txsum + nsamples) / nsamples + SCALE) / SCALE;
		y[i + 1] = ((rxsum + nsamples) / nsamples + SCALE) / SCALE +
		    SCALE * maxidx + SCALE / 2;
		t[i + 1] = (rosum + nsamples) / nsamples;
		maxidx++;
	}
#undef SCALE

#define SCALE_LOG	8
#define SCALE		(1 << SCALE_LOG)
	if (x[6] == x[3])
		return (1);	/* Prevent division by 0. */
	G = ((y[6] - y[3]) * SCALE + (x[6] - x[3])) / (x[6] - x[3]);
	if (G == 0)
		return (1);	/* Prevent division by 0. */

	sc->gain1[chain] = G;	/* Save low signal gain. */

	/* Find interception point. */
	icept = (G * (x[0] - x[3]) + SCALE) / SCALE + y[3];
	for (i = 0; i <= 3; i++) {
		y[i] = i * 32;
		x[i] = (y[i] * SCALE + G) / G;
	}
	for (i = 4; i <= maxidx; i++)
		y[i] -= icept;

	xnonlin = x[maxidx] - (y[maxidx] * SCALE + G) / G;
	order = (xnonlin + y[maxidx]) / y[maxidx];
	if (order == 0)
		M = 10;
	else if (order == 1)
		M = 9;
	else
		M = 8;

	I = (maxidx >= 16) ? 7 : maxidx / 2;
	L = maxidx - I;

	sumy2 = sumy4 = y2 = y4 = 0;
	for (i = 0; i <= L; i++) {
		if (y[i + I] == 0)
			return (1);	/* Prevent division by 0. */

		xnonlin = x[i + I] - ((y[i + I] * SCALE) + G) / G;
		xtilde[i] = ((xnonlin << M) + y[i + I]) / y[i + I];
		xtilde[i] = ((xtilde[i] << M) + y[i + I]) / y[i + I];
		xtilde[i] = ((xtilde[i] << M) + y[i + I]) / y[i + I];

		y2 = (y[i + I] * y[i + I] + SCALE * SCALE) / (SCALE * SCALE);

		sumy2 += y2;
		sumy4 += y2 * y2;

		b1[i] = y2 * (L + 1);
		b2[i] = y2;
	}
	for (i = 0; i <= L; i++) {
		b1[i] -= sumy2;
		b2[i] = sumy4 - sumy2 * b2[i];
	}

	maxxtildeabs = maxb1abs = maxb2abs = 0;
	for (i = 0; i <= L; i++) {
		tmp = abs(xtilde[i]);
		if (tmp > maxxtildeabs)
			maxxtildeabs = tmp;

		tmp = abs(b1[i]);
		if (tmp > maxb1abs)
			maxb1abs = tmp;

		tmp = abs(b2[i]);
		if (tmp > maxb2abs)
			maxb2abs = tmp;
	}
	Qx  = get_scale(maxxtildeabs);
	Qb1 = get_scale(maxb1abs);
	Qb2 = get_scale(maxb2abs);
	for (i = 0; i <= L; i++) {
		xtilde[i] /= 1 << Qx;
		b1[i] /= 1 << Qb1;
		b2[i] /= 1 << Qb2;
	}

	alpha = beta = 0;
	for (i = 0; i <= L; i++) {
		alpha += b1[i] * xtilde[i];
		beta  += b2[i] * xtilde[i];
	}

	scale = ((y4 / SCALE_LOG) * (L + 1) -
		 (y2 / SCALE_LOG) * sumy2) * SCALE_LOG;

	Qscale = get_scale(abs(scale));
	scale /= 1 << Qscale;
	Qalpha = get_scale(abs(alpha));
	alpha /= 1 << Qalpha;
	Qbeta  = get_scale(abs(beta));
	beta  /= 1 << Qbeta;

	order = 3 * M - Qx - Qb1 - Qbeta + 10 + Qscale;
	order5x = 1 << (order / 5);
	order5xrem = 1 << (order % 5);

	order = 3 * M - Qx - Qb2 - Qalpha + 10 + Qscale;
	order3x = 1 << (order / 3);
	order3xrem = 1 << (order % 3);

	for (i = 0; i < AR9003_PAPRD_MEM_TAB_SIZE; i++) {
		tmp = i * 32;

		/* Fifth order. */
		y5 = ((beta * tmp) / 64) / order5x;
		y5 = (y5 * tmp) / order5x;
		y5 = (y5 * tmp) / order5x;
		y5 = (y5 * tmp) / order5x;
		y5 = (y5 * tmp) / order5x;
		y5 = y5 / order5xrem;

		/* Third order. */
		y3 = (alpha * tmp) / order3x;
		y3 = (y3 * tmp) / order3x;
		y3 = (y3 * tmp) / order3x;
		y3 = y3 / order3xrem;

		in = y5 + y3 + (SCALE * tmp) / G;
		if (i >= 2 && in < sc->pa_in[chain][i - 1]) {
			in = sc->pa_in[chain][i - 1] +
			    (sc->pa_in[chain][i - 1] -
			     sc->pa_in[chain][i - 2]);
		}
		if (in > 1400)
			in = 1400;
		sc->pa_in[chain][i] = in;
	}

	/* Compute average theta of first 5 bins (linear region). */
	tavg = 0;
	for (i = 1; i <= 5; i++)
		tavg += t[i];
	tavg /= 5;
	for (i = 1; i <= 5; i++)
		t[i] = 0;
	for (i = 6; i <= maxidx; i++)
		t[i] -= tavg;

	alpha = beta = 0;
	for (i = 0; i <= L; i++) {
		ttilde = ((t[i + I] << M) + y[i + I]) / y[i + I];
		ttilde = ((ttilde << M) +  y[i + I]) / y[i + I];
		ttilde = ((ttilde << M) +  y[i + I]) / y[i + I];

		alpha += b2[i] * ttilde;
		beta  += b1[i] * ttilde;
	}

	Qalpha = get_scale(abs(alpha));
	alpha /= 1 << Qalpha;
	Qbeta  = get_scale(abs(beta));
	beta  /= 1 << Qbeta;

	order = 3 * M - Qx - Qb1 - Qbeta + 10 + Qscale + 5;
	order5x = 1 << (order / 5);
	order5xrem = 1 << (order % 5);

	order = 3 * M - Qx - Qb2 - Qalpha + 10 + Qscale + 5;
	order3x = 1 << (order / 3);
	order3xrem = 1 << (order % 3);

	for (i = 0; i <= 4; i++)
		sc->angle[chain][i] = 0;	/* Linear at that range. */
	for (i = 5; i < AR9003_PAPRD_MEM_TAB_SIZE; i++) {
		tmp = i * 32;

		/* Fifth order. */
		if (beta > 0)
			y5 = (((beta * tmp - 64) / 64) - order5x) / order5x;
		else
			y5 = (((beta * tmp - 64) / 64) + order5x) / order5x;
		y5 = (y5 * tmp) / order5x;
		y5 = (y5 * tmp) / order5x;
		y5 = (y5 * tmp) / order5x;
		y5 = (y5 * tmp) / order5x;
		y5 = y5 / order5xrem;

		/* Third order. */
		if (beta > 0)	/* XXX alpha? */
			y3 = (alpha * tmp - order3x) / order3x;
		else
			y3 = (alpha * tmp + order3x) / order3x;
		y3 = (y3 * tmp) / order3x;
		y3 = (y3 * tmp) / order3x;
		y3 = y3 / order3xrem;

		angle = y5 + y3;
		if (angle < -150)
			angle = -150;
		else if (angle > 150)
			angle = 150;
		sc->angle[chain][i] = angle;
	}
	/* Angle for entry 4 is derived from angle for entry 5. */
	sc->angle[chain][4] = (sc->angle[chain][5] + 2) / 2;

	return (0);
#undef SCALE
#undef SCALE_LOG
#undef NBINS
}

void
ar9003_enable_predistorter(struct athn_softc *sc, int chain)
{
	uint32_t reg;
	int i;

	/* Write digital predistorter lookup table. */
	for (i = 0; i < AR9003_PAPRD_MEM_TAB_SIZE; i++) {
		AR_WRITE(sc, AR_PHY_PAPRD_MEM_TAB_B(chain, i),
		    SM(AR_PHY_PAPRD_PA_IN, sc->pa_in[chain][i]) |
		    SM(AR_PHY_PAPRD_ANGLE, sc->angle[chain][i]));
	}

	reg = AR_READ(sc, AR_PHY_PA_GAIN123_B(chain));
	reg = RW(reg, AR_PHY_PA_GAIN123_PA_GAIN1, sc->gain1[chain]);
	AR_WRITE(sc, AR_PHY_PA_GAIN123_B(chain), reg);

	/* Indicate Tx power used for calibration (training signal). */
	reg = AR_READ(sc, AR_PHY_PAPRD_CTRL1_B(chain));
	reg = RW(reg, AR_PHY_PAPRD_CTRL1_POWER_AT_AM2AM_CAL, sc->trainpow);
	AR_WRITE(sc, AR_PHY_PAPRD_CTRL1_B(chain), reg);

	/* Enable digital predistorter for this chain. */
	AR_SETBITS(sc, AR_PHY_PAPRD_CTRL0_B(chain),
	    AR_PHY_PAPRD_CTRL0_PAPRD_ENABLE);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_paprd_enable(struct athn_softc *sc)
{
	int i;

	/* Enable digital predistorters for all Tx chains. */
	for (i = 0; i < AR9003_MAX_CHAINS; i++)
		if (sc->txchainmask & (1 << i))
			ar9003_enable_predistorter(sc, i);
}

/*
 * This function is called when our training signal has been sent.
 */
void
ar9003_paprd_tx_tone_done(struct athn_softc *sc)
{
	uint32_t lo[48], hi[48];
	int i;

	/* Make sure training is complete. */
	if (!(AR_READ(sc, AR_PHY_PAPRD_TRAINER_STAT1) &
	    AR_PHY_PAPRD_TRAINER_STAT1_TRAIN_DONE))
		return;

	/* Read feedback from training signal. */
	AR_CLRBITS(sc, AR_PHY_CHAN_INFO_MEMORY, AR_PHY_CHAN_INFO_TAB_S2_READ);
	for (i = 0; i < nitems(lo); i++)
		lo[i] = AR_READ(sc, AR_PHY_CHAN_INFO_TAB(0, i));
	AR_SETBITS(sc, AR_PHY_CHAN_INFO_MEMORY, AR_PHY_CHAN_INFO_TAB_S2_READ);
	for (i = 0; i < nitems(hi); i++)
		hi[i] = AR_READ(sc, AR_PHY_CHAN_INFO_TAB(0, i));

	AR_CLRBITS(sc, AR_PHY_PAPRD_TRAINER_STAT1,
	    AR_PHY_PAPRD_TRAINER_STAT1_TRAIN_DONE);

	/* Compute predistortion function based on this feedback. */
	if (ar9003_compute_predistortion(sc, lo, hi) != 0)
		return;

	/* Get next available Tx chain. */
	while (++sc->paprd_curchain < AR9003_MAX_CHAINS)
		if (sc->txchainmask & (1 << sc->paprd_curchain))
			break;
	if (sc->paprd_curchain == AR9003_MAX_CHAINS) {
		/* All Tx chains measured; enable digital predistortion. */
		ar9003_paprd_enable(sc);
	} else	/* Measure next Tx chain. */
		ar9003_paprd_tx_tone(sc);
}

void
ar9003_write_txpower(struct athn_softc *sc, int16_t power[ATHN_POWER_COUNT])
{
	/* Make sure forced gain is disabled. */
	AR_WRITE(sc, AR_PHY_TX_FORCED_GAIN, 0);

	AR_WRITE(sc, AR_PHY_PWRTX_RATE1,
	    (power[ATHN_POWER_OFDM18  ] & 0x3f) << 24 |
	    (power[ATHN_POWER_OFDM12  ] & 0x3f) << 16 |
	    (power[ATHN_POWER_OFDM9   ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_OFDM6   ] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE2,
	    (power[ATHN_POWER_OFDM54  ] & 0x3f) << 24 |
	    (power[ATHN_POWER_OFDM48  ] & 0x3f) << 16 |
	    (power[ATHN_POWER_OFDM36  ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_OFDM24  ] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE3,
	    (power[ATHN_POWER_CCK2_SP ] & 0x3f) << 24 |
	    (power[ATHN_POWER_CCK2_LP ] & 0x3f) << 16 |
	    /* NB: No eXtended Range for AR9003. */
	    (power[ATHN_POWER_CCK1_LP ] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE4,
	    (power[ATHN_POWER_CCK11_SP] & 0x3f) << 24 |
	    (power[ATHN_POWER_CCK11_LP] & 0x3f) << 16 |
	    (power[ATHN_POWER_CCK55_SP] & 0x3f) <<  8 |
	    (power[ATHN_POWER_CCK55_LP] & 0x3f));
	/*
	 * NB: AR_PHY_PWRTX_RATE5 needs to be written even if HT is disabled
	 * because it is read by PA predistortion functions.
	 */
	AR_WRITE(sc, AR_PHY_PWRTX_RATE5,
	    (power[ATHN_POWER_HT20( 5)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT20( 4)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT20( 1)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT20( 0)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE6,
	    (power[ATHN_POWER_HT20(13)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT20(12)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT20( 7)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT20( 6)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE7,
	    (power[ATHN_POWER_HT40( 5)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT40( 4)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT40( 1)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT40( 0)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE8,
	    (power[ATHN_POWER_HT40(13)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT40(12)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT40( 7)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT40( 6)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE10,
	    (power[ATHN_POWER_HT20(21)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT20(20)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT20(15)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT20(14)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE11,
	    (power[ATHN_POWER_HT40(23)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT40(22)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT20(23)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT20(22)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE12,
	    (power[ATHN_POWER_HT40(21)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT40(20)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT40(15)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT40(14)] & 0x3f));
	AR_WRITE_BARRIER(sc);
}

void
ar9003_reset_rx_gain(struct athn_softc *sc, struct ieee80211_channel *c)
{
#define X(x)	((uint32_t)(x) << 2)
	const struct athn_gain *prog = sc->rx_gain;
	const uint32_t *pvals;
	int i;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		pvals = prog->vals_2g;
	else
		pvals = prog->vals_5g;
	for (i = 0; i < prog->nregs; i++)
		AR_WRITE(sc, X(prog->regs[i]), pvals[i]);
	AR_WRITE_BARRIER(sc);
#undef X
}

void
ar9003_reset_tx_gain(struct athn_softc *sc, struct ieee80211_channel *c)
{
#define X(x)	((uint32_t)(x) << 2)
	const struct athn_gain *prog = sc->tx_gain;
	const uint32_t *pvals;
	int i;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		pvals = prog->vals_2g;
	else
		pvals = prog->vals_5g;
	for (i = 0; i < prog->nregs; i++)
		AR_WRITE(sc, X(prog->regs[i]), pvals[i]);
	AR_WRITE_BARRIER(sc);
#undef X
}

void
ar9003_hw_init(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
#define X(x)	((uint32_t)(x) << 2)
	struct athn_ops *ops = &sc->ops;
	const struct athn_ini *ini = sc->ini;
	const uint32_t *pvals;
	uint32_t reg;
	int i;

	/*
	 * The common init values include the pre and core phases for the
	 * SoC, MAC, BB and Radio subsystems.
	 */
	DPRINTFN(4, ("writing pre and core init vals\n"));
	for (i = 0; i < ini->ncmregs; i++) {
		AR_WRITE(sc, X(ini->cmregs[i]), ini->cmvals[i]);
		if (AR_IS_ANALOG_REG(X(ini->cmregs[i])))
			DELAY(100);
		if ((i & 0x1f) == 0)
			DELAY(1);
	}

	/*
	 * The modal init values include the post phase for the SoC, MAC,
	 * BB and Radio subsystems.
	 */
	if (extc != NULL) {
		if (IEEE80211_IS_CHAN_2GHZ(c))
			pvals = ini->vals_2g40;
		else
			pvals = ini->vals_5g40;
	} else {
		if (IEEE80211_IS_CHAN_2GHZ(c))
			pvals = ini->vals_2g20;
		else
			pvals = ini->vals_5g20;
	}
	DPRINTFN(4, ("writing post init vals\n"));
	for (i = 0; i < ini->nregs; i++) {
		AR_WRITE(sc, X(ini->regs[i]), pvals[i]);
		if (AR_IS_ANALOG_REG(X(ini->regs[i])))
			DELAY(100);
		if ((i & 0x1f) == 0)
			DELAY(1);
	}

	if (sc->rx_gain != NULL)
		ar9003_reset_rx_gain(sc, c);
	if (sc->tx_gain != NULL)
		ar9003_reset_tx_gain(sc, c);

	if (IEEE80211_IS_CHAN_5GHZ(c) &&
	    (sc->flags & ATHN_FLAG_FAST_PLL_CLOCK)) {
		/* Update modal values for fast PLL clock. */
		if (extc != NULL)
			pvals = ini->fastvals_5g40;
		else
			pvals = ini->fastvals_5g20;
		DPRINTFN(4, ("writing fast pll clock init vals\n"));
		for (i = 0; i < ini->nfastregs; i++) {
			AR_WRITE(sc, X(ini->fastregs[i]), pvals[i]);
			if (AR_IS_ANALOG_REG(X(ini->fastregs[i])))
				DELAY(100);
			if ((i & 0x1f) == 0)
				DELAY(1);
		}
	}

	/*
	 * Set the RX_ABORT and RX_DIS bits to prevent frames with corrupted
	 * descriptor status.
	 */
	AR_SETBITS(sc, AR_DIAG_SW, AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT);

	reg = AR_READ(sc, AR_PCU_MISC_MODE2);
	reg &= ~AR_PCU_MISC_MODE2_ADHOC_MCAST_KEYID_ENABLE;
	reg |= AR_PCU_MISC_MODE2_AGG_WEP_ENABLE_FIX;
	reg |= AR_PCU_MISC_MODE2_ENABLE_AGGWEP;
	AR_WRITE(sc, AR_PCU_MISC_MODE2, reg);
	AR_WRITE_BARRIER(sc);

	ar9003_set_phy(sc, c, extc);
	ar9003_init_chains(sc);

	ops->set_txpower(sc, c, extc);
#undef X
}

void
ar9003_get_lg_tpow(struct athn_softc *sc, struct ieee80211_channel *c,
    uint8_t ctl, const uint8_t *fbins,
    const struct ar_cal_target_power_leg *tgt, int nchans, uint8_t tpow[4])
{
	uint8_t fbin;
	int i, delta, lo, hi;

	lo = hi = -1;
	fbin = athn_chan2fbin(c);
	for (i = 0; i < nchans; i++) {
		delta = fbin - fbins[i];
		/* Find the largest sample that is <= our frequency. */
		if (delta >= 0 && (lo == -1 || delta < fbin - fbins[lo]))
			lo = i;
		/* Find the smallest sample that is >= our frequency. */
		if (delta <= 0 && (hi == -1 || delta > fbin - fbins[hi]))
			hi = i;
	}
	if (lo == -1)
		lo = hi;
	else if (hi == -1)
		hi = lo;
	/* Interpolate values. */
	for (i = 0; i < 4; i++) {
		tpow[i] = athn_interpolate(fbin,
		    fbins[lo], tgt[lo].tPow2x[i],
		    fbins[hi], tgt[hi].tPow2x[i]);
	}
	/* XXX Apply conformance test limit. */
}

void
ar9003_get_ht_tpow(struct athn_softc *sc, struct ieee80211_channel *c,
    uint8_t ctl, const uint8_t *fbins,
    const struct ar_cal_target_power_ht *tgt, int nchans, uint8_t tpow[14])
{
	uint8_t fbin;
	int i, delta, lo, hi;

	lo = hi = -1;
	fbin = athn_chan2fbin(c);
	for (i = 0; i < nchans; i++) {
		delta = fbin - fbins[i];
		/* Find the largest sample that is <= our frequency. */
		if (delta >= 0 && (lo == -1 || delta < fbin - fbins[lo]))
			lo = i;
		/* Find the smallest sample that is >= our frequency. */
		if (delta <= 0 && (hi == -1 || delta > fbin - fbins[hi]))
			hi = i;
	}
	if (lo == -1)
		lo = hi;
	else if (hi == -1)
		hi = lo;
	/* Interpolate values. */
	for (i = 0; i < 14; i++) {
		tpow[i] = athn_interpolate(fbin,
		    fbins[lo], tgt[lo].tPow2x[i],
		    fbins[hi], tgt[hi].tPow2x[i]);
	}
	/* XXX Apply conformance test limit. */
}

/*
 * Adaptive noise immunity.
 */
void
ar9003_set_noise_immunity_level(struct athn_softc *sc, int level)
{
	int high = level == 4;
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_DESIRED_SZ);
	reg = RW(reg, AR_PHY_DESIRED_SZ_TOT_DES, high ? -62 : -55);
	AR_WRITE(sc, AR_PHY_DESIRED_SZ, reg);

	reg = AR_READ(sc, AR_PHY_AGC);
	reg = RW(reg, AR_PHY_AGC_COARSE_LOW, high ? -70 : -64);
	reg = RW(reg, AR_PHY_AGC_COARSE_HIGH, high ? -12 : -14);
	AR_WRITE(sc, AR_PHY_AGC, reg);

	reg = AR_READ(sc, AR_PHY_FIND_SIG);
	reg = RW(reg, AR_PHY_FIND_SIG_FIRPWR, high ? -80 : -78);
	AR_WRITE(sc, AR_PHY_FIND_SIG, reg);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_enable_ofdm_weak_signal(struct athn_softc *sc)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_SFCORR_LOW);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M1_THRESH_LOW, 50);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M2_THRESH_LOW, 40);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW, 48);
	AR_WRITE(sc, AR_PHY_SFCORR_LOW, reg);

	reg = AR_READ(sc, AR_PHY_SFCORR);
	reg = RW(reg, AR_PHY_SFCORR_M1_THRESH, 77);
	reg = RW(reg, AR_PHY_SFCORR_M2_THRESH, 64);
	reg = RW(reg, AR_PHY_SFCORR_M2COUNT_THR, 16);
	AR_WRITE(sc, AR_PHY_SFCORR, reg);

	reg = AR_READ(sc, AR_PHY_SFCORR_EXT);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M1_THRESH_LOW, 50);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M2_THRESH_LOW, 40);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M1_THRESH, 77);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M2_THRESH, 64);
	AR_WRITE(sc, AR_PHY_SFCORR_EXT, reg);

	AR_SETBITS(sc, AR_PHY_SFCORR_LOW,
	    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_disable_ofdm_weak_signal(struct athn_softc *sc)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_SFCORR_LOW);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M1_THRESH_LOW, 127);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M2_THRESH_LOW, 127);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW, 63);
	AR_WRITE(sc, AR_PHY_SFCORR_LOW, reg);

	reg = AR_READ(sc, AR_PHY_SFCORR);
	reg = RW(reg, AR_PHY_SFCORR_M1_THRESH, 127);
	reg = RW(reg, AR_PHY_SFCORR_M2_THRESH, 127);
	reg = RW(reg, AR_PHY_SFCORR_M2COUNT_THR, 31);
	AR_WRITE(sc, AR_PHY_SFCORR, reg);

	reg = AR_READ(sc, AR_PHY_SFCORR_EXT);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M1_THRESH_LOW, 127);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M2_THRESH_LOW, 127);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M1_THRESH, 127);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M2_THRESH, 127);
	AR_WRITE(sc, AR_PHY_SFCORR_EXT, reg);

	AR_CLRBITS(sc, AR_PHY_SFCORR_LOW,
	    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_set_cck_weak_signal(struct athn_softc *sc, int high)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_CCK_DETECT);
	reg = RW(reg, AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK, high ? 6 : 8);
	AR_WRITE(sc, AR_PHY_CCK_DETECT, reg);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_set_firstep_level(struct athn_softc *sc, int level)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_FIND_SIG);
	reg = RW(reg, AR_PHY_FIND_SIG_FIRSTEP, level * 4);
	AR_WRITE(sc, AR_PHY_FIND_SIG, reg);
	AR_WRITE_BARRIER(sc);
}

void
ar9003_set_spur_immunity_level(struct athn_softc *sc, int level)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_TIMING5);
	reg = RW(reg, AR_PHY_TIMING5_CYCPWR_THR1, (level + 1) * 2);
	AR_WRITE(sc, AR_PHY_TIMING5, reg);
	AR_WRITE_BARRIER(sc);
}
