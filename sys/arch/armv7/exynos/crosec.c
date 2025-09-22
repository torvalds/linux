/* $OpenBSD: crosec.c,v 1.5 2021/10/24 17:52:27 mpi Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <armv7/exynos/crosecvar.h>

#ifdef DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

int	cros_ec_match(struct device *, void *, void *);
void	cros_ec_attach(struct device *, struct device *, void *);

int	cros_ec_send_command(struct cros_ec_softc *, uint8_t,
		int, const void *, int, uint8_t **, int);
int	cros_ec_command(struct cros_ec_softc *, uint8_t,
		int, const void *, int, void *, int);
int	cros_ec_command_inptr(struct cros_ec_softc *, uint8_t,
		int, const void *, int, uint8_t **, int);
int	cros_ec_i2c_command(struct cros_ec_softc *, uint8_t,
		int, const uint8_t *, int, uint8_t **, int);
int	cros_ec_calc_checksum(const uint8_t *, int);

const struct cfattach crosec_ca = {
	sizeof(struct cros_ec_softc), cros_ec_match, cros_ec_attach
};

struct cfdriver crosec_cd = {
	NULL, "crosec", DV_DULL
};

int
cros_ec_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "google,cros-ec-i2c") == 0)
		return 1;
	return 0;
}

void
cros_ec_attach(struct device *parent, struct device *self, void *aux)
{
	struct cros_ec_softc *sc = (struct cros_ec_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf("\n");

	if (cros_ec_check_version(sc)) {
		printf("%s: could not initialize ChromeOS EC\n", __func__);
		return;
	}

	if (cros_ec_init_keyboard(sc)) {
		printf("%s: could not initialize keyboard\n", __func__);
		return;
	}
}

int
cros_ec_check_version(struct cros_ec_softc *sc)
{
	struct ec_params_hello req;
	struct ec_response_hello *resp;

	sc->cmd_version_is_supported = 1;
	if (cros_ec_command_inptr(sc, EC_CMD_HELLO, 0, &req, sizeof(req),
				(uint8_t **)&resp, sizeof(*resp)) > 0) {
		/* new version supported */
		sc->cmd_version_is_supported = 1;
	} else {
		printf("%s: old EC interface not supported\n", __func__);
		return -1;
	}

	return 0;
}

int
cros_ec_i2c_command(struct cros_ec_softc *sc, uint8_t cmd, int cmd_version,
		const uint8_t *out, int out_len, uint8_t **in, int in_len)
{
	int out_bytes, in_bytes, ret;
	uint8_t *ptr = sc->out;
	uint8_t *inptr = sc->in;

	inptr += sizeof(uint64_t); /* returned data should be 64-bit aligned */
	if (!sc->cmd_version_is_supported) {
		/* old-style */
		*ptr++ = cmd;
		out_bytes = out_len + 1;
		in_bytes = in_len + 2;
		inptr--; /* status byte */
	} else {
		/* new-style */
		*ptr++ = EC_CMD_VERSION0 + cmd_version;
		*ptr++ = cmd;
		*ptr++ = out_len;
		out_bytes = out_len + 4;
		in_bytes = in_len + 3;
		inptr -= 2; /* status byte, length */
	}
	memcpy(ptr, out, out_len);
	ptr += out_len;

	if (sc->cmd_version_is_supported)
		*ptr++ = (uint8_t)
			 cros_ec_calc_checksum(sc->out, out_len + 3);

	iic_acquire_bus(sc->sc_tag, 0);
	ret = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, NULL, 0, &sc->out, out_bytes, 0);
	if (ret) {
		DPRINTF(("%s: I2C write failed\n", __func__));
		iic_release_bus(sc->sc_tag, 0);
		return -1;
	}

	ret = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, NULL, 0, inptr, in_bytes, 0);
	if (ret) {
		DPRINTF(("%s: I2C read failed\n", __func__));
		iic_release_bus(sc->sc_tag, 0);
		return -1;
	}

	iic_release_bus(sc->sc_tag, 0);

	if (*inptr != EC_RES_SUCCESS) {
		DPRINTF(("%s: bad result\n", __func__));
		return -(int)*inptr;
	}

	if (sc->cmd_version_is_supported) {
		int len, csum;

		len = inptr[1];
		if (len > sizeof(sc->in)) {
			DPRINTF(("%s: Received length too large\n", __func__));
			return -1;
		}
		csum = cros_ec_calc_checksum(inptr, 2 + len);
		if (csum != inptr[2 + len]) {
			DPRINTF(("%s: Invalid checksum\n", __func__));
			return -1;
		}
		in_len = min(in_len, len);
	}

	*in = sc->in + sizeof(uint64_t);

	return in_len;
}

int
cros_ec_send_command(struct cros_ec_softc *sc, uint8_t cmd, int cmd_version,
		const void *out, int out_len, uint8_t **in, int in_len)
{
	return cros_ec_i2c_command(sc, cmd, cmd_version,
				(const uint8_t *)out, out_len, in, in_len);
}

int
cros_ec_command(struct cros_ec_softc *sc, uint8_t cmd,
		int cmd_version, const void *out, int out_len,
		void *in, int in_len) {
	uint8_t *in_buffer;
	int len;

	len = cros_ec_command_inptr(sc, cmd, cmd_version, out, out_len,
			&in_buffer, in_len);

	if (len > 0) {
		if (in && in_buffer) {
			len = min(in_len, len);
			memmove(in, in_buffer, len);
		}
	}
	return len;
}

int
cros_ec_command_inptr(struct cros_ec_softc *sc, uint8_t cmd,
		int cmd_version, const void *out, int out_len,
		uint8_t **inp, int in_len) {
	uint8_t *in;
	int len;

	len = cros_ec_send_command(sc, cmd, cmd_version,
			(const uint8_t *)out, out_len, &in, in_len);

	/* Wait for the command to complete. */
	if (len == -EC_RES_IN_PROGRESS) {
		struct ec_response_get_comms_status *resp;

		do {
			int ret;

			delay(50000);
			ret = cros_ec_send_command(sc, EC_CMD_GET_COMMS_STATUS, 0,
					NULL, 0,
					(uint8_t **)&resp, sizeof(*resp));
			if (ret < 0)
				return ret;

			//timeout CROS_EC_CMD_TIMEOUT_MS
			//return -EC_RES_TIMEOUT
		} while (resp->flags & EC_COMMS_STATUS_PROCESSING);

		/* Let's get the response. */
		len = cros_ec_send_command(sc, EC_CMD_RESEND_RESPONSE, 0,
				out, out_len, &in, in_len);
	}

	if (inp != NULL)
		*inp = in;

	return len;
}

int
cros_ec_calc_checksum(const uint8_t *data, int size)
{
	int csum, i;

	for (i = csum = 0; i < size; i++)
		csum += data[i];
	return csum & 0xff;
}

int
cros_ec_scan_keyboard(struct cros_ec_softc *sc, uint8_t *scan, int len)
{
	if (cros_ec_command(sc, EC_CMD_CROS_EC_STATE, 0, NULL, 0, scan,
			len) < len)
		return -1;

	return 0;
}

int
cros_ec_info(struct cros_ec_softc *sc, struct ec_response_cros_ec_info *info)
{
	if (cros_ec_command(sc, EC_CMD_CROS_EC_INFO, 0, NULL, 0, info,
				sizeof(*info)) < sizeof(*info))
		return -1;

	return 0;
}
