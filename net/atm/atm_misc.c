/* net/atm/atm_misc.c - Various functions for use by ATM drivers */

/* Written 1995-2000 by Werner Almesberger, EPFL ICA */


#include <linux/module.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/skbuff.h>
#include <linux/sonet.h>
#include <linux/bitops.h>
#include <asm/atomic.h>
#include <asm/errno.h>


int atm_charge(struct atm_vcc *vcc,int truesize)
{
	atm_force_charge(vcc,truesize);
	if (atomic_read(&sk_atm(vcc)->sk_rmem_alloc) <= sk_atm(vcc)->sk_rcvbuf)
		return 1;
	atm_return(vcc,truesize);
	atomic_inc(&vcc->stats->rx_drop);
	return 0;
}


struct sk_buff *atm_alloc_charge(struct atm_vcc *vcc,int pdu_size,
    gfp_t gfp_flags)
{
	struct sock *sk = sk_atm(vcc);
	int guess = atm_guess_pdu2truesize(pdu_size);

	atm_force_charge(vcc,guess);
	if (atomic_read(&sk->sk_rmem_alloc) <= sk->sk_rcvbuf) {
		struct sk_buff *skb = alloc_skb(pdu_size,gfp_flags);

		if (skb) {
			atomic_add(skb->truesize-guess,
				   &sk->sk_rmem_alloc);
			return skb;
		}
	}
	atm_return(vcc,guess);
	atomic_inc(&vcc->stats->rx_drop);
	return NULL;
}


/*
 * atm_pcr_goal returns the positive PCR if it should be rounded up, the
 * negative PCR if it should be rounded down, and zero if the maximum available
 * bandwidth should be used.
 *
 * The rules are as follows (* = maximum, - = absent (0), x = value "x",
 * (x+ = x or next value above x, x- = x or next value below):
 *
 *	min max pcr	result		min max pcr	result
 *	-   -   -	* (UBR only)	x   -   -	x+
 *	-   -   *	*		x   -   *	*
 *	-   -   z	z-		x   -   z	z-
 *	-   *   -	*		x   *   -	x+
 *	-   *   *	*		x   *   *	*
 *	-   *   z	z-		x   *   z	z-
 *	-   y   -	y-		x   y   -	x+
 *	-   y   *	y-		x   y   *	y-
 *	-   y   z	z-		x   y   z	z-
 *
 * All non-error cases can be converted with the following simple set of rules:
 *
 *   if pcr == z then z-
 *   else if min == x && pcr == - then x+
 *     else if max == y then y-
 *	 else *
 */


int atm_pcr_goal(const struct atm_trafprm *tp)
{
	if (tp->pcr && tp->pcr != ATM_MAX_PCR)
		return -tp->pcr;
	if (tp->min_pcr && !tp->pcr)
		return tp->min_pcr;
	if (tp->max_pcr != ATM_MAX_PCR)
		return -tp->max_pcr;
	return 0;
}


void sonet_copy_stats(struct k_sonet_stats *from,struct sonet_stats *to)
{
#define __HANDLE_ITEM(i) to->i = atomic_read(&from->i)
	__SONET_ITEMS
#undef __HANDLE_ITEM
}


void sonet_subtract_stats(struct k_sonet_stats *from,struct sonet_stats *to)
{
#define __HANDLE_ITEM(i) atomic_sub(to->i,&from->i)
	__SONET_ITEMS
#undef __HANDLE_ITEM
}


EXPORT_SYMBOL(atm_charge);
EXPORT_SYMBOL(atm_alloc_charge);
EXPORT_SYMBOL(atm_pcr_goal);
EXPORT_SYMBOL(sonet_copy_stats);
EXPORT_SYMBOL(sonet_subtract_stats);
