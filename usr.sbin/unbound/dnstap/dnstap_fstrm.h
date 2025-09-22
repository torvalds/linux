/*
 * dnstap/dnstap_fstrm.h - Frame Streams protocol for dnstap
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * \file
 *
 * Definitions for the Frame Streams data transport protocol for
 * dnstap message logs.
 */

#ifndef DNSTAP_FSTRM_H
#define DNSTAP_FSTRM_H

/* Frame Streams data transfer protocol encode for DNSTAP messages.
 * The protocol looks to be specified in the libfstrm library.
 *
 * Quick writeup for DNSTAP usage, from reading fstrm/control.h eloquent
 * comments and fstrm/control.c for some bytesize details (the content type
 * length).
 *
 * The Frame Streams can be unidirectional or bi-directional.
 * bi-directional streams use control frame types READY, ACCEPT and FINISH.
 * uni-directional streams use control frame types START and STOP.
 * unknown control frame types should be ignored by the receiver, they
 * do not change the data frame encoding.
 *
 * bi-directional control frames implement a simple handshake protocol
 * between sender and receiver.
 *
 * The uni-directional control frames have one start and one stop frame,
 * before and after the data.  The start frame can have a content type.
 * The start and stop frames are not optional.
 *
 * data frames are preceded by 4byte length, bigendian.
 * zero length data frames are not possible, they are an escape that
 * signals the presence of a control frame.
 *
 * a control frame consists of 0 value in 4byte bigendian, this is really
 * the data frame length, with 0 the escape sequence that indicates one
 * control frame follows.
 * Then, 4byte bigendian, length of the control frame message.
 * Then, the control frame payload (of that length). with in it:
 *   4byte bigendian, control type (eg. START, STOP, READY, ACCEPT, FINISH).
 *   perhaps nothing more (STOP, FINISH), but for other types maybe
 *   control fields
 *      4byte bigendian, the control-field-type, currently only content-type.
 *      4byte bigendian, length of the string for this option.
 *      .. bytes of that string.
 *
 * The START type can have only one field.  Field max len 256.
 * control frame max frame length 512 (excludes the 0-escape and control
 * frame length bytes).
 *
 * the bidirectional type of transmission is like this:
 * client sends READY (with content type included),
 * client waits for ACCEPT (with content type included),
 * client sends START (with matched content type from ACCEPT)
 * .. data frames
 * client sends STOP.
 * client waits for FINISH frame.
 *
 */

/** max length of Frame Streams content type field string */
#define FSTRM_CONTENT_TYPE_LENGTH_MAX 256
/** control frame value to denote the control frame ACCEPT */
#define FSTRM_CONTROL_FRAME_ACCEPT 0x01
/** control frame value to denote the control frame START */
#define FSTRM_CONTROL_FRAME_START 0x02
/** control frame value to denote the control frame STOP */
#define FSTRM_CONTROL_FRAME_STOP 0x03
/** control frame value to denote the control frame READY */
#define FSTRM_CONTROL_FRAME_READY 0x04
/** control frame value to denote the control frame FINISH */
#define FSTRM_CONTROL_FRAME_FINISH 0x05
/** the constant that denotes the control field type that is the
 * string for the content type of the stream. */
#define FSTRM_CONTROL_FIELD_TYPE_CONTENT_TYPE 0x01
/** the content type for DNSTAP frame streams */
#define DNSTAP_CONTENT_TYPE             "protobuf:dnstap.Dnstap"

/**
 * This creates an FSTRM control frame of type START.
 * @param contenttype: a zero delimited string with the content type.
 * 	eg. use the constant DNSTAP_CONTENT_TYPE, which is defined as
 * 	"protobuf:dnstap.Dnstap", for a dnstap frame stream.
 * @param len: if a buffer is returned this is the length of that buffer.
 * @return NULL on malloc failure.  Returns a malloced buffer with the
 * protocol message.  The buffer starts with the 4 bytes of 0 that indicate
 * a control frame.  The buffer should be sent without preceding it with
 * the 'len' variable (like data frames are), but straight the content of the
 * buffer, because the lengths are included in the buffer.  This is so that
 * the zero control indicator can be included before the control frame length.
 */
void* fstrm_create_control_frame_start(char* contenttype, size_t* len);

/**
 * This creates an FSTRM control frame of type READY.
 * @param contenttype: a zero delimited string with the content type.
 * 	eg. use the constant DNSTAP_CONTENT_TYPE, which is defined as
 * 	"protobuf:dnstap.Dnstap", for a dnstap frame stream.
 * @param len: if a buffer is returned this is the length of that buffer.
 * @return NULL on malloc failure.  Returns a malloced buffer with the
 * protocol message.  The buffer starts with the 4 bytes of 0 that indicate
 * a control frame.  The buffer should be sent without preceding it with
 * the 'len' variable (like data frames are), but straight the content of the
 * buffer, because the lengths are included in the buffer.  This is so that
 * the zero control indicator can be included before the control frame length.
 */
void* fstrm_create_control_frame_ready(char* contenttype, size_t* len);

/**
 * This creates an FSTRM control frame of type STOP.
 * @param len: if a buffer is returned this is the length of that buffer.
 * @return NULL on malloc failure.  Returns a malloced buffer with the
 * protocol message.  The buffer starts with the 4 bytes of 0 that indicate
 * a control frame.  The buffer should be sent without preceding it with
 * the 'len' variable (like data frames are), but straight the content of the
 * buffer, because the lengths are included in the buffer.  This is so that
 * the zero control indicator can be included before the control frame length.
 */
void* fstrm_create_control_frame_stop(size_t* len);

/**
 * This creates an FSTRM control frame of type ACCEPT.
 * @param contenttype: a zero delimited string with the content type.
 * 	for dnstap streams use DNSTAP_CONTENT_TYPE.
 * @param len: if a buffer is returned this is the length of that buffer.
 * @return NULL on malloc failure.  Returns a malloced buffer with the
 * protocol message.  The buffer starts with the 4 bytes of 0 that indicate
 * a control frame.  The buffer should be sent without preceding it with
 * the 'len' variable (like data frames are), but straight the content of the
 * buffer, because the lengths are included in the buffer.  This is so that
 * the zero control indicator can be included before the control frame length.
 */
void* fstrm_create_control_frame_accept(char* contenttype, size_t* len);

/**
 * This creates an FSTRM control frame of type FINISH.
 * @param len: if a buffer is returned this is the length of that buffer.
 * @return NULL on malloc failure.  Returns a malloced buffer with the
 * protocol message.  The buffer starts with the 4 bytes of 0 that indicate
 * a control frame.  The buffer should be sent without preceding it with
 * the 'len' variable (like data frames are), but straight the content of the
 * buffer, because the lengths are included in the buffer.  This is so that
 * the zero control indicator can be included before the control frame length.
 */
void* fstrm_create_control_frame_finish(size_t* len);

/**
 * Return string that describes a control packet.  For debug, logs.
 * Like 'start content-type(protobuf:dnstap.Dnstap)' or 'stop'.
 * @param pkt: the packet data, that is the data after the 4 zero start
 * bytes and 4 length bytes.
 * @param len: the length of the control packet data, in pkt.  This is the
 * ntohl of the 4 bytes length preceding the data.
 * @return zero delimited string, malloced.  Or NULL on malloc failure.
 */
char* fstrm_describe_control(void* pkt, size_t len);

#endif /* DNSTAP_FSTRM_H */
