//===- SPIRVSymbolicOperands.td ----------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines symbolic/named operands for various SPIR-V instructions.
//
//===----------------------------------------------------------------------===//

include "llvm/TableGen/SearchableTable.td"

//===----------------------------------------------------------------------===//
// Lookup table containing symbolic operands with the following columns:
// - Category (Extension/Capability/BuiltIn/etc.)
// - Value (32-bit representation for binary emission)
// - Mnemonic (String representation for textual emission)
// - MinVersion
// - MaxVersion
//===----------------------------------------------------------------------===//

// Forward-declare classes used in SymbolicOperand
class OperandCategory;

class SymbolicOperand<OperandCategory category, bits<32> value, string mnemonic, bits<32> minVersion, bits<32> maxVersion> {
  OperandCategory Category = category;
  bits<32> Value = value;
  string Mnemonic = mnemonic;
  bits<32> MinVersion = minVersion;
  bits<32> MaxVersion = maxVersion;
}

def SymbolicOperands : GenericTable {
  let FilterClass = "SymbolicOperand";
  let Fields = ["Category", "Value", "Mnemonic", "MinVersion", "MaxVersion"];
  string TypeOf_Category = "OperandCategory";
  let PrimaryKey = ["Category", "Value"];
  // Function for looking up symbolic operands based on category and value.
  let PrimaryKeyName = "lookupSymbolicOperandByCategoryAndValue";
}

// Function for looking up symbolic operands based on just category.
def lookupSymbolicOperandByCategory : SearchIndex {
  let Table = SymbolicOperands;
  let Key = ["Category"];
}

// Function for looking up symbolic operands based on category and mnemonic.
def lookupSymbolicOperandByCategoryAndMnemonic : SearchIndex {
  let Table = SymbolicOperands;
  let Key = ["Category", "Mnemonic"];
}

//===----------------------------------------------------------------------===//
// Lookup table for matching symbolic operands (category + 32-bit value) to
// a SPIR-V extension.
//===----------------------------------------------------------------------===//

// Forward-declare classes used in ExtensionEntry
class Extension;

class ExtensionEntry<OperandCategory category, bits<32> value, Extension reqExtension> {
  OperandCategory Category = category;
  bits<32> Value = value;
  Extension ReqExtension = reqExtension;
}

def ExtensionEntries : GenericTable {
  let FilterClass = "ExtensionEntry";
  let Fields = ["Category", "Value", "ReqExtension"];
  string TypeOf_Category = "OperandCategory";
  string TypeOf_ReqExtension = "Extension";
  let PrimaryKey = ["Category", "Value"];
  // Function for looking up the extension by category + value.
  let PrimaryKeyName = "lookupExtensionByCategoryAndValue";
}

// Function to lookup symbolic operands enabled by a given extension.
def lookupSymbolicOperandsEnabledByExtension : SearchIndex {
  let Table = ExtensionEntries;
  let Key = ["ReqExtension", "Category"];
}

//===----------------------------------------------------------------------===//
// Lookup table for matching symbolic operands (category + 32-bit value) to
// SPIR-V capabilities. If an operand requires more than one capability, there
// will be multiple consecutive entries present in the table.
//===----------------------------------------------------------------------===//

// Forward-declare classes used in ExtensionEntry
class Capability;

class CapabilityEntry<OperandCategory category, bits<32> value, Capability reqCabaility> {
  OperandCategory Category = category;
  bits<32> Value = value;
  Capability ReqCapability = reqCabaility;
}

def CapabilityEntries : GenericTable {
  let FilterClass = "CapabilityEntry";
  let Fields = ["Category", "Value", "ReqCapability"];
  string TypeOf_Category = "OperandCategory";
  string TypeOf_ReqCapability = "Capability";
  let PrimaryKey = ["Category", "Value"];
  // Function for looking up a (the first) capability by category + value. Next
  // capabilities should be consecutive.
  let PrimaryKeyName = "lookupCapabilityByCategoryAndValue";
}

//===----------------------------------------------------------------------===//
// Multiclass used to define a SymbolicOperand and at the same time declare
// required extension and capabilities.
//===----------------------------------------------------------------------===//

multiclass SymbolicOperandWithRequirements<OperandCategory category, bits<32> value, string mnemonic, bits<32> minVersion, bits<32> maxVersion, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
    assert !ge(!size(mnemonic), 1), "No mnemonic/string representation provided for symbolic operand with value " # value;
    def : SymbolicOperand<category, value, mnemonic, minVersion, maxVersion>;

    assert !le(!size(reqExtensions), 1), "Too many required extensions for a symbolic/named operand: " # mnemonic;
    if !eq(!size(reqExtensions), 1) then {
        def : ExtensionEntry<category, value, reqExtensions[0]>;
    }

    foreach capability = reqCapabilities in {
        def : CapabilityEntry<category, value, capability>;
    }
}

//===----------------------------------------------------------------------===//
// Enum defining different categories of symbolic/named operands.
//===----------------------------------------------------------------------===//

def OperandCategory : GenericEnum {
  let FilterClass = "OperandCategory";
}

class OperandCategory;

def ExtensionOperand : OperandCategory;
def CapabilityOperand : OperandCategory;
def SourceLanguageOperand : OperandCategory;
def AddressingModelOperand : OperandCategory;
def ExecutionModelOperand : OperandCategory;
def MemoryModelOperand : OperandCategory;
def ExecutionModeOperand : OperandCategory;
def StorageClassOperand : OperandCategory;
def DimOperand : OperandCategory;
def SamplerAddressingModeOperand : OperandCategory;
def SamplerFilterModeOperand : OperandCategory;
def ImageFormatOperand : OperandCategory;
def ImageChannelOrderOperand : OperandCategory;
def ImageChannelDataTypeOperand : OperandCategory;
def ImageOperandOperand : OperandCategory;
def FPFastMathModeOperand : OperandCategory;
def FPRoundingModeOperand : OperandCategory;
def LinkageTypeOperand : OperandCategory;
def AccessQualifierOperand : OperandCategory;
def FunctionParameterAttributeOperand : OperandCategory;
def DecorationOperand : OperandCategory;
def BuiltInOperand : OperandCategory;
def SelectionControlOperand : OperandCategory;
def LoopControlOperand : OperandCategory;
def FunctionControlOperand : OperandCategory;
def MemorySemanticsOperand : OperandCategory;
def MemoryOperandOperand : OperandCategory;
def ScopeOperand : OperandCategory;
def GroupOperationOperand : OperandCategory;
def KernelEnqueueFlagsOperand : OperandCategory;
def KernelProfilingInfoOperand : OperandCategory;
def OpcodeOperand : OperandCategory;

//===----------------------------------------------------------------------===//
// Multiclass used to define Extesions enum values and at the same time
// SymbolicOperand entries.
//===----------------------------------------------------------------------===//

def Extension : GenericEnum, Operand<i32> {
  let FilterClass = "Extension";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = "printExtension";
}

class Extension<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass ExtensionOperand<bits<32> value> {
  def NAME : Extension<NAME, value>;
  defm : SymbolicOperandWithRequirements<ExtensionOperand, value, NAME, 0, 0, [], []>;
}

defm SPV_AMD_shader_explicit_vertex_parameter : ExtensionOperand<1>;
defm SPV_AMD_shader_trinary_minmax_extension : ExtensionOperand<2>;
defm SPV_AMD_gcn_shader : ExtensionOperand<3>;
defm SPV_KHR_shader_ballot : ExtensionOperand<4>;
defm SPV_AMD_shader_ballot : ExtensionOperand<5>;
defm SPV_AMD_gpu_shader_half_float : ExtensionOperand<6>;
defm SPV_KHR_shader_draw_parameters : ExtensionOperand<7>;
defm SPV_KHR_subgroup_vote : ExtensionOperand<8>;
defm SPV_KHR_16bit_storage : ExtensionOperand<9>;
defm SPV_KHR_device_group : ExtensionOperand<10>;
defm SPV_KHR_multiview : ExtensionOperand<11>;
defm SPV_NVX_multiview_per_view_attributes : ExtensionOperand<12>;
defm SPV_NV_viewport_array2 : ExtensionOperand<13>;
defm SPV_NV_stereo_view_rendering : ExtensionOperand<14>;
defm SPV_NV_sample_mask_override_coverage : ExtensionOperand<15>;
defm SPV_NV_geometry_shader_passthrough : ExtensionOperand<16>;
defm SPV_AMD_texture_gather_bias_lod : ExtensionOperand<17>;
defm SPV_KHR_storage_buffer_storage_class : ExtensionOperand<18>;
defm SPV_KHR_variable_pointers : ExtensionOperand<19>;
defm SPV_AMD_gpu_shader_int16 : ExtensionOperand<20>;
defm SPV_KHR_post_depth_coverage : ExtensionOperand<21>;
defm SPV_KHR_shader_atomic_counter_ops : ExtensionOperand<22>;
defm SPV_EXT_shader_stencil_export : ExtensionOperand<23>;
defm SPV_EXT_shader_viewport_index_layer : ExtensionOperand<24>;
defm SPV_AMD_shader_image_load_store_lod : ExtensionOperand<25>;
defm SPV_AMD_shader_fragment_mask : ExtensionOperand<26>;
defm SPV_EXT_fragment_fully_covered : ExtensionOperand<27>;
defm SPV_AMD_gpu_shader_half_float_fetch : ExtensionOperand<28>;
defm SPV_GOOGLE_decorate_string : ExtensionOperand<29>;
defm SPV_GOOGLE_hlsl_functionality1 : ExtensionOperand<30>;
defm SPV_NV_shader_subgroup_partitioned : ExtensionOperand<31>;
defm SPV_EXT_descriptor_indexing : ExtensionOperand<32>;
defm SPV_KHR_8bit_storage : ExtensionOperand<33>;
defm SPV_KHR_vulkan_memory_model : ExtensionOperand<34>;
defm SPV_NV_ray_tracing : ExtensionOperand<35>;
defm SPV_NV_compute_shader_derivatives : ExtensionOperand<36>;
defm SPV_NV_fragment_shader_barycentric : ExtensionOperand<37>;
defm SPV_NV_mesh_shader : ExtensionOperand<38>;
defm SPV_NV_shader_image_footprint : ExtensionOperand<39>;
defm SPV_NV_shading_rate : ExtensionOperand<40>;
defm SPV_INTEL_subgroups : ExtensionOperand<41>;
defm SPV_INTEL_media_block_io : ExtensionOperand<42>;
defm SPV_EXT_fragment_invocation_density : ExtensionOperand<44>;
defm SPV_KHR_no_integer_wrap_decoration : ExtensionOperand<45>;
defm SPV_KHR_float_controls : ExtensionOperand<46>;
defm SPV_EXT_physical_storage_buffer : ExtensionOperand<47>;
defm SPV_INTEL_fpga_memory_attributes : ExtensionOperand<48>;
defm SPV_NV_cooperative_matrix : ExtensionOperand<49>;
defm SPV_INTEL_shader_integer_functions2 : ExtensionOperand<50>;
defm SPV_INTEL_fpga_loop_controls : ExtensionOperand<51>;
defm SPV_EXT_fragment_shader_interlock : ExtensionOperand<52>;
defm SPV_NV_shader_sm_builtins : ExtensionOperand<53>;
defm SPV_KHR_shader_clock : ExtensionOperand<54>;
defm SPV_INTEL_unstructured_loop_controls : ExtensionOperand<55>;
defm SPV_EXT_demote_to_helper_invocation : ExtensionOperand<56>;
defm SPV_INTEL_fpga_reg : ExtensionOperand<57>;
defm SPV_INTEL_blocking_pipes : ExtensionOperand<58>;
defm SPV_GOOGLE_user_type : ExtensionOperand<59>;
defm SPV_KHR_physical_storage_buffer : ExtensionOperand<60>;
defm SPV_INTEL_kernel_attributes : ExtensionOperand<61>;
defm SPV_KHR_non_semantic_info : ExtensionOperand<62>;
defm SPV_INTEL_io_pipes : ExtensionOperand<63>;
defm SPV_KHR_ray_tracing : ExtensionOperand<64>;
defm SPV_KHR_ray_query : ExtensionOperand<65>;
defm SPV_INTEL_fpga_memory_accesses : ExtensionOperand<66>;
defm SPV_INTEL_arbitrary_precision_integers : ExtensionOperand<67>;
defm SPV_EXT_shader_atomic_float_add : ExtensionOperand<68>;
defm SPV_KHR_terminate_invocation : ExtensionOperand<69>;
defm SPV_KHR_fragment_shading_rate : ExtensionOperand<70>;
defm SPV_EXT_shader_image_int64 : ExtensionOperand<71>;
defm SPV_INTEL_fp_fast_math_mode : ExtensionOperand<72>;
defm SPV_INTEL_fpga_cluster_attributes : ExtensionOperand<73>;
defm SPV_INTEL_loop_fuse : ExtensionOperand<74>;
defm SPV_EXT_shader_atomic_float_min_max : ExtensionOperand<75>;
defm SPV_KHR_workgroup_memory_explicit_layout : ExtensionOperand<76>;
defm SPV_KHR_linkonce_odr : ExtensionOperand<77>;
defm SPV_KHR_expect_assume : ExtensionOperand<78>;
defm SPV_INTEL_fpga_dsp_control : ExtensionOperand<79>;
defm SPV_NV_bindless_texture : ExtensionOperand<80>;
defm SPV_INTEL_fpga_invocation_pipelining_attributes : ExtensionOperand<81>;
defm SPV_KHR_subgroup_uniform_control_flow : ExtensionOperand<82>;
defm SPV_HUAWEI_subpass_shading : ExtensionOperand<83>;
defm SPV_KHR_integer_dot_product : ExtensionOperand<84>;
defm SPV_EXT_shader_atomic_float16_add : ExtensionOperand<85>;
defm SPV_INTEL_runtime_aligned : ExtensionOperand<86>;
defm SPV_KHR_bit_instructions : ExtensionOperand<87>;
defm SPV_NV_ray_tracing_motion_blur : ExtensionOperand<88>;
defm SPV_KHR_uniform_group_instructions : ExtensionOperand<89>;
defm SPV_KHR_subgroup_rotate : ExtensionOperand<90>;
defm SPV_INTEL_split_barrier : ExtensionOperand<91>;
defm SPV_KHR_ray_cull_mask : ExtensionOperand<92>;
defm SPV_KHR_fragment_shader_barycentric : ExtensionOperand<93>;
defm SPV_EXT_relaxed_printf_string_address_space : ExtensionOperand<94>;
defm SPV_EXT_ycbcr_attachments : ExtensionOperand<95>;
defm SPV_EXT_mesh_shader : ExtensionOperand<96>;
defm SPV_ARM_core_builtins : ExtensionOperand<97>;
defm SPV_EXT_opacity_micromap : ExtensionOperand<98>;
defm SPV_NV_shader_invocation_reorder : ExtensionOperand<99>;
defm SPV_INTEL_usm_storage_classes : ExtensionOperand<100>;
defm SPV_INTEL_fpga_latency_control : ExtensionOperand<101>;
defm SPV_INTEL_fpga_argument_interfaces : ExtensionOperand<102>;
defm SPV_INTEL_optnone : ExtensionOperand<103>;
defm SPV_INTEL_function_pointers : ExtensionOperand<104>;
defm SPV_INTEL_variable_length_array : ExtensionOperand<105>;
defm SPV_INTEL_bfloat16_conversion : ExtensionOperand<106>;
defm SPV_INTEL_inline_assembly : ExtensionOperand<107>;
defm SPV_INTEL_cache_controls : ExtensionOperand<108>;
defm SPV_INTEL_global_variable_host_access : ExtensionOperand<109>;
defm SPV_INTEL_global_variable_fpga_decorations : ExtensionOperand<110>;
defm SPV_KHR_cooperative_matrix : ExtensionOperand<111>;

//===----------------------------------------------------------------------===//
// Multiclass used to define Capabilities enum values and at the same time
// SymbolicOperand entries with string mnemonics, versioning, extensions, and
// capabilities.
//===----------------------------------------------------------------------===//

def Capability : GenericEnum, Operand<i32> {
  let FilterClass = "Capability";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class Capability<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass CapabilityOperand<bits<32> value, bits<32> minVersion, bits<32> maxVersion, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
  def NAME : Capability<NAME, value>;
  defm : SymbolicOperandWithRequirements<CapabilityOperand, value, NAME, minVersion, maxVersion, reqExtensions, reqCapabilities>;
}

defm Matrix : CapabilityOperand<0, 0, 0, [], []>;
defm Shader : CapabilityOperand<1, 0, 0, [], [Matrix]>;
defm Geometry : CapabilityOperand<2, 0, 0, [], [Shader]>;
defm Tessellation : CapabilityOperand<3, 0, 0, [], [Shader]>;
defm Addresses : CapabilityOperand<4, 0, 0, [], []>;
defm Linkage : CapabilityOperand<5, 0, 0, [], []>;
defm Kernel : CapabilityOperand<6, 0, 0, [], []>;
defm Vector16 : CapabilityOperand<7, 0, 0, [], [Kernel]>;
defm Float16Buffer : CapabilityOperand<8, 0, 0, [], [Kernel]>;
defm Float16 : CapabilityOperand<9, 0, 0, [], []>;
defm Float64 : CapabilityOperand<10, 0, 0, [], []>;
defm Int64 : CapabilityOperand<11, 0, 0, [], []>;
defm Int64Atomics : CapabilityOperand<12, 0, 0, [], [Int64]>;
defm ImageBasic : CapabilityOperand<13, 0, 0, [], [Kernel]>;
defm ImageReadWrite : CapabilityOperand<14, 0, 0, [], [ImageBasic]>;
defm ImageMipmap : CapabilityOperand<15, 0, 0, [], [ImageBasic]>;
defm Pipes : CapabilityOperand<17, 0, 0, [], [Kernel]>;
defm Groups : CapabilityOperand<18, 0, 0, [], []>;
defm DeviceEnqueue : CapabilityOperand<19, 0, 0, [], []>;
defm LiteralSampler : CapabilityOperand<20, 0, 0, [], [Kernel]>;
defm AtomicStorage : CapabilityOperand<21, 0, 0, [], [Shader]>;
defm Int16 : CapabilityOperand<22, 0, 0, [], []>;
defm TessellationPointSize : CapabilityOperand<23, 0, 0, [], [Tessellation]>;
defm GeometryPointSize : CapabilityOperand<24, 0, 0, [], [Geometry]>;
defm ImageGatherExtended : CapabilityOperand<25, 0, 0, [], [Shader]>;
defm StorageImageMultisample : CapabilityOperand<27, 0, 0, [], [Shader]>;
defm UniformBufferArrayDynamicIndexing : CapabilityOperand<28, 0, 0, [], [Shader]>;
defm SampledImageArrayDymnamicIndexing : CapabilityOperand<29, 0, 0, [], [Shader]>;
defm ClipDistance : CapabilityOperand<32, 0, 0, [], [Shader]>;
defm CullDistance : CapabilityOperand<33, 0, 0, [], [Shader]>;
defm SampleRateShading : CapabilityOperand<35, 0, 0, [], [Shader]>;
defm SampledRect : CapabilityOperand<37, 0, 0, [], [Shader]>;
defm ImageRect : CapabilityOperand<36, 0, 0, [], [SampledRect]>;
defm GenericPointer : CapabilityOperand<38, 0, 0, [], [Addresses]>;
defm Int8 : CapabilityOperand<39, 0, 0, [], []>;
defm InputAttachment : CapabilityOperand<40, 0, 0, [], [Shader]>;
defm SparseResidency : CapabilityOperand<41, 0, 0, [], [Shader]>;
defm MinLod : CapabilityOperand<42, 0, 0, [], [Shader]>;
defm Sampled1D : CapabilityOperand<43, 0, 0, [], []>;
defm Image1D : CapabilityOperand<44, 0, 0, [], [Sampled1D]>;
defm SampledCubeArray : CapabilityOperand<45, 0, 0, [], [Shader]>;
defm ImageCubeArray : CapabilityOperand<34, 0, 0, [], [SampledCubeArray]>;
defm SampledBuffer : CapabilityOperand<46, 0, 0, [], []>;
defm ImageBuffer : CapabilityOperand<47, 0, 0, [], [SampledBuffer]>;
defm ImageMSArray : CapabilityOperand<48, 0, 0, [], [Shader]>;
defm StorageImageExtendedFormats : CapabilityOperand<49, 0, 0, [], [Shader]>;
defm ImageQuery : CapabilityOperand<50, 0, 0, [], [Shader]>;
defm DerivativeControl : CapabilityOperand<51, 0, 0, [], [Shader]>;
defm InterpolationFunction : CapabilityOperand<52, 0, 0, [], [Shader]>;
defm TransformFeedback : CapabilityOperand<53, 0, 0, [], [Shader]>;
defm GeometryStreams : CapabilityOperand<54, 0, 0, [], [Geometry]>;
defm StorageImageReadWithoutFormat : CapabilityOperand<55, 0, 0, [], [Shader]>;
defm StorageImageWriteWithoutFormat : CapabilityOperand<56, 0, 0, [], [Shader]>;
defm MultiViewport : CapabilityOperand<57, 0, 0, [], [Geometry]>;
defm SubgroupDispatch : CapabilityOperand<58, 0x10100, 0, [], [DeviceEnqueue]>;
defm NamedBarrier : CapabilityOperand<59, 0x10100, 0, [], [Kernel]>;
defm PipeStorage : CapabilityOperand<60, 0x10100, 0, [], [Pipes]>;
defm GroupNonUniform : CapabilityOperand<61, 0x10300, 0, [], []>;
defm GroupNonUniformVote : CapabilityOperand<62, 0x10300, 0, [], [GroupNonUniform]>;
defm GroupNonUniformArithmetic : CapabilityOperand<63, 0x10300, 0, [], [GroupNonUniform]>;
defm GroupNonUniformBallot : CapabilityOperand<64, 0x10300, 0, [], [GroupNonUniform]>;
defm GroupNonUniformShuffle : CapabilityOperand<65, 0x10300, 0, [], [GroupNonUniform]>;
defm GroupNonUniformShuffleRelative : CapabilityOperand<66, 0x10300, 0, [], [GroupNonUniform]>;
defm GroupNonUniformClustered : CapabilityOperand<67, 0x10300, 0, [], [GroupNonUniform]>;
defm GroupNonUniformQuad : CapabilityOperand<68, 0x10300, 0, [], [GroupNonUniform]>;
defm SubgroupBallotKHR : CapabilityOperand<4423, 0, 0, [SPV_KHR_shader_ballot], []>;
defm DrawParameters : CapabilityOperand<4427, 0x10300, 0, [SPV_KHR_shader_draw_parameters], [Shader]>;
defm SubgroupVoteKHR : CapabilityOperand<4431, 0, 0, [SPV_KHR_subgroup_vote], []>;
defm StorageBuffer16BitAccess : CapabilityOperand<4433, 0x10300, 0, [SPV_KHR_16bit_storage], []>;
defm StorageUniform16 : CapabilityOperand<4434, 0x10300, 0, [SPV_KHR_16bit_storage], [StorageBuffer16BitAccess]>;
defm StoragePushConstant16 : CapabilityOperand<4435, 0x10300, 0, [SPV_KHR_16bit_storage], []>;
defm StorageInputOutput16 : CapabilityOperand<4436, 0x10300, 0, [SPV_KHR_16bit_storage], []>;
defm DeviceGroup : CapabilityOperand<4437, 0x10300, 0, [SPV_KHR_device_group], []>;
defm MultiView : CapabilityOperand<4439, 0x10300, 0, [SPV_KHR_multiview], [Shader]>;
defm VariablePointersStorageBuffer : CapabilityOperand<4441, 0x10300, 0, [SPV_KHR_variable_pointers], [Shader]>;
defm VariablePointers : CapabilityOperand<4442, 0x10300, 0, [SPV_KHR_variable_pointers], [VariablePointersStorageBuffer]>;
defm AtomicStorageOps : CapabilityOperand<4445, 0, 0, [SPV_KHR_shader_atomic_counter_ops], []>;
defm SampleMaskPostDepthCoverage : CapabilityOperand<4447, 0, 0, [SPV_KHR_post_depth_coverage], []>;
defm StorageBuffer8BitAccess : CapabilityOperand<4448, 0, 0, [SPV_KHR_8bit_storage], []>;
defm UniformAndStorageBuffer8BitAccess : CapabilityOperand<4449, 0, 0, [SPV_KHR_8bit_storage], [StorageBuffer8BitAccess]>;
defm StoragePushConstant8 : CapabilityOperand<4450, 0, 0, [SPV_KHR_8bit_storage], []>;
defm DenormPreserve : CapabilityOperand<4464, 0x10400, 0, [SPV_KHR_float_controls], []>;
defm DenormFlushToZero : CapabilityOperand<4465, 0x10400, 0, [SPV_KHR_float_controls], []>;
defm SignedZeroInfNanPreserve : CapabilityOperand<4466, 0x10400, 0, [SPV_KHR_float_controls], []>;
defm RoundingModeRTE : CapabilityOperand<4467, 0x10400, 0, [SPV_KHR_float_controls], []>;
defm RoundingModeRTZ : CapabilityOperand<4468, 0x10400, 0, [SPV_KHR_float_controls], []>;
defm Float16ImageAMD : CapabilityOperand<5008, 0, 0, [], [Shader]>;
defm ImageGatherBiasLodAMD : CapabilityOperand<5009, 0, 0, [], [Shader]>;
defm FragmentMaskAMD : CapabilityOperand<5010, 0, 0, [], [Shader]>;
defm StencilExportEXT : CapabilityOperand<5013, 0, 0, [], [Shader]>;
defm ImageReadWriteLodAMD : CapabilityOperand<5015, 0, 0, [], [Shader]>;
defm ShaderClockKHR : CapabilityOperand<5055, 0, 0, [SPV_KHR_shader_clock], []>;
defm SampleMaskOverrideCoverageNV : CapabilityOperand<5249, 0, 0, [], [SampleRateShading]>;
defm GeometryShaderPassthroughNV : CapabilityOperand<5251, 0, 0, [], [Geometry]>;
defm ShaderViewportIndexLayerEXT : CapabilityOperand<5254, 0, 0, [], [MultiViewport]>;
defm ShaderViewportMaskNV : CapabilityOperand<5255, 0, 0, [], [ShaderViewportIndexLayerEXT]>;
defm ShaderStereoViewNV : CapabilityOperand<5259, 0, 0, [], [ShaderViewportMaskNV]>;
defm PerViewAttributesNV : CapabilityOperand<5260, 0, 0, [], [MultiView]>;
defm FragmentFullyCoveredEXT : CapabilityOperand<5265, 0, 0, [], [Shader]>;
defm MeshShadingNV : CapabilityOperand<5266, 0, 0, [], [Shader]>;
defm ShaderNonUniformEXT : CapabilityOperand<5301, 0, 0, [], [Shader]>;
defm RuntimeDescriptorArrayEXT : CapabilityOperand<5302, 0, 0, [], [Shader]>;
defm InputAttachmentArrayDynamicIndexingEXT : CapabilityOperand<5303, 0, 0, [], [InputAttachment]>;
defm UniformTexelBufferArrayDynamicIndexingEXT : CapabilityOperand<5304, 0, 0, [], [SampledBuffer]>;
defm StorageTexelBufferArrayDynamicIndexingEXT : CapabilityOperand<5305, 0, 0, [], [ImageBuffer]>;
defm UniformBufferArrayNonUniformIndexingEXT : CapabilityOperand<5306, 0, 0, [], [ShaderNonUniformEXT]>;
defm SampledImageArrayNonUniformIndexingEXT : CapabilityOperand<5307, 0, 0, [], [ShaderNonUniformEXT]>;
defm StorageBufferArrayNonUniformIndexingEXT : CapabilityOperand<5308, 0, 0, [], [ShaderNonUniformEXT]>;
defm StorageImageArrayNonUniformIndexingEXT : CapabilityOperand<5309, 0, 0, [], [ShaderNonUniformEXT]>;
defm InputAttachmentArrayNonUniformIndexingEXT : CapabilityOperand<5310, 0, 0, [], [InputAttachment, ShaderNonUniformEXT]>;
defm UniformTexelBufferArrayNonUniformIndexingEXT : CapabilityOperand<5311, 0, 0, [], [SampledBuffer, ShaderNonUniformEXT]>;
defm StorageTexelBufferArrayNonUniformIndexingEXT : CapabilityOperand<5312, 0, 0, [], [ImageBuffer, ShaderNonUniformEXT]>;
defm RayTracingNV : CapabilityOperand<5340, 0, 0, [], [Shader]>;
defm SubgroupShuffleINTEL : CapabilityOperand<5568, 0, 0, [SPV_INTEL_subgroups], []>;
defm SubgroupBufferBlockIOINTEL : CapabilityOperand<5569, 0, 0, [SPV_INTEL_subgroups], []>;
defm SubgroupImageBlockIOINTEL : CapabilityOperand<5570, 0, 0, [SPV_INTEL_subgroups], []>;
defm SubgroupImageMediaBlockIOINTEL : CapabilityOperand<5579, 0, 0, [], []>;
defm SubgroupAvcMotionEstimationINTEL : CapabilityOperand<5696, 0, 0, [], []>;
defm SubgroupAvcMotionEstimationIntraINTEL : CapabilityOperand<5697, 0, 0, [], []>;
defm SubgroupAvcMotionEstimationChromaINTEL : CapabilityOperand<5698, 0, 0, [], []>;
defm GroupNonUniformPartitionedNV : CapabilityOperand<5297, 0, 0, [], []>;
defm VulkanMemoryModelKHR : CapabilityOperand<5345, 0, 0, [], []>;
defm VulkanMemoryModelDeviceScopeKHR : CapabilityOperand<5346, 0, 0, [], []>;
defm ImageFootprintNV : CapabilityOperand<5282, 0, 0, [], []>;
defm FragmentBarycentricNV : CapabilityOperand<5284, 0, 0, [], []>;
defm ComputeDerivativeGroupQuadsNV : CapabilityOperand<5288, 0, 0, [], []>;
defm ComputeDerivativeGroupLinearNV : CapabilityOperand<5350, 0, 0, [], []>;
defm FragmentDensityEXT : CapabilityOperand<5291, 0, 0, [], [Shader]>;
defm PhysicalStorageBufferAddressesEXT : CapabilityOperand<5347, 0, 0, [], [Shader]>;
defm CooperativeMatrixNV : CapabilityOperand<5357, 0, 0, [], [Shader]>;
defm ArbitraryPrecisionIntegersINTEL : CapabilityOperand<5844, 0, 0, [SPV_INTEL_arbitrary_precision_integers], [Int8, Int16]>;
defm OptNoneINTEL : CapabilityOperand<6094, 0, 0, [SPV_INTEL_optnone], []>;
defm BitInstructions : CapabilityOperand<6025, 0, 0, [SPV_KHR_bit_instructions], []>;
defm ExpectAssumeKHR : CapabilityOperand<5629, 0, 0, [SPV_KHR_expect_assume], []>;
defm FunctionPointersINTEL : CapabilityOperand<5603, 0, 0, [SPV_INTEL_function_pointers], []>;
defm IndirectReferencesINTEL : CapabilityOperand<5604, 0, 0, [SPV_INTEL_function_pointers], []>;
defm AsmINTEL : CapabilityOperand<5606, 0, 0, [SPV_INTEL_inline_assembly], []>;
defm GroupNonUniformRotateKHR : CapabilityOperand<6026, 0, 0, [SPV_KHR_subgroup_rotate], [GroupNonUniform]>;
defm AtomicFloat32AddEXT : CapabilityOperand<6033, 0, 0, [SPV_EXT_shader_atomic_float_add], []>;
defm AtomicFloat64AddEXT : CapabilityOperand<6034, 0, 0, [SPV_EXT_shader_atomic_float_add], []>;
defm AtomicFloat16AddEXT : CapabilityOperand<6095, 0, 0, [SPV_EXT_shader_atomic_float16_add], []>;
defm AtomicFloat16MinMaxEXT : CapabilityOperand<5616, 0, 0, [SPV_EXT_shader_atomic_float_min_max], []>;
defm AtomicFloat32MinMaxEXT : CapabilityOperand<5612, 0, 0, [SPV_EXT_shader_atomic_float_min_max], []>;
defm AtomicFloat64MinMaxEXT : CapabilityOperand<5613, 0, 0, [SPV_EXT_shader_atomic_float_min_max], []>;
defm VariableLengthArrayINTEL : CapabilityOperand<5817, 0, 0, [SPV_INTEL_variable_length_array], []>;
defm GroupUniformArithmeticKHR : CapabilityOperand<6400, 0, 0, [SPV_KHR_uniform_group_instructions], []>;
defm USMStorageClassesINTEL : CapabilityOperand<5935, 0, 0, [SPV_INTEL_usm_storage_classes], [Kernel]>;
defm BFloat16ConversionINTEL : CapabilityOperand<6115, 0, 0, [SPV_INTEL_bfloat16_conversion], []>;
defm GlobalVariableHostAccessINTEL : CapabilityOperand<6187, 0, 0, [SPV_INTEL_global_variable_host_access], []>;
defm HostAccessINTEL : CapabilityOperand<6188, 0, 0, [SPV_INTEL_global_variable_host_access], []>;
defm GlobalVariableFPGADecorationsINTEL : CapabilityOperand<6189, 0, 0, [SPV_INTEL_global_variable_fpga_decorations], []>;
defm CacheControlsINTEL : CapabilityOperand<6441, 0, 0, [SPV_INTEL_cache_controls], []>;
defm CooperativeMatrixKHR : CapabilityOperand<6022, 0, 0, [SPV_KHR_cooperative_matrix], []>;

//===----------------------------------------------------------------------===//
// Multiclass used to define SourceLanguage enum values and at the same time
// SymbolicOperand entries.
//===----------------------------------------------------------------------===//

def SourceLanguage : GenericEnum, Operand<i32> {
  let FilterClass = "SourceLanguage";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class SourceLanguage<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass SourceLanguageOperand<bits<32> value> {
  def : SourceLanguage<NAME, value>;
  defm : SymbolicOperandWithRequirements<SourceLanguageOperand, value, NAME, 0, 0, [], []>;
}

defm Unknown : SourceLanguageOperand<0>;
defm ESSL : SourceLanguageOperand<1>;
defm GLSL : SourceLanguageOperand<2>;
defm OpenCL_C : SourceLanguageOperand<3>;
defm OpenCL_CPP : SourceLanguageOperand<4>;
defm HLSL : SourceLanguageOperand<5>;

//===----------------------------------------------------------------------===//
// Multiclass used to define AddressingModel enum values and at the same time
// SymbolicOperand entries with string mnemonics, and capabilities.
//===----------------------------------------------------------------------===//

def AddressingModel : GenericEnum, Operand<i32> {
  let FilterClass = "AddressingModel";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class AddressingModel<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass AddressingModelOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : AddressingModel<NAME, value>;
  defm : SymbolicOperandWithRequirements<AddressingModelOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm Logical : AddressingModelOperand<0, []>;
defm Physical32 : AddressingModelOperand<1, [Addresses]>;
defm Physical64 : AddressingModelOperand<2, [Addresses]>;
defm PhysicalStorageBuffer64EXT : AddressingModelOperand<5348, [PhysicalStorageBufferAddressesEXT]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define ExecutionModel enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def ExecutionModel : GenericEnum, Operand<i32> {
  let FilterClass = "ExecutionModel";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class ExecutionModel<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass ExecutionModelOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : ExecutionModel<NAME, value>;
  defm : SymbolicOperandWithRequirements<ExecutionModelOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm Vertex : ExecutionModelOperand<0, [Shader]>;
defm TessellationControl: ExecutionModelOperand<1, [Tessellation]>;
defm TessellationEvaluation: ExecutionModelOperand<2, [Tessellation]>;
defm Geometry: ExecutionModelOperand<3, [Geometry]>;
defm Fragment: ExecutionModelOperand<4, [Shader]>;
defm GLCompute: ExecutionModelOperand<5, [Shader]>;
defm Kernel: ExecutionModelOperand<6, [Kernel]>;
defm TaskNV: ExecutionModelOperand<5267, [MeshShadingNV]>;
defm MeshNV: ExecutionModelOperand<5268, [MeshShadingNV]>;
defm RayGenerationNV: ExecutionModelOperand<5313, [RayTracingNV]>;
defm IntersectionNV: ExecutionModelOperand<5314, [RayTracingNV]>;
defm AnyHitNV: ExecutionModelOperand<5315, [RayTracingNV]>;
defm ClosestHitNV: ExecutionModelOperand<5316, [RayTracingNV]>;
defm MissNV: ExecutionModelOperand<5317, [RayTracingNV]>;
defm CallableNV: ExecutionModelOperand<5318, [RayTracingNV]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define MemoryModel enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def MemoryModel : GenericEnum, Operand<i32> {
  let FilterClass = "MemoryModel";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class MemoryModel<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass MemoryModelOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : MemoryModel<NAME, value>;
  defm : SymbolicOperandWithRequirements<MemoryModelOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm Simple : MemoryModelOperand<0, [Shader]>;
defm GLSL450 : MemoryModelOperand<1, [Shader]>;
defm OpenCL : MemoryModelOperand<2, [Kernel]>;
defm VulkanKHR : MemoryModelOperand<3, [VulkanMemoryModelKHR]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define ExecutionMode enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def ExecutionMode : GenericEnum, Operand<i32> {
  let FilterClass = "ExecutionMode";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class ExecutionMode<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass ExecutionModeOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : ExecutionMode<NAME, value>;
  defm : SymbolicOperandWithRequirements<ExecutionModeOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm Invocations : ExecutionModeOperand<0, [Geometry]>;
defm SpacingEqual : ExecutionModeOperand<1, [Tessellation]>;
defm SpacingFractionalEven : ExecutionModeOperand<2, [Tessellation]>;
defm SpacingFractionalOdd : ExecutionModeOperand<3, [Tessellation]>;
defm VertexOrderCw : ExecutionModeOperand<4, [Tessellation]>;
defm VertexOrderCcw : ExecutionModeOperand<5, [Tessellation]>;
defm PixelCenterInteger : ExecutionModeOperand<6, [Shader]>;
defm OriginUpperLeft : ExecutionModeOperand<7, [Shader]>;
defm OriginLowerLeft : ExecutionModeOperand<8, [Shader]>;
defm EarlyFragmentTests : ExecutionModeOperand<9, [Shader]>;
defm PointMode : ExecutionModeOperand<10, [Tessellation]>;
defm Xfb : ExecutionModeOperand<11, [TransformFeedback]>;
defm DepthReplacing : ExecutionModeOperand<12, [Shader]>;
defm DepthGreater : ExecutionModeOperand<14, [Shader]>;
defm DepthLess : ExecutionModeOperand<15, [Shader]>;
defm DepthUnchanged : ExecutionModeOperand<16, [Shader]>;
defm LocalSize : ExecutionModeOperand<17, []>;
defm LocalSizeHint : ExecutionModeOperand<18, [Kernel]>;
defm InputPoints : ExecutionModeOperand<19, [Geometry]>;
defm InputLines : ExecutionModeOperand<20, [Geometry]>;
defm InputLinesAdjacency : ExecutionModeOperand<21, [Geometry]>;
defm Triangles : ExecutionModeOperand<22, [Geometry]>;
defm InputTrianglesAdjacency : ExecutionModeOperand<23, [Geometry]>;
defm Quads : ExecutionModeOperand<24, [Tessellation]>;
defm Isolines : ExecutionModeOperand<25, [Tessellation]>;
defm OutputVertices : ExecutionModeOperand<26, [Geometry]>;
defm OutputPoints : ExecutionModeOperand<27, [Geometry]>;
defm OutputLineStrip : ExecutionModeOperand<28, [Geometry]>;
defm OutputTriangleStrip : ExecutionModeOperand<29, [Geometry]>;
defm VecTypeHint : ExecutionModeOperand<30, [Kernel]>;
defm ContractionOff : ExecutionModeOperand<31, [Kernel]>;
defm Initializer : ExecutionModeOperand<33, [Kernel]>;
defm Finalizer : ExecutionModeOperand<34, [Kernel]>;
defm SubgroupSize : ExecutionModeOperand<35, [SubgroupDispatch]>;
defm SubgroupsPerWorkgroup : ExecutionModeOperand<36, [SubgroupDispatch]>;
defm SubgroupsPerWorkgroupId : ExecutionModeOperand<37, [SubgroupDispatch]>;
defm LocalSizeId : ExecutionModeOperand<38, []>;
defm LocalSizeHintId : ExecutionModeOperand<39, [Kernel]>;
defm PostDepthCoverage : ExecutionModeOperand<4446, [SampleMaskPostDepthCoverage]>;
defm DenormPreserve : ExecutionModeOperand<4459, [DenormPreserve]>;
defm DenormFlushToZero : ExecutionModeOperand<4460, [DenormFlushToZero]>;
defm SignedZeroInfNanPreserve : ExecutionModeOperand<4461, [SignedZeroInfNanPreserve]>;
defm RoundingModeRTE : ExecutionModeOperand<4462, [RoundingModeRTE]>;
defm RoundingModeRTZ : ExecutionModeOperand<4463, [RoundingModeRTZ]>;
defm StencilRefReplacingEXT : ExecutionModeOperand<5027, [StencilExportEXT]>;
defm OutputLinesNV : ExecutionModeOperand<5269, [MeshShadingNV]>;
defm DerivativeGroupQuadsNV : ExecutionModeOperand<5289, [ComputeDerivativeGroupQuadsNV]>;
defm DerivativeGroupLinearNV : ExecutionModeOperand<5290, [ComputeDerivativeGroupLinearNV]>;
defm OutputTrianglesNV : ExecutionModeOperand<5298, [MeshShadingNV]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define StorageClass enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def StorageClass : GenericEnum, Operand<i32> {
  let FilterClass = "StorageClass";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class StorageClass<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass StorageClassOperand<bits<32> value, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
  def : StorageClass<NAME, value>;
  defm : SymbolicOperandWithRequirements<StorageClassOperand, value, NAME, 0, 0, reqExtensions, reqCapabilities>;
}

defm UniformConstant : StorageClassOperand<0, [], []>;
defm Input : StorageClassOperand<1, [], []>;
defm Uniform : StorageClassOperand<2, [], [Shader]>;
defm Output : StorageClassOperand<3, [], [Shader]>;
defm Workgroup : StorageClassOperand<4, [], []>;
defm CrossWorkgroup : StorageClassOperand<5, [], []>;
defm Private : StorageClassOperand<6, [], [Shader]>;
defm Function : StorageClassOperand<7, [], []>;
defm Generic : StorageClassOperand<8, [], [GenericPointer]>;
defm PushConstant : StorageClassOperand<9, [], [Shader]>;
defm AtomicCounter : StorageClassOperand<10, [], [AtomicStorage]>;
defm Image : StorageClassOperand<11, [], []>;
defm StorageBuffer : StorageClassOperand<12, [], [Shader]>;
defm CallableDataNV : StorageClassOperand<5328, [], [RayTracingNV]>;
defm IncomingCallableDataNV : StorageClassOperand<5329, [], [RayTracingNV]>;
defm RayPayloadNV : StorageClassOperand<5338, [], [RayTracingNV]>;
defm HitAttributeNV : StorageClassOperand<5339, [], [RayTracingNV]>;
defm IncomingRayPayloadNV : StorageClassOperand<5342, [], [RayTracingNV]>;
defm ShaderRecordBufferNV : StorageClassOperand<5343, [], [RayTracingNV]>;
defm PhysicalStorageBufferEXT : StorageClassOperand<5349, [], [PhysicalStorageBufferAddressesEXT]>;
defm CodeSectionINTEL : StorageClassOperand<5605, [SPV_INTEL_function_pointers], [FunctionPointersINTEL]>;
defm DeviceOnlyINTEL : StorageClassOperand<5936, [SPV_INTEL_usm_storage_classes], [USMStorageClassesINTEL]>;
defm HostOnlyINTEL : StorageClassOperand<5937, [SPV_INTEL_usm_storage_classes], [USMStorageClassesINTEL]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define Dim enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def Dim : GenericEnum, Operand<i32> {
  let FilterClass = "Dim";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class Dim<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass DimOperand<bits<32> value, string mnemonic, list<Capability> reqCapabilities> {
  def NAME : Dim<NAME, value>;
  defm : SymbolicOperandWithRequirements<DimOperand, value, mnemonic, 0, 0, [], reqCapabilities>;
}

defm DIM_1D : DimOperand<0, "1D", [Sampled1D, Image1D]>;
defm DIM_2D : DimOperand<1, "2D", [Shader, Kernel, ImageMSArray]>;
defm DIM_3D : DimOperand<2, "3D", []>;
defm DIM_Cube : DimOperand<3, "Cube", [Shader, ImageCubeArray]>;
defm DIM_Rect : DimOperand<4, "Rect", [SampledRect, ImageRect]>;
defm DIM_Buffer : DimOperand<5, "Buffer", [SampledBuffer, ImageBuffer]>;
defm DIM_SubpassData : DimOperand<6, "SubpassData", [InputAttachment]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define SamplerAddressingMode enum values and at the same
// time SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def SamplerAddressingMode : GenericEnum, Operand<i32> {
  let FilterClass = "SamplerAddressingMode";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class SamplerAddressingMode<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass SamplerAddressingModeOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : SamplerAddressingMode<NAME, value>;
  defm : SymbolicOperandWithRequirements<SamplerAddressingModeOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm None : SamplerAddressingModeOperand<0, [Kernel]>;
defm ClampToEdge : SamplerAddressingModeOperand<1, [Kernel]>;
defm Clamp : SamplerAddressingModeOperand<2, [Kernel]>;
defm Repeat : SamplerAddressingModeOperand<3, [Kernel]>;
defm RepeatMirrored : SamplerAddressingModeOperand<4, [Kernel]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define SamplerFilterMode enum values and at the same
// time SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def SamplerFilterMode : GenericEnum, Operand<i32> {
  let FilterClass = "SamplerFilterMode";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class SamplerFilterMode<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass SamplerFilterModeOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : SamplerFilterMode<NAME, value>;
  defm : SymbolicOperandWithRequirements<SamplerFilterModeOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm Nearest : SamplerFilterModeOperand<0, [Kernel]>;
defm Linear : SamplerFilterModeOperand<1, [Kernel]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define ImageFormat enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def ImageFormat : GenericEnum, Operand<i32> {
  let FilterClass = "ImageFormat";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class ImageFormat<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass ImageFormatOperand<bits<32> value, list<Capability> reqCapabilities> {
  def NAME : ImageFormat<NAME, value>;
  defm : SymbolicOperandWithRequirements<ImageFormatOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm Unknown : ImageFormatOperand<0, []>;
defm Rgba32f : ImageFormatOperand<1, [Shader]>;
defm Rgba16f : ImageFormatOperand<2, [Shader]>;
defm R32f : ImageFormatOperand<3, [Shader]>;
defm Rgba8 : ImageFormatOperand<4, [Shader]>;
defm Rgba8Snorm : ImageFormatOperand<5, [Shader]>;
defm Rg32f : ImageFormatOperand<6, [StorageImageExtendedFormats]>;
defm Rg16f : ImageFormatOperand<7, [StorageImageExtendedFormats]>;
defm R11fG11fB10f : ImageFormatOperand<8, [StorageImageExtendedFormats]>;
defm R16f : ImageFormatOperand<9, [StorageImageExtendedFormats]>;
defm Rgba16 : ImageFormatOperand<10, [StorageImageExtendedFormats]>;
defm Rgb10A2 : ImageFormatOperand<11, [StorageImageExtendedFormats]>;
defm Rg16 : ImageFormatOperand<12, [StorageImageExtendedFormats]>;
defm Rg8 : ImageFormatOperand<13, [StorageImageExtendedFormats]>;
defm R16 : ImageFormatOperand<14, [StorageImageExtendedFormats]>;
defm R8 : ImageFormatOperand<15, [StorageImageExtendedFormats]>;
defm Rgba16Snorm : ImageFormatOperand<16, [StorageImageExtendedFormats]>;
defm Rg16Snorm : ImageFormatOperand<17, [StorageImageExtendedFormats]>;
defm Rg8Snorm : ImageFormatOperand<18, [StorageImageExtendedFormats]>;
defm R16Snorm : ImageFormatOperand<19, [StorageImageExtendedFormats]>;
defm R8Snorm : ImageFormatOperand<20, [StorageImageExtendedFormats]>;
defm Rgba32i : ImageFormatOperand<21, [Shader]>;
defm Rgba16i : ImageFormatOperand<22, [Shader]>;
defm Rgba8i : ImageFormatOperand<23, [Shader]>;
defm R32i : ImageFormatOperand<24, [Shader]>;
defm Rg32i : ImageFormatOperand<25, [StorageImageExtendedFormats]>;
defm Rg16i : ImageFormatOperand<26, [StorageImageExtendedFormats]>;
defm Rg8i : ImageFormatOperand<27, [StorageImageExtendedFormats]>;
defm R16i : ImageFormatOperand<28, [StorageImageExtendedFormats]>;
defm R8i : ImageFormatOperand<29, [StorageImageExtendedFormats]>;
defm Rgba32ui : ImageFormatOperand<30, [Shader]>;
defm Rgba16ui : ImageFormatOperand<31, [Shader]>;
defm Rgba8ui : ImageFormatOperand<32, [Shader]>;
defm R32ui : ImageFormatOperand<33, [Shader]>;
defm Rgb10a2ui : ImageFormatOperand<34, [StorageImageExtendedFormats]>;
defm Rg32ui : ImageFormatOperand<35, [StorageImageExtendedFormats]>;
defm Rg16ui : ImageFormatOperand<36, [StorageImageExtendedFormats]>;
defm Rg8ui : ImageFormatOperand<37, [StorageImageExtendedFormats]>;
defm R16ui : ImageFormatOperand<38, [StorageImageExtendedFormats]>;
defm R8ui : ImageFormatOperand<39, [StorageImageExtendedFormats]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define ImageChannelOrder enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def ImageChannelOrder : GenericEnum, Operand<i32> {
  let FilterClass = "ImageChannelOrder";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class ImageChannelOrder<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass ImageChannelOrderOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : ImageChannelOrder<NAME, value>;
  defm : SymbolicOperandWithRequirements<ImageChannelOrderOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm R : ImageChannelOrderOperand<0, [Kernel]>;
defm A : ImageChannelOrderOperand<1, [Kernel]>;
defm RG : ImageChannelOrderOperand<2, [Kernel]>;
defm RA : ImageChannelOrderOperand<3, [Kernel]>;
defm RGB : ImageChannelOrderOperand<4, [Kernel]>;
defm RGBA : ImageChannelOrderOperand<5, [Kernel]>;
defm BGRA : ImageChannelOrderOperand<6, [Kernel]>;
defm ARGB : ImageChannelOrderOperand<7, [Kernel]>;
defm Intensity : ImageChannelOrderOperand<8, [Kernel]>;
defm Luminance : ImageChannelOrderOperand<9, [Kernel]>;
defm Rx : ImageChannelOrderOperand<10, [Kernel]>;
defm RGx : ImageChannelOrderOperand<11, [Kernel]>;
defm RGBx : ImageChannelOrderOperand<12, [Kernel]>;
defm Depth : ImageChannelOrderOperand<13, [Kernel]>;
defm DepthStencil : ImageChannelOrderOperand<14, [Kernel]>;
defm sRGB : ImageChannelOrderOperand<15, [Kernel]>;
defm sRGBx : ImageChannelOrderOperand<16, [Kernel]>;
defm sRGBA : ImageChannelOrderOperand<17, [Kernel]>;
defm sBGRA : ImageChannelOrderOperand<18, [Kernel]>;
defm ABGR : ImageChannelOrderOperand<19, [Kernel]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define ImageChannelDataType enum values and at the same
// time SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def ImageChannelDataType : GenericEnum, Operand<i32> {
  let FilterClass = "ImageChannelDataType";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class ImageChannelDataType<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass ImageChannelDataTypeOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : ImageChannelDataType<NAME, value>;
  defm : SymbolicOperandWithRequirements<ImageChannelDataTypeOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm SnormInt8 : ImageChannelDataTypeOperand<0, []>;
defm SnormInt16 : ImageChannelDataTypeOperand<1, []>;
defm UnormInt8 : ImageChannelDataTypeOperand<2, [Kernel]>;
defm UnormInt16 : ImageChannelDataTypeOperand<3, [Kernel]>;
defm UnormShort565 : ImageChannelDataTypeOperand<4, [Kernel]>;
defm UnormShort555 : ImageChannelDataTypeOperand<5, [Kernel]>;
defm UnormInt101010 : ImageChannelDataTypeOperand<6, [Kernel]>;
defm SignedInt8 : ImageChannelDataTypeOperand<7, [Kernel]>;
defm SignedInt16 : ImageChannelDataTypeOperand<8, [Kernel]>;
defm SignedInt32 : ImageChannelDataTypeOperand<9, [Kernel]>;
defm UnsignedInt8 : ImageChannelDataTypeOperand<10, [Kernel]>;
defm UnsignedInt16 : ImageChannelDataTypeOperand<11, [Kernel]>;
defm UnsigendInt32 : ImageChannelDataTypeOperand<12, [Kernel]>;
defm HalfFloat : ImageChannelDataTypeOperand<13, [Kernel]>;
defm Float : ImageChannelDataTypeOperand<14, [Kernel]>;
defm UnormInt24 : ImageChannelDataTypeOperand<15, [Kernel]>;
defm UnormInt101010_2 : ImageChannelDataTypeOperand<16, [Kernel]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define ImageOperand enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def ImageOperand : GenericEnum, Operand<i32> {
  let FilterClass = "ImageOperand";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class ImageOperand<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass ImageOperandOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : ImageOperand<NAME, value>;
  defm : SymbolicOperandWithRequirements<ImageOperandOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm None : ImageOperandOperand<0x0, []>;
defm Bias : ImageOperandOperand<0x1, [Shader]>;
defm Lod : ImageOperandOperand<0x2, []>;
defm Grad : ImageOperandOperand<0x4, []>;
defm ConstOffset : ImageOperandOperand<0x8, []>;
defm Offset : ImageOperandOperand<0x10, [ImageGatherExtended]>;
defm ConstOffsets : ImageOperandOperand<0x20, [ImageGatherExtended]>;
defm Sample : ImageOperandOperand<0x40, []>;
defm MinLod : ImageOperandOperand<0x80, [MinLod]>;
defm MakeTexelAvailableKHR : ImageOperandOperand<0x100, [VulkanMemoryModelKHR]>;
defm MakeTexelVisibleKHR : ImageOperandOperand<0x200, [VulkanMemoryModelKHR]>;
defm NonPrivateTexelKHR : ImageOperandOperand<0x400, [VulkanMemoryModelKHR]>;
defm VolatileTexelKHR : ImageOperandOperand<0x800, [VulkanMemoryModelKHR]>;
defm SignExtend : ImageOperandOperand<0x1000, []>;
defm ZeroExtend : ImageOperandOperand<0x2000, []>;

//===----------------------------------------------------------------------===//
// Multiclass used to define FPFastMathMode enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def FPFastMathMode : GenericEnum, Operand<i32> {
  let FilterClass = "FPFastMathMode";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class FPFastMathMode<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass FPFastMathModeOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : FPFastMathMode<NAME, value>;
  defm : SymbolicOperandWithRequirements<FPFastMathModeOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm None : FPFastMathModeOperand<0x0, []>;
defm NotNaN : FPFastMathModeOperand<0x1, [Kernel]>;
defm NotInf : FPFastMathModeOperand<0x2, [Kernel]>;
defm NSZ : FPFastMathModeOperand<0x4, [Kernel]>;
defm AllowRecip : FPFastMathModeOperand<0x8, [Kernel]>;
defm Fast : FPFastMathModeOperand<0x10, [Kernel]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define FPRoundingMode enum values and at the same time
// SymbolicOperand entries with string mnemonics.
//===----------------------------------------------------------------------===//

def FPRoundingMode : GenericEnum, Operand<i32> {
  let FilterClass = "FPRoundingMode";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class FPRoundingMode<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass FPRoundingModeOperand<bits<32> value> {
  def NAME : FPRoundingMode<NAME, value>;
  defm : SymbolicOperandWithRequirements<FPRoundingModeOperand, value, NAME, 0, 0, [], []>;
}

defm RTE : FPRoundingModeOperand<0>;
defm RTZ : FPRoundingModeOperand<1>;
defm RTP : FPRoundingModeOperand<2>;
defm RTN : FPRoundingModeOperand<3>;

//===----------------------------------------------------------------------===//
// Multiclass used to define LinkageType enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def LinkageType : GenericEnum, Operand<i32> {
  let FilterClass = "LinkageType";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class LinkageType<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass LinkageTypeOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : LinkageType<NAME, value>;
  defm : SymbolicOperandWithRequirements<LinkageTypeOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm Export : LinkageTypeOperand<0, [Linkage]>;
defm Import : LinkageTypeOperand<1, [Linkage]>;
defm LinkOnceODR : LinkageTypeOperand<2, [Linkage]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define AccessQualifier enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def AccessQualifier : GenericEnum, Operand<i32> {
  let FilterClass = "AccessQualifier";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class AccessQualifier<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass AccessQualifierOperand<bits<32> value, list<Capability> reqCapabilities> {
  def NAME : AccessQualifier<NAME, value>;
  defm : SymbolicOperandWithRequirements<AccessQualifierOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm ReadOnly : AccessQualifierOperand<0, [Kernel]>;
defm WriteOnly : AccessQualifierOperand<1, [Kernel]>;
defm ReadWrite : AccessQualifierOperand<2, [Kernel]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define FunctionParameterAttribute enum values and at the
// same time SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def FunctionParameterAttribute : GenericEnum, Operand<i32> {
  let FilterClass = "FunctionParameterAttribute";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class FunctionParameterAttribute<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass FunctionParameterAttributeOperand<bits<32> value, list<Capability> reqCapabilities> {
  def : FunctionParameterAttribute<NAME, value>;
  defm : SymbolicOperandWithRequirements<FunctionParameterAttributeOperand, value, NAME, 0, 0, [], reqCapabilities>;
}

defm Zext : FunctionParameterAttributeOperand<0, [Kernel]>;
defm Sext : FunctionParameterAttributeOperand<1, [Kernel]>;
defm ByVal : FunctionParameterAttributeOperand<2, [Kernel]>;
defm Sret : FunctionParameterAttributeOperand<3, [Kernel]>;
defm NoAlias : FunctionParameterAttributeOperand<4, [Kernel]>;
defm NoCapture : FunctionParameterAttributeOperand<5, [Kernel]>;
defm NoWrite : FunctionParameterAttributeOperand<6, [Kernel]>;
defm NoReadWrite : FunctionParameterAttributeOperand<7, [Kernel]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define Decoration enum values and at the same time
// SymbolicOperand entries with string mnemonics, versioning, extensions and
// capabilities.
//===----------------------------------------------------------------------===//

def Decoration : GenericEnum, Operand<i32> {
  let FilterClass = "Decoration";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class Decoration<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass DecorationOperand<bits<32> value, bits<32> minVersion, bits<32> maxVersion, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
  def : Decoration<NAME, value>;
  defm : SymbolicOperandWithRequirements<DecorationOperand, value, NAME, minVersion, maxVersion, reqExtensions, reqCapabilities>;
}

defm RelaxedPrecision : DecorationOperand<0, 0, 0, [], [Shader]>;
defm SpecId : DecorationOperand<1, 0, 0, [], [Shader, Kernel]>;
defm Block : DecorationOperand<2, 0, 0, [], [Shader]>;
defm BufferBlock : DecorationOperand<3, 0, 0, [], [Shader]>;
defm RowMajor : DecorationOperand<4, 0, 0, [], [Matrix]>;
defm ColMajor : DecorationOperand<5, 0, 0, [], [Matrix]>;
defm ArrayStride : DecorationOperand<6, 0, 0, [], [Shader]>;
defm MatrixStride : DecorationOperand<7, 0, 0, [], [Matrix]>;
defm GLSLShared : DecorationOperand<8, 0, 0, [], [Shader]>;
defm GLSLPacked : DecorationOperand<9, 0, 0, [], [Shader]>;
defm CPacked : DecorationOperand<10, 0, 0, [], [Kernel]>;
defm BuiltIn : DecorationOperand<11, 0, 0, [], []>;
defm NoPerspective : DecorationOperand<13, 0, 0, [], [Shader]>;
defm Flat : DecorationOperand<14, 0, 0, [], [Shader]>;
defm Patch : DecorationOperand<15, 0, 0, [], [Tessellation]>;
defm Centroid : DecorationOperand<16, 0, 0, [], [Shader]>;
defm Sample : DecorationOperand<17, 0, 0, [], [SampleRateShading]>;
defm Invariant : DecorationOperand<18, 0, 0, [], [Shader]>;
defm Restrict : DecorationOperand<19, 0, 0, [], []>;
defm Aliased : DecorationOperand<20, 0, 0, [], []>;
defm Volatile : DecorationOperand<21, 0, 0, [], []>;
defm Constant : DecorationOperand<22, 0, 0, [], [Kernel]>;
defm Coherent : DecorationOperand<23, 0, 0, [], []>;
defm NonWritable : DecorationOperand<24, 0, 0, [], []>;
defm NonReadable : DecorationOperand<25, 0, 0, [], []>;
defm Uniform : DecorationOperand<26, 0, 0, [], [Shader]>;
defm UniformId : DecorationOperand<27, 0, 0, [], [Shader]>;
defm SaturatedConversion : DecorationOperand<28, 0, 0, [], [Kernel]>;
defm Stream : DecorationOperand<29, 0, 0, [], [GeometryStreams]>;
defm Location : DecorationOperand<30, 0, 0, [], [Shader]>;
defm Component : DecorationOperand<31, 0, 0, [], [Shader]>;
defm Index : DecorationOperand<32, 0, 0, [], [Shader]>;
defm Binding : DecorationOperand<33, 0, 0, [], [Shader]>;
defm DescriptorSet : DecorationOperand<34, 0, 0, [], [Shader]>;
defm Offset : DecorationOperand<35, 0, 0, [], [Shader]>;
defm XfbBuffer : DecorationOperand<36, 0, 0, [], [TransformFeedback]>;
defm XfbStride : DecorationOperand<37, 0, 0, [], [TransformFeedback]>;
defm FuncParamAttr : DecorationOperand<38, 0, 0, [], [Kernel]>;
defm FPRoundingMode : DecorationOperand<39, 0, 0, [], []>;
defm FPFastMathMode : DecorationOperand<40, 0, 0, [], [Kernel]>;
defm LinkageAttributes : DecorationOperand<41, 0, 0, [], [Linkage]>;
defm NoContraction : DecorationOperand<42, 0, 0, [], [Shader]>;
defm InputAttachmentIndex : DecorationOperand<43, 0, 0, [], [InputAttachment]>;
defm Alignment : DecorationOperand<44, 0, 0, [], [Kernel]>;
defm MaxByteOffset : DecorationOperand<45, 0, 0, [], [Addresses]>;
defm AlignmentId : DecorationOperand<46, 0, 0, [], [Kernel]>;
defm MaxByteOffsetId : DecorationOperand<47, 0, 0, [], [Addresses]>;
defm NoSignedWrap : DecorationOperand<4469, 0x10400, 0, [SPV_KHR_no_integer_wrap_decoration], []>;
defm NoUnsignedWrap : DecorationOperand<4470, 0x10400, 0, [SPV_KHR_no_integer_wrap_decoration], []>;
defm ExplicitInterpAMD : DecorationOperand<4999, 0, 0, [], []>;
defm OverrideCoverageNV : DecorationOperand<5248, 0, 0, [], [SampleMaskOverrideCoverageNV]>;
defm PassthroughNV : DecorationOperand<5250, 0, 0, [], [GeometryShaderPassthroughNV]>;
defm ViewportRelativeNV : DecorationOperand<5252, 0, 0, [], [ShaderViewportMaskNV]>;
defm SecondaryViewportRelativeNV : DecorationOperand<5256, 0, 0, [], [ShaderStereoViewNV]>;
defm PerPrimitiveNV : DecorationOperand<5271, 0, 0, [], [MeshShadingNV]>;
defm PerViewNV : DecorationOperand<5272, 0, 0, [], [MeshShadingNV]>;
defm PerVertexNV : DecorationOperand<5273, 0, 0, [], [FragmentBarycentricNV]>;
defm NonUniformEXT : DecorationOperand<5300, 0, 0, [], [ShaderNonUniformEXT]>;
defm CountBuffer : DecorationOperand<5634, 0, 0, [], []>;
defm UserSemantic : DecorationOperand<5635, 0, 0, [], []>;
defm RestrictPointerEXT : DecorationOperand<5355, 0, 0, [], [PhysicalStorageBufferAddressesEXT]>;
defm AliasedPointerEXT : DecorationOperand<5356, 0, 0, [], [PhysicalStorageBufferAddressesEXT]>;
defm ReferencedIndirectlyINTEL : DecorationOperand<5602, 0, 0, [], [IndirectReferencesINTEL]>;
defm ClobberINTEL : DecorationOperand<5607, 0, 0, [SPV_INTEL_inline_assembly], [AsmINTEL]>;
defm SideEffectsINTEL : DecorationOperand<5608, 0, 0, [SPV_INTEL_inline_assembly], [AsmINTEL]>;
defm ArgumentAttributeINTEL : DecorationOperand<6409, 0, 0, [], [FunctionPointersINTEL]>;
defm CacheControlLoadINTEL : DecorationOperand<6442, 0, 0, [], [CacheControlsINTEL]>;
defm CacheControlStoreINTEL : DecorationOperand<6443, 0, 0, [], [CacheControlsINTEL]>;
defm HostAccessINTEL : DecorationOperand<6188, 0, 0, [], [GlobalVariableHostAccessINTEL]>;
defm InitModeINTEL : DecorationOperand<6190, 0, 0, [], [GlobalVariableFPGADecorationsINTEL]>;
defm ImplementInRegisterMapINTEL : DecorationOperand<6191, 0, 0, [], [GlobalVariableFPGADecorationsINTEL]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define BuiltIn enum values and at the same time
// SymbolicOperand entries with string mnemonics, versioning, extensions and
// capabilities.
//===----------------------------------------------------------------------===//

def BuiltIn : GenericEnum, Operand<i32> {
  let FilterClass = "BuiltIn";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class BuiltIn<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass BuiltInOperand<bits<32> value, bits<32> minVersion, bits<32> maxVersion, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
  def NAME : BuiltIn<NAME, value>;
  defm : SymbolicOperandWithRequirements<BuiltInOperand, value, NAME, minVersion, maxVersion, reqExtensions, reqCapabilities>;
}

defm Position : BuiltInOperand<0, 0, 0, [], [Shader]>;
defm PointSize : BuiltInOperand<1, 0, 0, [], [Shader]>;
defm ClipDistanceVariable : BuiltInOperand<3, 0, 0, [], [ClipDistance]>;
defm CullDistanceVariable : BuiltInOperand<4, 0, 0, [], [CullDistance]>;
defm VertexId : BuiltInOperand<5, 0, 0, [], [Shader]>;
defm InstanceId : BuiltInOperand<6, 0, 0, [], [Shader]>;
defm PrimitiveId : BuiltInOperand<7, 0, 0, [], [Geometry, Tessellation, RayTracingNV]>;
defm InvocationId : BuiltInOperand<8, 0, 0, [], [Geometry, Tessellation]>;
defm Layer : BuiltInOperand<9, 0, 0, [], [Geometry]>;
defm ViewportIndex : BuiltInOperand<10, 0, 0, [], [MultiViewport]>;
defm TessLevelOuter : BuiltInOperand<11, 0, 0, [], [Tessellation]>;
defm TessLevelInner : BuiltInOperand<12, 0, 0, [], [Tessellation]>;
defm TessCoord : BuiltInOperand<13, 0, 0, [], [Tessellation]>;
defm PatchVertices : BuiltInOperand<14, 0, 0, [], [Tessellation]>;
defm FragCoord : BuiltInOperand<15, 0, 0, [], [Shader]>;
defm PointCoord : BuiltInOperand<16, 0, 0, [], [Shader]>;
defm FrontFacing : BuiltInOperand<17, 0, 0, [], [Shader]>;
defm SampleId : BuiltInOperand<18, 0, 0, [], [SampleRateShading]>;
defm SamplePosition : BuiltInOperand<19, 0, 0, [], [SampleRateShading]>;
defm SampleMask : BuiltInOperand<20, 0, 0, [], [Shader]>;
defm FragDepth : BuiltInOperand<22, 0, 0, [], [Shader]>;
defm HelperInvocation : BuiltInOperand<23, 0, 0, [], [Shader]>;
defm NumWorkgroups : BuiltInOperand<24, 0, 0, [], []>;
defm WorkgroupSize : BuiltInOperand<25, 0, 0, [], []>;
defm WorkgroupId : BuiltInOperand<26, 0, 0, [], []>;
defm LocalInvocationId : BuiltInOperand<27, 0, 0, [], []>;
defm GlobalInvocationId : BuiltInOperand<28, 0, 0, [], []>;
defm LocalInvocationIndex : BuiltInOperand<29, 0, 0, [], []>;
defm WorkDim : BuiltInOperand<30, 0, 0, [], [Kernel]>;
defm GlobalSize : BuiltInOperand<31, 0, 0, [], [Kernel]>;
defm EnqueuedWorkgroupSize : BuiltInOperand<32, 0, 0, [], [Kernel]>;
defm GlobalOffset : BuiltInOperand<33, 0, 0, [], [Kernel]>;
defm GlobalLinearId : BuiltInOperand<34, 0, 0, [], [Kernel]>;
defm SubgroupSize : BuiltInOperand<36, 0, 0, [], [Kernel, GroupNonUniform, SubgroupBallotKHR]>;
defm SubgroupMaxSize : BuiltInOperand<37, 0, 0, [], [Kernel]>;
defm NumSubgroups : BuiltInOperand<38, 0, 0, [], [Kernel, GroupNonUniform]>;
defm NumEnqueuedSubgroups : BuiltInOperand<39, 0, 0, [], [Kernel]>;
defm SubgroupId : BuiltInOperand<40, 0, 0, [], [Kernel, GroupNonUniform]>;
defm SubgroupLocalInvocationId : BuiltInOperand<41, 0, 0, [], [Kernel, GroupNonUniform, SubgroupBallotKHR]>;
defm VertexIndex : BuiltInOperand<42, 0, 0, [], [Shader]>;
defm InstanceIndex : BuiltInOperand<43, 0, 0, [], [Shader]>;
defm SubgroupEqMask : BuiltInOperand<4416, 0, 0, [], [SubgroupBallotKHR, GroupNonUniformBallot]>;
defm SubgroupGeMask : BuiltInOperand<4417, 0, 0, [], [SubgroupBallotKHR, GroupNonUniformBallot]>;
defm SubgroupGtMask : BuiltInOperand<4418, 0, 0, [], [SubgroupBallotKHR, GroupNonUniformBallot]>;
defm SubgroupLeMask : BuiltInOperand<4419, 0, 0, [], [SubgroupBallotKHR, GroupNonUniformBallot]>;
defm SubgroupLtMask : BuiltInOperand<4420, 0, 0, [], [SubgroupBallotKHR, GroupNonUniformBallot]>;
defm BaseVertex : BuiltInOperand<4424, 0, 0, [], [DrawParameters]>;
defm BaseInstance : BuiltInOperand<4425, 0, 0, [], [DrawParameters]>;
defm DrawIndex : BuiltInOperand<4426, 0, 0, [], [DrawParameters, MeshShadingNV]>;
defm DeviceIndex : BuiltInOperand<4438, 0, 0, [], [DeviceGroup]>;
defm ViewIndex : BuiltInOperand<4440, 0, 0, [], [MultiView]>;
defm BaryCoordNoPerspAMD : BuiltInOperand<4492, 0, 0, [], []>;
defm BaryCoordNoPerspCentroidAMD : BuiltInOperand<4493, 0, 0, [], []>;
defm BaryCoordNoPerspSampleAMD : BuiltInOperand<4494, 0, 0, [], []>;
defm BaryCoordSmoothAMD : BuiltInOperand<4495, 0, 0, [], []>;
defm BaryCoordSmoothCentroid : BuiltInOperand<4496, 0, 0, [], []>;
defm BaryCoordSmoothSample : BuiltInOperand<4497, 0, 0, [], []>;
defm BaryCoordPullModel : BuiltInOperand<4498, 0, 0, [], []>;
defm FragStencilRefEXT : BuiltInOperand<5014, 0, 0, [], [StencilExportEXT]>;
defm ViewportMaskNV : BuiltInOperand<5253, 0, 0, [], [ShaderViewportMaskNV, MeshShadingNV]>;
defm SecondaryPositionNV : BuiltInOperand<5257, 0, 0, [], [ShaderStereoViewNV]>;
defm SecondaryViewportMaskNV : BuiltInOperand<5258, 0, 0, [], [ShaderStereoViewNV]>;
defm PositionPerViewNV : BuiltInOperand<5261, 0, 0, [], [PerViewAttributesNV, MeshShadingNV]>;
defm ViewportMaskPerViewNV : BuiltInOperand<5262, 0, 0, [], [PerViewAttributesNV, MeshShadingNV]>;
defm FullyCoveredEXT : BuiltInOperand<5264, 0, 0, [], [FragmentFullyCoveredEXT]>;
defm TaskCountNV : BuiltInOperand<5274, 0, 0, [], [MeshShadingNV]>;
defm PrimitiveCountNV : BuiltInOperand<5275, 0, 0, [], [MeshShadingNV]>;
defm PrimitiveIndicesNV : BuiltInOperand<5276, 0, 0, [], [MeshShadingNV]>;
defm ClipDistancePerViewNV : BuiltInOperand<5277, 0, 0, [], [MeshShadingNV]>;
defm CullDistancePerViewNV : BuiltInOperand<5278, 0, 0, [], [MeshShadingNV]>;
defm LayerPerViewNV : BuiltInOperand<5279, 0, 0, [], [MeshShadingNV]>;
defm MeshViewCountNV : BuiltInOperand<5280, 0, 0, [], [MeshShadingNV]>;
defm MeshViewIndices : BuiltInOperand<5281, 0, 0, [], [MeshShadingNV]>;
defm BaryCoordNV : BuiltInOperand<5286, 0, 0, [], [FragmentBarycentricNV]>;
defm BaryCoordNoPerspNV : BuiltInOperand<5287, 0, 0, [], [FragmentBarycentricNV]>;
defm FragSizeEXT : BuiltInOperand<5292, 0, 0, [], [FragmentDensityEXT]>;
defm FragInvocationCountEXT : BuiltInOperand<5293, 0, 0, [], [FragmentDensityEXT]>;
defm LaunchIdNV : BuiltInOperand<5319, 0, 0, [], [RayTracingNV]>;
defm LaunchSizeNV : BuiltInOperand<5320, 0, 0, [], [RayTracingNV]>;
defm WorldRayOriginNV : BuiltInOperand<5321, 0, 0, [], [RayTracingNV]>;
defm WorldRayDirectionNV : BuiltInOperand<5322, 0, 0, [], [RayTracingNV]>;
defm ObjectRayOriginNV : BuiltInOperand<5323, 0, 0, [], [RayTracingNV]>;
defm ObjectRayDirectionNV : BuiltInOperand<5324, 0, 0, [], [RayTracingNV]>;
defm RayTminNV : BuiltInOperand<5325, 0, 0, [], [RayTracingNV]>;
defm RayTmaxNV : BuiltInOperand<5326, 0, 0, [], [RayTracingNV]>;
defm InstanceCustomIndexNV : BuiltInOperand<5327, 0, 0, [], [RayTracingNV]>;
defm ObjectToWorldNV : BuiltInOperand<5330, 0, 0, [], [RayTracingNV]>;
defm WorldToObjectNV : BuiltInOperand<5331, 0, 0, [], [RayTracingNV]>;
defm HitTNV : BuiltInOperand<5332, 0, 0, [], [RayTracingNV]>;
defm HitKindNV : BuiltInOperand<5333, 0, 0, [], [RayTracingNV]>;
defm IncomingRayFlagsNV : BuiltInOperand<5351, 0, 0, [], [RayTracingNV]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define SelectionControl enum values and at the same time
// SymbolicOperand entries with string mnemonics.
//===----------------------------------------------------------------------===//

def SelectionControl : GenericEnum, Operand<i32> {
  let FilterClass = "SelectionControl";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class SelectionControl<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass SelectionControlOperand<bits<32> value> {
  def : SelectionControl<NAME, value>;
  defm : SymbolicOperandWithRequirements<SelectionControlOperand, value, NAME, 0, 0, [], []>;
}

defm None : SelectionControlOperand<0x0>;
defm Flatten : SelectionControlOperand<0x1>;
defm DontFlatten : SelectionControlOperand<0x2>;

//===----------------------------------------------------------------------===//
// Multiclass used to define LoopControl enum values and at the same time
// SymbolicOperand entries with string mnemonics.
//===----------------------------------------------------------------------===//

def LoopControl : GenericEnum, Operand<i32> {
  let FilterClass = "LoopControl";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class LoopControl<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass LoopControlOperand<bits<32> value> {
  def : LoopControl<NAME, value>;
  defm : SymbolicOperandWithRequirements<LoopControlOperand, value, NAME, 0, 0, [], []>;
}

defm None : LoopControlOperand<0x0>;
defm Unroll : LoopControlOperand<0x1>;
defm DontUnroll : LoopControlOperand<0x2>;
defm DependencyInfinite : LoopControlOperand<0x4>;
defm DependencyLength : LoopControlOperand<0x8>;
defm MinIterations : LoopControlOperand<0x10>;
defm MaxIterations : LoopControlOperand<0x20>;
defm IterationMultiple : LoopControlOperand<0x40>;
defm PeelCount : LoopControlOperand<0x80>;
defm PartialCount : LoopControlOperand<0x100>;

//===----------------------------------------------------------------------===//
// Multiclass used to define FunctionControl enum values and at the same time
// SymbolicOperand entries with string mnemonics.
//===----------------------------------------------------------------------===//

def FunctionControl : GenericEnum, Operand<i32> {
  let FilterClass = "FunctionControl";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class FunctionControl<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass FunctionControlOperand<bits<32> value> {
  def : FunctionControl<NAME, value>;
  defm : SymbolicOperandWithRequirements<FunctionControlOperand, value, NAME, 0, 0, [], []>;
}

defm None : FunctionControlOperand<0x0>;
defm Inline : FunctionControlOperand<0x1>;
defm DontInline : FunctionControlOperand<0x2>;
defm Pure : FunctionControlOperand<0x4>;
defm Const : FunctionControlOperand<0x8>;

//===----------------------------------------------------------------------===//
// Multiclass used to define MemorySemantics enum values and at the same time
// SymbolicOperand entries with string mnemonics, versioning, extensions and
// capabilities.
//===----------------------------------------------------------------------===//

def MemorySemantics : GenericEnum, Operand<i32> {
  let FilterClass = "MemorySemantics";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class MemorySemantics<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass MemorySemanticsOperand<bits<32> value, bits<32> minVersion, bits<32> maxVersion, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
  def : MemorySemantics<NAME, value>;
  defm : SymbolicOperandWithRequirements<MemorySemanticsOperand, value, NAME, minVersion, maxVersion, reqExtensions, reqCapabilities>;
}

defm None : MemorySemanticsOperand<0x0, 0, 0, [], []>;
defm Acquire : MemorySemanticsOperand<0x2, 0, 0, [], []>;
defm Release : MemorySemanticsOperand<0x4, 0, 0, [], []>;
defm AcquireRelease : MemorySemanticsOperand<0x8, 0, 0, [], []>;
defm SequentiallyConsistent : MemorySemanticsOperand<0x10, 0, 0, [], []>;
defm UniformMemory : MemorySemanticsOperand<0x40, 0, 0, [], [Shader]>;
defm SubgroupMemory : MemorySemanticsOperand<0x80, 0, 0, [], []>;
defm WorkgroupMemory : MemorySemanticsOperand<0x100, 0, 0, [], []>;
defm CrossWorkgroupMemory : MemorySemanticsOperand<0x200, 0, 0, [], []>;
defm AtomicCounterMemory : MemorySemanticsOperand<0x400, 0, 0, [], [AtomicStorage]>;
defm ImageMemory : MemorySemanticsOperand<0x800, 0, 0, [], []>;
defm OutputMemoryKHR : MemorySemanticsOperand<0x1000, 0, 0, [], [VulkanMemoryModelKHR]>;
defm MakeAvailableKHR : MemorySemanticsOperand<0x2000, 0, 0, [], [VulkanMemoryModelKHR]>;
defm MakeVisibleKHR : MemorySemanticsOperand<0x4000, 0, 0, [], [VulkanMemoryModelKHR]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define MemoryOperand enum values and at the same time
// SymbolicOperand entries with string mnemonics, versioning, extensions and
// capabilities.
//===----------------------------------------------------------------------===//

def MemoryOperand : GenericEnum, Operand<i32> {
  let FilterClass = "MemoryOperand";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class MemoryOperand<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass MemoryOperandOperand<bits<32> value, bits<32> minVersion, bits<32> maxVersion, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
  def : MemoryOperand<NAME, value>;
  defm : SymbolicOperandWithRequirements<MemoryOperandOperand, value, NAME, minVersion, maxVersion, reqExtensions, reqCapabilities>;
}

defm None : MemoryOperandOperand<0x0, 0, 0, [], []>;
defm Volatile : MemoryOperandOperand<0x1, 0, 0, [], []>;
defm Aligned : MemoryOperandOperand<0x2, 0, 0, [], []>;
defm Nontemporal : MemoryOperandOperand<0x4, 0, 0, [], []>;
defm MakePointerAvailableKHR : MemoryOperandOperand<0x8, 0, 0, [], [VulkanMemoryModelKHR]>;
defm MakePointerVisibleKHR : MemoryOperandOperand<0x10, 0, 0, [], [VulkanMemoryModelKHR]>;
defm NonPrivatePointerKHR : MemoryOperandOperand<0x20, 0, 0, [], [VulkanMemoryModelKHR]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define Scope enum values and at the same time
// SymbolicOperand entries with string mnemonics, versioning, extensions and
// capabilities.
//===----------------------------------------------------------------------===//

def Scope : GenericEnum, Operand<i32> {
  let FilterClass = "Scope";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class Scope<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass ScopeOperand<bits<32> value, bits<32> minVersion, bits<32> maxVersion, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
  def : Scope<NAME, value>;
  defm : SymbolicOperandWithRequirements<ScopeOperand, value, NAME, minVersion, maxVersion, reqExtensions, reqCapabilities>;
}

defm CrossDevice : ScopeOperand<0, 0, 0, [], []>;
defm Device : ScopeOperand<1, 0, 0, [], []>;
defm Workgroup : ScopeOperand<2, 0, 0, [], []>;
defm Subgroup : ScopeOperand<3, 0, 0, [], []>;
defm Invocation : ScopeOperand<4, 0, 0, [], []>;
defm QueueFamilyKHR : ScopeOperand<5, 0, 0, [], [VulkanMemoryModelKHR]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define GroupOperation enum values and at the same time
// SymbolicOperand entries with string mnemonics, versioning, extensions and
// capabilities.
//===----------------------------------------------------------------------===//

def GroupOperation : GenericEnum, Operand<i32> {
  let FilterClass = "GroupOperation";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class GroupOperation<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass GroupOperationOperand<bits<32> value, bits<32> minVersion, bits<32> maxVersion, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
  def NAME : GroupOperation<NAME, value>;
  defm : SymbolicOperandWithRequirements<GroupOperationOperand, value, NAME, minVersion, maxVersion, reqExtensions, reqCapabilities>;
}

defm Reduce : GroupOperationOperand<0, 0, 0, [], [Kernel, GroupNonUniformArithmetic, GroupNonUniformBallot]>;
defm InclusiveScan : GroupOperationOperand<1, 0, 0, [], [Kernel, GroupNonUniformArithmetic, GroupNonUniformBallot]>;
defm ExclusiveScan : GroupOperationOperand<2, 0, 0, [], [Kernel, GroupNonUniformArithmetic, GroupNonUniformBallot]>;
defm ClusteredReduce : GroupOperationOperand<3, 0, 0, [], [GroupNonUniformClustered]>;
defm PartitionedReduceNV : GroupOperationOperand<6, 0, 0, [], [GroupNonUniformPartitionedNV]>;
defm PartitionedInclusiveScanNV : GroupOperationOperand<7, 0, 0, [], [GroupNonUniformPartitionedNV]>;
defm PartitionedExclusiveScanNV : GroupOperationOperand<8, 0, 0, [], [GroupNonUniformPartitionedNV]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define KernelEnqueueFlags enum values and at the same time
// SymbolicOperand entries with string mnemonics, versioning, extensions and
// capabilities.
//===----------------------------------------------------------------------===//

def KernelEnqueueFlags : GenericEnum, Operand<i32> {
  let FilterClass = "KernelEnqueueFlags";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class KernelEnqueueFlags<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass KernelEnqueueFlagsOperand<bits<32> value, bits<32> minVersion, bits<32> maxVersion, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
  def : KernelEnqueueFlags<NAME, value>;
  defm : SymbolicOperandWithRequirements<KernelEnqueueFlagsOperand, value, NAME, minVersion, maxVersion, reqExtensions, reqCapabilities>;
}

defm NoWait : KernelEnqueueFlagsOperand<0, 0, 0, [], [Kernel]>;
defm WaitKernel : KernelEnqueueFlagsOperand<1, 0, 0, [], [Kernel]>;
defm WaitWorkGroup : KernelEnqueueFlagsOperand<2, 0, 0, [], [Kernel]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define KernelProfilingInfo enum values and at the same time
// SymbolicOperand entries with string mnemonics, versioning, extensions and
// capabilities.
//===----------------------------------------------------------------------===//

def KernelProfilingInfo : GenericEnum, Operand<i32> {
  let FilterClass = "KernelProfilingInfo";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class KernelProfilingInfo<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass KernelProfilingInfoOperand<bits<32> value, bits<32> minVersion, bits<32> maxVersion, list<Extension> reqExtensions, list<Capability> reqCapabilities> {
  def : KernelProfilingInfo<NAME, value>;
  defm : SymbolicOperandWithRequirements<KernelProfilingInfoOperand, value, NAME, minVersion, maxVersion, reqExtensions, reqCapabilities>;
}

defm None : KernelProfilingInfoOperand<0x0, 0, 0, [], []>;
defm CmdExecTime : KernelProfilingInfoOperand<0x1, 0, 0, [], [Kernel]>;

//===----------------------------------------------------------------------===//
// Multiclass used to define Opcode enum values and at the same time
// SymbolicOperand entries with string mnemonics and capabilities.
//===----------------------------------------------------------------------===//

def Opcode : GenericEnum, Operand<i32> {
  let FilterClass = "Opcode";
  let NameField = "Name";
  let ValueField = "Value";
  let PrintMethod = !strconcat("printSymbolicOperand<OperandCategory::", FilterClass, "Operand>");
}

class Opcode<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

multiclass OpcodeOperand<bits<32> value> {
  def : Opcode<NAME, value>;
  defm : SymbolicOperandWithRequirements<OpcodeOperand, value, NAME, 0, 0, [], []>;
}
// TODO: implement other mnemonics.
defm InBoundsPtrAccessChain : OpcodeOperand<70>;
defm PtrCastToGeneric : OpcodeOperand<121>;
defm Bitcast : OpcodeOperand<124>;
defm ConvertPtrToU : OpcodeOperand<117>;
defm ConvertUToPtr : OpcodeOperand<120>;
