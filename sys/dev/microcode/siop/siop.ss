;	$OpenBSD: siop.ss,v 1.10 2010/07/23 07:47:13 jsg Exp $
;	$NetBSD: siop.ss,v 1.20 2005/11/18 23:10:32 bouyer Exp $

;
;  Copyright (c) 2000 Manuel Bouyer.
;
;  Redistribution and use in source and binary forms, with or without
;  modification, are permitted provided that the following conditions
;  are met:
;  1. Redistributions of source code must retain the above copyright
;     notice, this list of conditions and the following disclaimer.
;  2. Redistributions in binary form must reproduce the above copyright
;     notice, this list of conditions and the following disclaimer in the
;     documentation and/or other materials provided with the distribution.
;
;  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
;  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
;  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
;  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
;  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
;  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
;  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
;  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
;  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
;  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

ARCH 720

; offsets in siop_common_xfer
ABSOLUTE t_id = 40;
ABSOLUTE t_msg_in = 60;
ABSOLUTE t_ext_msg_in = 68;
ABSOLUTE t_ext_msg_data = 76;
ABSOLUTE t_msg_out = 84;
ABSOLUTE t_cmd = 92;
ABSOLUTE t_status = 100;
ABSOLUTE t_data = 108;

;; interrupt codes
; interrupts that need a valid DSA
ABSOLUTE int_done	= 0xff00;
ABSOLUTE int_msgin	= 0xff01;
ABSOLUTE int_extmsgin	= 0xff02;
ABSOLUTE int_extmsgdata	= 0xff03;
ABSOLUTE int_disc	= 0xff04;
ABSOLUTE int_saveoffset	= 0xff05;
; interrupts that don't have a valid DSA
ABSOLUTE int_reseltarg	= 0xff80;
ABSOLUTE int_resellun	= 0xff81;
ABSOLUTE int_reseltag	= 0xff82;
ABSOLUTE int_resfail	= 0xff83;
ABSOLUTE int_err 	= 0xffff;

; flags for scratcha0
ABSOLUTE flag_sdp 	= 0x01 ; got save data pointer
ABSOLUTE flag_data 	= 0x02 ; we're in data phase
ABSOLUTE flag_data_mask	= 0xfd ; ~flag_data

; main script symbols

ENTRY waitphase;
ENTRY send_msgout;
ENTRY msgout;
ENTRY msgin;
ENTRY handle_msgin;
ENTRY msgin_ack;
ENTRY dataout;
ENTRY datain;
ENTRY cmdout;
ENTRY status;
ENTRY disconnect;
ENTRY reselect;
ENTRY reselected;
ENTRY selected;
ENTRY script_sched;
ENTRY script_sched_slot0;
ENTRY get_extmsgdata;
ENTRY resel_targ0;
ENTRY msgin_space;
ENTRY lunsw_return;
ENTRY led_on1;
ENTRY led_on2;
ENTRY led_off;
EXTERN abs_script_sched_slot0;
EXTERN abs_targ0;
EXTERN abs_msgin;

; lun switch symbols
ENTRY lun_switch_entry;
ENTRY resel_lun0;
ENTRY restore_scntl3;
EXTERN abs_lunsw_return;

; tag switch symbols
ENTRY tag_switch_entry;
ENTRY resel_tag0;
EXTERN abs_tag0;

; command reselect script symbols
ENTRY rdsa0;
ENTRY rdsa1;
ENTRY rdsa2;
ENTRY rdsa3;
ENTRY ldsa_reload_dsa;
ENTRY ldsa_select;
ENTRY ldsa_data;

EXTERN ldsa_abs_reselected;
EXTERN ldsa_abs_reselect;
EXTERN ldsa_abs_selected;
EXTERN ldsa_abs_data;
EXTERN ldsa_abs_slot;

; main script

PROC  siop_script:

reselected:
; starting a new session, init 'local variables'
	MOVE 0 to SCRATCHA0	; flags
	MOVE 0 to SCRATCHA1	; DSA offset (for S/G save data pointer)
	MOVE SCRATCHA3 to SFBR  ; pending message ?
	JUMP REL(handle_msgin), IF not 0x20;
waitphase:
	JUMP REL(msgout), WHEN MSG_OUT;
	JUMP REL(msgin), WHEN MSG_IN;
	JUMP REL(dataout), WHEN DATA_OUT;
	JUMP REL(datain), WHEN DATA_IN;
	JUMP REL(cmdout), WHEN CMD;
	JUMP REL(status), WHEN STATUS;
	INT int_err;

reselect_fail:
	; check that host asserted SIGP, this'll clear SIGP in ISTAT
	MOVE CTEST2 & 0x40 TO SFBR;
	INT int_resfail,  IF 0x00;
; a NOP by default; patched with MOVE GPREG & 0xfe to GPREG on compile-time
; option "SIOP_SYMLED"
led_on1:
	NOP;
script_sched:
	; Clear DSA and init status
	MOVE 0xff to DSA0;
	MOVE 0xff to DSA1;
	MOVE 0xff to DSA2;
	MOVE 0xff to DSA3;
	MOVE 0 to SCRATCHA0	; flags
	MOVE 0 to SCRATCHA1	; DSA offset (for S/G save data pointer)
; the script scheduler: siop_start() we set the absolute jump addr, and then
; changes the FALSE to TRUE. The select script will change it back to false
; once the target is selected.
; The RAM could hold 370 slot entry, we limit it to 40. Should be more than
; enough.
script_sched_slot0:
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
	JUMP abs_script_sched_slot0, IF FALSE;
; Nothing to do, wait for reselect
reselect:
	; Clear DSA and init status
	MOVE 0xff to DSA0;
	MOVE 0xff to DSA1;
	MOVE 0xff to DSA2;
	MOVE 0xff to DSA3;
	MOVE 0x00 to SCRATCHA2; no tag
	MOVE 0x20 to SCRATCHA3; simple tag msg, ignored by reselected:
; a NOP by default; patched with MOVE GPREG | 0x01 to GPREG on compile-time
; option "SIOP_SYMLED"
led_off:
	NOP;
	WAIT RESELECT REL(reselect_fail)
; a NOP by default; patched with MOVE GPREG & 0xfe to GPREG on compile-time
; option "SIOP_SYMLED"
led_on2:
	NOP;
	MOVE SSID & 0x8f to SFBR
	MOVE SFBR to SCRATCHA0 ; save reselect ID
; find the right param for this target
resel_targ0:
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	JUMP abs_targ0, IF 0xff;
	INT int_reseltarg;
lunsw_return:
	MOVE 1, abs_msgin, WHEN MSG_IN;
	MOVE SFBR & 0x07 to SCRATCHA1; save LUN
	CLEAR ACK;
	RETURN, WHEN NOT MSG_IN; If no more message, jump to lun sw
	MOVE 1, abs_msgin, WHEN MSG_IN;
	CLEAR ACK;
	MOVE SFBR  to SCRATCHA3; save message
	RETURN, IF NOT 0x20; jump to lun sw if not simple tag msg
	MOVE 1, abs_msgin, WHEN MSG_IN; get tag
	CLEAR ACK;
	MOVE SFBR  to SCRATCHA2; save tag
	RETURN; jump to lun sw

handle_sdp:
	CLEAR ACK;
	MOVE SCRATCHA0 | flag_sdp TO SCRATCHA0;
	; should get a disconnect message now
msgin:
	CLEAR ATN
	MOVE FROM t_msg_in, WHEN MSG_IN;
handle_msgin:
	JUMP REL(handle_cmpl), IF 0x00        ; command complete message
	JUMP REL(handle_sdp), IF 0x02	      ; save data pointer message
	JUMP REL(handle_extin), IF 0x01	      ; extended message
	INT int_msgin, IF not 0x04;
	CALL REL(disconnect)                  ; disconnect message;
; if we didn't get sdp, no need to interrupt
	MOVE SCRATCHA0 & flag_sdp TO SFBR;
	INT int_disc, IF not 0x00;
; update offset if we did some data transfer
	MOVE SCRATCHA1 TO SFBR;
	JUMP REL(script_sched), if 0x00;
	INT int_saveoffset;

msgin_ack:
selected:
	CLEAR ACK;
	JUMP REL(waitphase);

; entry point for msgout after a msgin or status phase
send_msgout:
	SET ATN;
	CLEAR ACK;
msgout:
	MOVE FROM t_msg_out, WHEN MSG_OUT;
	CLEAR ATN;
	JUMP REL(waitphase);
cmdout:
	MOVE FROM t_cmd, WHEN CMD;
	JUMP REL(waitphase);
status:
	MOVE FROM t_status, WHEN STATUS;
	JUMP REL(waitphase);
datain:
	CALL REL(savedsa);
	MOVE SCRATCHA0 | flag_data TO SCRATCHA0;
datain_loop:
	MOVE FROM t_data, WHEN DATA_IN;
	MOVE SCRATCHA1 + 1 TO SCRATCHA1	; adjust offset
	MOVE DSA0 + 8 to DSA0;
	MOVE DSA1 + 0 to DSA1 WITH CARRY;
	MOVE DSA2 + 0 to DSA2 WITH CARRY;
	MOVE DSA3 + 0 to DSA3 WITH CARRY;
	JUMP REL(datain_loop), WHEN DATA_IN;
	CALL REL(restoredsa);
	MOVE SCRATCHA0 & flag_data_mask TO SCRATCHA0;
	JUMP REL(waitphase);

dataout:
	CALL REL(savedsa);
	MOVE SCRATCHA0 | flag_data TO SCRATCHA0;
dataout_loop:
	MOVE FROM t_data, WHEN DATA_OUT;
	MOVE SCRATCHA1 + 1 TO SCRATCHA1	; adjust offset
	MOVE DSA0 + 8 to DSA0;
	MOVE DSA1 + 0 to DSA1 WITH CARRY;
	MOVE DSA2 + 0 to DSA2 WITH CARRY;
	MOVE DSA3 + 0 to DSA3 WITH CARRY;
	JUMP REL(dataout_loop), WHEN DATA_OUT;
	CALL REL(restoredsa);
	MOVE SCRATCHA0 & flag_data_mask TO SCRATCHA0;
	JUMP REL(waitphase);

savedsa:
	MOVE DSA0 to SFBR;
	MOVE SFBR to SCRATCHB0;
	MOVE DSA1 to SFBR;
	MOVE SFBR to SCRATCHB1;
	MOVE DSA2 to SFBR;
	MOVE SFBR to SCRATCHB2;
	MOVE DSA3 to SFBR;
	MOVE SFBR to SCRATCHB3;
	RETURN;

restoredsa:
	MOVE SCRATCHB0 TO SFBR;
	MOVE SFBR TO DSA0;
	MOVE SCRATCHB1 TO SFBR;
	MOVE SFBR TO DSA1;
	MOVE SCRATCHB2 TO SFBR;
	MOVE SFBR TO DSA2;
	MOVE SCRATCHB3 TO SFBR;
	MOVE SFBR TO DSA3;
	RETURN;

disconnect:
	MOVE SCNTL2 & 0x7f TO SCNTL2;
	CLEAR ATN;
	CLEAR ACK;
	WAIT DISCONNECT;
	RETURN;

handle_cmpl:
	CALL REL(disconnect);
	INT int_done;

handle_extin:
	CLEAR ACK;
	MOVE FROM t_ext_msg_in, WHEN MSG_IN;
	INT int_extmsgin; /* let host fill in t_ext_msg_data */
get_extmsgdata:
	CLEAR ACK;
	MOVE FROM t_ext_msg_data, WHEN MSG_IN;
	INT int_extmsgdata;
msgin_space:
	NOP; space to store msgin when reselect


;; per-target switch script for LUNs
; hack: we first do a call to the target-specific code, so that a return
; in the main switch will jump to the lun switch.
PROC lun_switch:
restore_scntl3:
	MOVE 0xff TO SCNTL3;
	MOVE 0xff TO SXFER;
	JUMP abs_lunsw_return;
lun_switch_entry:
	CALL REL(restore_scntl3);
	MOVE SCRATCHA1 TO SFBR;
resel_lun0:
	INT int_resellun;

;; Per-device switch script for tag
PROC tag_switch:
tag_switch_entry:
	MOVE SCRATCHA2 TO SFBR; restore tag
resel_tag0:
	JUMP abs_tag0, IF 0x00;
	JUMP abs_tag0, IF 0x01;
	JUMP abs_tag0, IF 0x02;
	JUMP abs_tag0, IF 0x03;
	JUMP abs_tag0, IF 0x04;
	JUMP abs_tag0, IF 0x05;
	JUMP abs_tag0, IF 0x06;
	JUMP abs_tag0, IF 0x07;
	JUMP abs_tag0, IF 0x08;
	JUMP abs_tag0, IF 0x09;
	JUMP abs_tag0, IF 0x0a;
	JUMP abs_tag0, IF 0x0b;
	JUMP abs_tag0, IF 0x0c;
	JUMP abs_tag0, IF 0x0d;
	JUMP abs_tag0, IF 0x0e;
	JUMP abs_tag0, IF 0x0f;
	INT int_reseltag;

;; per-command script: select, and called after a reselect to load DSA

PROC load_dsa:
; Can't use MOVE MEMORY to load DSA, doesn't work I/O mapped
rdsa0:
	MOVE 0xf0 to DSA0;
rdsa1:
	MOVE 0xf1 to DSA1;
rdsa2:
	MOVE 0xf2 to DSA2;
rdsa3:
	MOVE 0xf3 to DSA3;
	RETURN;
ldsa_reload_dsa:
	CALL REL(rdsa0);
	JUMP ldsa_abs_reselected;
ldsa_select:
	CALL REL(rdsa0);
	SELECT ATN FROM t_id, ldsa_abs_reselect;
	MOVE MEMORY 4, ldsa_abs_data, ldsa_abs_slot;
	JUMP ldsa_abs_selected;
ldsa_data:
	NOP; contains data used by the MOVE MEMORY

PROC siop_led_on:
	MOVE GPREG & 0xfe TO GPREG;

PROC siop_led_off:
	MOVE GPREG | 0x01 TO GPREG;
