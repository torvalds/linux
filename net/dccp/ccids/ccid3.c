/*
 *  net/dccp/ccids/ccid3.c
 *
 *  Copyright (c) 2005 The University of Waikato, Hamilton, New Zealand.
 *
 *  An implementation of the DCCP protocol
 *
 *  This code has been developed by the University of Waikato WAND
 *  research group. For further information please see http://www.wand.net.nz/
 *  or e-mail Ian McDonald - iam4@cs.waikato.ac.nz
 *
 *  This code also uses code from Lulea University, rereleased as GPL by its
 *  authors:
 *  Copyright (c) 2003 Nils-Erik Mattsson, Joacim Haggmark, Magnus Erixzon
 *
 *  Changes to meet Linux coding standards, to make it meet latest ccid3 draft
 *  and to make it work as a loadable module in the DCCP stack written by
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>.
 *
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "../ccid.h"
#include "../dccp.h"
#include "ccid3.h"

#ifdef CCID3_DEBUG
extern int ccid3_debug;

#define ccid3_pr_debug(format, a...) \
	do { if (ccid3_debug) \
		printk(KERN_DEBUG "%s: " format, __FUNCTION__, ##a); \
	} while (0)
#else
#define ccid3_pr_debug(format, a...)
#endif

#define TFRC_MIN_PACKET_SIZE	   16
#define TFRC_STD_PACKET_SIZE	  256
#define TFRC_MAX_PACKET_SIZE	65535

#define USEC_IN_SEC                1000000

#define TFRC_INITIAL_TIMEOUT	   (2 * USEC_IN_SEC)
/* two seconds as per CCID3 spec 11 */

#define TFRC_OPSYS_HALF_TIME_GRAN	(USEC_IN_SEC / (2 * HZ))
/* above is in usecs - half the scheduling granularity as per RFC3448 4.6 */

#define TFRC_WIN_COUNT_PER_RTT	    4
#define TFRC_WIN_COUNT_LIMIT	   16

#define TFRC_MAX_BACK_OFF_TIME	   64
/* above is in seconds */

#define TFRC_SMALLEST_P		   40

#define TFRC_RECV_IVAL_F_LENGTH	    8          /* length(w[]) */

/* Number of later packets received before one is considered lost */
#define TFRC_RECV_NUM_LATE_LOSS	3

enum ccid3_options {
	TFRC_OPT_LOSS_EVENT_RATE = 192,
	TFRC_OPT_LOSS_INTERVALS	 = 193,
	TFRC_OPT_RECEIVE_RATE	 = 194,
};

static int ccid3_debug;

static kmem_cache_t *ccid3_tx_hist_slab;
static kmem_cache_t *ccid3_rx_hist_slab;
static kmem_cache_t *ccid3_loss_interval_hist_slab;

static inline struct ccid3_tx_hist_entry *ccid3_tx_hist_entry_new(int prio)
{
	struct ccid3_tx_hist_entry *entry = kmem_cache_alloc(ccid3_tx_hist_slab, prio);

	if (entry != NULL)
		entry->ccid3htx_sent = 0;

	return entry;
}

static inline void ccid3_tx_hist_entry_delete(struct ccid3_tx_hist_entry *entry)
{
	if (entry != NULL)
		kmem_cache_free(ccid3_tx_hist_slab, entry);
}

static inline struct ccid3_rx_hist_entry *ccid3_rx_hist_entry_new(struct sock *sk,
								  struct sk_buff *skb,
								  int prio)
{
	struct ccid3_rx_hist_entry *entry = kmem_cache_alloc(ccid3_rx_hist_slab, prio);

	if (entry != NULL) {
		const struct dccp_hdr *dh = dccp_hdr(skb);

		entry->ccid3hrx_seqno	  = DCCP_SKB_CB(skb)->dccpd_seq;
		entry->ccid3hrx_win_count = dh->dccph_ccval;
		entry->ccid3hrx_type	  = dh->dccph_type;
		entry->ccid3hrx_ndp 	  = dccp_sk(sk)->dccps_options_received.dccpor_ndp;
		do_gettimeofday(&(entry->ccid3hrx_tstamp));
	}

	return entry;
}

static inline void ccid3_rx_hist_entry_delete(struct ccid3_rx_hist_entry *entry)
{
	if (entry != NULL)
		kmem_cache_free(ccid3_rx_hist_slab, entry);
}

static void ccid3_rx_history_delete(struct list_head *hist)
{
	struct ccid3_rx_hist_entry *entry, *next;

	list_for_each_entry_safe(entry, next, hist, ccid3hrx_node) {
		list_del_init(&entry->ccid3hrx_node);
		kmem_cache_free(ccid3_rx_hist_slab, entry);
	}
}

static inline struct ccid3_loss_interval_hist_entry *ccid3_loss_interval_hist_entry_new(int prio)
{
	return kmem_cache_alloc(ccid3_loss_interval_hist_slab, prio);
}

static inline void ccid3_loss_interval_hist_entry_delete(struct ccid3_loss_interval_hist_entry *entry)
{
	if (entry != NULL)
		kmem_cache_free(ccid3_loss_interval_hist_slab, entry);
}

static void ccid3_loss_interval_history_delete(struct list_head *hist)
{
	struct ccid3_loss_interval_hist_entry *entry, *next;

	list_for_each_entry_safe(entry, next, hist, ccid3lih_node) {
		list_del_init(&entry->ccid3lih_node);
		kmem_cache_free(ccid3_loss_interval_hist_slab, entry);
	}
}

static int ccid3_init(struct sock *sk)
{
	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);
	return 0;
}

static void ccid3_exit(struct sock *sk)
{
	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);
}

/* TFRC sender states */
enum ccid3_hc_tx_states {
       	TFRC_SSTATE_NO_SENT = 1,
	TFRC_SSTATE_NO_FBACK,
	TFRC_SSTATE_FBACK,
	TFRC_SSTATE_TERM,
};

#ifdef CCID3_DEBUG
static const char *ccid3_tx_state_name(enum ccid3_hc_tx_states state)
{
	static char *ccid3_state_names[] = {
	[TFRC_SSTATE_NO_SENT]  = "NO_SENT",
	[TFRC_SSTATE_NO_FBACK] = "NO_FBACK",
	[TFRC_SSTATE_FBACK]    = "FBACK",
	[TFRC_SSTATE_TERM]     = "TERM",
	};

	return ccid3_state_names[state];
}
#endif

static inline void ccid3_hc_tx_set_state(struct sock *sk, enum ccid3_hc_tx_states state)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	enum ccid3_hc_tx_states oldstate = hctx->ccid3hctx_state;

	ccid3_pr_debug("%s(%p) %-8.8s -> %s\n",
		       dccp_role(sk), sk, ccid3_tx_state_name(oldstate), ccid3_tx_state_name(state));
	WARN_ON(state == oldstate);
	hctx->ccid3hctx_state = state;
}

static void timeval_sub(struct timeval large, struct timeval small, struct timeval *result) {

	result->tv_sec = large.tv_sec-small.tv_sec;
	if (large.tv_usec < small.tv_usec) {
		(result->tv_sec)--;
		result->tv_usec = USEC_IN_SEC+large.tv_usec-small.tv_usec;
	} else
		result->tv_usec = large.tv_usec-small.tv_usec;
}

static inline void timeval_fix(struct timeval *tv) {
	if (tv->tv_usec >= USEC_IN_SEC) {
		tv->tv_sec++;
		tv->tv_usec -= USEC_IN_SEC;
	}
}

/* returns the difference in usecs between timeval passed in and current time */
static inline u32 now_delta(struct timeval tv) {
	struct timeval now;
	
	do_gettimeofday(&now);
	return ((now.tv_sec-tv.tv_sec)*1000000+now.tv_usec-tv.tv_usec);
}

#define CALCX_ARRSIZE 500

#define CALCX_SPLIT 50000
/* equivalent to 0.05 */

static const u32 calcx_lookup[CALCX_ARRSIZE][2] = {
	{ 37172 , 8172 },
	{ 53499 , 11567 },
	{ 66664 , 14180 },
	{ 78298 , 16388 },
	{ 89021 , 18339 },
	{ 99147 , 20108 },
	{ 108858 , 21738 },
	{ 118273 , 23260 },
	{ 127474 , 24693 },
	{ 136520 , 26052 },
	{ 145456 , 27348 },
	{ 154316 , 28589 },
	{ 163130 , 29783 },
	{ 171919 , 30935 },
	{ 180704 , 32049 },
	{ 189502 , 33130 },
	{ 198328 , 34180 },
	{ 207194 , 35202 },
	{ 216114 , 36198 },
	{ 225097 , 37172 },
	{ 234153 , 38123 },
	{ 243294 , 39055 },
	{ 252527 , 39968 },
	{ 261861 , 40864 },
	{ 271305 , 41743 },
	{ 280866 , 42607 },
	{ 290553 , 43457 },
	{ 300372 , 44293 },
	{ 310333 , 45117 },
	{ 320441 , 45929 },
	{ 330705 , 46729 },
	{ 341131 , 47518 },
	{ 351728 , 48297 },
	{ 362501 , 49066 },
	{ 373460 , 49826 },
	{ 384609 , 50577 },
	{ 395958 , 51320 },
	{ 407513 , 52054 },
	{ 419281 , 52780 },
	{ 431270 , 53499 },
	{ 443487 , 54211 },
	{ 455940 , 54916 },
	{ 468635 , 55614 },
	{ 481581 , 56306 },
	{ 494785 , 56991 },
	{ 508254 , 57671 },
	{ 521996 , 58345 },
	{ 536019 , 59014 },
	{ 550331 , 59677 },
	{ 564939 , 60335 },
	{ 579851 , 60988 },
	{ 595075 , 61636 },
	{ 610619 , 62279 },
	{ 626491 , 62918 },
	{ 642700 , 63553 },
	{ 659253 , 64183 },
	{ 676158 , 64809 },
	{ 693424 , 65431 },
	{ 711060 , 66050 },
	{ 729073 , 66664 },
	{ 747472 , 67275 },
	{ 766266 , 67882 },
	{ 785464 , 68486 },
	{ 805073 , 69087 },
	{ 825103 , 69684 },
	{ 845562 , 70278 },
	{ 866460 , 70868 },
	{ 887805 , 71456 },
	{ 909606 , 72041 },
	{ 931873 , 72623 },
	{ 954614 , 73202 },
	{ 977839 , 73778 },
	{ 1001557 , 74352 },
	{ 1025777 , 74923 },
	{ 1050508 , 75492 },
	{ 1075761 , 76058 },
	{ 1101544 , 76621 },
	{ 1127867 , 77183 },
	{ 1154739 , 77741 },
	{ 1182172 , 78298 },
	{ 1210173 , 78852 },
	{ 1238753 , 79405 },
	{ 1267922 , 79955 },
	{ 1297689 , 80503 },
	{ 1328066 , 81049 },
	{ 1359060 , 81593 },
	{ 1390684 , 82135 },
	{ 1422947 , 82675 },
	{ 1455859 , 83213 },
	{ 1489430 , 83750 },
	{ 1523671 , 84284 },
	{ 1558593 , 84817 },
	{ 1594205 , 85348 },
	{ 1630518 , 85878 },
	{ 1667543 , 86406 },
	{ 1705290 , 86932 },
	{ 1743770 , 87457 },
	{ 1782994 , 87980 },
	{ 1822973 , 88501 },
	{ 1863717 , 89021 },
	{ 1905237 , 89540 },
	{ 1947545 , 90057 },
	{ 1990650 , 90573 },
	{ 2034566 , 91087 },
	{ 2079301 , 91600 },
	{ 2124869 , 92111 },
	{ 2171279 , 92622 },
	{ 2218543 , 93131 },
	{ 2266673 , 93639 },
	{ 2315680 , 94145 },
	{ 2365575 , 94650 },
	{ 2416371 , 95154 },
	{ 2468077 , 95657 },
	{ 2520707 , 96159 },
	{ 2574271 , 96660 },
	{ 2628782 , 97159 },
	{ 2684250 , 97658 },
	{ 2740689 , 98155 },
	{ 2798110 , 98651 },
	{ 2856524 , 99147 },
	{ 2915944 , 99641 },
	{ 2976382 , 100134 },
	{ 3037850 , 100626 },
	{ 3100360 , 101117 },
	{ 3163924 , 101608 },
	{ 3228554 , 102097 },
	{ 3294263 , 102586 },
	{ 3361063 , 103073 },
	{ 3428966 , 103560 },
	{ 3497984 , 104045 },
	{ 3568131 , 104530 },
	{ 3639419 , 105014 },
	{ 3711860 , 105498 },
	{ 3785467 , 105980 },
	{ 3860253 , 106462 },
	{ 3936229 , 106942 },
	{ 4013410 , 107422 },
	{ 4091808 , 107902 },
	{ 4171435 , 108380 },
	{ 4252306 , 108858 },
	{ 4334431 , 109335 },
	{ 4417825 , 109811 },
	{ 4502501 , 110287 },
	{ 4588472 , 110762 },
	{ 4675750 , 111236 },
	{ 4764349 , 111709 },
	{ 4854283 , 112182 },
	{ 4945564 , 112654 },
	{ 5038206 , 113126 },
	{ 5132223 , 113597 },
	{ 5227627 , 114067 },
	{ 5324432 , 114537 },
	{ 5422652 , 115006 },
	{ 5522299 , 115474 },
	{ 5623389 , 115942 },
	{ 5725934 , 116409 },
	{ 5829948 , 116876 },
	{ 5935446 , 117342 },
	{ 6042439 , 117808 },
	{ 6150943 , 118273 },
	{ 6260972 , 118738 },
	{ 6372538 , 119202 },
	{ 6485657 , 119665 },
	{ 6600342 , 120128 },
	{ 6716607 , 120591 },
	{ 6834467 , 121053 },
	{ 6953935 , 121514 },
	{ 7075025 , 121976 },
	{ 7197752 , 122436 },
	{ 7322131 , 122896 },
	{ 7448175 , 123356 },
	{ 7575898 , 123815 },
	{ 7705316 , 124274 },
	{ 7836442 , 124733 },
	{ 7969291 , 125191 },
	{ 8103877 , 125648 },
	{ 8240216 , 126105 },
	{ 8378321 , 126562 },
	{ 8518208 , 127018 },
	{ 8659890 , 127474 },
	{ 8803384 , 127930 },
	{ 8948702 , 128385 },
	{ 9095861 , 128840 },
	{ 9244875 , 129294 },
	{ 9395760 , 129748 },
	{ 9548529 , 130202 },
	{ 9703198 , 130655 },
	{ 9859782 , 131108 },
	{ 10018296 , 131561 },
	{ 10178755 , 132014 },
	{ 10341174 , 132466 },
	{ 10505569 , 132917 },
	{ 10671954 , 133369 },
	{ 10840345 , 133820 },
	{ 11010757 , 134271 },
	{ 11183206 , 134721 },
	{ 11357706 , 135171 },
	{ 11534274 , 135621 },
	{ 11712924 , 136071 },
	{ 11893673 , 136520 },
	{ 12076536 , 136969 },
	{ 12261527 , 137418 },
	{ 12448664 , 137867 },
	{ 12637961 , 138315 },
	{ 12829435 , 138763 },
	{ 13023101 , 139211 },
	{ 13218974 , 139658 },
	{ 13417071 , 140106 },
	{ 13617407 , 140553 },
	{ 13819999 , 140999 },
	{ 14024862 , 141446 },
	{ 14232012 , 141892 },
	{ 14441465 , 142339 },
	{ 14653238 , 142785 },
	{ 14867346 , 143230 },
	{ 15083805 , 143676 },
	{ 15302632 , 144121 },
	{ 15523842 , 144566 },
	{ 15747453 , 145011 },
	{ 15973479 , 145456 },
	{ 16201939 , 145900 },
	{ 16432847 , 146345 },
	{ 16666221 , 146789 },
	{ 16902076 , 147233 },
	{ 17140429 , 147677 },
	{ 17381297 , 148121 },
	{ 17624696 , 148564 },
	{ 17870643 , 149007 },
	{ 18119154 , 149451 },
	{ 18370247 , 149894 },
	{ 18623936 , 150336 },
	{ 18880241 , 150779 },
	{ 19139176 , 151222 },
	{ 19400759 , 151664 },
	{ 19665007 , 152107 },
	{ 19931936 , 152549 },
	{ 20201564 , 152991 },
	{ 20473907 , 153433 },
	{ 20748982 , 153875 },
	{ 21026807 , 154316 },
	{ 21307399 , 154758 },
	{ 21590773 , 155199 },
	{ 21876949 , 155641 },
	{ 22165941 , 156082 },
	{ 22457769 , 156523 },
	{ 22752449 , 156964 },
	{ 23049999 , 157405 },
	{ 23350435 , 157846 },
	{ 23653774 , 158287 },
	{ 23960036 , 158727 },
	{ 24269236 , 159168 },
	{ 24581392 , 159608 },
	{ 24896521 , 160049 },
	{ 25214642 , 160489 },
	{ 25535772 , 160929 },
	{ 25859927 , 161370 },
	{ 26187127 , 161810 },
	{ 26517388 , 162250 },
	{ 26850728 , 162690 },
	{ 27187165 , 163130 },
	{ 27526716 , 163569 },
	{ 27869400 , 164009 },
	{ 28215234 , 164449 },
	{ 28564236 , 164889 },
	{ 28916423 , 165328 },
	{ 29271815 , 165768 },
	{ 29630428 , 166208 },
	{ 29992281 , 166647 },
	{ 30357392 , 167087 },
	{ 30725779 , 167526 },
	{ 31097459 , 167965 },
	{ 31472452 , 168405 },
	{ 31850774 , 168844 },
	{ 32232445 , 169283 },
	{ 32617482 , 169723 },
	{ 33005904 , 170162 },
	{ 33397730 , 170601 },
	{ 33792976 , 171041 },
	{ 34191663 , 171480 },
	{ 34593807 , 171919 },
	{ 34999428 , 172358 },
	{ 35408544 , 172797 },
	{ 35821174 , 173237 },
	{ 36237335 , 173676 },
	{ 36657047 , 174115 },
	{ 37080329 , 174554 },
	{ 37507197 , 174993 },
	{ 37937673 , 175433 },
	{ 38371773 , 175872 },
	{ 38809517 , 176311 },
	{ 39250924 , 176750 },
	{ 39696012 , 177190 },
	{ 40144800 , 177629 },
	{ 40597308 , 178068 },
	{ 41053553 , 178507 },
	{ 41513554 , 178947 },
	{ 41977332 , 179386 },
	{ 42444904 , 179825 },
	{ 42916290 , 180265 },
	{ 43391509 , 180704 },
	{ 43870579 , 181144 },
	{ 44353520 , 181583 },
	{ 44840352 , 182023 },
	{ 45331092 , 182462 },
	{ 45825761 , 182902 },
	{ 46324378 , 183342 },
	{ 46826961 , 183781 },
	{ 47333531 , 184221 },
	{ 47844106 , 184661 },
	{ 48358706 , 185101 },
	{ 48877350 , 185541 },
	{ 49400058 , 185981 },
	{ 49926849 , 186421 },
	{ 50457743 , 186861 },
	{ 50992759 , 187301 },
	{ 51531916 , 187741 },
	{ 52075235 , 188181 },
	{ 52622735 , 188622 },
	{ 53174435 , 189062 },
	{ 53730355 , 189502 },
	{ 54290515 , 189943 },
	{ 54854935 , 190383 },
	{ 55423634 , 190824 },
	{ 55996633 , 191265 },
	{ 56573950 , 191706 },
	{ 57155606 , 192146 },
	{ 57741621 , 192587 },
	{ 58332014 , 193028 },
	{ 58926806 , 193470 },
	{ 59526017 , 193911 },
	{ 60129666 , 194352 },
	{ 60737774 , 194793 },
	{ 61350361 , 195235 },
	{ 61967446 , 195677 },
	{ 62589050 , 196118 },
	{ 63215194 , 196560 },
	{ 63845897 , 197002 },
	{ 64481179 , 197444 },
	{ 65121061 , 197886 },
	{ 65765563 , 198328 },
	{ 66414705 , 198770 },
	{ 67068508 , 199213 },
	{ 67726992 , 199655 },
	{ 68390177 , 200098 },
	{ 69058085 , 200540 },
	{ 69730735 , 200983 },
	{ 70408147 , 201426 },
	{ 71090343 , 201869 },
	{ 71777343 , 202312 },
	{ 72469168 , 202755 },
	{ 73165837 , 203199 },
	{ 73867373 , 203642 },
	{ 74573795 , 204086 },
	{ 75285124 , 204529 },
	{ 76001380 , 204973 },
	{ 76722586 , 205417 },
	{ 77448761 , 205861 },
	{ 78179926 , 206306 },
	{ 78916102 , 206750 },
	{ 79657310 , 207194 },
	{ 80403571 , 207639 },
	{ 81154906 , 208084 },
	{ 81911335 , 208529 },
	{ 82672880 , 208974 },
	{ 83439562 , 209419 },
	{ 84211402 , 209864 },
	{ 84988421 , 210309 },
	{ 85770640 , 210755 },
	{ 86558080 , 211201 },
	{ 87350762 , 211647 },
	{ 88148708 , 212093 },
	{ 88951938 , 212539 },
	{ 89760475 , 212985 },
	{ 90574339 , 213432 },
	{ 91393551 , 213878 },
	{ 92218133 , 214325 },
	{ 93048107 , 214772 },
	{ 93883493 , 215219 },
	{ 94724314 , 215666 },
	{ 95570590 , 216114 },
	{ 96422343 , 216561 },
	{ 97279594 , 217009 },
	{ 98142366 , 217457 },
	{ 99010679 , 217905 },
	{ 99884556 , 218353 },
	{ 100764018 , 218801 },
	{ 101649086 , 219250 },
	{ 102539782 , 219698 },
	{ 103436128 , 220147 },
	{ 104338146 , 220596 },
	{ 105245857 , 221046 },
	{ 106159284 , 221495 },
	{ 107078448 , 221945 },
	{ 108003370 , 222394 },
	{ 108934074 , 222844 },
	{ 109870580 , 223294 },
	{ 110812910 , 223745 },
	{ 111761087 , 224195 },
	{ 112715133 , 224646 },
	{ 113675069 , 225097 },
	{ 114640918 , 225548 },
	{ 115612702 , 225999 },
	{ 116590442 , 226450 },
	{ 117574162 , 226902 },
	{ 118563882 , 227353 },
	{ 119559626 , 227805 },
	{ 120561415 , 228258 },
	{ 121569272 , 228710 },
	{ 122583219 , 229162 },
	{ 123603278 , 229615 },
	{ 124629471 , 230068 },
	{ 125661822 , 230521 },
	{ 126700352 , 230974 },
	{ 127745083 , 231428 },
	{ 128796039 , 231882 },
	{ 129853241 , 232336 },
	{ 130916713 , 232790 },
	{ 131986475 , 233244 },
	{ 133062553 , 233699 },
	{ 134144966 , 234153 },
	{ 135233739 , 234608 },
	{ 136328894 , 235064 },
	{ 137430453 , 235519 },
	{ 138538440 , 235975 },
	{ 139652876 , 236430 },
	{ 140773786 , 236886 },
	{ 141901190 , 237343 },
	{ 143035113 , 237799 },
	{ 144175576 , 238256 },
	{ 145322604 , 238713 },
	{ 146476218 , 239170 },
	{ 147636442 , 239627 },
	{ 148803298 , 240085 },
	{ 149976809 , 240542 },
	{ 151156999 , 241000 },
	{ 152343890 , 241459 },
	{ 153537506 , 241917 },
	{ 154737869 , 242376 },
	{ 155945002 , 242835 },
	{ 157158929 , 243294 },
	{ 158379673 , 243753 },
	{ 159607257 , 244213 },
	{ 160841704 , 244673 },
	{ 162083037 , 245133 },
	{ 163331279 , 245593 },
	{ 164586455 , 246054 },
	{ 165848586 , 246514 },
	{ 167117696 , 246975 },
	{ 168393810 , 247437 },
	{ 169676949 , 247898 },
	{ 170967138 , 248360 },
	{ 172264399 , 248822 },
	{ 173568757 , 249284 },
	{ 174880235 , 249747 },
	{ 176198856 , 250209 },
	{ 177524643 , 250672 },
	{ 178857621 , 251136 },
	{ 180197813 , 251599 },
	{ 181545242 , 252063 },
	{ 182899933 , 252527 },
	{ 184261908 , 252991 },
	{ 185631191 , 253456 },
	{ 187007807 , 253920 },
	{ 188391778 , 254385 },
	{ 189783129 , 254851 },
	{ 191181884 , 255316 },
	{ 192588065 , 255782 },
	{ 194001698 , 256248 },
	{ 195422805 , 256714 },
	{ 196851411 , 257181 },
	{ 198287540 , 257648 },
	{ 199731215 , 258115 },
	{ 201182461 , 258582 },
	{ 202641302 , 259050 },
	{ 204107760 , 259518 },
	{ 205581862 , 259986 },
	{ 207063630 , 260454 },
	{ 208553088 , 260923 },
	{ 210050262 , 261392 },
	{ 211555174 , 261861 },
	{ 213067849 , 262331 },
	{ 214588312 , 262800 },
	{ 216116586 , 263270 },
	{ 217652696 , 263741 },
	{ 219196666 , 264211 },
	{ 220748520 , 264682 },
	{ 222308282 , 265153 },
	{ 223875978 , 265625 },
	{ 225451630 , 266097 },
	{ 227035265 , 266569 },
	{ 228626905 , 267041 },
	{ 230226576 , 267514 },
	{ 231834302 , 267986 },
	{ 233450107 , 268460 },
	{ 235074016 , 268933 },
	{ 236706054 , 269407 },
	{ 238346244 , 269881 },
	{ 239994613 , 270355 },
	{ 241651183 , 270830 },
	{ 243315981 , 271305 }
};

/* Calculate the send rate as per section 3.1 of RFC3448
 
Returns send rate in bytes per second

Integer maths and lookups are used as not allowed floating point in kernel

The function for Xcalc as per section 3.1 of RFC3448 is:

X =                            s
     -------------------------------------------------------------
     R*sqrt(2*b*p/3) + (t_RTO * (3*sqrt(3*b*p/8) * p * (1+32*p^2)))

where 
X is the trasmit rate in bytes/second
s is the packet size in bytes
R is the round trip time in seconds
p is the loss event rate, between 0 and 1.0, of the number of loss events 
  as a fraction of the number of packets transmitted
t_RTO is the TCP retransmission timeout value in seconds
b is the number of packets acknowledged by a single TCP acknowledgement

we can assume that b = 1 and t_RTO is 4 * R. With this the equation becomes:

X =                            s
     -----------------------------------------------------------------------
     R * sqrt(2 * p / 3) + (12 * R * (sqrt(3 * p / 8) * p * (1 + 32 * p^2)))


which we can break down into:

X =     s
     --------
     R * f(p)

where f(p) = sqrt(2 * p / 3) + (12 * sqrt(3 * p / 8) * p * (1 + 32 * p * p))

Function parameters:
s - bytes
R - RTT in usecs
p - loss rate (decimal fraction multiplied by 1,000,000)

Returns Xcalc in bytes per second

DON'T alter this code unless you run test cases against it as the code
has been manipulated to stop underflow/overlow.

*/
static u32 ccid3_calc_x(u16 s, u32 R, u32 p)
{
	int index;
	u32 f;
	u64 tmp1, tmp2;

	if (p < CALCX_SPLIT)
		index = (p / (CALCX_SPLIT / CALCX_ARRSIZE)) - 1;
	else
		index = (p / (1000000 / CALCX_ARRSIZE)) - 1;

	if (index < 0)
		/* p should be 0 unless there is a bug in my code */
		index = 0;

	if (R == 0)
		R = 1; /* RTT can't be zero or else divide by zero */

	BUG_ON(index >= CALCX_ARRSIZE);

	if (p >= CALCX_SPLIT)
		f = calcx_lookup[index][0];
	else
		f = calcx_lookup[index][1];

	tmp1 = ((u64)s * 100000000);
	tmp2 = ((u64)R * (u64)f);
	do_div(tmp2,10000);
	do_div(tmp1,tmp2); 
	/* don't alter above math unless you test due to overflow on 32 bit */

	return (u32)tmp1; 
}

/* Calculate new t_ipi (inter packet interval) by t_ipi = s / X_inst */
static inline void ccid3_calc_new_t_ipi(struct ccid3_hc_tx_sock *hctx)
{
	if (hctx->ccid3hctx_state == TFRC_SSTATE_NO_FBACK)
		return;
	/* if no feedback spec says t_ipi is 1 second (set elsewhere and then 
	 * doubles after every no feedback timer (separate function) */
	
	if (hctx->ccid3hctx_x < 10) {
		ccid3_pr_debug("ccid3_calc_new_t_ipi - ccid3hctx_x < 10\n");
		hctx->ccid3hctx_x = 10;
	}
	hctx->ccid3hctx_t_ipi = (hctx->ccid3hctx_s * 100000) 
		/ (hctx->ccid3hctx_x / 10);
	/* reason for above maths with 10 in there is to avoid 32 bit
	 * overflow for jumbo packets */

}

/* Calculate new delta by delta = min(t_ipi / 2, t_gran / 2) */
static inline void ccid3_calc_new_delta(struct ccid3_hc_tx_sock *hctx)
{
	hctx->ccid3hctx_delta = min_t(u32, hctx->ccid3hctx_t_ipi / 2, TFRC_OPSYS_HALF_TIME_GRAN);

}

/*
 * Update X by
 *    If (p > 0)
 *       x_calc = calcX(s, R, p);
 *       X = max(min(X_calc, 2 * X_recv), s / t_mbi);
 *    Else
 *       If (now - tld >= R)
 *          X = max(min(2 * X, 2 * X_recv), s / R);
 *          tld = now;
 */ 
static void ccid3_hc_tx_update_x(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;

	if (hctx->ccid3hctx_p >= TFRC_SMALLEST_P) {  /* to avoid large error in calcX */
		hctx->ccid3hctx_x_calc = ccid3_calc_x(hctx->ccid3hctx_s,
						      hctx->ccid3hctx_rtt,
						      hctx->ccid3hctx_p);
		hctx->ccid3hctx_x = max_t(u32, min_t(u32, hctx->ccid3hctx_x_calc, 2 * hctx->ccid3hctx_x_recv),
					       hctx->ccid3hctx_s / TFRC_MAX_BACK_OFF_TIME);
	} else if (now_delta(hctx->ccid3hctx_t_ld) >= hctx->ccid3hctx_rtt) {
		u32 rtt = hctx->ccid3hctx_rtt;
		if (rtt < 10) {
			rtt = 10;
		} /* avoid divide by zero below */
		
		hctx->ccid3hctx_x = max_t(u32, min_t(u32, 2 * hctx->ccid3hctx_x_recv, 2 * hctx->ccid3hctx_x),
					(hctx->ccid3hctx_s * 100000) / (rtt / 10));
		/* Using 100000 and 10 to avoid 32 bit overflow for jumbo frames */
		do_gettimeofday(&hctx->ccid3hctx_t_ld);
	}

	if (hctx->ccid3hctx_x == 0) {
		ccid3_pr_debug("ccid3hctx_x = 0!\n");
		hctx->ccid3hctx_x = 1;
	}
}

static void ccid3_hc_tx_no_feedback_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct dccp_sock *dp = dccp_sk(sk);
	unsigned long next_tmout = 0;
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	u32 rtt;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later. */
		/* XXX: set some sensible MIB */
		sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer, jiffies + HZ / 5);
		goto out;
	}

	ccid3_pr_debug("%s, sk=%p, state=%s\n", dccp_role(sk), sk,
		       ccid3_tx_state_name(hctx->ccid3hctx_state));
	
	if (hctx->ccid3hctx_x < 10) {
		ccid3_pr_debug("TFRC_SSTATE_NO_FBACK ccid3hctx_x < 10\n");
		hctx->ccid3hctx_x = 10;
	}

	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_TERM:
		goto out;
	case TFRC_SSTATE_NO_FBACK:
		/* Halve send rate */
		hctx->ccid3hctx_x /= 2;
		if (hctx->ccid3hctx_x < (hctx->ccid3hctx_s / TFRC_MAX_BACK_OFF_TIME))
			hctx->ccid3hctx_x = hctx->ccid3hctx_s / TFRC_MAX_BACK_OFF_TIME;

		ccid3_pr_debug("%s, sk=%p, state=%s, updated tx rate to %d bytes/s\n",
			       dccp_role(sk), sk, ccid3_tx_state_name(hctx->ccid3hctx_state),
			       hctx->ccid3hctx_x);
		next_tmout = max_t(u32, 2 * (hctx->ccid3hctx_s * 100000) 
				/ (hctx->ccid3hctx_x / 10), TFRC_INITIAL_TIMEOUT);
		/* do above maths with 100000 and 10 to prevent overflow on 32 bit */
		/* FIXME - not sure above calculation is correct. See section 5 of CCID3 11
		 * should adjust tx_t_ipi and double that to achieve it really */
		break;
	case TFRC_SSTATE_FBACK:
		/* Check if IDLE since last timeout and recv rate is less than 4 packets per RTT */
		rtt = hctx->ccid3hctx_rtt;
		if (rtt < 10)
			rtt = 10;
		/* stop divide by zero below */
		if (!hctx->ccid3hctx_idle || (hctx->ccid3hctx_x_recv >= 
				4 * (hctx->ccid3hctx_s * 100000) / (rtt / 10))) {
			ccid3_pr_debug("%s, sk=%p, state=%s, not idle\n", dccp_role(sk), sk,
				       ccid3_tx_state_name(hctx->ccid3hctx_state));
			/* Halve sending rate */

			/*  If (X_calc > 2 * X_recv)
			 *    X_recv = max(X_recv / 2, s / (2 * t_mbi));
			 *  Else
			 *    X_recv = X_calc / 4;
			 */
			BUG_ON(hctx->ccid3hctx_p >= TFRC_SMALLEST_P && hctx->ccid3hctx_x_calc == 0);

			/* check also if p is zero -> x_calc is infinity? */
			if (hctx->ccid3hctx_p < TFRC_SMALLEST_P ||
			    hctx->ccid3hctx_x_calc > 2 * hctx->ccid3hctx_x_recv)
				hctx->ccid3hctx_x_recv = max_t(u32, hctx->ccid3hctx_x_recv / 2,
								    hctx->ccid3hctx_s / (2 * TFRC_MAX_BACK_OFF_TIME));
			else
				hctx->ccid3hctx_x_recv = hctx->ccid3hctx_x_calc / 4;

			/* Update sending rate */
			ccid3_hc_tx_update_x(sk);
		}
		if (hctx->ccid3hctx_x == 0) {
			ccid3_pr_debug("TFRC_SSTATE_FBACK ccid3hctx_x = 0!\n");
			hctx->ccid3hctx_x = 10;
		}
		/* Schedule no feedback timer to expire in max(4 * R, 2 * s / X) */
		next_tmout = max_t(u32, inet_csk(sk)->icsk_rto, 
				   2 * (hctx->ccid3hctx_s * 100000) / (hctx->ccid3hctx_x / 10));
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hctx->ccid3hctx_state);
		dump_stack();
		goto out;
	}

	sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer, 
			jiffies + max_t(u32, 1, usecs_to_jiffies(next_tmout)));
	hctx->ccid3hctx_idle = 1;
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

static int ccid3_hc_tx_send_packet(struct sock *sk, struct sk_buff *skb,
				   int len, long *delay)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	struct ccid3_tx_hist_entry *new_packet = NULL;
	struct timeval now;
	int rc = -ENOTCONN;

//	ccid3_pr_debug("%s, sk=%p, skb=%p, len=%d\n", dccp_role(sk), sk, skb, len);
	/*
	 * check if pure ACK or Terminating */
	/* XXX: We only call this function for DATA and DATAACK, on, these packets can have
	 * zero length, but why the comment about "pure ACK"?
	 */
	if (hctx == NULL || len == 0 || hctx->ccid3hctx_state == TFRC_SSTATE_TERM)
		goto out;

	/* See if last packet allocated was not sent */
	if (!list_empty(&hctx->ccid3hctx_hist))
		new_packet = list_entry(hctx->ccid3hctx_hist.next,
					struct ccid3_tx_hist_entry, ccid3htx_node);

	if (new_packet == NULL || new_packet->ccid3htx_sent) {
		new_packet = ccid3_tx_hist_entry_new(SLAB_ATOMIC);

		rc = -ENOBUFS;
		if (new_packet == NULL) {
			ccid3_pr_debug("%s, sk=%p, not enough mem to add "
				       "to history, send refused\n", dccp_role(sk), sk);
			goto out;
		}

		list_add(&new_packet->ccid3htx_node, &hctx->ccid3hctx_hist);
	}

	do_gettimeofday(&now);

	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_NO_SENT:
		ccid3_pr_debug("%s, sk=%p, first packet(%llu)\n", dccp_role(sk), sk,
			       dp->dccps_gss);

		hctx->ccid3hctx_no_feedback_timer.function = ccid3_hc_tx_no_feedback_timer;
		hctx->ccid3hctx_no_feedback_timer.data     = (unsigned long)sk;
		sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer, jiffies + usecs_to_jiffies(TFRC_INITIAL_TIMEOUT));
		hctx->ccid3hctx_last_win_count	 = 0;
		hctx->ccid3hctx_t_last_win_count = now;
		ccid3_hc_tx_set_state(sk, TFRC_SSTATE_NO_FBACK);
		hctx->ccid3hctx_t_ipi = TFRC_INITIAL_TIMEOUT;

		/* Set nominal send time for initial packet */
		hctx->ccid3hctx_t_nom = now;
		(hctx->ccid3hctx_t_nom).tv_usec += hctx->ccid3hctx_t_ipi;
		timeval_fix(&(hctx->ccid3hctx_t_nom));
		ccid3_calc_new_delta(hctx);
		rc = 0;
		break;
	case TFRC_SSTATE_NO_FBACK:
	case TFRC_SSTATE_FBACK:
		*delay = (now_delta(hctx->ccid3hctx_t_nom) - hctx->ccid3hctx_delta);
		ccid3_pr_debug("send_packet delay=%ld\n",*delay);
		*delay /= -1000;
		/* divide by -1000 is to convert to ms and get sign right */
		rc = *delay > 0 ? -EAGAIN : 0;
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hctx->ccid3hctx_state);
		dump_stack();
		rc = -EINVAL;
		break;
	}

	/* Can we send? if so add options and add to packet history */
	if (rc == 0)
		new_packet->ccid3htx_win_count = DCCP_SKB_CB(skb)->dccpd_ccval = hctx->ccid3hctx_last_win_count;
out:
	return rc;
}

static void ccid3_hc_tx_packet_sent(struct sock *sk, int more, int len)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	struct ccid3_tx_hist_entry *packet = NULL;
	struct timeval now;

//	ccid3_pr_debug("%s, sk=%p, more=%d, len=%d\n", dccp_role(sk), sk, more, len);
	BUG_ON(hctx == NULL);

	if (hctx->ccid3hctx_state == TFRC_SSTATE_TERM) {
		ccid3_pr_debug("%s, sk=%p, while state is TFRC_SSTATE_TERM!\n",
			       dccp_role(sk), sk);
		return;
	}

	do_gettimeofday(&now);

	/* check if we have sent a data packet */
	if (len > 0) {
		unsigned long quarter_rtt;

		if (list_empty(&hctx->ccid3hctx_hist)) {
			printk(KERN_CRIT "%s: packet doesn't exists in history!\n", __FUNCTION__);
			return;
		}
		packet = list_entry(hctx->ccid3hctx_hist.next, struct ccid3_tx_hist_entry, ccid3htx_node);
		if (packet->ccid3htx_sent) {
			printk(KERN_CRIT "%s: no unsent packet in history!\n", __FUNCTION__);
			return;
		}
		packet->ccid3htx_tstamp = now;
		packet->ccid3htx_seqno  = dp->dccps_gss;
		// ccid3_pr_debug("%s, sk=%p, seqno=%llu inserted!\n", dccp_role(sk), sk, packet->ccid3htx_seqno);

		/*
		 * Check if win_count have changed */
		/* COMPLIANCE_BEGIN
		 * Algorithm in "8.1. Window Counter Valuer" in draft-ietf-dccp-ccid3-11.txt
		 */
		quarter_rtt = now_delta(hctx->ccid3hctx_t_last_win_count) / (hctx->ccid3hctx_rtt / 4);
		if (quarter_rtt > 0) {
			hctx->ccid3hctx_t_last_win_count = now;
			hctx->ccid3hctx_last_win_count	 = (hctx->ccid3hctx_last_win_count +
							    min_t(unsigned long, quarter_rtt, 5)) % 16;
			ccid3_pr_debug("%s, sk=%p, window changed from %u to %u!\n",
				       dccp_role(sk), sk,
				       packet->ccid3htx_win_count,
				       hctx->ccid3hctx_last_win_count);
		}
		/* COMPLIANCE_END */
#if 0
		ccid3_pr_debug("%s, sk=%p, packet sent (%llu,%u)\n",
			       dccp_role(sk), sk,
			       packet->ccid3htx_seqno,
			       packet->ccid3htx_win_count);
#endif
		hctx->ccid3hctx_idle = 0;
		packet->ccid3htx_sent = 1;
	} else
		ccid3_pr_debug("%s, sk=%p, seqno=%llu NOT inserted!\n",
			       dccp_role(sk), sk, dp->dccps_gss);

	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_NO_SENT:
		/* if first wasn't pure ack */
		if (len != 0)
			printk(KERN_CRIT "%s: %s, First packet sent is noted as a data packet\n",
			       __FUNCTION__, dccp_role(sk));
		return;
	case TFRC_SSTATE_NO_FBACK:
	case TFRC_SSTATE_FBACK:
		if (len > 0) {
			hctx->ccid3hctx_t_nom = now;
			ccid3_calc_new_t_ipi(hctx);
			ccid3_calc_new_delta(hctx);
			(hctx->ccid3hctx_t_nom).tv_usec += hctx->ccid3hctx_t_ipi;
			timeval_fix(&(hctx->ccid3hctx_t_nom));
		}
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hctx->ccid3hctx_state);
		dump_stack();
		break;
	}
}

static void ccid3_hc_tx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	struct ccid3_options_received *opt_recv;
	struct ccid3_tx_hist_entry *entry, *next, *packet;
	unsigned long next_tmout; 
	u16 t_elapsed;
	u32 pinv;
	u32 x_recv;
	u32 r_sample;
#if 0
	ccid3_pr_debug("%s, sk=%p(%s), skb=%p(%s)\n",
		       dccp_role(sk), sk, dccp_state_name(sk->sk_state),
		       skb, dccp_packet_name(DCCP_SKB_CB(skb)->dccpd_type));
#endif
	if (hctx == NULL)
		return;

	if (hctx->ccid3hctx_state == TFRC_SSTATE_TERM) {
		ccid3_pr_debug("%s, sk=%p, received a packet when terminating!\n", dccp_role(sk), sk);
		return;
	}

	/* we are only interested in ACKs */
	if (!(DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_ACK ||
	      DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_DATAACK))
		return;

	opt_recv = &hctx->ccid3hctx_options_received;

	t_elapsed = dp->dccps_options_received.dccpor_elapsed_time;
	x_recv = opt_recv->ccid3or_receive_rate;
	pinv = opt_recv->ccid3or_loss_event_rate;

	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_NO_SENT:
		/* FIXME: what to do here? */
		return;
	case TFRC_SSTATE_NO_FBACK:
	case TFRC_SSTATE_FBACK:
		/* Calculate new round trip sample by
		 * R_sample = (now - t_recvdata) - t_delay */
		/* get t_recvdata from history */
		packet = NULL;
		list_for_each_entry_safe(entry, next, &hctx->ccid3hctx_hist, ccid3htx_node)
			if (entry->ccid3htx_seqno == DCCP_SKB_CB(skb)->dccpd_ack_seq) {
				packet = entry;
				break;
			}

		if (packet == NULL) {
			ccid3_pr_debug("%s, sk=%p, seqno %llu(%s) does't exist in history!\n",
				       dccp_role(sk), sk, DCCP_SKB_CB(skb)->dccpd_ack_seq,
				       dccp_packet_name(DCCP_SKB_CB(skb)->dccpd_type));
			return;
		}

		/* Update RTT */
		r_sample = now_delta(packet->ccid3htx_tstamp);
		/* FIXME: */
		// r_sample -= usecs_to_jiffies(t_elapsed * 10);

		/* Update RTT estimate by 
		 * If (No feedback recv)
		 *    R = R_sample;
		 * Else
		 *    R = q * R + (1 - q) * R_sample;
		 *
		 * q is a constant, RFC 3448 recomments 0.9
		 */
		if (hctx->ccid3hctx_state == TFRC_SSTATE_NO_FBACK) {
			ccid3_hc_tx_set_state(sk, TFRC_SSTATE_FBACK);
			hctx->ccid3hctx_rtt = r_sample;
		} else
			hctx->ccid3hctx_rtt = (hctx->ccid3hctx_rtt * 9) / 10 + r_sample / 10;

		/*
		 * XXX: this is to avoid a division by zero in ccid3_hc_tx_packet_sent
		 *      implemention of the new window count.
		 */
		if (hctx->ccid3hctx_rtt < 4)
			hctx->ccid3hctx_rtt = 4;

		ccid3_pr_debug("%s, sk=%p, New RTT estimate=%uus, r_sample=%us\n",
			       dccp_role(sk), sk,
			       hctx->ccid3hctx_rtt,
			       r_sample);

		/* Update timeout interval */
		inet_csk(sk)->icsk_rto = max_t(u32, 4 * hctx->ccid3hctx_rtt, USEC_IN_SEC);

		/* Update receive rate */
		hctx->ccid3hctx_x_recv = x_recv;   /* x_recv in bytes per second */

		/* Update loss event rate */
		if (pinv == ~0 || pinv == 0)
			hctx->ccid3hctx_p = 0;
		else {
			hctx->ccid3hctx_p = 1000000 / pinv;

			if (hctx->ccid3hctx_p < TFRC_SMALLEST_P) {
				hctx->ccid3hctx_p = TFRC_SMALLEST_P;
				ccid3_pr_debug("%s, sk=%p, Smallest p used!\n", dccp_role(sk), sk);
			}
		}

		/* unschedule no feedback timer */
		sk_stop_timer(sk, &hctx->ccid3hctx_no_feedback_timer);

		/* Update sending rate */
		ccid3_hc_tx_update_x(sk);

		/* Update next send time */
		if (hctx->ccid3hctx_t_ipi > (hctx->ccid3hctx_t_nom).tv_usec) {
			(hctx->ccid3hctx_t_nom).tv_usec += USEC_IN_SEC;
			(hctx->ccid3hctx_t_nom).tv_sec--;
		}
		/* FIXME - if no feedback then t_ipi can go > 1 second */
		(hctx->ccid3hctx_t_nom).tv_usec -= hctx->ccid3hctx_t_ipi;
		ccid3_calc_new_t_ipi(hctx);
		(hctx->ccid3hctx_t_nom).tv_usec += hctx->ccid3hctx_t_ipi;
		timeval_fix(&(hctx->ccid3hctx_t_nom));
		ccid3_calc_new_delta(hctx);

		/* remove all packets older than the one acked from history */
		list_for_each_entry_safe_continue(entry, next, &hctx->ccid3hctx_hist, ccid3htx_node) {
			list_del_init(&entry->ccid3htx_node);
			ccid3_tx_hist_entry_delete(entry);
		}
		if (hctx->ccid3hctx_x < 10) {
			ccid3_pr_debug("ccid3_hc_tx_packet_recv hctx->ccid3hctx_x < 10\n");
			hctx->ccid3hctx_x = 10;
		}
		/* to prevent divide by zero below */

		/* Schedule no feedback timer to expire in max(4 * R, 2 * s / X) */
		next_tmout = max(inet_csk(sk)->icsk_rto,
			2 * (hctx->ccid3hctx_s * 100000) / (hctx->ccid3hctx_x/10));
		/* maths with 100000 and 10 is to prevent overflow with 32 bit */

		ccid3_pr_debug("%s, sk=%p, Scheduled no feedback timer to expire in %lu jiffies (%luus)\n",
			       dccp_role(sk), sk, usecs_to_jiffies(next_tmout), next_tmout); 

		sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer, 
				jiffies + max_t(u32,1,usecs_to_jiffies(next_tmout)));

		/* set idle flag */
		hctx->ccid3hctx_idle = 1;   
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hctx->ccid3hctx_state);
		dump_stack();
		break;
	}
}

static void ccid3_hc_tx_insert_options(struct sock *sk, struct sk_buff *skb)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;

	if (hctx == NULL || !(sk->sk_state == DCCP_OPEN || sk->sk_state == DCCP_PARTOPEN))
		return;

	 DCCP_SKB_CB(skb)->dccpd_ccval = hctx->ccid3hctx_last_win_count;
}

static int ccid3_hc_tx_parse_options(struct sock *sk, unsigned char option,
				   unsigned char len, u16 idx, unsigned char *value)
{
	int rc = 0;
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	struct ccid3_options_received *opt_recv;

	if (hctx == NULL)
		return 0;

	opt_recv = &hctx->ccid3hctx_options_received;

	if (opt_recv->ccid3or_seqno != dp->dccps_gsr) {
		opt_recv->ccid3or_seqno		     = dp->dccps_gsr;
		opt_recv->ccid3or_loss_event_rate    = ~0;
		opt_recv->ccid3or_loss_intervals_idx = 0;
		opt_recv->ccid3or_loss_intervals_len = 0;
		opt_recv->ccid3or_receive_rate	     = 0;
	}

	switch (option) {
	case TFRC_OPT_LOSS_EVENT_RATE:
		if (len != 4) {
			ccid3_pr_debug("%s, sk=%p, invalid len for TFRC_OPT_LOSS_EVENT_RATE\n",
				       dccp_role(sk), sk);
			rc = -EINVAL;
		} else {
			opt_recv->ccid3or_loss_event_rate = ntohl(*(u32 *)value);
			ccid3_pr_debug("%s, sk=%p, LOSS_EVENT_RATE=%u\n",
				       dccp_role(sk), sk,
				       opt_recv->ccid3or_loss_event_rate);
		}
		break;
	case TFRC_OPT_LOSS_INTERVALS:
		opt_recv->ccid3or_loss_intervals_idx = idx;
		opt_recv->ccid3or_loss_intervals_len = len;
		ccid3_pr_debug("%s, sk=%p, LOSS_INTERVALS=(%u, %u)\n",
			       dccp_role(sk), sk,
			       opt_recv->ccid3or_loss_intervals_idx,
			       opt_recv->ccid3or_loss_intervals_len);
		break;
	case TFRC_OPT_RECEIVE_RATE:
		if (len != 4) {
			ccid3_pr_debug("%s, sk=%p, invalid len for TFRC_OPT_RECEIVE_RATE\n",
				       dccp_role(sk), sk);
			rc = -EINVAL;
		} else {
			opt_recv->ccid3or_receive_rate = ntohl(*(u32 *)value);
			ccid3_pr_debug("%s, sk=%p, RECEIVE_RATE=%u\n",
				       dccp_role(sk), sk,
				       opt_recv->ccid3or_receive_rate);
		}
		break;
	}

	return rc;
}

static int ccid3_hc_tx_init(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx;

	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);

	hctx = dp->dccps_hc_tx_ccid_private = kmalloc(sizeof(*hctx), gfp_any());
	if (hctx == NULL)
		return -ENOMEM;

	memset(hctx, 0, sizeof(*hctx));

	if (dp->dccps_avg_packet_size >= TFRC_MIN_PACKET_SIZE &&
	    dp->dccps_avg_packet_size <= TFRC_MAX_PACKET_SIZE)
		hctx->ccid3hctx_s = (u16)dp->dccps_avg_packet_size;
	else
		hctx->ccid3hctx_s = TFRC_STD_PACKET_SIZE;

	hctx->ccid3hctx_x     = hctx->ccid3hctx_s; /* set transmission rate to 1 packet per second */
	hctx->ccid3hctx_rtt   = 4; /* See ccid3_hc_tx_packet_sent win_count calculatation */
	inet_csk(sk)->icsk_rto = USEC_IN_SEC;
	hctx->ccid3hctx_state = TFRC_SSTATE_NO_SENT;
	INIT_LIST_HEAD(&hctx->ccid3hctx_hist);
	init_timer(&hctx->ccid3hctx_no_feedback_timer);

	return 0;
}

static void ccid3_hc_tx_exit(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	struct ccid3_tx_hist_entry *entry, *next;

	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);
	BUG_ON(hctx == NULL);

	ccid3_hc_tx_set_state(sk, TFRC_SSTATE_TERM);
	sk_stop_timer(sk, &hctx->ccid3hctx_no_feedback_timer);

	/* Empty packet history */
	list_for_each_entry_safe(entry, next, &hctx->ccid3hctx_hist, ccid3htx_node) {
		list_del_init(&entry->ccid3htx_node);
		ccid3_tx_hist_entry_delete(entry);
	}

	kfree(dp->dccps_hc_tx_ccid_private);
	dp->dccps_hc_tx_ccid_private = NULL;
}

/*
 * RX Half Connection methods
 */

/* TFRC receiver states */
enum ccid3_hc_rx_states {
       	TFRC_RSTATE_NO_DATA = 1,
	TFRC_RSTATE_DATA,
	TFRC_RSTATE_TERM    = 127,
};

#ifdef CCID3_DEBUG
static const char *ccid3_rx_state_name(enum ccid3_hc_rx_states state)
{
	static char *ccid3_rx_state_names[] = {
	[TFRC_RSTATE_NO_DATA] = "NO_DATA",
	[TFRC_RSTATE_DATA]    = "DATA",
	[TFRC_RSTATE_TERM]    = "TERM",
	};

	return ccid3_rx_state_names[state];
}
#endif

static inline void ccid3_hc_rx_set_state(struct sock *sk, enum ccid3_hc_rx_states state)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	enum ccid3_hc_rx_states oldstate = hcrx->ccid3hcrx_state;

	ccid3_pr_debug("%s(%p) %-8.8s -> %s\n",
		       dccp_role(sk), sk, ccid3_rx_state_name(oldstate), ccid3_rx_state_name(state));
	WARN_ON(state == oldstate);
	hcrx->ccid3hcrx_state = state;
}

static int ccid3_hc_rx_add_hist(struct sock *sk, struct ccid3_rx_hist_entry *packet)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	struct ccid3_rx_hist_entry *entry, *next;
	u8 num_later = 0;

	if (list_empty(&hcrx->ccid3hcrx_hist))
		list_add(&packet->ccid3hrx_node, &hcrx->ccid3hcrx_hist);
	else {
		u64 seqno = packet->ccid3hrx_seqno;
		struct ccid3_rx_hist_entry *iter = list_entry(hcrx->ccid3hcrx_hist.next,
							      struct ccid3_rx_hist_entry,
							      ccid3hrx_node);
		if (after48(seqno, iter->ccid3hrx_seqno))
			list_add(&packet->ccid3hrx_node, &hcrx->ccid3hcrx_hist);
		else {
			if (iter->ccid3hrx_type == DCCP_PKT_DATA ||
			    iter->ccid3hrx_type == DCCP_PKT_DATAACK)
				num_later = 1;

			list_for_each_entry_continue(iter, &hcrx->ccid3hcrx_hist, ccid3hrx_node) {
				if (after48(seqno, iter->ccid3hrx_seqno)) {
					list_add(&packet->ccid3hrx_node, &iter->ccid3hrx_node);
					goto trim_history;
				}

				if (iter->ccid3hrx_type == DCCP_PKT_DATA ||
				    iter->ccid3hrx_type == DCCP_PKT_DATAACK)
					num_later++;

				if (num_later == TFRC_RECV_NUM_LATE_LOSS) {
					ccid3_rx_hist_entry_delete(packet);
					ccid3_pr_debug("%s, sk=%p, packet(%llu) already lost!\n",
						       dccp_role(sk), sk, seqno);
					return 1;
				}
			}

			if (num_later < TFRC_RECV_NUM_LATE_LOSS)
				list_add_tail(&packet->ccid3hrx_node, &hcrx->ccid3hcrx_hist);
			/* FIXME: else what? should we destroy the packet like above? */
		}
	}

trim_history:
	/* Trim history (remove all packets after the NUM_LATE_LOSS + 1 data packets) */
	num_later = TFRC_RECV_NUM_LATE_LOSS + 1;

	if (!list_empty(&hcrx->ccid3hcrx_loss_interval_hist)) {
		list_for_each_entry_safe(entry, next, &hcrx->ccid3hcrx_hist, ccid3hrx_node) {
			if (num_later == 0) {
				list_del_init(&entry->ccid3hrx_node);
				ccid3_rx_hist_entry_delete(entry);
			} else if (entry->ccid3hrx_type == DCCP_PKT_DATA ||
				   entry->ccid3hrx_type == DCCP_PKT_DATAACK)
				--num_later;
		}
	} else {
		int step = 0;
		u8 win_count = 0; /* Not needed, but lets shut up gcc */
		int tmp;
		/*
		 * We have no loss interval history so we need at least one
		 * rtt:s of data packets to approximate rtt.
		 */
		list_for_each_entry_safe(entry, next, &hcrx->ccid3hcrx_hist, ccid3hrx_node) {
			if (num_later == 0) {
				switch (step) {
				case 0:
					step = 1;
					/* OK, find next data packet */
					num_later = 1;
					break;
				case 1:
					step = 2;
					/* OK, find next data packet */
					num_later = 1;
					win_count = entry->ccid3hrx_win_count;
					break;
				case 2:
					tmp = win_count - entry->ccid3hrx_win_count;
					if (tmp < 0)
						tmp += TFRC_WIN_COUNT_LIMIT;
					if (tmp > TFRC_WIN_COUNT_PER_RTT + 1) {
						/* we have found a packet older than one rtt
						 * remove the rest */
						step = 3;
					} else /* OK, find next data packet */
						num_later = 1;
					break;
				case 3:
					list_del_init(&entry->ccid3hrx_node);
					ccid3_rx_hist_entry_delete(entry);
					break;
				}
			} else if (entry->ccid3hrx_type == DCCP_PKT_DATA ||
				   entry->ccid3hrx_type == DCCP_PKT_DATAACK)
				--num_later;
		}
	}

	return 0;
}

static void ccid3_hc_rx_send_feedback(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	struct ccid3_rx_hist_entry *entry, *packet;

	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);

	switch (hcrx->ccid3hcrx_state) {
	case TFRC_RSTATE_NO_DATA:
		hcrx->ccid3hcrx_x_recv = 0;
		break;
	case TFRC_RSTATE_DATA: {
		u32 delta = now_delta(hcrx->ccid3hcrx_tstamp_last_feedback);

		if (delta == 0)
			delta = 1; /* to prevent divide by zero */
		hcrx->ccid3hcrx_x_recv = (hcrx->ccid3hcrx_bytes_recv * USEC_IN_SEC) / delta;
	}
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hcrx->ccid3hcrx_state);
		dump_stack();
		return;
	}

	packet = NULL;
	list_for_each_entry(entry, &hcrx->ccid3hcrx_hist, ccid3hrx_node)
		if (entry->ccid3hrx_type == DCCP_PKT_DATA ||
		    entry->ccid3hrx_type == DCCP_PKT_DATAACK) {
			packet = entry;
			break;
		}

	if (packet == NULL) {
		printk(KERN_CRIT "%s: %s, sk=%p, no data packet in history!\n",
		       __FUNCTION__, dccp_role(sk), sk);
		dump_stack();
		return;
	}

	do_gettimeofday(&(hcrx->ccid3hcrx_tstamp_last_feedback));
	hcrx->ccid3hcrx_last_counter	     = packet->ccid3hrx_win_count;
	hcrx->ccid3hcrx_seqno_last_counter   = packet->ccid3hrx_seqno;
	hcrx->ccid3hcrx_bytes_recv	     = 0;

	/* Convert to multiples of 10us */
	hcrx->ccid3hcrx_elapsed_time = now_delta(packet->ccid3hrx_tstamp) / 10;
	if (hcrx->ccid3hcrx_p == 0)
		hcrx->ccid3hcrx_pinv = ~0;
	else
		hcrx->ccid3hcrx_pinv = 1000000 / hcrx->ccid3hcrx_p;
	dccp_send_ack(sk);
}

static void ccid3_hc_rx_insert_options(struct sock *sk, struct sk_buff *skb)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;

	if (hcrx == NULL || !(sk->sk_state == DCCP_OPEN || sk->sk_state == DCCP_PARTOPEN))
		return;

	if (hcrx->ccid3hcrx_elapsed_time != 0 && !dccp_packet_without_ack(skb))
		dccp_insert_option_elapsed_time(sk, skb, hcrx->ccid3hcrx_elapsed_time);

	if (DCCP_SKB_CB(skb)->dccpd_type != DCCP_PKT_DATA) {
		const u32 x_recv = htonl(hcrx->ccid3hcrx_x_recv);
		const u32 pinv   = htonl(hcrx->ccid3hcrx_pinv);

		dccp_insert_option(sk, skb, TFRC_OPT_LOSS_EVENT_RATE, &pinv, sizeof(pinv));
		dccp_insert_option(sk, skb, TFRC_OPT_RECEIVE_RATE, &x_recv, sizeof(x_recv));
	}

	DCCP_SKB_CB(skb)->dccpd_ccval = hcrx->ccid3hcrx_last_counter;
}

/* Weights used to calculate loss event rate */
/*
 * These are integers as per section 8 of RFC3448. We can then divide by 4 *
 * when we use it.
 */
const int ccid3_hc_rx_w[TFRC_RECV_IVAL_F_LENGTH] = { 4, 4, 4, 4, 3, 2, 1, 1, };

/*
 * args: fvalue - function value to match
 * returns:  p  closest to that value
 *
 * both fvalue and p are multiplied by 1,000,000 to use ints
 */
u32 calcx_reverse_lookup(u32 fvalue) {
	int ctr = 0;
	int small;

	if (fvalue < calcx_lookup[0][1])
		return 0;
	if (fvalue <= calcx_lookup[CALCX_ARRSIZE-1][1])
		small = 1;
	else if (fvalue > calcx_lookup[CALCX_ARRSIZE-1][0])
		return 1000000;
	else
		small = 0;
	while (fvalue > calcx_lookup[ctr][small])
		ctr++;
	if (small)
		return (CALCX_SPLIT * ctr / CALCX_ARRSIZE);
	else
		return (1000000 * ctr / CALCX_ARRSIZE) ;
}

/* calculate first loss interval
 *
 * returns estimated loss interval in usecs */

static u32 ccid3_hc_rx_calc_first_li(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	struct ccid3_rx_hist_entry *entry, *next, *tail = NULL;
	u32 rtt, delta, x_recv, fval, p, tmp2;
	struct timeval tstamp, tmp_tv;
	int interval = 0;
	int win_count = 0;
	int step = 0;
	u64 tmp1;

	list_for_each_entry_safe(entry, next, &hcrx->ccid3hcrx_hist, ccid3hrx_node) {
		if (entry->ccid3hrx_type == DCCP_PKT_DATA ||
		    entry->ccid3hrx_type == DCCP_PKT_DATAACK) {
			tail = entry;

			switch (step) {
			case 0:
				tstamp	  = entry->ccid3hrx_tstamp;
				win_count = entry->ccid3hrx_win_count;
				step = 1;
				break;
			case 1:
				interval = win_count - entry->ccid3hrx_win_count;
				if (interval < 0)
					interval += TFRC_WIN_COUNT_LIMIT;
				if (interval > 4)
					goto found;
				break;
			}
		}
	}

	if (step == 0) {
		printk(KERN_CRIT "%s: %s, sk=%p, packet history contains no data packets!\n",
		       __FUNCTION__, dccp_role(sk), sk);
		return ~0;
	}

	if (interval == 0) {
		ccid3_pr_debug("%s, sk=%p, Could not find a win_count interval > 0. Defaulting to 1\n",
			       dccp_role(sk), sk);
		interval = 1;
	}
found:
	timeval_sub(tstamp,tail->ccid3hrx_tstamp,&tmp_tv);
	rtt = (tmp_tv.tv_sec * USEC_IN_SEC + tmp_tv.tv_usec) * 4 / interval;
	ccid3_pr_debug("%s, sk=%p, approximated RTT to %uus\n",
		       dccp_role(sk), sk, rtt);
	if (rtt == 0)
		rtt = 1;

	delta = now_delta(hcrx->ccid3hcrx_tstamp_last_feedback);
	if (delta == 0)
		delta = 1;

	x_recv = (hcrx->ccid3hcrx_bytes_recv * USEC_IN_SEC) / delta;

	tmp1 = (u64)x_recv * (u64)rtt;
	do_div(tmp1,10000000);
	tmp2 = (u32)tmp1;
	fval = (hcrx->ccid3hcrx_s * 100000) / tmp2;
	/* do not alter order above or you will get overflow on 32 bit */
	p = calcx_reverse_lookup(fval);
	ccid3_pr_debug("%s, sk=%p, receive rate=%u bytes/s, implied loss rate=%u\n",\
			dccp_role(sk), sk, x_recv, p);

	if (p == 0)
		return ~0;
	else
		return 1000000 / p; 
}

static void ccid3_hc_rx_update_li(struct sock *sk, u64 seq_loss, u8 win_loss)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	struct ccid3_loss_interval_hist_entry *li_entry;

	if (seq_loss != DCCP_MAX_SEQNO + 1) {
		ccid3_pr_debug("%s, sk=%p, seq_loss=%llu, win_loss=%u, packet loss detected\n",
			       dccp_role(sk), sk, seq_loss, win_loss);
		
		if (list_empty(&hcrx->ccid3hcrx_loss_interval_hist)) {
			struct ccid3_loss_interval_hist_entry *li_tail = NULL;
			int i;

			ccid3_pr_debug("%s, sk=%p, first loss event detected, creating history\n", dccp_role(sk), sk);
			for (i = 0; i <= TFRC_RECV_IVAL_F_LENGTH; ++i) {
				li_entry = ccid3_loss_interval_hist_entry_new(SLAB_ATOMIC);
				if (li_entry == NULL) {
					ccid3_loss_interval_history_delete(&hcrx->ccid3hcrx_loss_interval_hist);
					ccid3_pr_debug("%s, sk=%p, not enough mem for creating history\n",
						       dccp_role(sk), sk);
					return;
				}
				if (li_tail == NULL)
					li_tail = li_entry;
				list_add(&li_entry->ccid3lih_node, &hcrx->ccid3hcrx_loss_interval_hist);
			}

			li_entry->ccid3lih_seqno     = seq_loss;
			li_entry->ccid3lih_win_count = win_loss;

			li_tail->ccid3lih_interval   = ccid3_hc_rx_calc_first_li(sk);
		}
	}
	/* FIXME: find end of interval */
}

static void ccid3_hc_rx_detect_loss(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	struct ccid3_rx_hist_entry *entry, *a_next, *b_next, *packet;
	struct ccid3_rx_hist_entry *a_loss = NULL;
	struct ccid3_rx_hist_entry *b_loss = NULL;
	u64 seq_loss = DCCP_MAX_SEQNO + 1;
	u8 win_loss = 0;
	u8 num_later = TFRC_RECV_NUM_LATE_LOSS;

	list_for_each_entry_safe(entry, b_next, &hcrx->ccid3hcrx_hist, ccid3hrx_node) {
		if (num_later == 0) {
			b_loss = entry;
			break;
		} else if (entry->ccid3hrx_type == DCCP_PKT_DATA ||
			   entry->ccid3hrx_type == DCCP_PKT_DATAACK)
			--num_later;
	}

	if (b_loss == NULL)
		goto out_update_li;

	a_next = b_next;
	num_later = 1;

	list_for_each_entry_safe_continue(entry, a_next, &hcrx->ccid3hcrx_hist, ccid3hrx_node) {
		if (num_later == 0) {
			a_loss = entry;
			break;
		} else if (entry->ccid3hrx_type == DCCP_PKT_DATA ||
			   entry->ccid3hrx_type == DCCP_PKT_DATAACK)
			--num_later;
	}

	if (a_loss == NULL) {
		if (list_empty(&hcrx->ccid3hcrx_loss_interval_hist)) {
			/* no loss event have occured yet */
			ccid3_pr_debug("%s, sk=%p, TODO: find a lost data "
					"packet by comparing to initial seqno\n",
				       dccp_role(sk), sk);
			goto out_update_li;
		} else {
			pr_info("%s: %s, sk=%p, ERROR! Less than 4 data packets in history",
				__FUNCTION__, dccp_role(sk), sk);
			return;
		}
	}

	/* Locate a lost data packet */
	entry = packet = b_loss;
	list_for_each_entry_safe_continue(entry, b_next, &hcrx->ccid3hcrx_hist, ccid3hrx_node) {
		u64 delta = dccp_delta_seqno(entry->ccid3hrx_seqno, packet->ccid3hrx_seqno);

		if (delta != 0) {
			if (packet->ccid3hrx_type == DCCP_PKT_DATA ||
			    packet->ccid3hrx_type == DCCP_PKT_DATAACK)
				--delta;
			/*
			 * FIXME: check this, probably this % usage is because
			 * in earlier drafts the ndp count was just 8 bits
			 * long, but now it cam be up to 24 bits long.
			 */
#if 0
			if (delta % DCCP_NDP_LIMIT !=
			    (packet->ccid3hrx_ndp - entry->ccid3hrx_ndp) % DCCP_NDP_LIMIT)
#endif
			if (delta != packet->ccid3hrx_ndp - entry->ccid3hrx_ndp) {
				seq_loss = entry->ccid3hrx_seqno;
				dccp_inc_seqno(&seq_loss);
			}
		}
		packet = entry;
		if (packet == a_loss)
			break;
	}

	if (seq_loss != DCCP_MAX_SEQNO + 1)
		win_loss = a_loss->ccid3hrx_win_count;

out_update_li:
	ccid3_hc_rx_update_li(sk, seq_loss, win_loss);
}

static u32 ccid3_hc_rx_calc_i_mean(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	struct ccid3_loss_interval_hist_entry *li_entry, *li_next;
	int i = 0;
	u32 i_tot;
	u32 i_tot0 = 0;
	u32 i_tot1 = 0;
	u32 w_tot  = 0;

	list_for_each_entry_safe(li_entry, li_next, &hcrx->ccid3hcrx_loss_interval_hist, ccid3lih_node) {
		if (i < TFRC_RECV_IVAL_F_LENGTH) {
			i_tot0 += li_entry->ccid3lih_interval * ccid3_hc_rx_w[i];
			w_tot  += ccid3_hc_rx_w[i];
		}

		if (i != 0)
			i_tot1 += li_entry->ccid3lih_interval * ccid3_hc_rx_w[i - 1];

		if (++i > TFRC_RECV_IVAL_F_LENGTH)
			break;
	}

	if (i != TFRC_RECV_IVAL_F_LENGTH) {
		pr_info("%s: %s, sk=%p, ERROR! Missing entry in interval history!\n",
			__FUNCTION__, dccp_role(sk), sk);
		return 0;
	}

	i_tot = max(i_tot0, i_tot1);

	/* FIXME: Why do we do this? -Ian McDonald */
	if (i_tot * 4 < w_tot)
		i_tot = w_tot * 4;

	return i_tot * 4 / w_tot;
}

static void ccid3_hc_rx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	struct ccid3_rx_hist_entry *packet;
	struct timeval now;
	u8 win_count;
	u32 p_prev;
	int ins;
#if 0
	ccid3_pr_debug("%s, sk=%p(%s), skb=%p(%s)\n",
		       dccp_role(sk), sk, dccp_state_name(sk->sk_state),
		       skb, dccp_packet_name(DCCP_SKB_CB(skb)->dccpd_type));
#endif
	if (hcrx == NULL)
		return;

	BUG_ON(!(hcrx->ccid3hcrx_state == TFRC_RSTATE_NO_DATA ||
		 hcrx->ccid3hcrx_state == TFRC_RSTATE_DATA));

	switch (DCCP_SKB_CB(skb)->dccpd_type) {
	case DCCP_PKT_ACK:
		if (hcrx->ccid3hcrx_state == TFRC_RSTATE_NO_DATA)
			return;
	case DCCP_PKT_DATAACK:
		if (dp->dccps_options_received.dccpor_timestamp_echo == 0)
			break;
		p_prev = hcrx->ccid3hcrx_rtt;
		do_gettimeofday(&now);
		/* hcrx->ccid3hcrx_rtt = now - dp->dccps_options_received.dccpor_timestamp_echo -
				      usecs_to_jiffies(dp->dccps_options_received.dccpor_elapsed_time * 10);
		FIXME - I think above code is broken - have to look at options more, will also need
		to fix pr_debug below */
		if (p_prev != hcrx->ccid3hcrx_rtt)
			ccid3_pr_debug("%s, sk=%p, New RTT estimate=%lu jiffies, tstamp_echo=%u, elapsed time=%u\n",
				       dccp_role(sk), sk, hcrx->ccid3hcrx_rtt,
				       dp->dccps_options_received.dccpor_timestamp_echo,
				       dp->dccps_options_received.dccpor_elapsed_time);
		break;
	case DCCP_PKT_DATA:
		break;
	default:
		ccid3_pr_debug("%s, sk=%p, not DATA/DATAACK/ACK packet(%s)\n",
			       dccp_role(sk), sk,
			       dccp_packet_name(DCCP_SKB_CB(skb)->dccpd_type));
		return;
	}

	packet = ccid3_rx_hist_entry_new(sk, skb, SLAB_ATOMIC);
	if (packet == NULL) {
		ccid3_pr_debug("%s, sk=%p, Not enough mem to add rx packet to history (consider it lost)!",
			       dccp_role(sk), sk);
		return;
	}

	win_count = packet->ccid3hrx_win_count;

	ins = ccid3_hc_rx_add_hist(sk, packet);

	if (DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_ACK)
		return;

	switch (hcrx->ccid3hcrx_state) {
	case TFRC_RSTATE_NO_DATA:
		ccid3_pr_debug("%s, sk=%p(%s), skb=%p, sending initial feedback\n",
			       dccp_role(sk), sk, dccp_state_name(sk->sk_state), skb);
		ccid3_hc_rx_send_feedback(sk);
		ccid3_hc_rx_set_state(sk, TFRC_RSTATE_DATA);
		return;
	case TFRC_RSTATE_DATA:
		hcrx->ccid3hcrx_bytes_recv += skb->len - dccp_hdr(skb)->dccph_doff * 4;
		if (ins == 0) {
			do_gettimeofday(&now);
			if ((now_delta(hcrx->ccid3hcrx_tstamp_last_ack)) >= hcrx->ccid3hcrx_rtt) {
				hcrx->ccid3hcrx_tstamp_last_ack = now;
				ccid3_hc_rx_send_feedback(sk);
			}
			return;
		}
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hcrx->ccid3hcrx_state);
		dump_stack();
		return;
	}

	/* Dealing with packet loss */
	ccid3_pr_debug("%s, sk=%p(%s), skb=%p, data loss! Reacting...\n",
		       dccp_role(sk), sk, dccp_state_name(sk->sk_state), skb);

	ccid3_hc_rx_detect_loss(sk);
	p_prev = hcrx->ccid3hcrx_p;
	
	/* Calculate loss event rate */
	if (!list_empty(&hcrx->ccid3hcrx_loss_interval_hist))
		/* Scaling up by 1000000 as fixed decimal */
		hcrx->ccid3hcrx_p = 1000000 / ccid3_hc_rx_calc_i_mean(sk);

	if (hcrx->ccid3hcrx_p > p_prev) {
		ccid3_hc_rx_send_feedback(sk);
		return;
	}
}

static int ccid3_hc_rx_init(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx;

	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);

	hcrx = dp->dccps_hc_rx_ccid_private = kmalloc(sizeof(*hcrx), gfp_any());
	if (hcrx == NULL)
		return -ENOMEM;

	memset(hcrx, 0, sizeof(*hcrx));

	if (dp->dccps_avg_packet_size >= TFRC_MIN_PACKET_SIZE &&
	    dp->dccps_avg_packet_size <= TFRC_MAX_PACKET_SIZE)
		hcrx->ccid3hcrx_s = (u16)dp->dccps_avg_packet_size;
	else
		hcrx->ccid3hcrx_s = TFRC_STD_PACKET_SIZE;

	hcrx->ccid3hcrx_state = TFRC_RSTATE_NO_DATA;
	INIT_LIST_HEAD(&hcrx->ccid3hcrx_hist);
	INIT_LIST_HEAD(&hcrx->ccid3hcrx_loss_interval_hist);

	return 0;
}

static void ccid3_hc_rx_exit(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;

	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);

	if (hcrx == NULL)
		return;

	ccid3_hc_rx_set_state(sk, TFRC_RSTATE_TERM);

	/* Empty packet history */
	ccid3_rx_history_delete(&hcrx->ccid3hcrx_hist);

	/* Empty loss interval history */
	ccid3_loss_interval_history_delete(&hcrx->ccid3hcrx_loss_interval_hist);

	kfree(dp->dccps_hc_rx_ccid_private);
	dp->dccps_hc_rx_ccid_private = NULL;
}

static struct ccid ccid3 = {
	.ccid_id		   = 3,
	.ccid_name		   = "ccid3",
	.ccid_owner		   = THIS_MODULE,
	.ccid_init		   = ccid3_init,
	.ccid_exit		   = ccid3_exit,
	.ccid_hc_tx_init	   = ccid3_hc_tx_init,
	.ccid_hc_tx_exit	   = ccid3_hc_tx_exit,
	.ccid_hc_tx_send_packet	   = ccid3_hc_tx_send_packet,
	.ccid_hc_tx_packet_sent	   = ccid3_hc_tx_packet_sent,
	.ccid_hc_tx_packet_recv	   = ccid3_hc_tx_packet_recv,
	.ccid_hc_tx_insert_options = ccid3_hc_tx_insert_options,
	.ccid_hc_tx_parse_options  = ccid3_hc_tx_parse_options,
	.ccid_hc_rx_init	   = ccid3_hc_rx_init,
	.ccid_hc_rx_exit	   = ccid3_hc_rx_exit,
	.ccid_hc_rx_insert_options = ccid3_hc_rx_insert_options,
	.ccid_hc_rx_packet_recv	   = ccid3_hc_rx_packet_recv,
};
 
module_param(ccid3_debug, int, 0444);
MODULE_PARM_DESC(ccid3_debug, "Enable debug messages");

static __init int ccid3_module_init(void)
{
	int rc = -ENOMEM;

	ccid3_tx_hist_slab = kmem_cache_create("dccp_ccid3_tx_history",
					       sizeof(struct ccid3_tx_hist_entry), 0,
					       SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (ccid3_tx_hist_slab == NULL)
		goto out;

	ccid3_rx_hist_slab = kmem_cache_create("dccp_ccid3_rx_history",
					       sizeof(struct ccid3_rx_hist_entry), 0,
					       SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (ccid3_rx_hist_slab == NULL)
		goto out_free_tx_history;

	ccid3_loss_interval_hist_slab = kmem_cache_create("dccp_ccid3_loss_interval_history",
							  sizeof(struct ccid3_loss_interval_hist_entry), 0,
							  SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (ccid3_loss_interval_hist_slab == NULL)
		goto out_free_rx_history;

	rc = ccid_register(&ccid3);
	if (rc != 0) 
		goto out_free_loss_interval_history;

out:
	return rc;
out_free_loss_interval_history:
	kmem_cache_destroy(ccid3_loss_interval_hist_slab);
	ccid3_loss_interval_hist_slab = NULL;
out_free_rx_history:
	kmem_cache_destroy(ccid3_rx_hist_slab);
	ccid3_rx_hist_slab = NULL;
out_free_tx_history:
	kmem_cache_destroy(ccid3_tx_hist_slab);
	ccid3_tx_hist_slab = NULL;
	goto out;
}
module_init(ccid3_module_init);

static __exit void ccid3_module_exit(void)
{
	ccid_unregister(&ccid3);

	if (ccid3_tx_hist_slab != NULL) {
		kmem_cache_destroy(ccid3_tx_hist_slab);
		ccid3_tx_hist_slab = NULL;
	}
	if (ccid3_rx_hist_slab != NULL) {
		kmem_cache_destroy(ccid3_rx_hist_slab);
		ccid3_rx_hist_slab = NULL;
	}
	if (ccid3_loss_interval_hist_slab != NULL) {
		kmem_cache_destroy(ccid3_loss_interval_hist_slab);
		ccid3_loss_interval_hist_slab = NULL;
	}
}
module_exit(ccid3_module_exit);

MODULE_AUTHOR("Ian McDonald <iam4@cs.waikato.ac.nz> & Arnaldo Carvalho de Melo <acme@ghostprotocols.net>");
MODULE_DESCRIPTION("DCCP TFRC CCID3 CCID");
MODULE_LICENSE("GPL");
MODULE_ALIAS("net-dccp-ccid-3");
