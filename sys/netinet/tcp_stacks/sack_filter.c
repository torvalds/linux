/*-
 * Copyright (c) 2017 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <sys/sockopt.h>
#include <netinet/tcp.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_seq.h>
#ifndef _KERNEL
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <limits.h>
#include <getopt.h>
#endif
#include "sack_filter.h"

/*
 * Sack filter is used to filter out sacks
 * that have already been processed. The idea
 * is pretty simple really, consider two sacks
 *
 * SACK 1
 *   cum-ack A
 *     sack B - C
 * SACK 2
 *   cum-ack A
 *     sack D - E
 *     sack B - C
 * 
 * The previous sack information (B-C) is repeated
 * in SACK 2. If the receiver gets SACK 1 and then
 * SACK 2 then any work associated with B-C as already
 * been completed. This only effects where we may have
 * (as in bbr or rack) cases where we walk a linked list.
 *
 * Now the utility trys to keep everything in a single
 * cache line. This means that its not perfect and 
 * it could be that so big of sack's come that a 
 * "remembered" processed sack falls off the list and
 * so gets re-processed. Thats ok, it just means we
 * did some extra work. We could of course take more
 * cache line hits by expanding the size of this
 * structure, but then that would cost more.
 */

#ifndef _KERNEL
int detailed_dump = 0;
uint64_t cnt_skipped_oldsack = 0;
uint64_t cnt_used_oldsack = 0;
int highest_used=0;
int over_written=0;
int empty_avail=0;
int no_collapse = 0;
FILE *out = NULL;
FILE *in = NULL;
#endif

#define sack_blk_used(sf, i) ((1 << i) & sf->sf_bits)
#define sack_blk_set(sf, i) ((1 << i) | sf->sf_bits)
#define sack_blk_clr(sf, i) (~(1 << i) & sf->sf_bits)

#ifndef _KERNEL
static
#endif
void
sack_filter_clear(struct sack_filter *sf, tcp_seq seq)
{
	sf->sf_ack = seq;
	sf->sf_bits = 0;
	sf->sf_cur = 0;
	sf->sf_used = 0;
}
/*
 * Given a previous sack filter block, filter out
 * any entries where the cum-ack moves over them
 * fully or partially.
 */
static void
sack_filter_prune(struct sack_filter *sf, tcp_seq th_ack)
{
	int32_t i;
	/* start with the oldest */
	for (i = 0; i < SACK_FILTER_BLOCKS; i++) {
		if (sack_blk_used(sf, i)) {
			if (SEQ_GT(th_ack, sf->sf_blks[i].end)) {
				/* This block is consumed */
				sf->sf_bits = sack_blk_clr(sf, i);
				sf->sf_used--;
			} else if (SEQ_GT(th_ack, sf->sf_blks[i].start)) {
				/* Some of it is acked */
				sf->sf_blks[i].start = th_ack;
				/* We could in theory break here, but
				 * there are some broken implementations
				 * that send multiple blocks. We want
				 * to catch them all with similar seq's.
				 */
			}
		}
	}
	sf->sf_ack = th_ack;
}

/* 
 * Return true if you find that
 * the sackblock b is on the score
 * board. Update it along the way
 * if part of it is on the board.
 */
static int32_t
is_sack_on_board(struct sack_filter *sf, struct sackblk *b)
{
	int32_t i, cnt;
	for (i = sf->sf_cur, cnt=0; cnt < SACK_FILTER_BLOCKS; cnt++) {
		if (sack_blk_used(sf, i)) {
			if (SEQ_LT(b->start, sf->sf_ack)) {
				/* Behind cum-ack update */
				b->start = sf->sf_ack;
			}
			if (SEQ_LT(b->end, sf->sf_ack)) {
				/* End back behind too */
				b->end = sf->sf_ack;
			}
			if (b->start == b->end)
				return(1);
			/* Jonathans Rule 1 */
			if (SEQ_LEQ(sf->sf_blks[i].start, b->start) &&
			    SEQ_GEQ(sf->sf_blks[i].end, b->end)) {
				/**
				 * Our board has this entirely in
				 * whole or in part:
				 *
				 * board  |-------------|
				 * sack   |-------------|
				 * <or>
				 * board  |-------------|
				 * sack       |----|
				 *
				 */
				return(1);
			}
			/* Jonathans Rule 2 */
			if(SEQ_LT(sf->sf_blks[i].end, b->start)) {
				/**
				 * Not near each other:
				 * 
				 * board   |---|
				 * sack           |---|
				 */
				goto nxt_blk;
			}
			/* Jonathans Rule 3 */
			if (SEQ_GT(sf->sf_blks[i].start, b->end)) {
				/**
				 * Not near each other:
				 * 
				 * board         |---|
				 * sack  |---|
				 */
				goto nxt_blk;
			}
			if (SEQ_LEQ(sf->sf_blks[i].start, b->start)) {
				/** 
				 * The board block partial meets:
				 *
				 *  board   |--------|
				 *  sack        |----------|  
				 *    <or>
				 *  board   |--------|
				 *  sack    |--------------|  
				 *
				 * up with this one (we have part of it).
				 * 1) Update the board block to the new end
				 *      and
				 * 2) Update the start of this block to my end.
				 */
				b->start = sf->sf_blks[i].end;
				sf->sf_blks[i].end = b->end;
				goto nxt_blk;
			}
			if (SEQ_GEQ(sf->sf_blks[i].end, b->end)) {
				/** 
				 * The board block partial meets:
				 *
				 *  board       |--------|
				 *  sack  |----------|  
				 *     <or>
				 *  board       |----|
				 *  sack  |----------|  
				 * 1) Update the board block to the new start
				 *      and
				 * 2) Update the start of this block to my end.
				 */
				b->end = sf->sf_blks[i].start;
				sf->sf_blks[i].start = b->start;
				goto nxt_blk;
			}
		} 
	nxt_blk:
		i++;
		i %= SACK_FILTER_BLOCKS;
	}
	/* Did we totally consume it in pieces? */
	if (b->start != b->end)
		return(0);
	else
		return(1);
}

static int32_t
sack_filter_old(struct sack_filter *sf, struct sackblk *in, int  numblks)
{
	int32_t num, i;
	struct sackblk blkboard[TCP_MAX_SACK];
	/* 
	 * An old sack has arrived. It may contain data
	 * we do not have. We might not have it since
	 * we could have had a lost ack <or> we might have the
	 * entire thing on our current board. We want to prune
	 * off anything we have. With this function though we
	 * won't add to the board.
	 */
	for( i = 0, num = 0; i<numblks; i++ ) {
		if (is_sack_on_board(sf, &in[i])) {
#ifndef _KERNEL
			cnt_skipped_oldsack++;
#endif
			continue;
		}
		/* Did not find it (or found only 
		 * a piece of it). Copy it to 
		 * our outgoing board.
		 */
		memcpy(&blkboard[num], &in[i], sizeof(struct sackblk));
#ifndef _KERNEL
		cnt_used_oldsack++;
#endif
		num++;
	}
	if (num) {
		memcpy(in, blkboard, (num * sizeof(struct sackblk)));
	}
	return (num);
}

/* 
 * Given idx its used but there is space available 
 * move the entry to the next free slot
 */
static void
sack_move_to_empty(struct sack_filter *sf, uint32_t idx)
{
	int32_t i, cnt;

	i = (idx + 1) % SACK_FILTER_BLOCKS;
	for (cnt=0; cnt <(SACK_FILTER_BLOCKS-1); cnt++) {
		if (sack_blk_used(sf, i) == 0) {
			memcpy(&sf->sf_blks[i], &sf->sf_blks[idx], sizeof(struct sackblk));			
			sf->sf_bits = sack_blk_clr(sf, idx);
			sf->sf_bits = sack_blk_set(sf, i);
			return;
		}
		i++;
		i %= SACK_FILTER_BLOCKS;
	}
}

static int32_t
sack_filter_new(struct sack_filter *sf, struct sackblk *in, int numblks, tcp_seq th_ack)
{
	struct sackblk blkboard[TCP_MAX_SACK];
	int32_t num, i;
	/* 
	 * First lets trim the old and possibly 
	 * throw any away we have. 
	 */
	for(i=0, num=0; i<numblks; i++) {
		if (is_sack_on_board(sf, &in[i]))
			continue;
		memcpy(&blkboard[num], &in[i], sizeof(struct sackblk));
		num++;
	}
	if (num == 0)
		return(num);

	/* Now what we are left is either 
	 * completely merged on to the board
	 * from the above steps, or are new
	 * and need to be added to the board
	 * with the last one updated to current.
	 *
	 * First copy it out we want to return that
	 * to our caller for processing.
	 */
	memcpy(in, blkboard, (num * sizeof(struct sackblk)));	
	numblks = num;
	/* Now go through and add to our board as needed */
	for(i=(num-1); i>=0; i--) {
		if (is_sack_on_board(sf, &blkboard[i]))
			continue;
		/* Add this guy its not listed */
		sf->sf_cur++;
		sf->sf_cur %= SACK_FILTER_BLOCKS;
		if ((sack_blk_used(sf, sf->sf_cur)) &&
		    (sf->sf_used < SACK_FILTER_BLOCKS)) {
			sack_move_to_empty(sf, sf->sf_cur);
		}
#ifndef _KERNEL
		if (sack_blk_used(sf, sf->sf_cur)) {
			over_written++;
			if (sf->sf_used < SACK_FILTER_BLOCKS)
				empty_avail++;
		}
#endif
		memcpy(&sf->sf_blks[sf->sf_cur], &in[i], sizeof(struct sackblk));
		if (sack_blk_used(sf, sf->sf_cur) == 0) {
			sf->sf_used++;
#ifndef _KERNEL
			if (sf->sf_used > highest_used)
				highest_used = sf->sf_used;
#endif
			sf->sf_bits = sack_blk_set(sf, sf->sf_cur);
		}
	}
	return(numblks);
}

/*
 * Given a sack block on the board (the skip index) see if
 * any other used entries overlap or meet, if so return the index.
 */
static int32_t
sack_blocks_overlap_or_meet(struct sack_filter *sf, struct sackblk *sb, uint32_t skip)
{
	int32_t i;
	
	for(i=0; i<SACK_FILTER_BLOCKS; i++) {
		if (sack_blk_used(sf, i) == 0)
			continue;
		if (i == skip)
			continue;
		if (SEQ_GEQ(sf->sf_blks[i].end, sb->start) &&
		    SEQ_LEQ(sf->sf_blks[i].end, sb->end) &&
		    SEQ_LEQ(sf->sf_blks[i].start, sb->start)) {
			/** 
			 * The two board blocks meet:
			 *
			 *  board1   |--------|
			 *  board2       |----------|  
			 *    <or>
			 *  board1   |--------|
			 *  board2   |--------------|  
			 *    <or>
			 *  board1   |--------|
			 *  board2   |--------|
			 */
			return(i);
		}
		if (SEQ_LEQ(sf->sf_blks[i].start, sb->end) &&
		    SEQ_GEQ(sf->sf_blks[i].start, sb->start) &&
		    SEQ_GEQ(sf->sf_blks[i].end, sb->end)) {
			/** 
			 * The board block partial meets:
			 *
			 *  board       |--------|
			 *  sack  |----------|  
			 *     <or>
			 *  board       |----|
			 *  sack  |----------|  
			 * 1) Update the board block to the new start
			 *      and
			 * 2) Update the start of this block to my end.
			 */
			return(i);
		}
	}
	return (-1);
}

/*
 * Collapse entry src into entry into
 * and free up the src entry afterwards.
 */
static void
sack_collapse(struct sack_filter *sf, int32_t src, int32_t into)
{
	if (SEQ_LT(sf->sf_blks[src].start, sf->sf_blks[into].start)) {
		/* src has a lower starting point */
		sf->sf_blks[into].start = sf->sf_blks[src].start;
	}
	if (SEQ_GT(sf->sf_blks[src].end, sf->sf_blks[into].end)) {
		/* src has a higher ending point */
		sf->sf_blks[into].end = sf->sf_blks[src].end;
	}
	sf->sf_bits = sack_blk_clr(sf, src);
	sf->sf_used--;
}

static void
sack_board_collapse(struct sack_filter *sf)
{
	int32_t i, j, i_d, j_d;

	for(i=0; i<SACK_FILTER_BLOCKS; i++) {
		if (sack_blk_used(sf, i) == 0)
			continue;
		/*
		 * Look at all other blocks but this guy 
		 * to see if they overlap. If so we collapse
		 * the two blocks together.
		 */
		j = sack_blocks_overlap_or_meet(sf, &sf->sf_blks[i], i);
		if (j == -1) {
			/* No overlap */
			continue;
		}
		/* 
		 * Ok j and i overlap with each other, collapse the
		 * one out furthest away from the current position.
		 */
		if (sf->sf_cur > i)
			i_d = sf->sf_cur - i;
		else
			i_d = i - sf->sf_cur;
		if (sf->sf_cur > j)
			j_d = sf->sf_cur - j;
		else
			j_d = j - sf->sf_cur;
		if (j_d > i_d) {
			sack_collapse(sf, j, i);
		} else
			sack_collapse(sf, i, j);
	}
}

#ifndef _KERNEL
static
#endif
int
sack_filter_blks(struct sack_filter *sf, struct sackblk *in, int numblks, tcp_seq th_ack)
{
	int32_t i, ret;
	
	if (numblks > TCP_MAX_SACK) {
		panic("sf:%p sb:%p Impossible number of sack blocks %d > 4\n",
		      sf, in, 
		      numblks);
		return(numblks);
	}
	if ((sf->sf_used == 0) && numblks) {
		/* 
		 * We are brand new add the blocks in 
		 * reverse order. Note we can see more
		 * than one in new, since ack's could be lost.
		 */
		sf->sf_ack = th_ack;
		for(i=(numblks-1), sf->sf_cur=0; i >= 0; i--) {
			memcpy(&sf->sf_blks[sf->sf_cur], &in[i], sizeof(struct sackblk));
			sf->sf_bits = sack_blk_set(sf, sf->sf_cur);
			sf->sf_cur++;
			sf->sf_cur %= SACK_FILTER_BLOCKS;
			sf->sf_used++;
#ifndef _KERNEL
			if (sf->sf_used > highest_used)
				highest_used = sf->sf_used;
#endif
		}
		if (sf->sf_cur)
			sf->sf_cur--;
		return(numblks);
	}
	if (SEQ_GT(th_ack, sf->sf_ack)) {
		sack_filter_prune(sf, th_ack);
	}
	if (numblks) {
		if (SEQ_GEQ(th_ack, sf->sf_ack)) {
			ret = sack_filter_new(sf, in, numblks, th_ack);
		} else {
			ret = sack_filter_old(sf, in, numblks);
		}
	} else
		ret = 0;
#ifndef _KERNEL
	if ((sf->sf_used > 1) && (no_collapse == 0))
		sack_board_collapse(sf);

#else	
	if (sf->sf_used > 1) 
		sack_board_collapse(sf);

#endif
	return (ret);
}

#ifndef _KERNEL
uint64_t saved=0;
uint64_t tot_sack_blks=0;

static void
sack_filter_dump(FILE *out, struct sack_filter *sf)
{
	int i;
	fprintf(out, "	sf_ack:%u sf_bits:0x%x c:%d used:%d\n",
		sf->sf_ack, sf->sf_bits,
		sf->sf_cur, sf->sf_used);

	for(i=0; i<SACK_FILTER_BLOCKS; i++) {
		if (sack_blk_used(sf, i)) {
			fprintf(out, "Entry:%d start:%u end:%u\n", i,
			       sf->sf_blks[i].start,
			       sf->sf_blks[i].end);
		}
	}
}

int
main(int argc, char **argv)
{
	char buffer[512];
	struct sackblk blks[TCP_MAX_SACK];
	FILE *err;
	tcp_seq th_ack, snd_una;
	struct sack_filter sf;
	int32_t numblks,i;
	int snd_una_set=0;
	double a, b, c;
	int invalid_sack_print = 0;	
	uint32_t chg_remembered=0;
	uint32_t sack_chg=0;
	char line_buf[10][256];
	int line_buf_at=0;

	in = stdin;
	out = stdout;
	while ((i = getopt(argc, argv, "ndIi:o:?h")) != -1) {
		switch (i) {
		case 'n':
			no_collapse = 1;
			break;
		case 'd':
			detailed_dump = 1;
			break;
		case'I':
			invalid_sack_print = 1;
			break;
		case 'i':
			in = fopen(optarg, "r");
			if (in == NULL) {
				fprintf(stderr, "Fatal error can't open %s for input\n", optarg);
				exit(-1);
			}
			break;
		case 'o':
			out = fopen(optarg, "w");
			if (out == NULL) {
				fprintf(stderr, "Fatal error can't open %s for output\n", optarg);
				exit(-1);
			}
			break;
		default:
		case '?':
		case 'h':
			fprintf(stderr, "Use %s [ -i infile -o outfile -I]\n", argv[0]);
			return(0);
			break;
		};
	}
	sack_filter_clear(&sf, 0);
	memset(buffer, 0, sizeof(buffer));
	memset(blks, 0, sizeof(blks));
	numblks = 0;
	fprintf(out, "************************************\n");
	while (fgets(buffer, sizeof(buffer), in) != NULL) {
		sprintf(line_buf[line_buf_at], "%s", buffer);
		line_buf_at++;
		if (strncmp(buffer, "QUIT", 4) == 0) {
			break;
		} else if (strncmp(buffer, "DONE", 4) == 0) {
			int nn, ii;
			if (numblks) {
				uint32_t szof, tot_chg;
				for(ii=0; ii<line_buf_at; ii++) {
					fprintf(out, "%s", line_buf[ii]);
				}
				fprintf(out, "------------------------------------\n");
				nn = sack_filter_blks(&sf, blks, numblks, th_ack);
				saved += numblks - nn;
				tot_sack_blks += numblks;
				fprintf(out, "ACK:%u\n", sf.sf_ack);
				for(ii=0, tot_chg=0; ii<nn; ii++) {
					szof = blks[ii].end - blks[ii].start;
					tot_chg += szof;
					fprintf(out, "SACK:%u:%u [%u]\n",
					       blks[ii].start,
						blks[ii].end, szof);
				}
				fprintf(out,"************************************\n");
				chg_remembered = tot_chg;
				if (detailed_dump) {
					sack_filter_dump(out, &sf);
					fprintf(out,"************************************\n");
				}
			}
			memset(blks, 0, sizeof(blks));
			memset(line_buf, 0, sizeof(line_buf));
			line_buf_at=0;
			numblks = 0;
		} else if (strncmp(buffer, "CHG:", 4) == 0) {
			sack_chg = strtoul(&buffer[4], NULL, 0);
			if ((sack_chg != chg_remembered) &&
			    (sack_chg > chg_remembered)){
				fprintf(out,"***WARNING WILL RODGERS DANGER!! sack_chg:%u last:%u\n",
					sack_chg, chg_remembered
					);
			}
			sack_chg = chg_remembered = 0;
		} else if (strncmp(buffer, "RXT", 3) == 0) {
			sack_filter_clear(&sf, snd_una);
		} else if (strncmp(buffer, "ACK:", 4) == 0) {
			th_ack = strtoul(&buffer[4], NULL, 0);
			if (snd_una_set == 0) {
				snd_una = th_ack;
				snd_una_set = 1;
			} else if (SEQ_GT(th_ack, snd_una)) {
				snd_una = th_ack;
			}
		} else if (strncmp(buffer, "EXIT", 4) == 0) {
			sack_filter_clear(&sf, snd_una);
			sack_chg = chg_remembered = 0;
		} else if (strncmp(buffer, "SACK:", 5) == 0) {
			char *end=NULL;
			uint32_t start;
			uint32_t endv;
			start = strtoul(&buffer[5], &end, 0);
			if (end) {
				endv = strtoul(&end[1], NULL, 0);
			} else {
				fprintf(out, "--Sack invalid skip 0 start:%u : ??\n", start);
				continue;
			}
			if (SEQ_LT(endv, start)) {
				fprintf(out, "--Sack invalid skip 1 endv:%u < start:%u\n", endv, start);
				continue;
			}
			if (numblks == TCP_MAX_SACK) {
				fprintf(out, "--Exceeded max %d\n", numblks);
				exit(0);
			}
			blks[numblks].start = start;
			blks[numblks].end = endv;
			numblks++;
		}
		memset(buffer, 0, sizeof(buffer));
	}
	if (in != stdin) {
		fclose(in);
	}
	if (out != stdout) {
		fclose(out);
	}
	a = saved * 100.0;
	b = tot_sack_blks * 1.0;
	if (b > 0.0)
		c = a/b;
	else
		c = 0.0;
	if (out != stdout)
		err = stdout;
	else
		err = stderr;
	fprintf(err, "Saved %lu sack blocks out of %lu (%2.3f%%) old_skip:%lu old_usd:%lu high_cnt:%d ow:%d ea:%d\n",
		saved, tot_sack_blks, c, cnt_skipped_oldsack, cnt_used_oldsack, highest_used, over_written, empty_avail);
	return(0);
}
#endif
