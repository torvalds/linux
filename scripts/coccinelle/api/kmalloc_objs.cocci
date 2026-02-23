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

// This excludes anything that is assigning to or from integral types or
// string literals. Everything else gets the sizeof() extracted for the
// kmalloc_obj() type/var argument. sizeof(void *) is also excluded because
// it will need case-by-case double-checking to make sure the right type is
// being assigned.
@direct depends on patch && !(file in "tools") && !(file in "samples")@
typedef u8, u16, u32, u64;
typedef __u8, __u16, __u32, __u64;
typedef uint8_t, uint16_t, uint32_t, uint64_t;
typedef uchar, ushort, uint, ulong;
typedef __le16, __le32, __le64;
typedef __be16, __be32, __be64;
typedef wchar_t;
type INTEGRAL = {u8,__u8,uint8_t,char,unsigned char,uchar,wchar_t,
		 u16,__u16,uint16_t,unsigned short,ushort,
		 u32,__u32,uint32_t,unsigned int,uint,
		 u64,__u64,uint64_t,unsigned long,ulong,
		 __le16,__le32,__le64,__be16,__be32,__be64};
char [] STRING;
INTEGRAL *BYTES;
INTEGRAL **BYTES_PTRS;
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
-	VAR = ALLOC((sizeof(*VAR)), GFP)
+	VAR = ALLOC_OBJ(*VAR, GFP)
|
	ALLOC((\(sizeof(STRING)\|sizeof(INTEGRAL)\|sizeof(INTEGRAL *)\)), GFP)
|
	BYTES = ALLOC((sizeof(E)), GFP)
|
	BYTES = ALLOC((sizeof(TYPE)), GFP)
|
	BYTES_PTRS = ALLOC((sizeof(E)), GFP)
|
	BYTES_PTRS = ALLOC((sizeof(TYPE)), GFP)
|
	ALLOC((sizeof(void *)), GFP)
|
-	ALLOC((sizeof(E)), GFP)
+	ALLOC_OBJ(E, GFP)
|
-	ALLOC((sizeof(TYPE)), GFP)
+	ALLOC_OBJ(TYPE, GFP)
|
	ALLOC_ARRAY(COUNT, (\(sizeof(STRING)\|sizeof(INTEGRAL)\|sizeof(INTEGRAL *)\)), GFP)
|
	BYTES = ALLOC_ARRAY(COUNT, (sizeof(E)), GFP)
|
	BYTES = ALLOC_ARRAY(COUNT, (sizeof(TYPE)), GFP)
|
	BYTES_PTRS = ALLOC_ARRAY(COUNT, (sizeof(E)), GFP)
|
	BYTES_PTRS = ALLOC_ARRAY(COUNT, (sizeof(TYPE)), GFP)
|
	ALLOC_ARRAY((\(sizeof(STRING)\|sizeof(INTEGRAL)\|sizeof(INTEGRAL *)\)), COUNT, GFP)
|
	BYTES = ALLOC_ARRAY((sizeof(E)), COUNT, GFP)
|
	BYTES = ALLOC_ARRAY((sizeof(TYPE)), COUNT, GFP)
|
	BYTES_PTRS = ALLOC_ARRAY((sizeof(E)), COUNT, GFP)
|
	BYTES_PTRS = ALLOC_ARRAY((sizeof(TYPE)), COUNT, GFP)
|
	ALLOC_ARRAY(COUNT, (sizeof(void *)), GFP)
|
	ALLOC_ARRAY((sizeof(void *)), COUNT, GFP)
|
-	ALLOC_ARRAY(COUNT, (sizeof(E)), GFP)
+	ALLOC_OBJS(E, COUNT, GFP)
|
-	ALLOC_ARRAY(COUNT, (sizeof(TYPE)), GFP)
+	ALLOC_OBJS(TYPE, COUNT, GFP)
|
-	ALLOC_ARRAY((sizeof(E)), COUNT, GFP)
+	ALLOC_OBJS(E, COUNT, GFP)
|
-	ALLOC_ARRAY((sizeof(TYPE)), COUNT, GFP)
+	ALLOC_OBJS(TYPE, COUNT, GFP)
|
-	ALLOC(struct_size(VAR, FLEX, COUNT), GFP)
+	ALLOC_FLEX(*VAR, FLEX, COUNT, GFP)
|
-	ALLOC(struct_size_t(TYPE, FLEX, COUNT), GFP)
+	ALLOC_FLEX(TYPE, FLEX, COUNT, GFP)
)
