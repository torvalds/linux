// SPDX-License-Identifier: GPL-2.0-only
/// Use kmalloc_obj family of macros for allocations
///
// Confidence: High
// Options: --include-headers-for-types --all-includes --include-headers --keep-comments

virtual patch

@initialize:python@
@@
import sys

def alloc_array(name):
	func = "FAILED_RENAME"
	if name == "kmalloc_array":
		func = "kmalloc_objs"
	elif name == "kvmalloc_array":
		func = "kvmalloc_objs"
	elif name == "kcalloc":
		func = "kzalloc_objs"
	elif name == "kvcalloc":
		func = "kvzalloc_objs"
	else:
		print(f"Unknown transform for {name}", file=sys.stderr)
	return func

// Allocations sized by a byte-sized type (BYTE_TYPES) or a string literal,
// and allocations assigned to a pointer to a byte-sized type, are byte
// buffers and are left alone. sizeof(void *) is also excluded because it
// will need case-by-case double-checking to make sure the right type is
// being assigned.
//
// Allocations sized by a multi-byte integral type (MULTIBYTE_TYPES) are
// converted when they are assigned to a pointer to that same type.
// Otherwise they are left alone: the target may be a pointer to an array
// of that type, such as "s16 (*pairs)[2]", for which the converted
// allocation would have the wrong pointer type. For the same reason,
// arrays of pointers to integral types sized as sizeof(char *) and the
// like are left alone. Allocations of other types assigned to a pointer to
// a multi-byte integral type are left alone too.
//
// Everything else gets the sizeof() extracted for the kmalloc_obj()
// type/var argument.
//
// The first matching alternative below wins, so the exclusions must come
// before the more general conversions.
@direct depends on patch && !(file in "tools") && !(file in "samples")@
typedef u8, u16, u32, u64;
typedef __u8, __u16, __u32, __u64;
typedef uint8_t, uint16_t, uint32_t, uint64_t;
typedef s8, s16, s32, s64;
typedef __s8, __s16, __s32, __s64;
typedef int8_t, int16_t, int32_t, int64_t;
typedef uchar, ushort, uint, ulong;
typedef __le16, __le32, __le64;
typedef __be16, __be32, __be64;
typedef wchar_t;
type BYTE_TYPES = {char,signed char,unsigned char,uchar,
		   u8,__u8,uint8_t,s8,__s8,int8_t};
type MULTIBYTE_TYPES = {short,short int,signed short,signed short int,
			unsigned short,unsigned short int,ushort,
			int,signed,signed int,unsigned,unsigned int,uint,
			long,long int,signed long,signed long int,
			unsigned long,unsigned long int,ulong,
			long long,long long int,
			signed long long,signed long long int,
			unsigned long long,unsigned long long int,
			u16,__u16,uint16_t,s16,__s16,int16_t,
			u32,__u32,uint32_t,s32,__s32,int32_t,
			u64,__u64,uint64_t,s64,__s64,int64_t,
			__le16,__le32,__le64,__be16,__be32,__be64,
			wchar_t};
char [] STRING;
BYTE_TYPES *BYTES;
MULTIBYTE_TYPES *MULTIBYTES;
const MULTIBYTE_TYPES *CONST_MULTIBYTES;
MULTIBYTE_TYPES MULTIBYTE;
type TYPE;
expression VAR;
expression GFP;
expression COUNT;
expression FLEX;
expression E;
identifier ALLOC =~ "^kv?[mz]alloc$";
fresh identifier ALLOC_OBJ = ALLOC ## "_obj";
fresh identifier ALLOC_FLEX = ALLOC ## "_flex";
identifier ALLOC_ARRAY = {kmalloc_array,kvmalloc_array,kcalloc,kvcalloc};
fresh identifier ALLOC_OBJS = script:python(ALLOC_ARRAY) { alloc_array(ALLOC_ARRAY) };
@@

(
// Convert a single object sized by its target: p = kmalloc(sizeof(*p), gfp)
-	VAR = ALLOC((sizeof(*VAR)), GFP)
+	VAR = ALLOC_OBJ(*VAR, GFP)
|
// Exclude byte buffers and integral pointers: kmalloc(sizeof(u8), gfp),
//   kmalloc(sizeof("str"), gfp), kmalloc(sizeof(char *), gfp)
	ALLOC((\(sizeof(STRING)\|sizeof(BYTE_TYPES)\|
		sizeof(BYTE_TYPES *)\|sizeof(MULTIBYTE_TYPES *)\)), GFP)
|
// Exclude anything assigned to a byte pointer:
//   u8 *buf = kmalloc(sizeof(*hdr), gfp)
	BYTES = ALLOC((\(sizeof(E)\|sizeof(TYPE)\)), GFP)
|
// Convert a multi-byte type to a pointer to it:
//   u32 *p = kmalloc(sizeof(u32), gfp)
	\(MULTIBYTES\|CONST_MULTIBYTES\) =
-		ALLOC((sizeof(MULTIBYTE_TYPES)), GFP)
+		ALLOC_OBJ(MULTIBYTE_TYPES, GFP)
|
// Same by expression: u32 *p = kmalloc(sizeof(p[0]), gfp)
	\(MULTIBYTES\|CONST_MULTIBYTES\) =
-		ALLOC((sizeof(MULTIBYTE)), GFP)
+		ALLOC_OBJ(MULTIBYTE, GFP)
|
// Exclude other multi-byte sizes, e.g. to pointers to arrays: s16 (*p)[2] = ...
	ALLOC((\(sizeof(MULTIBYTE_TYPES)\|sizeof(MULTIBYTE)\)), GFP)
|
// Exclude anything else assigned to a multi-byte pointer:
//   u32 *p = kmalloc(sizeof(*hdr), gfp)
	\(MULTIBYTES\|CONST_MULTIBYTES\) = ALLOC((\(sizeof(E)\|sizeof(TYPE)\)), GFP)
|
// Exclude void pointers, to be checked by hand: kmalloc(sizeof(void *), gfp)
	ALLOC((sizeof(void *)), GFP)
|
// Convert any other expression: p = kmalloc(sizeof(s->item), gfp)
-	ALLOC((sizeof(E)), GFP)
+	ALLOC_OBJ(E, GFP)
|
// Convert any other type: p = kmalloc(sizeof(struct item), gfp)
-	ALLOC((sizeof(TYPE)), GFP)
+	ALLOC_OBJ(TYPE, GFP)
|
// The same, for arrays allocated as (count, size):
// Exclude byte buffers and integral pointers: kcalloc(n, sizeof(u8), gfp),
//   kcalloc(n, sizeof(char *), gfp)
	ALLOC_ARRAY(COUNT, (\(sizeof(STRING)\|sizeof(BYTE_TYPES)\|
			    sizeof(BYTE_TYPES *)\|sizeof(MULTIBYTE_TYPES *)\)), GFP)
|
// Exclude arrays assigned to a byte pointer:
//   u8 *buf = kcalloc(n, sizeof(*hdr), gfp)
	BYTES = ALLOC_ARRAY(COUNT, (\(sizeof(E)\|sizeof(TYPE)\)), GFP)
|
// Convert a multi-byte array to a pointer to it:
//   u32 *p = kcalloc(n, sizeof(u32), gfp)
	\(MULTIBYTES\|CONST_MULTIBYTES\) =
-		ALLOC_ARRAY(COUNT, (sizeof(MULTIBYTE_TYPES)), GFP)
+		ALLOC_OBJS(MULTIBYTE_TYPES, COUNT, GFP)
|
// Same by expression: u32 *p = kcalloc(n, sizeof(*p), gfp)
	\(MULTIBYTES\|CONST_MULTIBYTES\) =
-		ALLOC_ARRAY(COUNT, (sizeof(MULTIBYTE)), GFP)
+		ALLOC_OBJS(MULTIBYTE, COUNT, GFP)
|
// Exclude other multi-byte arrays:
//   s16 (*pairs)[2] = kcalloc(n, sizeof(s16), gfp)
	ALLOC_ARRAY(COUNT, (\(sizeof(MULTIBYTE_TYPES)\|sizeof(MULTIBYTE)\)), GFP)
|
// Exclude other arrays assigned to a multi-byte pointer:
//   u32 *p = kcalloc(n, sizeof(*hdr), gfp)
	\(MULTIBYTES\|CONST_MULTIBYTES\) = ALLOC_ARRAY(COUNT, (\(sizeof(E)\|sizeof(TYPE)\)), GFP)
|
// Exclude arrays of void pointers: kcalloc(n, sizeof(void *), gfp)
	ALLOC_ARRAY(COUNT, (sizeof(void *)), GFP)
|
// Convert any other expression: p = kcalloc(n, sizeof(*p), gfp)
-	ALLOC_ARRAY(COUNT, (sizeof(E)), GFP)
+	ALLOC_OBJS(E, COUNT, GFP)
|
// Convert any other type: p = kcalloc(n, sizeof(struct item), gfp)
-	ALLOC_ARRAY(COUNT, (sizeof(TYPE)), GFP)
+	ALLOC_OBJS(TYPE, COUNT, GFP)
|
// The same, for arrays allocated as (size, count):
// Exclude byte buffers and integral pointers: kcalloc(sizeof(u8), n, gfp),
//   kcalloc(sizeof(char *), n, gfp)
	ALLOC_ARRAY((\(sizeof(STRING)\|sizeof(BYTE_TYPES)\|
		      sizeof(BYTE_TYPES *)\|sizeof(MULTIBYTE_TYPES *)\)), COUNT, GFP)
|
// Exclude arrays assigned to a byte pointer:
//   u8 *buf = kcalloc(sizeof(*hdr), n, gfp)
	BYTES = ALLOC_ARRAY((\(sizeof(E)\|sizeof(TYPE)\)), COUNT, GFP)
|
// Convert a multi-byte array to a pointer to it:
//   u32 *p = kcalloc(sizeof(u32), n, gfp)
	\(MULTIBYTES\|CONST_MULTIBYTES\) =
-		ALLOC_ARRAY((sizeof(MULTIBYTE_TYPES)), COUNT, GFP)
+		ALLOC_OBJS(MULTIBYTE_TYPES, COUNT, GFP)
|
// Same by expression: u32 *p = kcalloc(sizeof(*p), n, gfp)
	\(MULTIBYTES\|CONST_MULTIBYTES\) =
-		ALLOC_ARRAY((sizeof(MULTIBYTE)), COUNT, GFP)
+		ALLOC_OBJS(MULTIBYTE, COUNT, GFP)
|
// Exclude other multi-byte arrays:
//   s16 (*pairs)[2] = kcalloc(sizeof(s16), n, gfp)
	ALLOC_ARRAY((\(sizeof(MULTIBYTE_TYPES)\|sizeof(MULTIBYTE)\)), COUNT, GFP)
|
// Exclude other arrays assigned to a multi-byte pointer:
//   u32 *p = kcalloc(sizeof(*hdr), n, gfp)
	\(MULTIBYTES\|CONST_MULTIBYTES\) = ALLOC_ARRAY((\(sizeof(E)\|sizeof(TYPE)\)), COUNT, GFP)
|
// Exclude arrays of void pointers: kcalloc(sizeof(void *), n, gfp)
	ALLOC_ARRAY((sizeof(void *)), COUNT, GFP)
|
// Convert any other expression: p = kcalloc(sizeof(*p), n, gfp)
-	ALLOC_ARRAY((sizeof(E)), COUNT, GFP)
+	ALLOC_OBJS(E, COUNT, GFP)
|
// Convert any other type: p = kcalloc(sizeof(struct item), n, gfp)
-	ALLOC_ARRAY((sizeof(TYPE)), COUNT, GFP)
+	ALLOC_OBJS(TYPE, COUNT, GFP)
|
// Convert flexible array structures: p = kmalloc(struct_size(p, data, n), gfp)
-	ALLOC(struct_size(VAR, FLEX, COUNT), GFP)
+	ALLOC_FLEX(*VAR, FLEX, COUNT, GFP)
|
// Same by type: kmalloc(struct_size_t(struct item, data, n), gfp)
-	ALLOC(struct_size_t(TYPE, FLEX, COUNT), GFP)
+	ALLOC_FLEX(TYPE, FLEX, COUNT, GFP)
)

@drop_gfp_kernel depends on patch && !(file in "tools") && !(file in "samples")@
identifier ALLOC = {kmalloc_obj,kmalloc_objs,kmalloc_flex,
		    kzalloc_obj,kzalloc_objs,kzalloc_flex,
		    kvmalloc_obj,kvmalloc_objs,kvmalloc_flex,
		    kvzalloc_obj,kvzalloc_objs,kvzalloc_flex};
@@

	ALLOC(...
-		 , GFP_KERNEL
	     )
