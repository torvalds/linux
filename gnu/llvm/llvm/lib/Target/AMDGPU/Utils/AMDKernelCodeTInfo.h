//===--------------------- AMDKernelCodeTInfo.h ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
//
/// \file - specifies tables for amd_kernel_code_t structure parsing/printing
//
//===----------------------------------------------------------------------===//

#define QNAME(name) AMDGPUMCKernelCodeT::name
#define FLD_T(name) decltype(QNAME(name)), &QNAME(name)

#ifndef PRINTFIELD
#define PRINTFIELD(sname, aname, name) printField<FLD_T(name)>
#endif

#ifndef FIELD2
#define FIELD2(sname, aname, name)                                             \
  RECORD(sname, aname, PRINTFIELD(sname, aname, name), parseField<FLD_T(name)>)
#endif

#ifndef FIELD
#define FIELD(name) FIELD2(name, name, name)
#endif

#ifndef PRINTCODEPROP
#define PRINTCODEPROP(name) \
  printBitField<FLD_T(code_properties),\
                AMD_CODE_PROPERTY_##name##_SHIFT,\
                AMD_CODE_PROPERTY_##name##_WIDTH>
#endif

#ifndef PARSECODEPROP
#define PARSECODEPROP(name) \
  parseBitField<FLD_T(code_properties),\
                AMD_CODE_PROPERTY_##name##_SHIFT,\
                AMD_CODE_PROPERTY_##name##_WIDTH>
#endif

#ifndef CODEPROP
#define CODEPROP(name, shift) \
  RECORD(name, name, PRINTCODEPROP(shift), PARSECODEPROP(shift))
#endif

// have to define these lambdas because of Set/GetMacro
#ifndef PRINTCOMP
#define PRINTCOMP(GetMacro, Shift) \
[](StringRef Name, const amd_kernel_code_t &C, raw_ostream &OS) { \
   printName(OS, Name) << \
     (int)GetMacro(C.compute_pgm_resource_registers >> Shift); \
}
#endif

#ifndef PARSECOMP
#define PARSECOMP(SetMacro, Shift) \
[](amd_kernel_code_t &C, MCAsmParser &MCParser, raw_ostream &Err) { \
   int64_t Value = 0; \
   if (!expectAbsExpression(MCParser, Value, Err)) \
     return false; \
   C.compute_pgm_resource_registers &= ~(SetMacro(0xFFFFFFFFFFFFFFFFULL) << Shift); \
   C.compute_pgm_resource_registers |= SetMacro(Value) << Shift; \
   return true; \
}
#endif

#ifndef COMPPGM
#define COMPPGM(name, aname, GetMacro, SetMacro, Shift) \
  RECORD(name, aname, PRINTCOMP(GetMacro, Shift), PARSECOMP(SetMacro, Shift))
#endif

#ifndef COMPPGM1
#define COMPPGM1(name, aname, AccMacro) \
  COMPPGM(name, aname, G_00B848_##AccMacro, S_00B848_##AccMacro, 0)
#endif

#ifndef COMPPGM2
#define COMPPGM2(name, aname, AccMacro) \
  COMPPGM(name, aname, G_00B84C_##AccMacro, S_00B84C_##AccMacro, 32)
#endif

///////////////////////////////////////////////////////////////////////////////
// Begin of the table
// Define RECORD(name, print, parse) in your code to get field definitions
// and include this file

FIELD2(amd_code_version_major,        kernel_code_version_major,  amd_kernel_code_version_major),
FIELD2(amd_code_version_minor,        kernel_code_version_minor,  amd_kernel_code_version_minor),
FIELD2(amd_machine_kind,              machine_kind,               amd_machine_kind),
FIELD2(amd_machine_version_major,     machine_version_major,      amd_machine_version_major),
FIELD2(amd_machine_version_minor,     machine_version_minor,      amd_machine_version_minor),
FIELD2(amd_machine_version_stepping,  machine_version_stepping,   amd_machine_version_stepping),

FIELD(kernel_code_entry_byte_offset),
FIELD(kernel_code_prefetch_byte_size),

COMPPGM1(granulated_workitem_vgpr_count,  compute_pgm_rsrc1_vgprs,          VGPRS),
COMPPGM1(granulated_wavefront_sgpr_count, compute_pgm_rsrc1_sgprs,          SGPRS),
COMPPGM1(priority,                        compute_pgm_rsrc1_priority,       PRIORITY),
COMPPGM1(float_mode,                      compute_pgm_rsrc1_float_mode,     FLOAT_MODE), // TODO: split float_mode
COMPPGM1(priv,                            compute_pgm_rsrc1_priv,           PRIV),
COMPPGM1(enable_dx10_clamp,               compute_pgm_rsrc1_dx10_clamp,     DX10_CLAMP),
COMPPGM1(debug_mode,                      compute_pgm_rsrc1_debug_mode,     DEBUG_MODE),
COMPPGM1(enable_ieee_mode,                compute_pgm_rsrc1_ieee_mode,      IEEE_MODE),
COMPPGM1(enable_wgp_mode,                 compute_pgm_rsrc1_wgp_mode,       WGP_MODE),
COMPPGM1(enable_mem_ordered,              compute_pgm_rsrc1_mem_ordered,    MEM_ORDERED),
COMPPGM1(enable_fwd_progress,             compute_pgm_rsrc1_fwd_progress,   FWD_PROGRESS),
// TODO: bulky
// TODO: cdbg_user
COMPPGM2(enable_sgpr_private_segment_wave_byte_offset, compute_pgm_rsrc2_scratch_en, SCRATCH_EN),
COMPPGM2(user_sgpr_count,                 compute_pgm_rsrc2_user_sgpr,      USER_SGPR),
COMPPGM2(enable_trap_handler,             compute_pgm_rsrc2_trap_handler,   TRAP_HANDLER),
COMPPGM2(enable_sgpr_workgroup_id_x,      compute_pgm_rsrc2_tgid_x_en,      TGID_X_EN),
COMPPGM2(enable_sgpr_workgroup_id_y,      compute_pgm_rsrc2_tgid_y_en,      TGID_Y_EN),
COMPPGM2(enable_sgpr_workgroup_id_z,      compute_pgm_rsrc2_tgid_z_en,      TGID_Z_EN),
COMPPGM2(enable_sgpr_workgroup_info,      compute_pgm_rsrc2_tg_size_en,     TG_SIZE_EN),
COMPPGM2(enable_vgpr_workitem_id,         compute_pgm_rsrc2_tidig_comp_cnt, TIDIG_COMP_CNT),
COMPPGM2(enable_exception_msb,            compute_pgm_rsrc2_excp_en_msb,    EXCP_EN_MSB), // TODO: split enable_exception_msb
COMPPGM2(granulated_lds_size,             compute_pgm_rsrc2_lds_size,       LDS_SIZE),
COMPPGM2(enable_exception,                compute_pgm_rsrc2_excp_en,        EXCP_EN), // TODO: split enable_exception

CODEPROP(enable_sgpr_private_segment_buffer,  ENABLE_SGPR_PRIVATE_SEGMENT_BUFFER),
CODEPROP(enable_sgpr_dispatch_ptr,            ENABLE_SGPR_DISPATCH_PTR),
CODEPROP(enable_sgpr_queue_ptr,               ENABLE_SGPR_QUEUE_PTR),
CODEPROP(enable_sgpr_kernarg_segment_ptr,     ENABLE_SGPR_KERNARG_SEGMENT_PTR),
CODEPROP(enable_sgpr_dispatch_id,             ENABLE_SGPR_DISPATCH_ID),
CODEPROP(enable_sgpr_flat_scratch_init,       ENABLE_SGPR_FLAT_SCRATCH_INIT),
CODEPROP(enable_sgpr_private_segment_size,    ENABLE_SGPR_PRIVATE_SEGMENT_SIZE),
CODEPROP(enable_sgpr_grid_workgroup_count_x,  ENABLE_SGPR_GRID_WORKGROUP_COUNT_X),
CODEPROP(enable_sgpr_grid_workgroup_count_y,  ENABLE_SGPR_GRID_WORKGROUP_COUNT_Y),
CODEPROP(enable_sgpr_grid_workgroup_count_z,  ENABLE_SGPR_GRID_WORKGROUP_COUNT_Z),
CODEPROP(enable_wavefront_size32,             ENABLE_WAVEFRONT_SIZE32),
CODEPROP(enable_ordered_append_gds,           ENABLE_ORDERED_APPEND_GDS),
CODEPROP(private_element_size,                PRIVATE_ELEMENT_SIZE),
CODEPROP(is_ptr64,                            IS_PTR64),
CODEPROP(is_dynamic_callstack,                IS_DYNAMIC_CALLSTACK),
CODEPROP(is_debug_enabled,                    IS_DEBUG_SUPPORTED),
CODEPROP(is_xnack_enabled,                    IS_XNACK_SUPPORTED),

FIELD(workitem_private_segment_byte_size),
FIELD(workgroup_group_segment_byte_size),
FIELD(gds_segment_byte_size),
FIELD(kernarg_segment_byte_size),
FIELD(workgroup_fbarrier_count),
FIELD(wavefront_sgpr_count),
FIELD(workitem_vgpr_count),
FIELD(reserved_vgpr_first),
FIELD(reserved_vgpr_count),
FIELD(reserved_sgpr_first),
FIELD(reserved_sgpr_count),
FIELD(debug_wavefront_private_segment_offset_sgpr),
FIELD(debug_private_segment_buffer_sgpr),
FIELD(kernarg_segment_alignment),
FIELD(group_segment_alignment),
FIELD(private_segment_alignment),
FIELD(wavefront_size),
FIELD(call_convention),
FIELD(runtime_loader_kernel_symbol)
// TODO: control_directive

// end of the table
///////////////////////////////////////////////////////////////////////////////

#undef QNAME
#undef FLD_T
#undef PRINTFIELD
#undef FIELD2
#undef FIELD
#undef PRINTCODEPROP
#undef PARSECODEPROP
#undef CODEPROP
#undef PRINTCOMP
#undef PARSECOMP
#undef COMPPGM
#undef COMPPGM1
#undef COMPPGM2
