// SPDX-License-Identifier: GPL-2.0-only
/// Use devm_kmalloc_obj family of macros for allocations
///
// Confidence: High
// Options: --include-headers-for-types --all-includes --include-headers --keep-comments

virtual patch

@initialize:python@
@@
import sys

def alloc_array(name):
	func = "FAILED_RENAME"
	if name == "devm_kmalloc_array":
		func = "devm_kmalloc_objs"
	elif name == "devm_kvmalloc_array":
		func = "devm_kvmalloc_objs"
	elif name == "devm_kcalloc":
		func = "devm_kzalloc_objs"
	elif name == "devm_kvcalloc":
		func = "devm_kvzalloc_objs"
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
expression DEV;
identifier ALLOC =~ "^devm_kv?[mz]alloc$";
fresh identifier ALLOC_OBJ = ALLOC ## "_obj";
fresh identifier ALLOC_FLEX = ALLOC ## "_flex";
identifier ALLOC_ARRAY = {devm_kmalloc_array,devm_kvmalloc_array,devm_kcalloc,devm_kvcalloc};
fresh identifier ALLOC_OBJS = script:python(ALLOC_ARRAY) { alloc_array(ALLOC_ARRAY) };
@@

(
-	VAR = ALLOC(DEV,(sizeof(*VAR)), GFP)
+	VAR = ALLOC_OBJ(DEV,*VAR, GFP)
|
	ALLOC(DEV,(\(sizeof(STRING)\|sizeof(INTEGRAL)\|sizeof(INTEGRAL *)\)), GFP)
|
	BYTES = ALLOC(DEV,(sizeof(E)), GFP)
|
	BYTES = ALLOC(DEV,(sizeof(TYPE)), GFP)
|
	BYTES_PTRS = ALLOC(DEV,(sizeof(E)), GFP)
|
	BYTES_PTRS = ALLOC(DEV,(sizeof(TYPE)), GFP)
|
	ALLOC(DEV,(sizeof(void *)), GFP)
|
-	ALLOC(DEV,(sizeof(E)), GFP)
+	ALLOC_OBJ(DEV, E, GFP)
|
-	ALLOC(DEV,(sizeof(TYPE)), GFP)
+	ALLOC_OBJ(DEV,TYPE, GFP)
|
	ALLOC_ARRAY(DEV,COUNT, (\(sizeof(STRING)\|sizeof(INTEGRAL)\|sizeof(INTEGRAL *)\)), GFP)
|
	BYTES = ALLOC_ARRAY(DEV, COUNT, (sizeof(E)), GFP)
|
	BYTES = ALLOC_ARRAY(DEV,COUNT, (sizeof(TYPE)), GFP)
|
	BYTES_PTRS = ALLOC_ARRAY(DEV, COUNT, (sizeof(E)), GFP)
|
	BYTES_PTRS = ALLOC_ARRAY(DEV, COUNT, (sizeof(TYPE)), GFP)
|
	ALLOC_ARRAY(DEV, (\(sizeof(STRING)\|sizeof(INTEGRAL)\|sizeof(INTEGRAL *)\)), COUNT, GFP)
|
	BYTES = ALLOC_ARRAY(DEV, (sizeof(E)), COUNT, GFP)
|
	BYTES = ALLOC_ARRAY(DEV, (sizeof(TYPE)), COUNT, GFP)
|
	BYTES_PTRS = ALLOC_ARRAY(DEV, (sizeof(E)), COUNT, GFP)
|
	BYTES_PTRS = ALLOC_ARRAY(DEV, (sizeof(TYPE)), COUNT, GFP)
|
	ALLOC_ARRAY(DEV, COUNT, (sizeof(void *)), GFP)
|
	ALLOC_ARRAY(DEV, (sizeof(void *)), COUNT, GFP)
|
-	ALLOC_ARRAY(DEV, COUNT, (sizeof(E)), GFP)
+	ALLOC_OBJS(DEV, E, COUNT, GFP)
|
-	ALLOC_ARRAY(DEV, COUNT, (sizeof(TYPE)), GFP)
+	ALLOC_OBJS(DEV, TYPE, COUNT, GFP)
|
-	ALLOC_ARRAY(DEV, (sizeof(E)), COUNT, GFP)
+	ALLOC_OBJS(DEV, E, COUNT, GFP)
|
-	ALLOC_ARRAY(DEV, (sizeof(TYPE)), COUNT, GFP)
+	ALLOC_OBJS(DEV, TYPE, COUNT, GFP)
|
-	ALLOC(DEV, struct_size(VAR, FLEX, COUNT), GFP)
+	ALLOC_FLEX(DEV, *VAR, FLEX, COUNT, GFP)
|
-	ALLOC(DEV, struct_size_t(TYPE, FLEX, COUNT), GFP)
+	ALLOC_FLEX(DEV, TYPE, FLEX, COUNT, GFP)
)

@drop_gfp_kernel depends on patch && !(file in "tools") && !(file in "samples")@
identifier ALLOC = {devm_kmalloc_obj,devm_kmalloc_objs,devm_kmalloc_flex,
		    devm_kzalloc_obj,devm_kzalloc_objs,devm_kzalloc_flex,
		    devm_kvmalloc_obj,devm_kvmalloc_objs,devm_kvmalloc_flex,
		    devm_kvzalloc_obj,devm_kvzalloc_objs,devm_kvzalloc_flex};
@@

	ALLOC(...
-		 , GFP_KERNEL
	     )
