/*	$OpenBSD: glxsb.c,v 1.42 2023/01/30 10:49:05 jsg Exp $	*/

/*
 * Copyright (c) 2006 Tom Cosgrove <tom@openbsd.org>
 * Copyright (c) 2003, 2004 Theo de Raadt
 * Copyright (c) 2003 Jason Wright
 *
 * Permission to use, copy, modify, and distribute this software for any
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
 * Driver for the security block on the AMD Geode LX processors
 * http://www.amd.com/files/connectivitysolutions/geode/geode_lx/33234d_lx_ds.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/cpufunc.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#ifdef CRYPTO
#include <crypto/cryptodev.h>
#include <crypto/xform.h>
#include <crypto/cryptosoft.h>
#endif

#define SB_GLD_MSR_CAP		0x58002000	/* RO - Capabilities */
#define SB_GLD_MSR_CONFIG	0x58002001	/* RW - Master Config */
#define SB_GLD_MSR_SMI		0x58002002	/* RW - SMI */
#define SB_GLD_MSR_ERROR	0x58002003	/* RW - Error */
#define SB_GLD_MSR_PM		0x58002004	/* RW - Power Mgmt */
#define SB_GLD_MSR_DIAG		0x58002005	/* RW - Diagnostic */
#define SB_GLD_MSR_CTRL		0x58002006	/* RW - Security Block Cntrl */

						/* For GLD_MSR_CTRL: */
#define SB_GMC_DIV0		0x0000		/* AES update divisor values */
#define SB_GMC_DIV1		0x0001
#define SB_GMC_DIV2		0x0002
#define SB_GMC_DIV3		0x0003
#define SB_GMC_DIV_MASK		0x0003
#define SB_GMC_SBI		0x0004		/* AES swap bits */
#define SB_GMC_SBY		0x0008		/* AES swap bytes */
#define SB_GMC_TW		0x0010		/* Time write (EEPROM) */
#define SB_GMC_T_SEL0		0x0000		/* RNG post-proc: none */
#define SB_GMC_T_SEL1		0x0100		/* RNG post-proc: LFSR */
#define SB_GMC_T_SEL2		0x0200		/* RNG post-proc: whitener */
#define SB_GMC_T_SEL3		0x0300		/* RNG LFSR+whitener */
#define SB_GMC_T_SEL_MASK	0x0300
#define SB_GMC_T_NE		0x0400		/* Noise (generator) Enable */
#define SB_GMC_T_TM		0x0800		/* RNG test mode */
						/*     (deterministic) */

/* Security Block configuration/control registers (offsets from base) */

#define SB_CTL_A		0x0000		/* RW - SB Control A */
#define SB_CTL_B		0x0004		/* RW - SB Control B */
#define SB_AES_INT		0x0008		/* RW - SB AES Interrupt */
#define SB_SOURCE_A		0x0010		/* RW - Source A */
#define SB_DEST_A		0x0014		/* RW - Destination A */
#define SB_LENGTH_A		0x0018		/* RW - Length A */
#define SB_SOURCE_B		0x0020		/* RW - Source B */
#define SB_DEST_B		0x0024		/* RW - Destination B */
#define SB_LENGTH_B		0x0028		/* RW - Length B */
#define SB_WKEY			0x0030		/* WO - Writable Key 0-3 */
#define SB_WKEY_0		0x0030		/* WO - Writable Key 0 */
#define SB_WKEY_1		0x0034		/* WO - Writable Key 1 */
#define SB_WKEY_2		0x0038		/* WO - Writable Key 2 */
#define SB_WKEY_3		0x003C		/* WO - Writable Key 3 */
#define SB_CBC_IV		0x0040		/* RW - CBC IV 0-3 */
#define SB_CBC_IV_0		0x0040		/* RW - CBC IV 0 */
#define SB_CBC_IV_1		0x0044		/* RW - CBC IV 1 */
#define SB_CBC_IV_2		0x0048		/* RW - CBC IV 2 */
#define SB_CBC_IV_3		0x004C		/* RW - CBC IV 3 */
#define SB_RANDOM_NUM		0x0050		/* RW - Random Number */
#define SB_RANDOM_NUM_STATUS	0x0054		/* RW - Random Number Status */
#define SB_EEPROM_COMM		0x0800		/* RW - EEPROM Command */
#define SB_EEPROM_ADDR		0x0804		/* RW - EEPROM Address */
#define SB_EEPROM_DATA		0x0808		/* RW - EEPROM Data */
#define SB_EEPROM_SEC_STATE	0x080C		/* RW - EEPROM Security State */

						/* For SB_CTL_A and _B */
#define SB_CTL_ST		0x0001		/* Start operation (enc/dec) */
#define SB_CTL_ENC		0x0002		/* Encrypt (0 is decrypt) */
#define SB_CTL_DEC		0x0000		/* Decrypt */
#define SB_CTL_WK		0x0004		/* Use writable key (we set) */
#define SB_CTL_DC		0x0008		/* Destination coherent */
#define SB_CTL_SC		0x0010		/* Source coherent */
#define SB_CTL_CBC		0x0020		/* CBC (0 is ECB) */

						/* For SB_AES_INT */
#define SB_AI_DISABLE_AES_A	0x00001		/* Disable AES A compl int */
#define SB_AI_ENABLE_AES_A	0x00000		/* Enable AES A compl int */
#define SB_AI_DISABLE_AES_B	0x00002		/* Disable AES B compl int */
#define SB_AI_ENABLE_AES_B	0x00000		/* Enable AES B compl int */
#define SB_AI_DISABLE_EEPROM	0x00004		/* Disable EEPROM op comp int */
#define SB_AI_ENABLE_EEPROM	0x00000		/* Enable EEPROM op compl int */
#define SB_AI_AES_A_COMPLETE	0x10000		/* AES A operation complete */
#define SB_AI_AES_B_COMPLETE	0x20000		/* AES B operation complete */
#define SB_AI_EEPROM_COMPLETE	0x40000		/* EEPROM operation complete */

#define SB_RNS_TRNG_VALID	0x0001		/* in SB_RANDOM_NUM_STATUS */

#define SB_MEM_SIZE		0x0810		/* Size of memory block */

#define SB_AES_ALIGN		0x0010		/* Source and dest buffers */
						/* must be 16-byte aligned */
#define SB_AES_BLOCK_SIZE	0x0010

/*
 * The Geode LX security block AES acceleration doesn't perform scatter-
 * gather: it just takes source and destination addresses.  Therefore the
 * plain- and ciphertexts need to be contiguous.  To this end, we allocate
 * a buffer for both, and accept the overhead of copying in and out.  If
 * the number of bytes in one operation is bigger than allowed for by the
 * buffer (buffer is twice the size of the max length, as it has both input
 * and output) then we have to perform multiple encryptions/decryptions.
 */
#define GLXSB_MAX_AES_LEN	16384

#ifdef CRYPTO
struct glxsb_dma_map {
	bus_dmamap_t		dma_map;
	bus_dma_segment_t	dma_seg;
	int			dma_nsegs;
	int			dma_size;
	caddr_t			dma_vaddr;
	uint32_t		dma_paddr;
};
struct glxsb_session {
	uint32_t	ses_key[4];
	int		ses_klen;
	int		ses_used;
	struct swcr_data *ses_swd_auth;
	struct swcr_data *ses_swd_enc;
};
#endif /* CRYPTO */

struct glxsb_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	struct timeout		sc_to;

#ifdef CRYPTO
	bus_dma_tag_t		sc_dmat;
	struct glxsb_dma_map	sc_dma;
	int32_t			sc_cid;
	int			sc_nsessions;
	struct glxsb_session	*sc_sessions;
#endif /* CRYPTO */

	uint64_t		save_gld_msr;	
};

int	glxsb_match(struct device *, void *, void *);
void	glxsb_attach(struct device *, struct device *, void *);
int	glxsb_activate(struct device *, int);
void	glxsb_rnd(void *);

const struct cfattach glxsb_ca = {
	sizeof(struct glxsb_softc), glxsb_match, glxsb_attach, NULL,
	glxsb_activate
};

struct cfdriver glxsb_cd = {
	NULL, "glxsb", DV_DULL
};


#ifdef CRYPTO

#define GLXSB_SESSION(sid)		((sid) & 0x0fffffff)
#define	GLXSB_SID(crd,ses)		(((crd) << 28) | ((ses) & 0x0fffffff))

static struct glxsb_softc *glxsb_sc;

int glxsb_crypto_setup(struct glxsb_softc *);
int glxsb_crypto_newsession(uint32_t *, struct cryptoini *);
int glxsb_crypto_process(struct cryptop *);
int glxsb_crypto_freesession(uint64_t);
static __inline void glxsb_aes(struct glxsb_softc *, uint32_t, uint32_t,
    uint32_t, void *, int, void *);

int glxsb_dma_alloc(struct glxsb_softc *, int, struct glxsb_dma_map *);
void glxsb_dma_pre_op(struct glxsb_softc *, struct glxsb_dma_map *);
void glxsb_dma_post_op(struct glxsb_softc *, struct glxsb_dma_map *);
void glxsb_dma_free(struct glxsb_softc *, struct glxsb_dma_map *);

#endif /* CRYPTO */


int
glxsb_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_AMD &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_GEODE_LX_CRYPTO)
		return (1);

	return (0);
}

void
glxsb_attach(struct device *parent, struct device *self, void *aux)
{
	struct glxsb_softc *sc = (void *) self;
	struct pci_attach_args *pa = aux;
	bus_addr_t membase;
	bus_size_t memsize;
	uint64_t msr;
#ifdef CRYPTO
	uint32_t intr;
#endif

	msr = rdmsr(SB_GLD_MSR_CAP);
	if ((msr & 0xFFFF00) != 0x130400) {
		printf(": unknown ID 0x%x\n", (int) ((msr & 0xFFFF00) >> 16));
		return;
	}

	/* printf(": revision %d", (int) (msr & 0xFF)); */

	/* Map in the security block configuration/control registers */
	if (pci_mapreg_map(pa, PCI_MAPREG_START,
	    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT, 0, &sc->sc_iot,
	    &sc->sc_ioh, &membase, &memsize, SB_MEM_SIZE)) {
		printf(": can't find mem space\n");
		return;
	}

	/*
	 * Configure the Security Block.
	 *
	 * We want to enable the noise generator (T_NE), and enable the
	 * linear feedback shift register and whitener post-processing
	 * (T_SEL = 3).  Also ensure that test mode (deterministic values)
	 * is disabled.
	 */
	msr = rdmsr(SB_GLD_MSR_CTRL);
	msr &= ~(SB_GMC_T_TM | SB_GMC_T_SEL_MASK);
	msr |= SB_GMC_T_NE | SB_GMC_T_SEL3;
#if 0
	msr |= SB_GMC_SBI | SB_GMC_SBY;		/* for AES, if necessary */
#endif
	wrmsr(SB_GLD_MSR_CTRL, msr);

	/* Install a periodic collector for the "true" (AMD's word) RNG */
	timeout_set(&sc->sc_to, glxsb_rnd, sc);
	glxsb_rnd(sc);
	printf(": RNG");

#ifdef CRYPTO
	/* We don't have an interrupt handler, so disable completion INTs */
	intr = SB_AI_DISABLE_AES_A | SB_AI_DISABLE_AES_B |
	    SB_AI_DISABLE_EEPROM | SB_AI_AES_A_COMPLETE |
	    SB_AI_AES_B_COMPLETE | SB_AI_EEPROM_COMPLETE;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, SB_AES_INT, intr);

	sc->sc_dmat = pa->pa_dmat;

	if (glxsb_crypto_setup(sc))
		printf(" AES");
#endif

	printf("\n");
}

int
glxsb_activate(struct device *self, int act)
{
	struct glxsb_softc *sc = (struct glxsb_softc *)self;

	switch (act) {
	case DVACT_QUIESCE:
		/* XXX should wait for current crypto op to finish */
		break;
	case DVACT_SUSPEND:
		sc->save_gld_msr = rdmsr(SB_GLD_MSR_CTRL);
		break;
	case DVACT_RESUME:
		wrmsr(SB_GLD_MSR_CTRL, sc->save_gld_msr);
		break;
	}
	return (0);
}

void
glxsb_rnd(void *v)
{
	struct glxsb_softc *sc = v;
	uint32_t status, value;

	status = bus_space_read_4(sc->sc_iot, sc->sc_ioh, SB_RANDOM_NUM_STATUS);
	if (status & SB_RNS_TRNG_VALID) {
		value = bus_space_read_4(sc->sc_iot, sc->sc_ioh, SB_RANDOM_NUM);
		enqueue_randomness(value);
	}

	timeout_add_msec(&sc->sc_to, 10);
}

#ifdef CRYPTO
int
glxsb_crypto_setup(struct glxsb_softc *sc)
{
	int algs[CRYPTO_ALGORITHM_MAX + 1];

	/* Allocate a contiguous DMA-able buffer to work in */
	if (glxsb_dma_alloc(sc, GLXSB_MAX_AES_LEN * 2, &sc->sc_dma) != 0)
		return 0;

	bzero(algs, sizeof(algs));
	algs[CRYPTO_AES_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_MD5_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA1_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_RIPEMD160_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_256_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_384_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_512_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;

	sc->sc_cid = crypto_get_driverid(0);
	if (sc->sc_cid < 0)
		return 0;

	crypto_register(sc->sc_cid, algs, glxsb_crypto_newsession,
	    glxsb_crypto_freesession, glxsb_crypto_process);

	sc->sc_nsessions = 0;

	glxsb_sc = sc;

	return 1;
}

int
glxsb_crypto_newsession(uint32_t *sidp, struct cryptoini *cri)
{
	struct glxsb_softc *sc = glxsb_sc;
	struct glxsb_session *ses = NULL;
	const struct auth_hash	*axf;
	const struct enc_xform	*txf;
	struct cryptoini	*c;
	struct swcr_data	*swd;
	int sesn, i;

	if (sc == NULL || sidp == NULL || cri == NULL)
		return (EINVAL);

	for (sesn = 0; sesn < sc->sc_nsessions; sesn++) {
		if (sc->sc_sessions[sesn].ses_used == 0) {
			ses = &sc->sc_sessions[sesn];
			break;
		}
	}

	if (ses == NULL) {
		sesn = sc->sc_nsessions;
		ses = mallocarray(sesn + 1, sizeof(*ses), M_DEVBUF,
		    M_NOWAIT);
		if (ses == NULL)
			return (ENOMEM);
		if (sesn != 0) {
			bcopy(sc->sc_sessions, ses, sesn * sizeof(*ses));
			explicit_bzero(sc->sc_sessions, sesn * sizeof(*ses));
			free(sc->sc_sessions, M_DEVBUF, sesn * sizeof(*ses));
		}
		sc->sc_sessions = ses;
		ses = &sc->sc_sessions[sesn];
		sc->sc_nsessions++;
	}

	bzero(ses, sizeof(*ses));
	ses->ses_used = 1;

	for (c = cri; c != NULL; c = c->cri_next) {
		switch (c->cri_alg) {
		case CRYPTO_AES_CBC:

			if (c->cri_klen != 128) {
				swd = malloc(sizeof(struct swcr_data),
				    M_CRYPTO_DATA, M_NOWAIT|M_ZERO);
				if (swd == NULL) {
					glxsb_crypto_freesession(sesn);
					return (ENOMEM);
				}
				ses->ses_swd_enc = swd;
				txf = &enc_xform_aes;
				if (txf->ctxsize > 0) {
					swd->sw_kschedule =
					    malloc(txf->ctxsize,
						M_CRYPTO_DATA,
						M_NOWAIT|M_ZERO);
					if (swd->sw_kschedule == NULL) {
						glxsb_crypto_freesession(sesn);
						return (EINVAL);
					}
				}
				if (txf->setkey(swd->sw_kschedule, c->cri_key,
				    c->cri_klen / 8) < 0) {
					glxsb_crypto_freesession(sesn);
					return (EINVAL);
				}
				swd->sw_exf = txf;
				break;
			}

			ses->ses_klen = c->cri_klen;

			/* Copy the key (Geode LX wants the primary key only) */
			bcopy(c->cri_key, ses->ses_key, sizeof(ses->ses_key));
			break;

		case CRYPTO_MD5_HMAC:
			axf = &auth_hash_hmac_md5_96;
			goto authcommon;
		case CRYPTO_SHA1_HMAC:
			axf = &auth_hash_hmac_sha1_96;
			goto authcommon;
		case CRYPTO_RIPEMD160_HMAC:
			axf = &auth_hash_hmac_ripemd_160_96;
			goto authcommon;
		case CRYPTO_SHA2_256_HMAC:
			axf = &auth_hash_hmac_sha2_256_128;
			goto authcommon;
		case CRYPTO_SHA2_384_HMAC:
			axf = &auth_hash_hmac_sha2_384_192;
			goto authcommon;
		case CRYPTO_SHA2_512_HMAC:
			axf = &auth_hash_hmac_sha2_512_256;
		authcommon:
			swd = malloc(sizeof(struct swcr_data), M_CRYPTO_DATA,
			    M_NOWAIT|M_ZERO);
			if (swd == NULL) {
				glxsb_crypto_freesession(sesn);
				return (ENOMEM);
			}
			ses->ses_swd_auth = swd;

			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				glxsb_crypto_freesession(sesn);
				return (ENOMEM);
			}

			swd->sw_octx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_octx == NULL) {
				glxsb_crypto_freesession(sesn);
				return (ENOMEM);
			}

			for (i = 0; i < c->cri_klen / 8; i++)
				c->cri_key[i] ^= HMAC_IPAD_VAL;

			axf->Init(swd->sw_ictx);
			axf->Update(swd->sw_ictx, c->cri_key, c->cri_klen / 8);
			axf->Update(swd->sw_ictx, hmac_ipad_buffer,
			    axf->blocksize - (c->cri_klen / 8));

			for (i = 0; i < c->cri_klen / 8; i++)
				c->cri_key[i] ^= (HMAC_IPAD_VAL ^
				    HMAC_OPAD_VAL);

			axf->Init(swd->sw_octx);
			axf->Update(swd->sw_octx, c->cri_key, c->cri_klen / 8);
			axf->Update(swd->sw_octx, hmac_opad_buffer,
			    axf->blocksize - (c->cri_klen / 8));

			for (i = 0; i < c->cri_klen / 8; i++)
				c->cri_key[i] ^= HMAC_OPAD_VAL;

			swd->sw_axf = axf;
			swd->sw_alg = c->cri_alg;

			break;
		default:
			glxsb_crypto_freesession(sesn);
			return (EINVAL);
		}
	}

	*sidp = GLXSB_SID(0, sesn);
	return (0);
}

int
glxsb_crypto_freesession(uint64_t tid)
{
	struct glxsb_softc *sc = glxsb_sc;
	struct swcr_data *swd;
	const struct auth_hash *axf;
	const struct enc_xform *txf;
	int sesn;
	uint32_t sid = ((uint32_t)tid) & 0xffffffff;

	if (sc == NULL)
		return (EINVAL);
	sesn = GLXSB_SESSION(sid);
	if (sesn >= sc->sc_nsessions)
		return (EINVAL);
	if ((swd = sc->sc_sessions[sesn].ses_swd_enc)) {
		txf = swd->sw_exf;

		if (swd->sw_kschedule) {
			explicit_bzero(swd->sw_kschedule, txf->ctxsize);
			free(swd->sw_kschedule, M_CRYPTO_DATA, txf->ctxsize);
		}
		free(swd, M_CRYPTO_DATA, sizeof(*swd));
	}
	if ((swd = sc->sc_sessions[sesn].ses_swd_auth)) {
		axf = swd->sw_axf;

		if (swd->sw_ictx) {
			explicit_bzero(swd->sw_ictx, axf->ctxsize);
			free(swd->sw_ictx, M_CRYPTO_DATA, axf->ctxsize);
		}
		if (swd->sw_octx) {
			explicit_bzero(swd->sw_octx, axf->ctxsize);
			free(swd->sw_octx, M_CRYPTO_DATA, axf->ctxsize);
		}
		free(swd, M_CRYPTO_DATA, sizeof(*swd));
	}
	explicit_bzero(&sc->sc_sessions[sesn], sizeof(sc->sc_sessions[sesn]));
	return (0);
}

/*
 * Must be called at splnet() or higher
 */
static __inline void
glxsb_aes(struct glxsb_softc *sc, uint32_t control, uint32_t psrc,
    uint32_t pdst, void *key, int len, void *iv)
{
	uint32_t status;
	int i;

	if (len & 0xF) {
		printf("%s: len must be a multiple of 16 (not %d)\n",
		    sc->sc_dev.dv_xname, len);
		return;
	}

	/* Set the source */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, SB_SOURCE_A, psrc);

	/* Set the destination address */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, SB_DEST_A, pdst);

	/* Set the data length */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, SB_LENGTH_A, len);

	/* Set the IV */
	if (iv != NULL) {
		bus_space_write_region_4(sc->sc_iot, sc->sc_ioh,
		    SB_CBC_IV, iv, 4);
		control |= SB_CTL_CBC;
	}

	/* Set the key */
	bus_space_write_region_4(sc->sc_iot, sc->sc_ioh, SB_WKEY, key, 4);

	/* Ask the security block to do it */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, SB_CTL_A,
	    control | SB_CTL_WK | SB_CTL_DC | SB_CTL_SC | SB_CTL_ST);

	/*
	 * Now wait until it is done.
	 *
	 * We do a busy wait.  Obviously the number of iterations of
	 * the loop required to perform the AES operation depends upon
	 * the number of bytes to process.
	 *
	 * On a 500 MHz Geode LX we see
	 *
	 *	length (bytes)	typical max iterations
	 *	    16		   12
	 *	    64		   22
	 *	   256		   59
	 *	  1024		  212
	 *	  8192		1,537
	 *
	 * Since we have a maximum size of operation defined in
	 * GLXSB_MAX_AES_LEN, we use this constant to decide how long
	 * to wait.  Allow an order of magnitude longer than it should
	 * really take, just in case.
	 */
	for (i = 0; i < GLXSB_MAX_AES_LEN * 10; i++) {
		status = bus_space_read_4(sc->sc_iot, sc->sc_ioh, SB_CTL_A);

		if ((status & SB_CTL_ST) == 0)		/* Done */
			return;
	}

	printf("%s: operation failed to complete\n", sc->sc_dev.dv_xname);
}

static int
glxsb_crypto_swauth(struct cryptop *crp, struct cryptodesc *crd,
    struct swcr_data *sw, caddr_t buf)
{
	int	type;

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		type = CRYPTO_BUF_MBUF;
	else
		type = CRYPTO_BUF_IOV;
		
	return (swcr_authcompute(crp, crd, sw, buf, type));
}

static int
glxsb_crypto_swenc(struct cryptop *crp, struct cryptodesc *crd,
    struct swcr_data *sw, caddr_t buf)
{
	int	type;

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		type = CRYPTO_BUF_MBUF;
	else
		type = CRYPTO_BUF_IOV;
		
	return (swcr_encdec(crd, sw, buf, type));
}

static int
glxsb_crypto_encdec(struct cryptop *crp, struct cryptodesc *crd,
    struct glxsb_session *ses, struct glxsb_softc *sc, caddr_t buf)
{
	char *op_src, *op_dst;
	uint32_t op_psrc, op_pdst;
	uint8_t op_iv[SB_AES_BLOCK_SIZE];
	int err = 0;
	int len, tlen, xlen;
	int offset;
	uint32_t control;

	if (crd == NULL || (crd->crd_len % SB_AES_BLOCK_SIZE) != 0) {
		err = EINVAL;
		goto out;
	}

	/* How much of our buffer will we need to use? */
	xlen = crd->crd_len > GLXSB_MAX_AES_LEN ?
	    GLXSB_MAX_AES_LEN : crd->crd_len;

	/*
	 * XXX Check if we can have input == output on Geode LX.
	 * XXX In the meantime, use two separate (adjacent) buffers.
	 */
	op_src = sc->sc_dma.dma_vaddr;
	op_dst = sc->sc_dma.dma_vaddr + xlen;

	op_psrc = sc->sc_dma.dma_paddr;
	op_pdst = sc->sc_dma.dma_paddr + xlen;

	if (crd->crd_flags & CRD_F_ENCRYPT) {
		control = SB_CTL_ENC;
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, op_iv, sizeof(op_iv));
		else
			arc4random_buf(op_iv, sizeof(op_iv));

		if ((crd->crd_flags & CRD_F_IV_PRESENT) == 0) {
			if (crp->crp_flags & CRYPTO_F_IMBUF)
				err = m_copyback((struct mbuf *)crp->crp_buf,
				    crd->crd_inject, sizeof(op_iv), op_iv,
				    M_NOWAIT);
			else if (crp->crp_flags & CRYPTO_F_IOV)
				cuio_copyback((struct uio *)crp->crp_buf,
				    crd->crd_inject, sizeof(op_iv), op_iv);
			else
				bcopy(op_iv,
				    crp->crp_buf + crd->crd_inject, sizeof(op_iv));
			if (err)
				goto out;
		}
	} else {
		control = SB_CTL_DEC;
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, op_iv, sizeof(op_iv));
		else {
			if (crp->crp_flags & CRYPTO_F_IMBUF)
				m_copydata((struct mbuf *)crp->crp_buf,
				    crd->crd_inject, sizeof(op_iv), op_iv);
			else if (crp->crp_flags & CRYPTO_F_IOV)
				cuio_copydata((struct uio *)crp->crp_buf,
				    crd->crd_inject, sizeof(op_iv), op_iv);
			else
				bcopy(crp->crp_buf + crd->crd_inject,
				    op_iv, sizeof(op_iv));
		}
	}

	offset = 0;
	tlen = crd->crd_len;

	/* Process the data in GLXSB_MAX_AES_LEN chunks */
	while (tlen > 0) {
		len = (tlen > GLXSB_MAX_AES_LEN) ? GLXSB_MAX_AES_LEN : tlen;

		if (crp->crp_flags & CRYPTO_F_IMBUF)
			m_copydata((struct mbuf *)crp->crp_buf,
			    crd->crd_skip + offset, len, op_src);
		else if (crp->crp_flags & CRYPTO_F_IOV)
			cuio_copydata((struct uio *)crp->crp_buf,
			    crd->crd_skip + offset, len, op_src);
		else
			bcopy(crp->crp_buf + crd->crd_skip + offset, op_src,
			    len);

		glxsb_dma_pre_op(sc, &sc->sc_dma);

		glxsb_aes(sc, control, op_psrc, op_pdst, ses->ses_key,
		    len, op_iv);

		glxsb_dma_post_op(sc, &sc->sc_dma);

		if (crp->crp_flags & CRYPTO_F_IMBUF)
			err = m_copyback((struct mbuf *)crp->crp_buf,
			    crd->crd_skip + offset, len, op_dst, M_NOWAIT);
		else if (crp->crp_flags & CRYPTO_F_IOV)
			cuio_copyback((struct uio *)crp->crp_buf,
			    crd->crd_skip + offset, len, op_dst);
		else
			bcopy(op_dst, crp->crp_buf + crd->crd_skip + offset,
			    len);
		if (err)
			break;

		offset += len;
		tlen -= len;

		if (tlen > 0) {
			/* Copy out last block for use as next iteration */
			if (crd->crd_flags & CRD_F_ENCRYPT)
				bcopy(op_dst + len - sizeof(op_iv), op_iv,
				    sizeof(op_iv));
			else
				bcopy(op_src + len - sizeof(op_iv), op_iv,
				    sizeof(op_iv));
		}
	}

	/* All AES processing has now been done. */
	explicit_bzero(sc->sc_dma.dma_vaddr, xlen * 2);

out:
	return (err);
}

int
glxsb_crypto_process(struct cryptop *crp)
{
	struct glxsb_softc *sc = glxsb_sc;
	struct glxsb_session *ses;
	struct cryptodesc *crd;
	int sesn,err = 0;
	int s, i;

	s = splnet();

	KASSERT(crp->crp_ndesc >= 1);

	sesn = GLXSB_SESSION(crp->crp_sid);
	if (sesn >= sc->sc_nsessions) {
		err = EINVAL;
		goto out;
	}
	ses = &sc->sc_sessions[sesn];
	if (ses->ses_used == 0) {
		err = EINVAL;
		goto out;
	}

	for (i = 0; i < crp->crp_ndesc; i++) {
		crd = &crp->crp_desc[i];
		switch (crd->crd_alg) {
		case CRYPTO_AES_CBC:
			if (ses->ses_swd_enc) {
				if ((err = glxsb_crypto_swenc(crp, crd, ses->ses_swd_enc,
				    crp->crp_buf)) != 0)
					goto out;
			} else if ((err = glxsb_crypto_encdec(crp, crd, ses, sc,
			    crp->crp_buf)) != 0)
				goto out;
			break;

		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
			if ((err = glxsb_crypto_swauth(crp, crd, ses->ses_swd_auth,
			    crp->crp_buf)) != 0)
				goto out;
			break;

		default:
			err = EINVAL;
			goto out;
		}
	}

out:
	splx(s);
	return (err);
}

int
glxsb_dma_alloc(struct glxsb_softc *sc, int size, struct glxsb_dma_map *dma)
{
	int rc;

	dma->dma_nsegs = 1;
	dma->dma_size = size;

	rc = bus_dmamap_create(sc->sc_dmat, size, dma->dma_nsegs, size,
	    0, BUS_DMA_NOWAIT, &dma->dma_map);
	if (rc != 0) {
		printf("%s: couldn't create DMA map for %d bytes (%d)\n",
		    sc->sc_dev.dv_xname, size, rc);

		goto fail0;
	}

	rc = bus_dmamem_alloc(sc->sc_dmat, size, SB_AES_ALIGN, 0,
	    &dma->dma_seg, dma->dma_nsegs, &dma->dma_nsegs, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: couldn't allocate DMA memory of %d bytes (%d)\n",
		    sc->sc_dev.dv_xname, size, rc);

		goto fail1;
	}

	rc = bus_dmamem_map(sc->sc_dmat, &dma->dma_seg, 1, size,
	    &dma->dma_vaddr, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: couldn't map DMA memory for %d bytes (%d)\n",
		    sc->sc_dev.dv_xname, size, rc);

		goto fail2;
	}

	rc = bus_dmamap_load(sc->sc_dmat, dma->dma_map, dma->dma_vaddr,
	    size, NULL, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: couldn't load DMA memory for %d bytes (%d)\n",
		    sc->sc_dev.dv_xname, size, rc);

		goto fail3;
	}

	dma->dma_paddr = dma->dma_map->dm_segs[0].ds_addr;

	return 0;

fail3:
	bus_dmamem_unmap(sc->sc_dmat, dma->dma_vaddr, size);
fail2:
	bus_dmamem_free(sc->sc_dmat, &dma->dma_seg, dma->dma_nsegs);
fail1:
	bus_dmamap_destroy(sc->sc_dmat, dma->dma_map);
fail0:
	return rc;
}

void
glxsb_dma_pre_op(struct glxsb_softc *sc, struct glxsb_dma_map *dma)
{
	bus_dmamap_sync(sc->sc_dmat, dma->dma_map, 0, dma->dma_size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
glxsb_dma_post_op(struct glxsb_softc *sc, struct glxsb_dma_map *dma)
{
	bus_dmamap_sync(sc->sc_dmat, dma->dma_map, 0, dma->dma_size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
}

void
glxsb_dma_free(struct glxsb_softc *sc, struct glxsb_dma_map *dma)
{
	bus_dmamap_unload(sc->sc_dmat, dma->dma_map);
	bus_dmamem_unmap(sc->sc_dmat, dma->dma_vaddr, dma->dma_size);
	bus_dmamem_free(sc->sc_dmat, &dma->dma_seg, dma->dma_nsegs);
	bus_dmamap_destroy(sc->sc_dmat, dma->dma_map);
}

#endif /* CRYPTO */
