/*-
 * Copyright (c) 2007, Erik Tews, Andrei Pychkine and Ralf-Philipp Weinmann
 *		       <aircrack-ptw@cdc.informatik.tu-darmstadt.de>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "aircrack-ptw-lib.h"


#define n PTW_n
#define CONTROLSESSIONS PTW_CONTROLSESSIONS
#define KEYHSBYTES PTW_KEYHSBYTES
#define KSBYTES PTW_KSBYTES
#define IVBYTES PTW_IVBYTES
#define TESTBYTES 6


// Internal state of rc4
typedef struct {
	uint8_t i;
	uint8_t j;
	uint8_t s[n];
} rc4state;


// Helper structures for sorting
typedef struct {
	int keybyte;
	uint8_t value;
	int distance;
} sorthelper;

typedef struct {
	int keybyte;
	double difference;
} doublesorthelper;

// The rc4 initial state, the idendity permutation
static const uint8_t rc4initial[] =
{0,1,2,3,4,5,6,7,8,9,10,
11,12,13,14,15,16,17,18,19,20,
21,22,23,24,25,26,27,28,29,30,
31,32,33,34,35,36,37,38,39,40,
41,42,43,44,45,46,47,48,49,50,
51,52,53,54,55,56,57,58,59,60,
61,62,63,64,65,66,67,68,69,70,
71,72,73,74,75,76,77,78,79,80,
81,82,83,84,85,86,87,88,89,90,
91,92,93,94,95,96,97,98,99,100,
101,102,103,104,105,106,107,108,109,110,
111,112,113,114,115,116,117,118,119,120,
121,122,123,124,125,126,127,128,129,130,
131,132,133,134,135,136,137,138,139,140,
141,142,143,144,145,146,147,148,149,150,
151,152,153,154,155,156,157,158,159,160,
161,162,163,164,165,166,167,168,169,170,
171,172,173,174,175,176,177,178,179,180,
181,182,183,184,185,186,187,188,189,190,
191,192,193,194,195,196,197,198,199,200,
201,202,203,204,205,206,207,208,209,210,
211,212,213,214,215,216,217,218,219,220,
221,222,223,224,225,226,227,228,229,230,
231,232,233,234,235,236,237,238,239,240,
241,242,243,244,245,246,247,248,249,250,
251,252,253,254,255};


// Values for p_correct_i
static const double eval[] = {
0.00534392069257663,
0.00531787585068872,
0.00531345769225911,
0.00528812219217898,
0.00525997750378221,
0.00522647312237696,
0.00519132541143668,
0.0051477139367225,
0.00510438884847959,
0.00505484662057323,
0.00500502783556246,
0.00495094196451801,
0.0048983441590402};

// For sorting
static int compare(const void * ina, const void * inb) {
        PTW_tableentry * a = (PTW_tableentry * )ina;
        PTW_tableentry * b = (PTW_tableentry * )inb;
        if (a->votes > b->votes) {
                return -1;
        } else if (a->votes == b->votes) {
                return 0;
        } else {
                return 1;
        }
}

// For sorting
static int comparedoublesorthelper(const void * ina, const void * inb) {
        doublesorthelper * a = (doublesorthelper * )ina;
        doublesorthelper * b = (doublesorthelper * )inb;
        if (a->difference > b->difference) {
                return 1;
        } else if (a->difference == b->difference) {
                return 0;
        } else {
                return -1;
        }
}


// RC4 key setup
static void rc4init ( uint8_t * key, int keylen, rc4state * state) {
	int i;
	int j;
	uint8_t tmp;
	memcpy(state->s, &rc4initial, n);
	j = 0;
	for (i = 0; i < n; i++) {
		j = (j + state->s[i] + key[i % keylen]) % n;
		tmp = state->s[i];
		state->s[i] = state->s[j];
		state->s[j] = tmp;
	}
	state->i = 0;
	state->j = 0;
}

// RC4 key stream generation
static uint8_t rc4update(rc4state * state) {
	uint8_t tmp;
	uint8_t k;
	state->i++;
	state->j += state->s[state->i];
	tmp = state->s[state->i];
	state->s[state->i] = state->s[state->j];
	state->s[state->j] = tmp;
	k = state->s[state->i] + state->s[state->j];

	return state->s[k];
}

// For sorting
static int comparesorthelper(const void * ina, const void * inb) {
	sorthelper * a = (sorthelper * ) ina;
	sorthelper * b = (sorthelper * ) inb;
	if (a->distance > b->distance) {
		return 1;
	} else if (a->distance == b->distance) {
		return 0;
	} else {
		return -1;
	}
}

/*
 * Guess the values for sigma_i
 * iv - IV which was used for this packet
 * keystream - keystream recovered
 * result - buffer for the values of sigma_i
 * kb - how many keybytes should be guessed
 */
static void guesskeybytes(uint8_t * iv, uint8_t * keystream, uint8_t * result, int kb) {
        uint8_t state[n];
        uint8_t j = 0;
        uint8_t tmp;
        int i;
        int jj = IVBYTES;
        uint8_t ii;
        uint8_t s = 0;
        memcpy(state, rc4initial, n);
        for (i = 0; i < IVBYTES; i++) {
                j += state[i] + iv[i];
                tmp = state[i];
                state[i] = state[j];
                state[j] = tmp;
        }
        for (i = 0; i < kb; i++) {
                tmp = jj - keystream[jj-1];
                ii = 0;
                while(tmp != state[ii]) {
                        ii++;
                }
                s += state[jj];
                ii -= (j+s);
                result[i] = ii;
                jj++;
        }
        return;
}

/*
 * Is a guessed key correct?
 */
static int correct(PTW_attackstate * state, uint8_t * key, int keylen) {
	int i;
        int j;
        uint8_t keybuf[PTW_KSBYTES];
        rc4state rc4state;

        for (i = 0; i < state->sessions_collected; i++) {
                memcpy(&keybuf[IVBYTES], key, keylen);
                memcpy(keybuf, state->sessions[i].iv, IVBYTES);
                rc4init(keybuf, keylen+IVBYTES, &rc4state);
                for (j = 0; j < TESTBYTES; j++) {
                        if  ((rc4update(&rc4state) ^ state->sessions[i].keystream[j]) != 0) {
                                return 0;
                        }
                }
        }
        return 1;
}

/*
 * Calculate the squaresum of the errors for both distributions
 */
static void getdrv(PTW_tableentry orgtable[][n], int keylen, double * normal, double * ausreiser) {
        int i,j;
	int numvotes = 0;
        double e;
	double e2;
	double emax;
        double help = 0.0;
	double maxhelp = 0;
	double maxi = 0;
        for (i = 0; i < n; i++) {
                numvotes += orgtable[0][i].votes;
        }
        e = numvotes/n;
        for (i = 0; i < keylen; i++) {
		emax = eval[i] * numvotes;
		e2 = ((1.0 - eval[i])/255.0) * numvotes;
		normal[i] = 0;
		ausreiser[i] = 0;
		maxhelp = 0;
		maxi = 0;
		for (j = 0; j < n; j++) {
			if (orgtable[i][j].votes > maxhelp) {
				maxhelp = orgtable[i][j].votes;
				maxi = j;
			}
		}
                for (j = 0; j < n; j++) {
			if (j == maxi) {
				help = (1.0-orgtable[i][j].votes/emax);
			} else {
				help = (1.0-orgtable[i][j].votes/e2);
			}
			help = help*help;
			ausreiser[i] += help;
			help = (1.0-orgtable[i][j].votes/e);
			help = help*help;
			normal[i] += help;
                }
        }
}

/*
 * Guess a single keybyte
 */
static int doRound(PTW_tableentry sortedtable[][n], int keybyte, int fixat, uint8_t fixvalue, int * searchborders, uint8_t * key, int keylen, PTW_attackstate * state, uint8_t sum, int * strongbytes) {
	int i;
	uint8_t tmp;
	if (keybyte == keylen) {
		return correct(state, key, keylen);
	} else if (strongbytes[keybyte] == 1) {
		// printf("assuming byte %d to be strong\n", keybyte);
		tmp = 3 + keybyte;
		for (i = keybyte-1; i >= 1; i--) {
			tmp += 3 + key[i] + i;
			key[keybyte] = 256-tmp;
			if(doRound(sortedtable, keybyte+1, fixat, fixvalue, searchborders, key, keylen, state, (256-tmp+sum)%256, strongbytes) == 1) {
				printf("hit with strongbyte for keybyte %d\n", keybyte);
				return 1;
			}
		}
		return 0;
	} else if (keybyte == fixat) {
		key[keybyte] = fixvalue-sum;
		return doRound(sortedtable, keybyte+1, fixat, fixvalue, searchborders, key, keylen, state, fixvalue, strongbytes);
	} else {
		for (i = 0; i < searchborders[keybyte]; i++) {
			key[keybyte] = sortedtable[keybyte][i].b - sum;
			if (doRound(sortedtable, keybyte+1, fixat, fixvalue, searchborders, key, keylen, state, sortedtable[keybyte][i].b, strongbytes) == 1) {
				return 1;
			}
		}
		return 0;
	}
}

/*
 * Do the actual computation of the key
 */
static int doComputation(PTW_attackstate * state, uint8_t * key, int keylen, PTW_tableentry table[][n], sorthelper * sh2, int * strongbytes, int keylimit) {
	int i,j;
	int choices[KEYHSBYTES];
	int prod;
	int fixat;
	int fixvalue;

	for (i = 0; i < keylen; i++) {
		if (strongbytes[i] == 1) {
			choices[i] = i;
		} else {
			choices[i] = 1;
		}
	}
	i = 0;
	prod = 0;
	fixat = -1;
	fixvalue = 0;

	while(prod < keylimit) {
		if (doRound(table, 0, fixat, fixvalue, choices, key, keylen, state, 0, strongbytes) == 1) {
			// printf("hit with %d choices\n", prod);
			return 1;
		}
		choices[sh2[i].keybyte]++;
		fixat = sh2[i].keybyte;
		// printf("choices[%d] is now %d\n", sh2[i].keybyte, choices[sh2[i].keybyte]);
		fixvalue = sh2[i].value;
		prod = 1;
		for (j = 0; j < keylen; j++) {
			prod *= choices[j];
		}
		do {
			i++;
		} while (strongbytes[sh2[i].keybyte] == 1);

	}
	return 0;
}
		

/*
 * Guess which key bytes could be strong and start actual computation of the key
 */
int PTW_computeKey(PTW_attackstate * state, uint8_t * keybuf, int keylen, int testlimit) {
	int strongbytes[KEYHSBYTES];
	double normal[KEYHSBYTES];
	double ausreisser[KEYHSBYTES];
	doublesorthelper helper[KEYHSBYTES];
	int simple, onestrong, twostrong;
	int i,j;

	onestrong = (testlimit/10)*2;
	twostrong = (testlimit/10)*1;
	simple = testlimit - onestrong - twostrong;

	PTW_tableentry (*table)[n] = alloca(sizeof(PTW_tableentry) * n * keylen);
	if (table == NULL) {
		printf("could not allocate memory\n");
		exit(-1);
	}
	memcpy(table, state->table, sizeof(PTW_tableentry) * n * keylen);

	// now, sort the table
	for (i = 0; i < keylen; i++) {
                qsort(&table[i][0], n, sizeof(PTW_tableentry), &compare);
		strongbytes[i] = 0;
        }

	sorthelper (* sh)[n-1] = alloca(sizeof(sorthelper) * (n-1) * keylen);
	if (sh == NULL) {
		printf("could not allocate memory\n");
		exit(-1);
	}

	
	for (i = 0; i < keylen; i++) {
		for (j = 1; j < n; j++) {
			sh[i][j-1].distance = table[i][0].votes - table[i][j].votes;
			sh[i][j-1].value = table[i][j].b;
			sh[i][j-1].keybyte = i;
		}
	}
	qsort(sh, (n-1)*keylen, sizeof(sorthelper), &comparesorthelper);


	if (doComputation(state, keybuf, keylen, table, (sorthelper *) sh, strongbytes, simple)) {
		return 1;
	}

	// Now one strong byte
	getdrv(state->table, keylen, normal, ausreisser);
	for (i = 0; i < keylen-1; i++) {
		helper[i].keybyte = i+1;
		helper[i].difference = normal[i+1] - ausreisser[i+1];
	}
	qsort(helper, keylen-1, sizeof(doublesorthelper), &comparedoublesorthelper);
	strongbytes[helper[0].keybyte] = 1;
	if (doComputation(state, keybuf, keylen, table, (sorthelper *) sh, strongbytes, onestrong)) {
		return 1;
	}

	// two strong bytes
	strongbytes[helper[1].keybyte] = 1;
	if (doComputation(state, keybuf, keylen, table, (sorthelper *) sh, strongbytes, twostrong)) {
		return 1;
	}

	return 0;
}

/*
 * Add a new session to the attack
 * state - state of attack
 * iv - IV used in the session
 * keystream - recovered keystream from the session
 */
int PTW_addsession(PTW_attackstate * state, uint8_t * iv, uint8_t * keystream) {
	int i;
	int il;
	int ir;
	uint8_t buf[PTW_KEYHSBYTES];

	i = (iv[0] << 16) | (iv[1] << 8) | (iv[2]);
	il = i/8;
	ir = 1 << (i%8);
	if ((state->seen_iv[il] & ir) == 0) {
		state->packets_collected++;
		state->seen_iv[il] |= ir;
		guesskeybytes(iv, keystream, buf, PTW_KEYHSBYTES);
                for (i = 0; i < KEYHSBYTES; i++) {
                	state->table[i][buf[i]].votes++;
                }
		if (state->sessions_collected < CONTROLSESSIONS) {
			memcpy(state->sessions[state->sessions_collected].iv, iv, IVBYTES);
			memcpy(state->sessions[state->sessions_collected].keystream, keystream, KSBYTES);
			state->sessions_collected++;
		}
		return 1;
	} else {
		return 0;
	}
}

/*
 * Allocate a new attackstate
 */
PTW_attackstate * PTW_newattackstate() {
	int i,k;
	PTW_attackstate * state = NULL;
	state = malloc(sizeof(PTW_attackstate));
	if (state == NULL) {
		return NULL;
	}
	bzero(state, sizeof(PTW_attackstate));
	for (i = 0; i < PTW_KEYHSBYTES; i++) {
                for (k = 0; k < n; k++) {
                        state->table[i][k].b = k;
                }
        }
        return state;
}

/*
 * Free an allocated attackstate
 */
void PTW_freeattackstate(PTW_attackstate * state) {
	free(state);
	return;
}

