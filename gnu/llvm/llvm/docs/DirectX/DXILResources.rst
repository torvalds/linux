======================
DXIL Resource Handling
======================

.. contents::
   :local:

.. toctree::
   :hidden:

Introduction
============

Resources in DXIL are represented via ``TargetExtType`` in LLVM IR and
eventually lowered by the DirectX backend into metadata in DXIL.

In DXC and DXIL, static resources are represented as lists of SRVs (Shader
Resource Views), UAVs (Uniform Access Views), CBVs (Constant Bffer Views), and
Samplers. This metadata consists of a "resource record ID" which uniquely
identifies a resource and type information. As of shader model 6.6, there are
also dynamic resources, which forgo the metadata and are described via
``annotateHandle`` operations in the instruction stream instead.

In LLVM we attempt to unify some of the alternative representations that are
present in DXC, with the aim of making handling of resources in the middle end
of the compiler simpler and more consistent.

Resource Type Information and Properties
========================================

There are a number of properties associated with a resource in DXIL.

`Resource ID`
   An arbitrary ID that must be unique per resource type (SRV, UAV, etc).

   In LLVM we don't bother representing this, instead opting to generate it at
   DXIL lowering time.

`Binding information`
   Information about where the resource comes from. This is either (a) a
   register space, lower bound in that space, and size of the binding, or (b)
   an index into a dynamic resource heap.

   In LLVM we represent binding information in the arguments of the
   :ref:`handle creation intrinsics <dxil-resources-handles>`. When generating
   DXIL we transform these calls to metadata, ``dx.op.createHandle``,
   ``dx.op.createHandleFromBinding``, ``dx.op.createHandleFromHeap``, and
   ``dx.op.createHandleForLib`` as needed.

`Type information`
   The type of data that's accessible via the resource. For buffers and
   textures this can be a simple type like ``float`` or ``float4``, a struct,
   or raw bytes. For constant buffers this is just a size. For samplers this is
   the kind of sampler.

   In LLVM we embed this information as a parameter on the ``target()`` type of
   the resource. See :ref:`dxil-resources-types-of-resource`.

`Resource kind information`
   The kind of resource. In HLSL we have things like ``ByteAddressBuffer``,
   ``RWTexture2D``, and ``RasterizerOrderedStructuredBuffer``. These map to a
   set of DXIL kinds like ``RawBuffer`` and ``Texture2D`` with fields for
   certain properties such as ``IsUAV`` and ``IsROV``.

   In LLVM we represent this in the ``target()`` type. We omit information
   that's deriveable from the type information, but we do have fields to encode
   ``IsWriteable``, ``IsROV``, and ``SampleCount`` when needed.

.. note:: TODO: There are two fields in the DXIL metadata that are not
   represented as part of the target type: ``IsGloballyCoherent`` and
   ``HasCounter``.

   Since these are derived from analysis, storing them on the type would mean
   we need to change the type during the compiler pipeline. That just isn't
   practical. It isn't entirely clear to me that we need to serialize this info
   into the IR during the compiler pipeline anyway - we can probably get away
   with an analysis pass that can calculate the information when we need it.

   If analysis is insufficient we'll need something akin to ``annotateHandle``
   (but limited to these two properties) or to encode these in the handle
   creation.

.. _dxil-resources-types-of-resource:

Types of Resource
=================

We define a set of ``TargetExtTypes`` that is similar to the HLSL
representations for the various resources, albeit with a few things
parameterized. This is different than DXIL, as simplifying the types to
something like "dx.srv" and "dx.uav" types would mean the operations on these
types would have to be overly generic.

Buffers
-------

.. code-block:: llvm

   target("dx.TypedBuffer", ElementType, IsWriteable, IsROV)
   target("dx.RawBuffer", ElementType, IsWriteable, IsROV)

We need two separate buffer types to account for the differences between the
16-byte `bufferLoad`_ / `bufferStore`_ operations that work on DXIL's
TypedBuffers and the `rawBufferLoad`_ / `rawBufferStore`_ operations that are
used for DXIL's RawBuffers and StructuredBuffers. We call the latter
"RawBuffer" to match the naming of the operations, but it can represent both
the Raw and Structured variants.

For TypedBuffer, the element type must be an integer or floating point type.
For RawBuffer the type can be an integer, floating point, or struct type.
HLSL's ByteAddressBuffer is represented by an `i8` element type.

These types are generally used by BufferLoad and BufferStore operations, as
well as atomics.

There are a few fields to describe variants of all of these types:

.. list-table:: Buffer Fields
   :header-rows: 1

   * - Field
     - Description
   * - ElementType
     - Type for a single element, such as ``i8``, ``v4f32``, or a structure
       type.
   * - IsWriteable
     - Whether or not the field is writeable. This distinguishes SRVs (not
       writeable) and UAVs (writeable).
   * - IsROV
     - Whether the UAV is a rasterizer ordered view. Always ``0`` for SRVs.

.. _bufferLoad: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst#bufferload
.. _bufferStore: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst#bufferstore
.. _rawBufferLoad: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst#rawbufferload
.. _rawBufferStore: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst#rawbufferstore

Resource Operations
===================

.. _dxil-resources-handles:

Resource Handles
----------------

We provide a few different ways to instantiate resources in the IR via the
``llvm.dx.handle.*`` intrinsics. These intrinsics are overloaded on return
type, returning an appropriate handle for the resource, and represent binding
information in the arguments to the intrinsic.

The three operations we need are ``llvm.dx.handle.fromBinding``,
``llvm.dx.handle.fromHeap``, and ``llvm.dx.handle.fromPointer``. These are
rougly equivalent to the DXIL operations ``dx.op.createHandleFromBinding``,
``dx.op.createHandleFromHeap``, and ``dx.op.createHandleForLib``, but they fold
the subsequent ``dx.op.annotateHandle`` operation in. Note that we don't have
an analogue for `dx.op.createHandle`_, since ``dx.op.createHandleFromBinding``
subsumes it.

.. _dx.op.createHandle: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst#resource-handles

.. list-table:: ``@llvm.dx.handle.fromBinding``
   :header-rows: 1

   * - Argument
     -
     - Type
     - Description
   * - Return value
     -
     - A ``target()`` type
     - A handle which can be operated on
   * - ``%reg_space``
     - 1
     - ``i32``
     - Register space ID in the root signature for this resource.
   * - ``%lower_bound``
     - 2
     - ``i32``
     - Lower bound of the binding in its register space.
   * - ``%range_size``
     - 3
     - ``i32``
     - Range size of the binding.
   * - ``%index``
     - 4
     - ``i32``
     - Index of the resource to access.
   * - ``%non-uniform``
     - 5
     - i1
     - Must be ``true`` if the resource index may be non-uniform.

.. note:: TODO: Can we drop the uniformity bit? I suspect we can derive it from
          uniformity analysis...

Examples:

.. code-block:: llvm

   ; RWBuffer<float4> Buf : register(u5, space3)
   %buf = call target("dx.TypedBuffer", float, 1, 0)
               @llvm.dx.handle.fromBinding.tdx.TypedBuffer_f32_1_0(
                   i32 3, i32 5, i32 1, i32 0, i1 false)

   ; RWBuffer<uint> Buf : register(u7, space2)
   %buf = call target("dx.TypedBuffer", i32, 1, 0)
               @llvm.dx.handle.fromBinding.tdx.TypedBuffer_i32_1_0t(
                   i32 2, i32 7, i32 1, i32 0, i1 false)

   ; Buffer<uint4> Buf[24] : register(t3, space5)
   %buf = call target("dx.TypedBuffer", i32, 0, 0)
               @llvm.dx.handle.fromBinding.tdx.TypedBuffer_i32_0_0t(
                   i32 2, i32 7, i32 24, i32 0, i1 false)

   ; struct S { float4 a; uint4 b; };
   ; StructuredBuffer<S> Buf : register(t2, space4)
   %buf = call target("dx.RawBuffer", {<4 x f32>, <4 x i32>}, 0, 0)
               @llvm.dx.handle.fromBinding.tdx.RawBuffer_sl_v4f32v4i32s_0_0t(
                   i32 4, i32 2, i32 1, i32 0, i1 false)

   ; ByteAddressBuffer Buf : register(t8, space1)
   %buf = call target("dx.RawBuffer", i8, 0, 0)
               @llvm.dx.handle.fromBinding.tdx.RawBuffer_i8_0_0t(
                   i32 1, i32 8, i32 1, i32 0, i1 false)

.. list-table:: ``@llvm.dx.handle.fromHeap``
   :header-rows: 1

   * - Argument
     -
     - Type
     - Description
   * - Return value
     -
     - A ``target()`` type
     - A handle which can be operated on
   * - ``%index``
     - 0
     - ``i32``
     - Index of the resource to access.
   * - ``%non-uniform``
     - 1
     - i1
     - Must be ``true`` if the resource index may be non-uniform.

Examples:

.. code-block:: llvm

   ; RWStructuredBuffer<float4> Buf = ResourceDescriptorHeap[2];
   declare
     target("dx.RawBuffer", <4 x float>, 1, 0)
     @llvm.dx.handle.fromHeap.tdx.RawBuffer_v4f32_1_0(
         i32 %index, i1 %non_uniform)
   ; ...
   %buf = call target("dx.RawBuffer", <4 x f32>, 1, 0)
               @llvm.dx.handle.fromHeap.tdx.RawBuffer_v4f32_1_0(
                   i32 2, i1 false)

Buffer Loads and Stores
-----------------------

*relevant types: Buffers*

We need to treat buffer loads and stores from "dx.TypedBuffer" and
"dx.RawBuffer" separately. For TypedBuffer, we have ``llvm.dx.typedBufferLoad``
and ``llvm.dx.typedBufferStore``, which load and store 16-byte "rows" of data
via a simple index. For RawBuffer, we have ``llvm.dx.rawBufferPtr``, which
return a pointer that can be indexed, loaded, and stored to as needed.

The typed load and store operations always operate on exactly 16 bytes of data,
so there are only a few valid overloads. For types that are 32-bits or smaller,
we operate on 4-element vectors, such as ``<4 x i32>``, ``<4 x float>``, or
``<4 x half>``. Note that in 16-bit cases each 16-bit value occupies 32-bits of
storage. For 64-bit types we operate on 2-element vectors - ``<2 x double>`` or
``<2 x i64>``. When a type like `Buffer<float>` is used at the HLSL level, it
is expected that this will operate on a single float in each 16 byte row - that
is, a load would use the ``<4 x float>`` variant and then extract the first
element.

.. note:: In DXC, trying to operate on a ``Buffer<double4>`` crashes the
          compiler. We should probably just reject this in the frontend.

The TypedBuffer intrinsics are lowered to the `bufferLoad`_ and `bufferStore`_
operations, and the operations on the memory accessed by RawBufferPtr are
lowered to `rawBufferLoad`_ and `rawBufferStore`_. Note that if we want to
support DXIL versions prior to 1.2 we'll need to lower the RawBuffer loads and
stores to the non-raw operations as well.

.. note:: TODO: We need to account for `CheckAccessFullyMapped`_ here.

   In DXIL the load operations always return an ``i32`` status value, but this
   isn't very ergonomic when it isn't used. We can (1) bite the bullet and have
   the loads return `{%ret_type, %i32}` all the time, (2) create a variant or
   update the signature iff the status is used, or (3) hide this in a sideband
   channel somewhere. I'm leaning towards (2), but could probably be convinced
   that the ugliness of (1) is worth the simplicity.

.. _CheckAccessFullyMapped: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/checkaccessfullymapped

.. list-table:: ``@llvm.dx.typedBufferLoad``
   :header-rows: 1

   * - Argument
     -
     - Type
     - Description
   * - Return value
     -
     - A 4- or 2-element vector of the type of the buffer
     - The data loaded from the buffer
   * - ``%buffer``
     - 0
     - ``target(dx.TypedBuffer, ...)``
     - The buffer to load from
   * - ``%index``
     - 1
     - ``i32``
     - Index into the buffer

Examples:

.. code-block:: llvm

   %ret = call <4 x float> @llvm.dx.typedBufferLoad.tdx.TypedBuffer_f32_0_0t(
       target("dx.TypedBuffer", f32, 0, 0) %buffer, i32 %index)
   %ret = call <4 x i32> @llvm.dx.typedBufferLoad.tdx.TypedBuffer_i32_0_0t(
       target("dx.TypedBuffer", i32, 0, 0) %buffer, i32 %index)
   %ret = call <4 x half> @llvm.dx.typedBufferLoad.tdx.TypedBuffer_f16_0_0t(
       target("dx.TypedBuffer", f16, 0, 0) %buffer, i32 %index)
   %ret = call <2 x double> @llvm.dx.typedBufferLoad.tdx.TypedBuffer_f64_0_0t(
       target("dx.TypedBuffer", double, 0, 0) %buffer, i32 %index)

.. list-table:: ``@llvm.dx.typedBufferStore``
   :header-rows: 1

   * - Argument
     -
     - Type
     - Description
   * - Return value
     -
     - ``void``
     -
   * - ``%buffer``
     - 0
     - ``target(dx.TypedBuffer, ...)``
     - The buffer to store into
   * - ``%index``
     - 1
     - ``i32``
     - Index into the buffer
   * - ``%data``
     - 2
     - A 4- or 2-element vector of the type of the buffer
     - The data to store

Examples:

.. code-block:: llvm

   call void @llvm.dx.bufferStore.tdx.Buffer_f32_1_0t(
       target("dx.TypedBuffer", f32, 1, 0) %buf, i32 %index, <4 x f32> %data)
   call void @llvm.dx.bufferStore.tdx.Buffer_f16_1_0t(
       target("dx.TypedBuffer", f16, 1, 0) %buf, i32 %index, <4 x f16> %data)
   call void @llvm.dx.bufferStore.tdx.Buffer_f64_1_0t(
       target("dx.TypedBuffer", f64, 1, 0) %buf, i32 %index, <2 x f64> %data)

.. list-table:: ``@llvm.dx.rawBufferPtr``
   :header-rows: 1

   * - Argument
     -
     - Type
     - Description
   * - Return value
     -
     - ``ptr``
     - Pointer to an element of the buffer
   * - ``%buffer``
     - 0
     - ``target(dx.RawBuffer, ...)``
     - The buffer to load from
   * - ``%index``
     - 1
     - ``i32``
     - Index into the buffer

Examples:

.. code-block:: llvm

   ; Load a float4 from a buffer
   %buf = call ptr @llvm.dx.rawBufferPtr.tdx.RawBuffer_v4f32_0_0t(
       target("dx.RawBuffer", <4 x f32>, 0, 0) %buffer, i32 %index)
   %val = load <4 x float>, ptr %buf, align 16

   ; Load the double from a struct containing an int, a float, and a double
   %buf = call ptr @llvm.dx.rawBufferPtr.tdx.RawBuffer_sl_i32f32f64s_0_0t(
       target("dx.RawBuffer", {i32, f32, f64}, 0, 0) %buffer, i32 %index)
   %val = getelementptr inbounds {i32, f32, f64}, ptr %buf, i32 0, i32 2
   %d = load double, ptr %val, align 8

   ; Load a float from a byte address buffer
   %buf = call ptr @llvm.dx.rawBufferPtr.tdx.RawBuffer_i8_0_0t(
       target("dx.RawBuffer", i8, 0, 0) %buffer, i32 %index)
   %val = getelementptr inbounds float, ptr %buf, i64 0
   %f = load float, ptr %val, align 4

   ; Store to a buffer containing float4
   %addr = call ptr @llvm.dx.rawBufferPtr.tdx.RawBuffer_v4f32_0_0t(
       target("dx.RawBuffer", <4 x f32>, 0, 0) %buffer, i32 %index)
   store <4 x float> %val, ptr %addr

   ; Store the double in a struct containing an int, a float, and a double
   %buf = call ptr @llvm.dx.rawBufferPtr.tdx.RawBuffer_sl_i32f32f64s_0_0t(
       target("dx.RawBuffer", {i32, f32, f64}, 0, 0) %buffer, i32 %index)
   %addr = getelementptr inbounds {i32, f32, f64}, ptr %buf, i32 0, i32 2
   store double %d, ptr %addr

   ; Store a float into a byte address buffer
   %buf = call ptr @llvm.dx.rawBufferPtr.tdx.RawBuffer_i8_0_0t(
       target("dx.RawBuffer", i8, 0, 0) %buffer, i32 %index)
   %addr = getelementptr inbounds float, ptr %buf, i64 0
   store float %f, ptr %val

