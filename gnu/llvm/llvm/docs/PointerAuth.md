# Pointer Authentication

## Introduction

Pointer Authentication is a mechanism by which certain pointers are signed.
When a pointer gets signed, a cryptographic hash of its value and other values
(pepper and salt) is stored in unused bits of that pointer.

Before the pointer is used, it needs to be authenticated, i.e., have its
signature checked.  This prevents pointer values of unknown origin from being
used to replace the signed pointer value.

For more details, see the clang documentation page for
[Pointer Authentication](https://clang.llvm.org/docs/PointerAuthentication.html).

At the IR level, it is represented using:

* a [set of intrinsics](#intrinsics) (to sign/authenticate pointers)
* a [signed pointer constant](#constant) (to sign globals)
* a [call operand bundle](#operand-bundle) (to authenticate called pointers)
* a [set of function attributes](#function-attributes) (to describe what
  pointers are signed and how, to control implicit codegen in the backend, as
  well as preserve invariants in the mid-level optimizer)

The current implementation leverages the
[Armv8.3-A PAuth/Pointer Authentication Code](#armv8-3-a-pauth-pointer-authentication-code)
instructions in the [AArch64 backend](#aarch64-support).
This support is used to implement the Darwin arm64e ABI, as well as the
[PAuth ABI Extension to ELF](https://github.com/ARM-software/abi-aa/blob/main/pauthabielf64/pauthabielf64.rst).


## LLVM IR Representation

### Intrinsics

These intrinsics are provided by LLVM to expose pointer authentication
operations.


#### '`llvm.ptrauth.sign`'

##### Syntax:

```llvm
declare i64 @llvm.ptrauth.sign(i64 <value>, i32 <key>, i64 <discriminator>)
```

##### Overview:

The '`llvm.ptrauth.sign`' intrinsic signs a raw pointer.


##### Arguments:

The `value` argument is the raw pointer value to be signed.
The `key` argument is the identifier of the key to be used to generate the
signed value.
The `discriminator` argument is the additional diversity data to be used as a
discriminator (an integer, an address, or a blend of the two).

##### Semantics:

The '`llvm.ptrauth.sign`' intrinsic implements the `sign`_ operation.
It returns a signed value.

If `value` is already a signed value, the behavior is undefined.

If `value` is not a pointer value for which `key` is appropriate, the
behavior is undefined.


#### '`llvm.ptrauth.auth`'

##### Syntax:

```llvm
declare i64 @llvm.ptrauth.auth(i64 <value>, i32 <key>, i64 <discriminator>)
```

##### Overview:

The '`llvm.ptrauth.auth`' intrinsic authenticates a signed pointer.

##### Arguments:

The `value` argument is the signed pointer value to be authenticated.
The `key` argument is the identifier of the key that was used to generate
the signed value.
The `discriminator` argument is the additional diversity data to be used as a
discriminator.

##### Semantics:

The '`llvm.ptrauth.auth`' intrinsic implements the `auth`_ operation.
It returns a raw pointer value.
If `value` does not have a correct signature for `key` and `discriminator`,
the intrinsic traps in a target-specific way.


#### '`llvm.ptrauth.strip`'

##### Syntax:

```llvm
declare i64 @llvm.ptrauth.strip(i64 <value>, i32 <key>)
```

##### Overview:

The '`llvm.ptrauth.strip`' intrinsic strips the embedded signature out of a
possibly-signed pointer.


##### Arguments:

The `value` argument is the signed pointer value to be stripped.
The `key` argument is the identifier of the key that was used to generate
the signed value.

##### Semantics:

The '`llvm.ptrauth.strip`' intrinsic implements the `strip`_ operation.
It returns a raw pointer value.  It does **not** check that the
signature is valid.

`key` should identify a key that is appropriate for `value`, as defined
by the target-specific [keys](#keys)).

If `value` is a raw pointer value, it is returned as-is (provided the `key`
is appropriate for the pointer).

If `value` is not a pointer value for which `key` is appropriate, the
behavior is target-specific.

If `value` is a signed pointer value, but `key` does not identify the
same key that was used to generate `value`, the behavior is
target-specific.


#### '`llvm.ptrauth.resign`'

##### Syntax:

```llvm
declare i64 @llvm.ptrauth.resign(i64 <value>,
                                 i32 <old key>, i64 <old discriminator>,
                                 i32 <new key>, i64 <new discriminator>)
```

##### Overview:

The '`llvm.ptrauth.resign`' intrinsic re-signs a signed pointer using
a different key and diversity data.

##### Arguments:

The `value` argument is the signed pointer value to be authenticated.
The `old key` argument is the identifier of the key that was used to generate
the signed value.
The `old discriminator` argument is the additional diversity data to be used
as a discriminator in the auth operation.
The `new key` argument is the identifier of the key to use to generate the
resigned value.
The `new discriminator` argument is the additional diversity data to be used
as a discriminator in the sign operation.

##### Semantics:

The '`llvm.ptrauth.resign`' intrinsic performs a combined `auth`_ and `sign`_
operation, without exposing the intermediate raw pointer.
It returns a signed pointer value.
If `value` does not have a correct signature for `old key` and
`old discriminator`, the intrinsic traps in a target-specific way.

#### '`llvm.ptrauth.sign_generic`'

##### Syntax:

```llvm
declare i64 @llvm.ptrauth.sign_generic(i64 <value>, i64 <discriminator>)
```

##### Overview:

The '`llvm.ptrauth.sign_generic`' intrinsic computes a generic signature of
arbitrary data.

##### Arguments:

The `value` argument is the arbitrary data value to be signed.
The `discriminator` argument is the additional diversity data to be used as a
discriminator.

##### Semantics:

The '`llvm.ptrauth.sign_generic`' intrinsic computes the signature of a given
combination of value and additional diversity data.

It returns a full signature value (as opposed to a signed pointer value, with
an embedded partial signature).

As opposed to [`llvm.ptrauth.sign`](#llvm-ptrauth-sign), it does not interpret
`value` as a pointer value.  Instead, it is an arbitrary data value.


#### '`llvm.ptrauth.blend`'

##### Syntax:

```llvm
declare i64 @llvm.ptrauth.blend(i64 <address discriminator>, i64 <integer discriminator>)
```

##### Overview:

The '`llvm.ptrauth.blend`' intrinsic blends a pointer address discriminator
with a small integer discriminator to produce a new "blended" discriminator.

##### Arguments:

The `address discriminator` argument is a pointer value.
The `integer discriminator` argument is a small integer, as specified by the
target.

##### Semantics:

The '`llvm.ptrauth.blend`' intrinsic combines a small integer discriminator
with a pointer address discriminator, in a way that is specified by the target
implementation.


### Constant

[Intrinsics](#intrinsics) can be used to produce signed pointers dynamically,
in code, but not for signed pointers referenced by constants, in, e.g., global
initializers.

The latter are represented using a
[``ptrauth`` constant](https://llvm.org/docs/LangRef.html#ptrauth-constant),
which describes an authenticated relocation producing a signed pointer.

```llvm
ptrauth (ptr CST, i32 KEY, i64 DISC, ptr ADDRDISC)
```

is equivalent to:

```llvm
  %disc = call i64 @llvm.ptrauth.blend(i64 ptrtoint(ptr ADDRDISC to i64), i64 DISC)
  %signedval = call i64 @llvm.ptrauth.sign(ptr CST, i32 KEY, i64 %disc)
```

### Operand Bundle

Function pointers used as indirect call targets can be signed when materialized,
and authenticated before calls.  This can be accomplished with the
[`llvm.ptrauth.auth`](#llvm-ptrauth-auth) intrinsic, feeding its result to
an indirect call.

However, that exposes the intermediate, unauthenticated pointer, e.g., if it
gets spilled to the stack.  An attacker can then overwrite the pointer in
memory, negating the security benefit provided by pointer authentication.
To prevent that, the `ptrauth` operand bundle may be used: it guarantees that
the intermediate call target is kept in a register and never stored to memory.
This hardening benefit is similar to that provided by
[`llvm.ptrauth.resign`](#llvm-ptrauth-resign)).

Concretely:

```llvm
define void @f(void ()* %fp) {
  call void %fp() [ "ptrauth"(i32 <key>, i64 <data>) ]
  ret void
}
```

is functionally equivalent to:

```llvm
define void @f(void ()* %fp) {
  %fp_i = ptrtoint void ()* %fp to i64
  %fp_auth = call i64 @llvm.ptrauth.auth(i64 %fp_i, i32 <key>, i64 <data>)
  %fp_auth_p = inttoptr i64 %fp_auth to void ()*
  call void %fp_auth_p()
  ret void
}
```

but with the added guarantee that `%fp_i`, `%fp_auth`, and `%fp_auth_p`
are not stored to (and reloaded from) memory.


### Function Attributes

Some function attributes are used to describe other pointer authentication
operations that are not otherwise explicitly expressed in IR.

#### ``ptrauth-indirect-gotos``

``ptrauth-indirect-gotos`` specifies that indirect gotos in this function
should authenticate their target.  At the IR level, no other change is needed.
When lowering [``blockaddress`` constants](https://llvm.org/docs/LangRef.html#blockaddress),
and [``indirectbr`` instructions](https://llvm.org/docs/LangRef.html#i-indirectbr),
this tells the backend to respectively sign and authenticate the pointers.

The specific scheme isn't ABI-visible.  Currently, the AArch64 backend
signs blockaddresses using the `ASIA` key, with an integer discriminator
derived from the parent function's name, using the SipHash stable discriminator:
```
  ptrauth_string_discriminator("<function_name> blockaddress")
```


## AArch64 Support

AArch64 is currently the only architecture with full support of the pointer
authentication primitives, based on Armv8.3-A instructions.

### Armv8.3-A PAuth Pointer Authentication Code

The Armv8.3-A architecture extension defines the PAuth feature, which provides
support for instructions that manipulate Pointer Authentication Codes (PAC).

#### Keys

5 keys are supported by the PAuth feature.

Of those, 4 keys are interchangeably usable to specify the key used in IR
constructs:
* `ASIA`/`ASIB` are instruction keys (encoded as respectively 0 and 1).
* `ASDA`/`ASDB` are data keys (encoded as respectively 2 and 3).

`ASGA` is a special key that cannot be explicitly specified, and is only ever
used implicitly, to implement the
[`llvm.ptrauth.sign_generic`](#llvm-ptrauth-sign-generic) intrinsic.

#### Instructions

The IR [Intrinsics](#intrinsics) described above map onto these
instructions as such:
* [`llvm.ptrauth.sign`](#llvm-ptrauth-sign): `PAC{I,D}{A,B}{Z,SP,}`
* [`llvm.ptrauth.auth`](#llvm-ptrauth-auth): `AUT{I,D}{A,B}{Z,SP,}`
* [`llvm.ptrauth.strip`](#llvm-ptrauth-strip): `XPAC{I,D}`
* [`llvm.ptrauth.blend`](#llvm-ptrauth-blend): The semantics of the blend
  operation are specified by the ABI.  In both the ELF PAuth ABI Extension and
  arm64e, it's a `MOVK` into the high 16 bits.  Consequently, this limits
  the width of the integer discriminator used in blends to 16 bits.
* [`llvm.ptrauth.sign_generic`](#llvm-ptrauth-sign-generic): `PACGA`
* [`llvm.ptrauth.resign`](#llvm-ptrauth-resign): `AUT*+PAC*`.  These are
  represented as a single pseudo-instruction in the backend to guarantee that
  the intermediate raw pointer value is not spilled and attackable.

#### Assembly Representation

At the assembly level, authenticated relocations are represented
using the `@AUTH` modifier:

```asm
    .quad _target@AUTH(<key>,<discriminator>[,addr])
```

where:
* `key` is the Armv8.3-A key identifier (`ia`, `ib`, `da`, `db`)
* `discriminator` is the 16-bit unsigned discriminator value
* `addr` signifies that the authenticated pointer is address-discriminated
  (that is, that the relocation's target address is to be blended into the
  `discriminator` before it is used in the sign operation.

For example:
```asm
  _authenticated_reference_to_sym:
    .quad _sym@AUTH(db,0)
  _authenticated_reference_to_sym_addr_disc:
    .quad _sym@AUTH(ia,12,addr)
```

#### ELF Object File Representation

At the object file level, authenticated relocations are represented
using the `R_AARCH64_AUTH_ABS64` relocation kind (with value `0xE100`).

The signing schema is encoded in the place of relocation to be applied
as follows:

```
| 63                | 62       | 61:60    | 59:48    |  47:32        | 31:0                |
| ----------------- | -------- | -------- | -------- | ------------- | ------------------- |
| address diversity | reserved | key      | reserved | discriminator | reserved for addend |
```
