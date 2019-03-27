#-
# Copyright (c) 2003 Mathew Kanner
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

INTERFACE synth;

#include <sys/systm.h>

CODE {

synth_killnote_t nokillnote;
synth_startnote_t nostartnote;
synth_setinstr_t nosetinstr;
synth_hwcontrol_t nohwcontrol;
synth_aftertouch_t noaftertouch;
synth_panning_t nopanning;
synth_controller_t nocontroller;
synth_volumemethod_t novolumemethod;
synth_bender_t nobender;
synth_setupvoice_t nosetupvoice;
synth_sendsysex_t nosendsysex;
synth_allocvoice_t noallocvoice;
synth_writeraw_t nowriteraw;
synth_reset_t noreset;
synth_shortname_t noshortname;
synth_open_t noopen;
synth_close_t noclose;
synth_query_t noquery;
synth_insync_t noinsync;
synth_alloc_t noalloc;

    int
	nokillnote(void *_kobj, uint8_t _chn, uint8_t _note, uint8_t _vel)
	{
	    printf("nokillnote\n");
	    return 0;
	}

    int
	noopen(void *_kobj, void *_arg, int mode)
	{
	    printf("noopen\n");
	    return 0;
	}

    int
	noquery(void *_kboj)
	{
	    printf("noquery\n");
	    return 0;
	}

    int
	nostartnote(void *_kb, uint8_t _voice, uint8_t _note, uint8_t _parm)
	{
	    printf("nostartnote\n");
	    return 0;
	}

    int
	nosetinstr(void *_kb, uint8_t _chn, uint16_t _patchno)
	{
	    printf("nosetinstr\n");
	    return 0;
	}

    int
	nohwcontrol(void *_kb, uint8_t *_event)
	{
	    printf("nohwcontrol\n");
	    return 0;
	}

    int 
	noaftertouch ( void /* X */ * _kobj, uint8_t _x1, uint8_t _x2)
	{
	    printf("noaftertouch\n");
	    return 0;
	}

    int
	nopanning ( void /* X */ * _kobj, uint8_t _x1, uint8_t _x2)
	{
	    printf("nopanning\n");
	    return 0;
	}

    int 
	nocontroller ( void /* X */ * _kobj, uint8_t _x1, uint8_t _x2, uint16_t _x3)
	{
	    printf("nocontroller\n");
	    return 0;
	}

    int 
	novolumemethod (
		void /* X */ * _kobj,
		uint8_t _x1)
	{
	    printf("novolumemethod\n");
	    return 0;
	}

    int 
	nobender ( void /* X */ * _kobj, uint8_t _voice, uint16_t _bend)
	{
	    printf("nobender\n");
	    return 0;
	}

    int 
	nosetupvoice ( void /* X */ * _kobj, uint8_t _voice, uint8_t _chn)
	{

	    printf("nosetupvoice\n");
	    return 0;
	}

    int 
	nosendsysex ( void /* X */ * _kobj, void * _buf, size_t _len)
	{
	    printf("nosendsysex\n");
	    return 0;
	}

    int 
	noallocvoice ( void /* X */ * _kobj, uint8_t _chn, uint8_t _note, void *_x)
	{
	    printf("noallocvoice\n");
	    return 0;
	}

    int 
	nowriteraw ( void /* X */ * _kobjt, uint8_t * _buf, size_t _len)
	{
	    printf("nowriteraw\n");
	    return 1;
	}

    int 
	noreset ( void /* X */ * _kobjt)
	{

	    printf("noreset\n");
	    return 0;
	}

    char *
	noshortname (void /* X */ * _kobjt)
	{
	    printf("noshortname\n");
	    return "noshortname";
	}

    int 
	noclose ( void /* X */ * _kobjt)
	{

	    printf("noclose\n");
	    return 0;
	}

    int
	noinsync (void /* X */ * _kobjt)
	{

	    printf("noinsync\n");
	    return 0;
	}

    int 
	noalloc ( void /* x */ * _kbojt, uint8_t _chn, uint8_t _note)
	{
	    printf("noalloc\n");
	    return 0;
	}
}

METHOD int killnote {
	void /* X */ *_kobj;
	uint8_t	_chan;
	uint8_t	_note;
	uint8_t	_vel;
} DEFAULT nokillnote;

METHOD int startnote {
	void /* X */ *_kobj;
	uint8_t	_voice;
	uint8_t	_note;
	uint8_t	_parm;
} DEFAULT nostartnote;

METHOD int setinstr {
	void /* X */ *_kobj;
	uint8_t	_chn;
	uint16_t _patchno;
} DEFAULT nosetinstr;

METHOD int hwcontrol {
	void /* X */ *_kobj;
	uint8_t *_event;
} DEFAULT nohwcontrol;

METHOD int aftertouch {
	void /* X */ *_kobj;
	uint8_t	_x1;
	uint8_t	_x2;
} DEFAULT noaftertouch;

METHOD int panning {
	void /* X */ *_kobj;
	uint8_t	_x1;
	uint8_t	_x2;
} DEFAULT nopanning;

METHOD int controller {
	void /* X */ *_kobj;
	uint8_t	_x1;
	uint8_t	_x2;
	uint16_t _x3;
} DEFAULT nocontroller;

METHOD int volumemethod {
	void /* X */ *_kobj;
	uint8_t	_x1;
} DEFAULT novolumemethod;

METHOD int bender {
	void /* X */ *_kobj;
	uint8_t	_voice;
	uint16_t _bend;
} DEFAULT nobender;

METHOD int setupvoice {
	void /* X */ *_kobj;
	uint8_t	_voice;
	uint8_t	_chn;
} DEFAULT nosetupvoice;

METHOD int sendsysex {
	void /* X */ *_kobj;
	void   *_buf;
	size_t	_len;
} DEFAULT nosendsysex;

METHOD int allocvoice {
	void /* X */ *_kobj;
	uint8_t	_chn;
	uint8_t	_note;
	void   *_x;
} DEFAULT noallocvoice;

METHOD int writeraw {
	void /* X */ *_kobjt;
	uint8_t *_buf;
	size_t	_len;
} DEFAULT nowriteraw;

METHOD int reset {
	void /* X */ *_kobjt;
} DEFAULT noreset;

METHOD char * shortname {
	void /* X */ *_kobjt;
} DEFAULT noshortname;

METHOD int open {
	void /* X */ *_kobjt;
	void   *_sythn;
	int	_mode;
} DEFAULT noopen;

METHOD int close {
	void /* X */ *_kobjt;
} DEFAULT noclose;

METHOD int query {
	void /* X */ *_kobjt;
} DEFAULT noquery;

METHOD int insync {
	void /* X */ *_kobjt;
} DEFAULT noinsync;

METHOD int alloc {
	void /* x */ *_kbojt;
	uint8_t	_chn;
	uint8_t	_note;
} DEFAULT noalloc;
