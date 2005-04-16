/* $Id: capiutil.h,v 1.5.6.2 2001/09/23 22:24:33 kai Exp $
 *
 * CAPI 2.0 defines & types
 *
 * From CAPI 2.0 Development Kit AVM 1995 (msg.c)
 * Rewritten for Linux 1996 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __CAPIUTIL_H__
#define __CAPIUTIL_H__

#include <asm/types.h>

#define CAPIMSG_BASELEN		8
#define CAPIMSG_U8(m, off)	(m[off])
#define CAPIMSG_U16(m, off)	(m[off]|(m[(off)+1]<<8))
#define CAPIMSG_U32(m, off)	(m[off]|(m[(off)+1]<<8)|(m[(off)+2]<<16)|(m[(off)+3]<<24))
#define	CAPIMSG_LEN(m)		CAPIMSG_U16(m,0)
#define	CAPIMSG_APPID(m)	CAPIMSG_U16(m,2)
#define	CAPIMSG_COMMAND(m)	CAPIMSG_U8(m,4)
#define	CAPIMSG_SUBCOMMAND(m)	CAPIMSG_U8(m,5)
#define CAPIMSG_CMD(m)		(((m[4])<<8)|(m[5]))
#define	CAPIMSG_MSGID(m)	CAPIMSG_U16(m,6)
#define CAPIMSG_CONTROLLER(m)	(m[8] & 0x7f)
#define CAPIMSG_CONTROL(m)	CAPIMSG_U32(m, 8)
#define CAPIMSG_NCCI(m)		CAPIMSG_CONTROL(m)
#define CAPIMSG_DATALEN(m)	CAPIMSG_U16(m,16) /* DATA_B3_REQ */

static inline void capimsg_setu8(void *m, int off, __u8 val)
{
	((__u8 *)m)[off] = val;
}

static inline void capimsg_setu16(void *m, int off, __u16 val)
{
	((__u8 *)m)[off] = val & 0xff;
	((__u8 *)m)[off+1] = (val >> 8) & 0xff;
}

static inline void capimsg_setu32(void *m, int off, __u32 val)
{
	((__u8 *)m)[off] = val & 0xff;
	((__u8 *)m)[off+1] = (val >> 8) & 0xff;
	((__u8 *)m)[off+2] = (val >> 16) & 0xff;
	((__u8 *)m)[off+3] = (val >> 24) & 0xff;
}

#define	CAPIMSG_SETLEN(m, len)		capimsg_setu16(m, 0, len)
#define	CAPIMSG_SETAPPID(m, applid)	capimsg_setu16(m, 2, applid)
#define	CAPIMSG_SETCOMMAND(m,cmd)	capimsg_setu8(m, 4, cmd)
#define	CAPIMSG_SETSUBCOMMAND(m, cmd)	capimsg_setu8(m, 5, cmd)
#define	CAPIMSG_SETMSGID(m, msgid)	capimsg_setu16(m, 6, msgid)
#define	CAPIMSG_SETCONTROL(m, contr)	capimsg_setu32(m, 8, contr)
#define	CAPIMSG_SETDATALEN(m, len)	capimsg_setu16(m, 16, len)

/*----- basic-type definitions -----*/

typedef __u8 *_cstruct;

typedef enum {
	CAPI_COMPOSE,
	CAPI_DEFAULT
} _cmstruct;

/*
   The _cmsg structure contains all possible CAPI 2.0 parameter.
   All parameters are stored here first. The function CAPI_CMSG_2_MESSAGE
   assembles the parameter and builds CAPI2.0 conform messages.
   CAPI_MESSAGE_2_CMSG disassembles CAPI 2.0 messages and stores the
   parameter in the _cmsg structure
 */

typedef struct {
	/* Header */
	__u16 ApplId;
	__u8 Command;
	__u8 Subcommand;
	__u16 Messagenumber;

	/* Parameter */
	union {
		__u32 adrController;
		__u32 adrPLCI;
		__u32 adrNCCI;
	} adr;

	_cmstruct AdditionalInfo;
	_cstruct B1configuration;
	__u16 B1protocol;
	_cstruct B2configuration;
	__u16 B2protocol;
	_cstruct B3configuration;
	__u16 B3protocol;
	_cstruct BC;
	_cstruct BChannelinformation;
	_cmstruct BProtocol;
	_cstruct CalledPartyNumber;
	_cstruct CalledPartySubaddress;
	_cstruct CallingPartyNumber;
	_cstruct CallingPartySubaddress;
	__u32 CIPmask;
	__u32 CIPmask2;
	__u16 CIPValue;
	__u32 Class;
	_cstruct ConnectedNumber;
	_cstruct ConnectedSubaddress;
	__u32 Data;
	__u16 DataHandle;
	__u16 DataLength;
	_cstruct FacilityConfirmationParameter;
	_cstruct Facilitydataarray;
	_cstruct FacilityIndicationParameter;
	_cstruct FacilityRequestParameter;
	__u16 FacilitySelector;
	__u16 Flags;
	__u32 Function;
	_cstruct HLC;
	__u16 Info;
	_cstruct InfoElement;
	__u32 InfoMask;
	__u16 InfoNumber;
	_cstruct Keypadfacility;
	_cstruct LLC;
	_cstruct ManuData;
	__u32 ManuID;
	_cstruct NCPI;
	__u16 Reason;
	__u16 Reason_B3;
	__u16 Reject;
	_cstruct Useruserdata;

	/* intern */
	unsigned l, p;
	unsigned char *par;
	__u8 *m;

	/* buffer to construct message */
	__u8 buf[180];

} _cmsg;

/*
 * capi_cmsg2message() assembles the parameter from _cmsg to a CAPI 2.0
 * conform message
 */
unsigned capi_cmsg2message(_cmsg * cmsg, __u8 * msg);

/*
 *  capi_message2cmsg disassembles a CAPI message an writes the parameter
 *  into _cmsg for easy access
 */
unsigned capi_message2cmsg(_cmsg * cmsg, __u8 * msg);

/*
 * capi_cmsg_header() fills the _cmsg structure with default values, so only
 * parameter with non default values must be changed before sending the
 * message.
 */
unsigned capi_cmsg_header(_cmsg * cmsg, __u16 _ApplId,
			  __u8 _Command, __u8 _Subcommand,
			  __u16 _Messagenumber, __u32 _Controller);

/*
 * capi_info2str generated a readable string for Capi2.0 reasons.
 */
char *capi_info2str(__u16 reason);

/*-----------------------------------------------------------------------*/

/*
 * Debugging / Tracing functions
 */
char *capi_cmd2str(__u8 cmd, __u8 subcmd);
char *capi_cmsg2str(_cmsg * cmsg);
char *capi_message2str(__u8 * msg);

/*-----------------------------------------------------------------------*/

static inline void capi_cmsg_answer(_cmsg * cmsg)
{
	cmsg->Subcommand |= 0x01;
}

/*-----------------------------------------------------------------------*/

static inline void capi_fill_CONNECT_B3_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					    __u32 adr,
					    _cstruct NCPI)
{
	capi_cmsg_header(cmsg, ApplId, 0x82, 0x80, Messagenumber, adr);
	cmsg->NCPI = NCPI;
}

static inline void capi_fill_FACILITY_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					  __u32 adr,
					  __u16 FacilitySelector,
				       _cstruct FacilityRequestParameter)
{
	capi_cmsg_header(cmsg, ApplId, 0x80, 0x80, Messagenumber, adr);
	cmsg->FacilitySelector = FacilitySelector;
	cmsg->FacilityRequestParameter = FacilityRequestParameter;
}

static inline void capi_fill_INFO_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
				      __u32 adr,
				      _cstruct CalledPartyNumber,
				      _cstruct BChannelinformation,
				      _cstruct Keypadfacility,
				      _cstruct Useruserdata,
				      _cstruct Facilitydataarray)
{
	capi_cmsg_header(cmsg, ApplId, 0x08, 0x80, Messagenumber, adr);
	cmsg->CalledPartyNumber = CalledPartyNumber;
	cmsg->BChannelinformation = BChannelinformation;
	cmsg->Keypadfacility = Keypadfacility;
	cmsg->Useruserdata = Useruserdata;
	cmsg->Facilitydataarray = Facilitydataarray;
}

static inline void capi_fill_LISTEN_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					__u32 adr,
					__u32 InfoMask,
					__u32 CIPmask,
					__u32 CIPmask2,
					_cstruct CallingPartyNumber,
					_cstruct CallingPartySubaddress)
{
	capi_cmsg_header(cmsg, ApplId, 0x05, 0x80, Messagenumber, adr);
	cmsg->InfoMask = InfoMask;
	cmsg->CIPmask = CIPmask;
	cmsg->CIPmask2 = CIPmask2;
	cmsg->CallingPartyNumber = CallingPartyNumber;
	cmsg->CallingPartySubaddress = CallingPartySubaddress;
}

static inline void capi_fill_ALERT_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
				       __u32 adr,
				       _cstruct BChannelinformation,
				       _cstruct Keypadfacility,
				       _cstruct Useruserdata,
				       _cstruct Facilitydataarray)
{
	capi_cmsg_header(cmsg, ApplId, 0x01, 0x80, Messagenumber, adr);
	cmsg->BChannelinformation = BChannelinformation;
	cmsg->Keypadfacility = Keypadfacility;
	cmsg->Useruserdata = Useruserdata;
	cmsg->Facilitydataarray = Facilitydataarray;
}

static inline void capi_fill_CONNECT_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					 __u32 adr,
					 __u16 CIPValue,
					 _cstruct CalledPartyNumber,
					 _cstruct CallingPartyNumber,
					 _cstruct CalledPartySubaddress,
					 _cstruct CallingPartySubaddress,
					 __u16 B1protocol,
					 __u16 B2protocol,
					 __u16 B3protocol,
					 _cstruct B1configuration,
					 _cstruct B2configuration,
					 _cstruct B3configuration,
					 _cstruct BC,
					 _cstruct LLC,
					 _cstruct HLC,
					 _cstruct BChannelinformation,
					 _cstruct Keypadfacility,
					 _cstruct Useruserdata,
					 _cstruct Facilitydataarray)
{

	capi_cmsg_header(cmsg, ApplId, 0x02, 0x80, Messagenumber, adr);
	cmsg->CIPValue = CIPValue;
	cmsg->CalledPartyNumber = CalledPartyNumber;
	cmsg->CallingPartyNumber = CallingPartyNumber;
	cmsg->CalledPartySubaddress = CalledPartySubaddress;
	cmsg->CallingPartySubaddress = CallingPartySubaddress;
	cmsg->B1protocol = B1protocol;
	cmsg->B2protocol = B2protocol;
	cmsg->B3protocol = B3protocol;
	cmsg->B1configuration = B1configuration;
	cmsg->B2configuration = B2configuration;
	cmsg->B3configuration = B3configuration;
	cmsg->BC = BC;
	cmsg->LLC = LLC;
	cmsg->HLC = HLC;
	cmsg->BChannelinformation = BChannelinformation;
	cmsg->Keypadfacility = Keypadfacility;
	cmsg->Useruserdata = Useruserdata;
	cmsg->Facilitydataarray = Facilitydataarray;
}

static inline void capi_fill_DATA_B3_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					 __u32 adr,
					 __u32 Data,
					 __u16 DataLength,
					 __u16 DataHandle,
					 __u16 Flags)
{

	capi_cmsg_header(cmsg, ApplId, 0x86, 0x80, Messagenumber, adr);
	cmsg->Data = Data;
	cmsg->DataLength = DataLength;
	cmsg->DataHandle = DataHandle;
	cmsg->Flags = Flags;
}

static inline void capi_fill_DISCONNECT_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					    __u32 adr,
					    _cstruct BChannelinformation,
					    _cstruct Keypadfacility,
					    _cstruct Useruserdata,
					    _cstruct Facilitydataarray)
{

	capi_cmsg_header(cmsg, ApplId, 0x04, 0x80, Messagenumber, adr);
	cmsg->BChannelinformation = BChannelinformation;
	cmsg->Keypadfacility = Keypadfacility;
	cmsg->Useruserdata = Useruserdata;
	cmsg->Facilitydataarray = Facilitydataarray;
}

static inline void capi_fill_DISCONNECT_B3_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					       __u32 adr,
					       _cstruct NCPI)
{

	capi_cmsg_header(cmsg, ApplId, 0x84, 0x80, Messagenumber, adr);
	cmsg->NCPI = NCPI;
}

static inline void capi_fill_MANUFACTURER_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					      __u32 adr,
					      __u32 ManuID,
					      __u32 Class,
					      __u32 Function,
					      _cstruct ManuData)
{

	capi_cmsg_header(cmsg, ApplId, 0xff, 0x80, Messagenumber, adr);
	cmsg->ManuID = ManuID;
	cmsg->Class = Class;
	cmsg->Function = Function;
	cmsg->ManuData = ManuData;
}

static inline void capi_fill_RESET_B3_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					  __u32 adr,
					  _cstruct NCPI)
{

	capi_cmsg_header(cmsg, ApplId, 0x87, 0x80, Messagenumber, adr);
	cmsg->NCPI = NCPI;
}

static inline void capi_fill_SELECT_B_PROTOCOL_REQ(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
						   __u32 adr,
						   __u16 B1protocol,
						   __u16 B2protocol,
						   __u16 B3protocol,
						_cstruct B1configuration,
						_cstruct B2configuration,
						_cstruct B3configuration)
{

	capi_cmsg_header(cmsg, ApplId, 0x41, 0x80, Messagenumber, adr);
	cmsg->B1protocol = B1protocol;
	cmsg->B2protocol = B2protocol;
	cmsg->B3protocol = B3protocol;
	cmsg->B1configuration = B1configuration;
	cmsg->B2configuration = B2configuration;
	cmsg->B3configuration = B3configuration;
}

static inline void capi_fill_CONNECT_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					  __u32 adr,
					  __u16 Reject,
					  __u16 B1protocol,
					  __u16 B2protocol,
					  __u16 B3protocol,
					  _cstruct B1configuration,
					  _cstruct B2configuration,
					  _cstruct B3configuration,
					  _cstruct ConnectedNumber,
					  _cstruct ConnectedSubaddress,
					  _cstruct LLC,
					  _cstruct BChannelinformation,
					  _cstruct Keypadfacility,
					  _cstruct Useruserdata,
					  _cstruct Facilitydataarray)
{
	capi_cmsg_header(cmsg, ApplId, 0x02, 0x83, Messagenumber, adr);
	cmsg->Reject = Reject;
	cmsg->B1protocol = B1protocol;
	cmsg->B2protocol = B2protocol;
	cmsg->B3protocol = B3protocol;
	cmsg->B1configuration = B1configuration;
	cmsg->B2configuration = B2configuration;
	cmsg->B3configuration = B3configuration;
	cmsg->ConnectedNumber = ConnectedNumber;
	cmsg->ConnectedSubaddress = ConnectedSubaddress;
	cmsg->LLC = LLC;
	cmsg->BChannelinformation = BChannelinformation;
	cmsg->Keypadfacility = Keypadfacility;
	cmsg->Useruserdata = Useruserdata;
	cmsg->Facilitydataarray = Facilitydataarray;
}

static inline void capi_fill_CONNECT_ACTIVE_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
						 __u32 adr)
{

	capi_cmsg_header(cmsg, ApplId, 0x03, 0x83, Messagenumber, adr);
}

static inline void capi_fill_CONNECT_B3_ACTIVE_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
						    __u32 adr)
{

	capi_cmsg_header(cmsg, ApplId, 0x83, 0x83, Messagenumber, adr);
}

static inline void capi_fill_CONNECT_B3_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					     __u32 adr,
					     __u16 Reject,
					     _cstruct NCPI)
{
	capi_cmsg_header(cmsg, ApplId, 0x82, 0x83, Messagenumber, adr);
	cmsg->Reject = Reject;
	cmsg->NCPI = NCPI;
}

static inline void capi_fill_CONNECT_B3_T90_ACTIVE_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
							__u32 adr)
{

	capi_cmsg_header(cmsg, ApplId, 0x88, 0x83, Messagenumber, adr);
}

static inline void capi_fill_DATA_B3_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					  __u32 adr,
					  __u16 DataHandle)
{

	capi_cmsg_header(cmsg, ApplId, 0x86, 0x83, Messagenumber, adr);
	cmsg->DataHandle = DataHandle;
}

static inline void capi_fill_DISCONNECT_B3_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
						__u32 adr)
{

	capi_cmsg_header(cmsg, ApplId, 0x84, 0x83, Messagenumber, adr);
}

static inline void capi_fill_DISCONNECT_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					     __u32 adr)
{

	capi_cmsg_header(cmsg, ApplId, 0x04, 0x83, Messagenumber, adr);
}

static inline void capi_fill_FACILITY_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					   __u32 adr,
					   __u16 FacilitySelector)
{

	capi_cmsg_header(cmsg, ApplId, 0x80, 0x83, Messagenumber, adr);
	cmsg->FacilitySelector = FacilitySelector;
}

static inline void capi_fill_INFO_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
				       __u32 adr)
{

	capi_cmsg_header(cmsg, ApplId, 0x08, 0x83, Messagenumber, adr);
}

static inline void capi_fill_MANUFACTURER_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					       __u32 adr,
					       __u32 ManuID,
					       __u32 Class,
					       __u32 Function,
					       _cstruct ManuData)
{

	capi_cmsg_header(cmsg, ApplId, 0xff, 0x83, Messagenumber, adr);
	cmsg->ManuID = ManuID;
	cmsg->Class = Class;
	cmsg->Function = Function;
	cmsg->ManuData = ManuData;
}

static inline void capi_fill_RESET_B3_RESP(_cmsg * cmsg, __u16 ApplId, __u16 Messagenumber,
					   __u32 adr)
{

	capi_cmsg_header(cmsg, ApplId, 0x87, 0x83, Messagenumber, adr);
}

#endif				/* __CAPIUTIL_H__ */
