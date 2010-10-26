#ifndef LINUX_IFX_MODEM_H
#define LINUX_IFX_MODEM_H

struct ifx_modem_platform_data {
	unsigned short rst_out; /* modem reset out */
	unsigned short pwr_on;  /* power on */
	unsigned short rst_pmu; /* reset modem */
	unsigned short tx_pwr;  /* modem power threshold */
	unsigned short srdy;    /* SRDY */
	unsigned short mrdy;    /* MRDY */
	unsigned short is_6160;	/* Modem type */
};

#endif
