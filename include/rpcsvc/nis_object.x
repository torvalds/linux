%/*-
% * Copyright (c) 2010, Oracle America, Inc.
% *
% * Redistribution and use in source and binary forms, with or without
% * modification, are permitted provided that the following conditions are
% * met:
% *
% *     * Redistributions of source code must retain the above copyright
% *       notice, this list of conditions and the following disclaimer.
% *     * Redistributions in binary form must reproduce the above
% *       copyright notice, this list of conditions and the following
% *       disclaimer in the documentation and/or other materials
% *       provided with the distribution.
% *     * Neither the name of the "Oracle America, Inc." nor the names of its
% *       contributors may be used to endorse or promote products derived
% *       from this software without specific prior written permission.
% *
% *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
% *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
% *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
% *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
% *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
% *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
% *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
% *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
% *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
% *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
% *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
% *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
% */

/*
 *	nis_object.x
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

/* $FreeBSD$ */
/* From: %#pragma ident	"@(#)nis_object.x	1.10	94/05/03 SMI" */

#if RPC_HDR
%
%#ifndef __nis_object_h
%#define __nis_object_h
%
#endif
/* 
 * 	This file defines the format for a NIS object in RPC language.
 * It is included by the main .x file and the database access protocol
 * file. It is common because both of them need to deal with the same
 * type of object. Generating the actual code though is a bit messy because
 * the nis.x file and the nis_dba.x file will generate xdr routines to 
 * encode/decode objects when only one set is needed. Such is life when
 * one is using rpcgen.
 *
 * Note, the protocol doesn't specify any limits on such things as
 * maximum name length, number of attributes, etc. These are enforced
 * by the database backend. When you hit them you will no. Also see
 * the db_getlimits() function for fetching the limit values.
 *
 */

/* Some manifest constants, chosen to maximize flexibility without
 * plugging the wire full of data.
 */
const NIS_MAXSTRINGLEN = 255;
const NIS_MAXNAMELEN   = 1024;
const NIS_MAXATTRNAME  = 32;
const NIS_MAXATTRVAL   = 2048;
const NIS_MAXCOLUMNS   = 64;
const NIS_MAXATTR      = 16;
const NIS_MAXPATH      = 1024;
const NIS_MAXREPLICAS  = 128;
const NIS_MAXLINKS     = 16;

const NIS_PK_NONE      = 0;	/* no public key (unix/sys auth) */
const NIS_PK_DH	       = 1;	/* Public key is Diffie-Hellman type */
const NIS_PK_RSA       = 2;	/* Public key if RSA type */
const NIS_PK_KERB      = 3;	/* Use kerberos style authentication */

/*
 * The fundamental name type of NIS. The name may consist of two parts,
 * the first being the fully qualified name, and the second being an 
 * optional set of attribute/value pairs.
 */
struct nis_attr {
	string	zattr_ndx<>;	/* name of the index 		*/
	opaque	zattr_val<>;	/* Value for the attribute. 	*/
};

typedef string nis_name<>;	/* The NIS name itself. */

/* NIS object types are defined by the following enumeration. The numbers
 * they use are based on the following scheme :
 *		     0 - 1023 are reserved for Sun,
 * 		1024 - 2047 are defined to be private to a particular tree.
 *		2048 - 4095 are defined to be user defined.
 *		4096 - ...  are reserved for future use.
 */

enum zotypes {
	BOGUS_OBJ  	= 0,	/* Uninitialized object structure 	*/
	NO_OBJ   	= 1,	/* NULL object (no data)	 	*/
	DIRECTORY_OBJ 	= 2,	/* Directory object describing domain 	*/
	GROUP_OBJ  	= 3,	/* Group object (a list of names) 	*/
	TABLE_OBJ  	= 4,	/* Table object (a database schema) 	*/
	ENTRY_OBJ  	= 5,	/* Entry object (a database record) 	*/
	LINK_OBJ   	= 6, 	/* A name link.				*/
	PRIVATE_OBJ   	= 7 	/* Private object (all opaque data) 	*/
};

/*
 * The types of Name services NIS knows about. They are enumerated
 * here. The Binder code will use this type to determine if it has
 * a set of library routines that will access the indicated name service.
 */
enum nstype {
	UNKNOWN = 0,
	NIS = 1,	/* Nis Plus Service		*/
	SUNYP = 2,	/* Old NIS Service		*/
	IVY = 3,	/* Nis Plus Plus Service	*/
	DNS = 4,	/* Domain Name Service		*/
	X500 = 5,	/* ISO/CCCIT X.500 Service	*/
	DNANS = 6,	/* Digital DECNet Name Service	*/
	XCHS = 7,	/* Xerox ClearingHouse Service	*/
	CDS= 8
};

/*
 * DIRECTORY - The name service object. These objects identify other name
 * servers that are serving some portion of the name space. Each has a
 * type associated with it. The resolver library will note whether or not
 * is has the needed routines to access that type of service. 
 * The oarmask structure defines an access rights mask on a per object 
 * type basis for the name spaces. The only bits currently used are 
 * create and destroy. By enabling or disabling these access rights for
 * a specific object type for a one of the accessor entities (owner,
 * group, world) the administrator can control what types of objects 
 * may be freely added to the name space and which require the 
 * administrator's approval.
 */
struct oar_mask {
	u_long	oa_rights;	/* Access rights mask 	*/
	zotypes	oa_otype;	/* Object type 		*/
};

struct endpoint {
	string		uaddr<>;
	string		family<>;   /* Transport family (INET, OSI, etc) */
	string		proto<>;    /* Protocol (TCP, UDP, CLNP,  etc)   */
};

/*
 * Note: pkey is a netobj which is limited to 1024 bytes which limits the
 * keysize to 8192 bits. This is consider to be a reasonable limit for
 * the expected lifetime of this service.
 */
struct nis_server {
	nis_name	name; 	 	/* Principal name of the server  */
	endpoint	ep<>;  		/* Universal addr(s) for server  */
	u_long		key_type;	/* Public key type		 */
	netobj		pkey;		/* server's public key  	 */
};

struct directory_obj {
	nis_name   do_name;	 /* Name of the directory being served   */
	nstype	   do_type;	 /* one of NIS, DNS, IVY, YP, or X.500 	 */
	nis_server do_servers<>; /* <0> == Primary name server     	 */
	u_long	   do_ttl;	 /* Time To Live (for caches) 		 */
	oar_mask   do_armask<>;  /* Create/Destroy rights by object type */
};

/* 
 * ENTRY - This is one row of data from an information base. 
 * The type value is used by the client library to convert the entry to 
 * it's internal structure representation. The Table name is a back pointer
 * to the table where the entry is stored. This allows the client library 
 * to determine where to send a request if the client wishes to change this
 * entry but got to it through a LINK rather than directly.
 * If the entry is a "standalone" entry then this field is void.
 */
const EN_BINARY   = 1;	/* Indicates value is binary data 	*/
const EN_CRYPT    = 2;	/* Indicates the value is encrypted	*/
const EN_XDR      = 4;	/* Indicates the value is XDR encoded	*/
const EN_MODIFIED = 8;	/* Indicates entry is modified. 	*/
const EN_ASN1     = 64;	/* Means contents use ASN.1 encoding    */

struct entry_col {
	u_long	ec_flags;	/* Flags for this value */
	opaque	ec_value<>;	/* It's textual value	*/
};

struct entry_obj {
	string 	en_type<>;	/* Type of entry such as "passwd" */
	entry_col en_cols<>;	/* Value for the entry		  */
};

/*
 * GROUP - The group object contains a list of NIS principal names. Groups
 * are used to authorize principals. Each object has a set of access rights
 * for members of its group. Principal names in groups are in the form 
 * name.directory and recursive groups are expressed as @groupname.directory
 */
struct group_obj {
	u_long		gr_flags;	/* Flags controlling group	*/
	nis_name	gr_members<>;  	/* List of names in group 	*/
};

/*
 * LINK - This is the LINK object. It is quite similar to a symbolic link
 * in the UNIX filesystem. The attributes in the main object structure are
 * relative to the LINK data and not what it points to (like the file system)
 * "modify" privleges here indicate the right to modify what the link points
 * at and not to modify that actual object pointed to by the link.
 */
struct link_obj {
	zotypes	 li_rtype;	/* Real type of the object	*/
	nis_attr li_attrs<>;	/* Attribute/Values for tables	*/
	nis_name li_name; 	/* The object's real NIS name	*/
};

/*
 * TABLE - This is the table object. It implements a simple 
 * data base that applications and use for configuration or 
 * administration purposes. The role of the table is to group together
 * a set of related entries. Tables are the simple database component
 * of NIS. Like many databases, tables are logically divided into columns
 * and rows. The columns are labeled with indexes and each ENTRY makes
 * up a row. Rows may be addressed within the table by selecting one
 * or more indexes, and values for those indexes. Each row which has
 * a value for the given index that matches the desired value is returned.
 * Within the definition of each column there is a flags variable, this
 * variable contains flags which determine whether or not the column is
 * searchable, contains binary data, and access rights for the entry objects
 * column value. 
 */

const TA_BINARY     = 1;	/* Means table data is binary 		*/
const TA_CRYPT      = 2;	/* Means value should be encrypted 	*/
const TA_XDR        = 4;	/* Means value is XDR encoded		*/
const TA_SEARCHABLE = 8;	/* Means this column is searchable	*/
const TA_CASE       = 16;	/* Means this column is Case Sensitive	*/
const TA_MODIFIED   = 32;	/* Means this columns attrs are modified*/
const TA_ASN1       = 64;	/* Means contents use ASN.1 encoding     */

struct table_col {
	string	tc_name<64>;	/* Column Name 	 	   */
	u_long	tc_flags;	/* control flags	   */
	u_long	tc_rights;	/* Access rights mask	   */
};

struct table_obj {
	string 	  ta_type<64>;	 /* Table type such as "passwd"	*/
	int	  ta_maxcol;	 /* Total number of columns	*/
	u_char	  ta_sep;	 /* Separator character 	*/
	table_col ta_cols<>; 	 /* The number of table indexes */
	string	  ta_path<>;	 /* A search path for this table */
};

/*
 * This union joins together all of the currently known objects. 
 */
union objdata switch (zotypes zo_type) {
        case DIRECTORY_OBJ :
                struct directory_obj di_data;
        case GROUP_OBJ :
                struct group_obj gr_data;
        case TABLE_OBJ :
                struct table_obj ta_data;
        case ENTRY_OBJ:
                struct entry_obj en_data;
        case LINK_OBJ :
                struct link_obj li_data;
        case PRIVATE_OBJ :
                opaque	po_data<>;
	case NO_OBJ :
		void;
        case BOGUS_OBJ :
		void;
        default :
                void;
};

/*
 * This is the basic NIS object data type. It consists of a generic part
 * which all objects contain, and a specialized part which varies depending
 * on the type of the object. All of the specialized sections have been
 * described above. You might have wondered why they all start with an 
 * integer size, followed by the useful data. The answer is, when the 
 * server doesn't recognize the type returned it treats it as opaque data. 
 * And the definition for opaque data is {int size; char *data;}. In this
 * way, servers and utility routines that do not understand a given type
 * may still pass it around. One has to be careful in setting
 * this variable accurately, it must take into account such things as
 * XDR padding of structures etc. The best way to set it is to note one's
 * position in the XDR encoding stream, encode the structure, look at the
 * new position and calculate the size. 
 */
struct nis_oid {
	u_long	ctime;		/* Time of objects creation 	*/
	u_long	mtime;		/* Time of objects modification */
};

struct nis_object {
	nis_oid	 zo_oid;	/* object identity verifier.		*/ 
	nis_name zo_name;	/* The NIS name for this object		*/
	nis_name zo_owner;	/* NIS name of object owner.		*/
	nis_name zo_group;	/* NIS name of access group.		*/
	nis_name zo_domain;	/* The administrator for the object	*/
	u_long	 zo_access;	/* Access rights (owner, group, world)	*/
	u_long	 zo_ttl;	/* Object's time to live in seconds.	*/
	objdata	 zo_data;	/* Data structure for this type 	*/
};
#if RPC_HDR
%
%#endif /* if __nis_object_h */
%
#endif
