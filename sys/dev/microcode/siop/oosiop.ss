;	$OpenBSD: oosiop.ss,v 1.2 2004/10/01 04:08:45 jsg Exp $
;	$NetBSD: oosiop.ss,v 1.2 2003/04/06 09:48:42 tsutsui Exp $

;
; Copyright (c) 2001 Shuichiro URATA.  All rights reserved.
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions
; are met:
; 1. Redistributions of source code must retain the above copyright
;    notice, this list of conditions and the following disclaimer.
; 2. Redistributions in binary form must reproduce the above copyright
;    notice, this list of conditions and the following disclaimer in the
;    documentation and/or other materials provided with the distribution.
; 3. The name of the author may not be used to endorse or promote products
;    derived from this software without specific prior written permission.
;
; THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
; IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
; OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
; IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
; INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
; NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
; DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
; THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
; (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
; THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;

; NCR 53c700 script
;

ARCH 700

; interrupt codes
ABSOLUTE int_done	= 0xbeef0000
ABSOLUTE int_msgin	= 0xbeef0001
ABSOLUTE int_extmsg	= 0xbeef0002
ABSOLUTE int_resel	= 0xbeef0003
ABSOLUTE int_res_id	= 0xbeef0004
ABSOLUTE int_resfail	= 0xbeef0005
ABSOLUTE int_disc	= 0xbeef0006
ABSOLUTE int_err 	= 0xdeadbeef

; patch entries
ENTRY p_resel_msgin_move
ENTRY p_select
ENTRY p_datain_jump
ENTRY p_dataout_jump
ENTRY p_msgin_move
ENTRY p_msgout_move
ENTRY p_cmdout_move
ENTRY p_status_move
ENTRY p_extmsglen_move
ENTRY p_extmsgin_move


PROC  oosiop_script:

ENTRY wait_reselect
wait_reselect:
	WAIT RESELECT REL(reselect_fail)
	INT int_resel
reselect_fail:
	INT int_resfail

ENTRY wait_resel_identify
wait_resel_identify:
	INT int_err, WHEN NOT MSG_IN
p_resel_msgin_move:
	MOVE 0, 0, WHEN MSG_IN
	INT int_res_id

ENTRY start_select
start_select:
p_select:
	SELECT ATN 0, REL(wait_reselect)

ENTRY phasedispatch
phasedispatch:
	JUMP REL(msgin), WHEN MSG_IN
	JUMP REL(msgout), WHEN MSG_OUT
	JUMP REL(status), WHEN STATUS
	JUMP REL(cmdout), WHEN CMD
p_datain_jump:
	JUMP 0, WHEN DATA_IN
p_dataout_jump:
	JUMP 0, WHEN DATA_OUT
	INT int_err

msgin:
	CLEAR ATN
p_msgin_move:
	MOVE 0, 0, WHEN MSG_IN
	JUMP REL(complete), IF 0x00
	JUMP REL(extmsgsetup), IF 0x01
	JUMP REL(disconnect), IF 0x04
	INT int_msgin

ENTRY ack_msgin
ack_msgin:
	CLEAR ACK
	JUMP REL(phasedispatch)

ENTRY sendmsg
sendmsg:
	SET ATN
	CLEAR ACK
msgout:
p_msgout_move:
	MOVE 0, 0, WHEN MSG_OUT
	CLEAR ATN
	JUMP REL(phasedispatch)

cmdout:
	CLEAR ATN
p_cmdout_move:
	MOVE 0, 0, WHEN CMD
	JUMP REL(phasedispatch)

status:
p_status_move:
	MOVE 0, 0, WHEN STATUS
	JUMP REL(phasedispatch)

disconnect:
	CLEAR ACK
	WAIT DISCONNECT
	INT int_disc

complete:
	CLEAR ACK
	WAIT DISCONNECT
	INT int_done

; receive extended message length
extmsgsetup:
	CLEAR ACK
	INT int_err, IF NOT MSG_IN
p_extmsglen_move:
	MOVE 0, 0, WHEN MSG_IN
	INT int_extmsg

; receive extended message
ENTRY rcv_extmsg
rcv_extmsg:
	CLEAR ACK
	INT int_err, IF NOT MSG_IN
p_extmsgin_move:
	MOVE 0, 0, WHEN MSG_IN
	INT int_msgin
