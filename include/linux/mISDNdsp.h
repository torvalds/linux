/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __mISDNdsp_H__
#define __mISDNdsp_H__

struct mISDN_dsp_element_arg {
	char	*name;
	char	*def;
	char	*desc;
};

struct mISDN_dsp_element {
	char	*name;
	void	*(*new)(const char *arg);
	void	(*free)(void *p);
	void	(*process_tx)(void *p, unsigned char *data, int len);
	void	(*process_rx)(void *p, unsigned char *data, int len,
			unsigned int txlen);
	int	num_args;
	struct mISDN_dsp_element_arg
		*args;
};

extern int  mISDN_dsp_element_register(struct mISDN_dsp_element *elem);
extern void mISDN_dsp_element_unregister(struct mISDN_dsp_element *elem);

struct dsp_features {
	int	hfc_id; /* unique id to identify the chip (or -1) */
	int	hfc_dtmf; /* set if HFCmulti card supports dtmf */
	int	hfc_conf; /* set if HFCmulti card supports conferences */
	int	hfc_loops; /* set if card supports tone loops */
	int	hfc_echocanhw; /* set if card supports echocancelation*/
	int	pcm_id; /* unique id to identify the pcm bus (or -1) */
	int	pcm_slots; /* number of slots on the pcm bus */
	int	pcm_banks; /* number of IO banks of pcm bus */
	int	unclocked; /* data is not clocked (has jitter/loss) */
	int	unordered; /* data is unordered (packets have index) */
};

#endif

