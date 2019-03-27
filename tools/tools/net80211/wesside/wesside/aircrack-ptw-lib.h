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
#include <stdint.h>

// Number of bytes we use for our table of seen IVs, this is (2^24)/8
#define PTW_IVTABLELEN 2097152

// How many sessions do we use to check if a guessed key is correct
// 10 seems to be a reasonable choice
#define PTW_CONTROLSESSIONS 10

// The maximum possible length of the main key, 13 is the maximum for a 104 bit key
#define PTW_KEYHSBYTES 13

// How long the IV is, 3 is the default value for WEP
#define PTW_IVBYTES 3

// How many bytes of a keystream we collect, 16 are needed for a 104 bit key
#define PTW_KSBYTES 16

// The MAGIC VALUE!!
#define PTW_n 256

// We use this to keep track of the outputs of A_i
typedef struct {
	// How often the value b appeard as an output of A_i
	int votes;

	uint8_t b;
} PTW_tableentry;

// A recovered session
typedef struct {
	// The IV used in this session
        uint8_t iv[PTW_IVBYTES];
	// The keystream used in this session
        uint8_t keystream[PTW_KSBYTES];
} PTW_session;

// The state of an attack
// You should usually never modify these values manually
typedef struct {
	// How many unique packets or IVs have been collected
        int packets_collected;
	// Table to check for duplicate IVs
        uint8_t seen_iv[PTW_IVTABLELEN];
	// How many sessions for checking a guessed key have been collected
        int sessions_collected;
	// The actual recovered sessions
        PTW_session sessions[PTW_CONTROLSESSIONS];
	// The table with votes for the keybytesums
        PTW_tableentry table[PTW_KEYHSBYTES][PTW_n];
} PTW_attackstate;

PTW_attackstate * PTW_newattackstate();
void PTW_freeattackstate(PTW_attackstate *);
int PTW_addsession(PTW_attackstate *, uint8_t *, uint8_t *);
int PTW_computeKey(PTW_attackstate *, uint8_t *, int, int);
