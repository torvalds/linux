#!/usr/bin/awk -f

#-
# Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer,
#    without modification.
# 2. Redistributions in binary form must reproduce at minimum a disclaimer
#    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
#    redistribution must be conditioned upon including a substantially
#    similar Disclaimer requirement for further binary redistribution.
#
# NO WARRANTY
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
# AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
# OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGES.
# 
# $FreeBSD$

BEGIN	{ main() }
END	{ at_exit() }

#
# Print usage
#
function usage() {
	print "usage: bhnd_nvram_map.awk <input map> [-hd] [-o output file]"
	_EARLY_EXIT = 1
	exit 1
}

function main(_i) {
	RS="\n"

	OUTPUT_FILE = null

	# Probe awk implementation's hex digit handling
	if ("0xA" + 0 != 10) {
		AWK_REQ_HEX_PARSING=1
	}

	# Output type
	OUT_T = null
	OUT_T_HEADER = "HEADER"
	OUT_T_DATA = "DATA"
	VERBOSE = 0

	# Tab width to use when calculating output alignment
	TAB_WIDTH = 8

	# Enable debug output
	DEBUG = 0

	# Maximum revision
	REV_MAX = 256

	# Parse arguments
	if (ARGC < 2)
		usage()

	for (_i = 1; _i < ARGC; _i++) {
		if (ARGV[_i] == "--debug") {
			DEBUG = 1
		} else if (ARGV[_i] == "-d" && OUT_T == null) {
			OUT_T = OUT_T_DATA
		} else if (ARGV[_i] == "-h" && OUT_T == null) {
			OUT_T = OUT_T_HEADER
		} else if (ARGV[_i] == "-v") {
		        VERBOSE = 1
		} else if (ARGV[_i] == "-o") {
			_i++
			if (_i >= ARGC)
				usage()

			OUTPUT_FILE = ARGV[_i]
		} else if (ARGV[_i] == "--") {
			_i++
			break
		} else if (ARGV[_i] !~ /^-/) {
			FILENAME = ARGV[_i]
		} else {
			print "unknown option " ARGV[_i]
			usage()
		}
	}

	ARGC=2

	if (OUT_T == null) {
		print("error: one of -d or -h required")
		usage()
	}

	if (FILENAME == null) {
		print("error: no input file specified")
		usage()
	}

	if (OUTPUT_FILE == "-") {
		OUTPUT_FILE = "/dev/stdout"
	} else if (OUTPUT_FILE == null) {
		OUTPUT_FILE_IDX = split(FILENAME, _g_output_path, "/")
		OUTPUT_FILE = _g_output_path[OUTPUT_FILE_IDX]

		if (OUTPUT_FILE !~ /^bhnd_/)
			OUTPUT_FILE = "bhnd_" OUTPUT_FILE

		if (OUT_T == OUT_T_HEADER)
			OUTPUT_FILE = OUTPUT_FILE ".h" 
		else
			OUTPUT_FILE = OUTPUT_FILE "_data.h"
	}

	# Common Regexs
	UINT_REGEX	= "^(0|[1-9][0-9]*)$"
	HEX_REGEX	= "^(0x[A-Fa-f0-9]+)$"
	OFF_REGEX	= "^(0|[1-9][0-9]*)|^(0x[A-Fa-f0-9]+)"
	REL_OFF_REGEX	= "^\\+(0|[1-9][0-9]*)|^\\+(0x[A-Fa-f0-9]+)"

	ARRAY_REGEX	= "\\[(0|[1-9][0-9]*)\\]"
	TYPES_REGEX	= "^(((u|i)(8|16|32))|char)("ARRAY_REGEX")?$"

	IDENT_REGEX		= "[A-Za-z_][A-Za-z0-9_]*"
	SVAR_IDENT_REGEX	= "^<"IDENT_REGEX">{?$"	# <var> identifiers
	VAR_IDENT_REGEX		= "^"IDENT_REGEX"{?$"	# var identifiers

	VACCESS_REGEX	= "^(private|internal)$"

	# Property array keys
	PROP_ID		= "p_id"
	PROP_NAME	= "p_name"

	# Prop path array keys
	PPATH_HEAD	= "ppath_head"
	PPATH_TAIL	= "ppath_tail"

	# Object array keys
	OBJ_IS_CLS	= "o_is_cls"
	OBJ_SUPER	= "o_super"
	OBJ_PROP	= "o_prop"

	# Class array keys
	CLS_NAME	= "cls_name"
	CLS_PROP	= "cls_prop"

	# C SPROM binding opcodes/opcode flags
	SPROM_OPCODE_EOF		= "SPROM_OPCODE_EOF"
	SPROM_OPCODE_NELEM		= "SPROM_OPCODE_NELEM"
	SPROM_OPCODE_VAR_END		= "SPROM_OPCODE_VAR_END"
	SPROM_OPCODE_VAR_IMM		= "SPROM_OPCODE_VAR_IMM"
	SPROM_OPCODE_VAR_REL_IMM	= "SPROM_OPCODE_VAR_REL_IMM"
	SPROM_OPCODE_VAR		= "SPROM_OPCODE_VAR"
	SPROM_OPCODE_REV_IMM		= "SPROM_OPCODE_REV_IMM"
	SPROM_OPCODE_REV_RANGE		= "SPROM_OPCODE_REV_RANGE"
	  SPROM_OP_REV_START_MASK	= "SPROM_OP_REV_START_MASK"
	  SPROM_OP_REV_START_SHIFT	= "SPROM_OP_REV_START_SHIFT"
	  SPROM_OP_REV_END_MASK		= "SPROM_OP_REV_END_MASK"
	  SPROM_OP_REV_END_SHIFT	= "SPROM_OP_REV_END_SHIFT"
	SPROM_OPCODE_MASK_IMM		= "SPROM_OPCODE_MASK_IMM"
	SPROM_OPCODE_MASK		= "SPROM_OPCODE_MASK"
	SPROM_OPCODE_SHIFT_IMM		= "SPROM_OPCODE_SHIFT_IMM"
	SPROM_OPCODE_SHIFT		= "SPROM_OPCODE_SHIFT"
	SPROM_OPCODE_OFFSET_REL_IMM	= "SPROM_OPCODE_OFFSET_REL_IMM"
	SPROM_OPCODE_OFFSET		= "SPROM_OPCODE_OFFSET"
	SPROM_OPCODE_TYPE		= "SPROM_OPCODE_TYPE"
	SPROM_OPCODE_TYPE_IMM		= "SPROM_OPCODE_TYPE_IMM"
	SPROM_OPCODE_DO_BINDN_IMM	= "SPROM_OPCODE_DO_BINDN_IMM"
	SPROM_OPCODE_DO_BIND		= "SPROM_OPCODE_DO_BIND"
	SPROM_OPCODE_DO_BINDN		= "SPROM_OPCODE_DO_BINDN"
	  SPROM_OP_BIND_SKIP_IN_MASK	= "SPROM_OP_BIND_SKIP_IN_MASK"
	  SPROM_OP_BIND_SKIP_IN_SHIFT	= "SPROM_OP_BIND_SKIP_IN_SHIFT"
	  SPROM_OP_BIND_SKIP_IN_SIGN	= "SPROM_OP_BIND_SKIP_IN_SIGN"
	  SPROM_OP_BIND_SKIP_OUT_MASK	= "SPROM_OP_BIND_SKIP_OUT_MASK"
	  SPROM_OP_BIND_SKIP_OUT_SHIFT	= "SPROM_OP_BIND_SKIP_OUT_SHIFT"

	SPROM_OP_DATA_U8		= "SPROM_OP_DATA_U8"
	SPROM_OP_DATA_U8_SCALED		= "SPROM_OP_DATA_U8_SCALED"
	SPROM_OP_DATA_U16		= "SPROM_OP_DATA_U16"
	SPROM_OP_DATA_U32		= "SPROM_OP_DATA_U32"
	SPROM_OP_DATA_I8		= "SPROM_OP_DATA_I8"

	SPROM_OP_BIND_SKIP_IN_MAX	=  3	# maximum SKIP_IN value
	SPROM_OP_BIND_SKIP_IN_MIN	= -3	# minimum SKIP_IN value
	SPROM_OP_BIND_SKIP_OUT_MAX	=  1	# maximum SKIP_OUT value
	SPROM_OP_BIND_SKIP_OUT_MIN	=  0	# minimum SKIP_OUT value
	SPROM_OP_IMM_MAX		= 15	# maximum immediate value
	SPROM_OP_REV_RANGE_MAX		= 15	# maximum SROM rev range value

	# SPROM opcode encoding state
	SromOpStream = class_new("SromOpStream")
		class_add_prop(SromOpStream, p_layout, "layout")
		class_add_prop(SromOpStream, p_revisions, "revisions")
		class_add_prop(SromOpStream, p_vid, "vid")
		class_add_prop(SromOpStream, p_offset, "offset")
		class_add_prop(SromOpStream, p_type, "type")
		class_add_prop(SromOpStream, p_nelem, "nelem")
		class_add_prop(SromOpStream, p_mask, "mask")
		class_add_prop(SromOpStream, p_shift, "shift")
		class_add_prop(SromOpStream, p_bind_total, "bind_total")
		class_add_prop(SromOpStream, p_pending_bind, "pending_bind")

	# SROM pending bind operation
	SromOpBind = class_new("SromOpBind")
		class_add_prop(SromOpBind, p_segment, "segment")
		class_add_prop(SromOpBind, p_count, "count")
		class_add_prop(SromOpBind, p_offset, "offset")
		class_add_prop(SromOpBind, p_width, "width")
		class_add_prop(SromOpBind, p_skip_in, "skip_in")
		class_add_prop(SromOpBind, p_skip_out, "skip_out")
		class_add_prop(SromOpBind, p_buffer, "buffer")

	# Map class definition
	Map = class_new("Map")

	# Array class definition
	Array = class_new("Array")
		class_add_prop(Array, p_count, "count")

	# MacroType class definition
	# Used to define a set of known macro types that may be generated
	MacroType = class_new("MacroType")
		class_add_prop(MacroType, p_name, "name")
		class_add_prop(MacroType, p_const_suffix, "const_suffix")

	MTypeVarName	= macro_type_new("name", "")		# var name
	MTypeVarID	= macro_type_new("id", "_ID")		# var unique ID
	MTypeVarMaxLen	= macro_type_new("len", "_MAXLEN")	# var max array length

	# Preprocessor Constant
	MacroDefine = class_new("MacroDefine")
		class_add_prop(MacroDefine, p_name, "name")
		class_add_prop(MacroDefine, p_value, "value")

	# ParseState definition
	ParseState = class_new("ParseState")
		class_add_prop(ParseState, p_ctx, "ctx")
		class_add_prop(ParseState, p_is_block, "is_block")
		class_add_prop(ParseState, p_line, "line")

	# Value Formats
	Fmt = class_new("Fmt")
		class_add_prop(Fmt, p_name, "name")
		class_add_prop(Fmt, p_symbol, "symbol")
		class_add_prop(Fmt, p_array_fmt, "array_fmt")

	FmtHex		= fmt_new("hex", "bhnd_nvram_val_bcm_hex_fmt")
	FmtDec 		= fmt_new("decimal", "bhnd_nvram_val_bcm_decimal_fmt")
	FmtMAC		= fmt_new("macaddr", "bhnd_nvram_val_bcm_macaddr_fmt")
	FmtLEDDC	= fmt_new("leddc", "bhnd_nvram_val_bcm_leddc_fmt")
	FmtCharArray	= fmt_new("char_array", "bhnd_nvram_val_char_array_fmt")
	FmtChar		= fmt_new("char", "bhnd_nvram_val_char_array_fmt",
			      FmtCharArray)
	FmtStr		= fmt_new("string", "bhnd_nvram_val_bcm_string_fmt")

	# User-specifiable value formats
	ValueFormats = map_new()
		map_set(ValueFormats, get(FmtHex,	p_name), FmtHex)
		map_set(ValueFormats, get(FmtDec,	p_name), FmtDec)
		map_set(ValueFormats, get(FmtMAC,	p_name), FmtMAC)
		map_set(ValueFormats, get(FmtLEDDC,	p_name), FmtLEDDC)
		map_set(ValueFormats, get(FmtStr,	p_name), FmtStr)

	# Data Types
	Type = class_new("Type")
		class_add_prop(Type, p_name, "name")
		class_add_prop(Type, p_width, "width")
		class_add_prop(Type, p_signed, "signed")
		class_add_prop(Type, p_const, "const")
		class_add_prop(Type, p_const_val, "const_val")
		class_add_prop(Type, p_array_const, "array_const")
		class_add_prop(Type, p_array_const_val, "array_const_val")
		class_add_prop(Type, p_default_fmt, "default_fmt")
		class_add_prop(Type, p_mask, "mask")

	ArrayType = class_new("ArrayType", AST)
		class_add_prop(ArrayType, p_type, "type")
		class_add_prop(ArrayType, p_count, "count")

	UInt8Max	=  255
	UInt16Max	=  65535
	UInt32Max	=  4294967295
	Int8Min		= -128
	Int8Max		=  127
	Int16Min	= -32768
	Int16Max	=  32767
	Int32Min	= -2147483648
	Int32Max	=  2147483648
	CharMin		=  Int8Min
	CharMax		=  Int8Max

	UInt8	= type_new("u8", 1, 0, "BHND_NVRAM_TYPE_UINT8",
	   "BHND_NVRAM_TYPE_UINT8_ARRAY", FmtHex, UInt8Max, 0, 16)

	UInt16	= type_new("u16", 2, 0, "BHND_NVRAM_TYPE_UINT16",
	   "BHND_NVRAM_TYPE_UINT16_ARRAY", FmtHex, UInt16Max, 1, 17)

	UInt32	= type_new("u32", 4, 0, "BHND_NVRAM_TYPE_UINT32",
	   "BHND_NVRAM_TYPE_UINT32_ARRAY", FmtHex, UInt32Max, 2, 18)

	Int8	= type_new("i8", 1, 1, "BHND_NVRAM_TYPE_INT8",
	   "BHND_NVRAM_TYPE_INT8_ARRAY", FmtDec, UInt8Max, 4, 20)

	Int16	= type_new("i16", 2, 1, "BHND_NVRAM_TYPE_INT16",
	   "BHND_NVRAM_TYPE_INT16_ARRAY", FmtDec, UInt16Max, 5, 21)

	Int32	= type_new("i32", 4, 1, "BHND_NVRAM_TYPE_INT32",
	   "BHND_NVRAM_TYPE_INT32_ARRAY", FmtDec, UInt32Max, 6, 22)

	Char	= type_new("char", 1, 1, "BHND_NVRAM_TYPE_CHAR",
	   "BHND_NVRAM_TYPE_CHAR_ARRAY", FmtChar, UInt8Max, 8, 24)

	BaseTypes = map_new()
		map_set(BaseTypes, get(UInt8,	p_name), UInt8)
		map_set(BaseTypes, get(UInt16,	p_name), UInt16)
		map_set(BaseTypes, get(UInt32,	p_name), UInt32)
		map_set(BaseTypes, get(Int8,	p_name), Int8)
		map_set(BaseTypes, get(Int16,	p_name), Int16)
		map_set(BaseTypes, get(Int32,	p_name), Int32)
		map_set(BaseTypes, get(Char,	p_name), Char)

	BaseTypesArray = map_to_array(BaseTypes)
	BaseTypesCount = array_size(BaseTypesArray)

	# Variable Flags
	VFlag = class_new("VFlag")
		class_add_prop(VFlag, p_name, "name")
		class_add_prop(VFlag, p_const, "const")

	VFlagPrivate	= vflag_new("private", "BHND_NVRAM_VF_MFGINT")
	VFlagIgnoreAll1	= vflag_new("ignall1", "BHND_NVRAM_VF_IGNALL1")

	# Variable Access Type Constants
	VAccess = class_new("VAccess")
	VAccessPublic	= obj_new(VAccess)	# Public
	VAccessPrivate	= obj_new(VAccess)	# MFG Private
	VAccessInternal	= obj_new(VAccess)	# Implementation-Internal

	#
	# AST node classes
	#
	AST = class_new("AST")
		class_add_prop(AST, p_line, "line")

	SymbolContext = class_new("SymbolContext", AST)
		class_add_prop(SymbolContext, p_vars, "vars")

	# NVRAM root parser context
	NVRAM = class_new("NVRAM", SymbolContext)
		class_add_prop(NVRAM, p_var_groups, "var_groups")
		class_add_prop(NVRAM, p_srom_layouts, "srom_layouts")
		class_add_prop(NVRAM, p_srom_table, "srom_table")

	# Variable Group
	VarGroup = class_new("VarGroup", SymbolContext)
		class_add_prop(VarGroup, p_name, "name")

	# Revision Range
	RevRange = class_new("RevRange", AST)
		class_add_prop(RevRange, p_start, "start")
		class_add_prop(RevRange, p_end, "end")

	# String Constant
	StringConstant = class_new("StringConstant", AST)
		class_add_prop(StringConstant, p_value, "value")		# string
		class_add_prop(StringConstant, p_continued, "continued")	# bool

	# Variable Declaration
	Var = class_new("Var", AST)
		class_add_prop(Var, p_access, "access")		# VAccess
		class_add_prop(Var, p_name, "name")		# string
		class_add_prop(Var, p_desc, "desc")		# StringConstant
		class_add_prop(Var, p_help, "help")		# StringConstant
		class_add_prop(Var, p_type, "type")		# AbstractType
		class_add_prop(Var, p_fmt, "fmt")		# Fmt
		class_add_prop(Var, p_ignall1, "ignall1")	# bool
		# ID is assigned once all variables are sorted
		class_add_prop(Var, p_vid, "vid")		# int

	# Common interface inherited by parser contexts that support
	# registration of SROM variable entries
	SromContext = class_new("SromContext", AST)
		class_add_prop(SromContext, p_revisions, "revisions")

	# SROM Layout Node
	SromLayout = class_new("SromLayout", SromContext)
		class_add_prop(SromLayout, p_entries, "entries")	# Array<SromEntry>
		class_add_prop(SromLayout, p_revmap, "revmap")		# Map<(string,int), SromEntry>
		class_add_prop(SromLayout, p_output_var_counts,		# Map<int, int> (rev->count)
		    "output_var_counts")

	# SROM Layout Filter Node
	# Represents a filter over a parent SromLayout's revisions 
	SromLayoutFilter = class_new("SromLayoutFilter", SromContext)
		class_add_prop(SromLayoutFilter, p_parent, "parent")

	# SROM variable entry
	SromEntry = class_new("SromEntry", AST)
		class_add_prop(SromEntry, p_var, "var")
		class_add_prop(SromEntry, p_revisions, "revisions")
		class_add_prop(SromEntry, p_base_offset, "base_offset")
		class_add_prop(SromEntry, p_type, "type")
		class_add_prop(SromEntry, p_offsets, "offsets")

	# SROM variable offset
	SromOffset = class_new("SromOffset", AST)
		class_add_prop(SromOffset, p_segments, "segments")

	# SROM variable offset segment
	SromSegment = class_new("SromSegment", AST)
		class_add_prop(SromSegment, p_offset, "offset")
		class_add_prop(SromSegment, p_type, "type")
		class_add_prop(SromSegment, p_mask, "mask")
		class_add_prop(SromSegment, p_shift, "shift")
		class_add_prop(SromSegment, p_value, "value")

	# Create the parse state stack
	_g_parse_stack_depth = 0
	_g_parse_stack[0] = null

	# Push the root parse state
	parser_state_push(nvram_new(), 0)
}

function at_exit(_block_start, _state, _output_vars, _noutput_vars, _name, _var,
    _i)
{
	# Skip completion handling if exiting from an error
	if (_EARLY_EXIT)
		exit 1

	# Check for complete block closure
	if (!in_parser_context(NVRAM)) {
		_state = parser_state_get()
		_block_start = get(_state, p_line)
		errorx("missing '}' for block opened on line " _block_start "")
	}

	# Apply lexicographical sorting to our variable names. To support more
	# effecient table searching, we guarantee a stable sort order (using C
	# collation).
	#
	# This also has a side-effect of generating a unique monotonic ID
	# for all variables, which we will emit as a #define and can use as a
	# direct index into the C variable table
	_output_vars = array_new()
	for (_name in _g_var_names) {
		_var = _g_var_names[_name]

		# Don't include internal variables in the output
		if (var_is_internal(_var))
			continue

		array_append(_output_vars, _var)
	}

	# Sort by variable name
	array_sort(_output_vars, prop_to_path(p_name))

	# Set all variable ID properties to their newly assigned ID value
	_noutput_vars = array_size(_output_vars)
	for (_i = 0; _i < _noutput_vars; _i++) {
		_var = array_get(_output_vars, _i)
		set(_var, p_vid, _i)
	}

	# Truncate output file and write common header
	printf("") > OUTPUT_FILE
	emit("/*\n")
	emit(" * THIS FILE IS AUTOMATICALLY GENERATED. DO NOT EDIT.\n")
	emit(" *\n")
	emit(" * generated from nvram map: " FILENAME "\n")
	emit(" */\n")
	emit("\n")

	# Emit all variable definitions
	if (OUT_T == OUT_T_DATA) {
		write_data(_output_vars)
	} else if (OUT_T == OUT_T_HEADER) {
		write_header(_output_vars)
	}
	if (VERBOSE == 1) {
	        printf("%u variable records written to %s\n", array_size(_output_vars),
		       OUTPUT_FILE) >> "/dev/stderr"
	}
}

# Write the public header (output type HEADER)
function write_header(output_vars, _noutput_vars, _var,
    _tab_align, _macro, _macros, _num_macros, _i)
{
	# Produce our array of #defines
	_num_macros = 0
	_noutput_vars = array_size(output_vars)
	for (_i = 0; _i < _noutput_vars; _i++) {
		_var = array_get(output_vars, _i)

		# Variable name
		_macro = var_get_macro(_var, MTypeVarName, \
		    "\"" get(_var, p_name) "\"")
		_macros[_num_macros++] = _macro

		# Variable array length
		if (var_has_array_type(_var)) {			
			_macro = var_get_macro(_var, MTypeVarMaxLen,
			    var_get_array_len(_var))
			_macros[_num_macros++] = _macro
		}
	}

	# Calculate value tab alignment position for our macros
	_tab_align = macros_get_tab_alignment(_macros, _num_macros)

	# Write the macros
	for (_i = 0; _i < _num_macros; _i++)
		write_macro_define(_macros[_i], _tab_align)
}

# Write the private data header (output type DATA)
function write_data(output_vars, _noutput_vars, _var, _nvram, _layouts,
    _nlayouts, _layout, _revs, _rev, _rev_start, _rev_end, _base_type,
    _srom_table, _nsrom_table, _i, _j)
{
	_nvram = parser_state_get_context(NVRAM)
	_layouts = get(_nvram, p_srom_layouts)
	_nlayouts = array_size(_layouts)

	_noutput_vars = array_size(output_vars)

	# Write all our private NVAR_ID defines
	write_data_defines(output_vars)

	# Write all layout binding opcodes, and build an array
	# mapping SROM revision to corresponding SROM layout
	_srom_table = array_new()
	for (_i = 0; _i < _nlayouts; _i++) {
		_layout = array_get(_layouts, _i)

		# Write binding opcode table to our output file
		write_srom_bindings(_layout)

		# Add entries to _srom_table for all covered revisions
		_revs = get(_layout, p_revisions)
		_rev_start = get(_revs, p_start)
		_rev_end = get(_revs, p_end)

		for (_j = _rev_start; _j <= _rev_end; _j++)
			array_append(_srom_table, _j)
	}

	# Sort in ascending order, by SROM revision
	array_sort(_srom_table)
	_nsrom_table = array_size(_srom_table)

	# Write the variable definitions
	emit("/* Variable definitions */\n")
	emit("const struct bhnd_nvram_vardefn " \
	    "bhnd_nvram_vardefns[] = {\n")
	output_depth++
	for (_i = 0; _i < _noutput_vars; _i++) {
		write_data_nvram_vardefn(array_get(output_vars, _i))
	}
	output_depth--
	emit("};\n")
	emit("const size_t bhnd_nvram_num_vardefns = " _noutput_vars ";\n")

	# Write static asserts for raw type constant values that must be kept
	# synchronized with the code
	for (_i = 0; _i < BaseTypesCount; _i++) {
		_base_type = array_get(BaseTypesArray, _i)

		emit(sprintf("_Static_assert(%s == %u, \"%s\");\n",
		    type_get_const(_base_type), type_get_const_val(_base_type),
		    "type constant out of sync"))

		emit(sprintf("_Static_assert(%s == %u, \"%s\");\n",
		    get(_base_type, p_array_const),
		    get(_base_type, p_array_const_val),
		    "array type constant out of sync"))
	}

	# Write all top-level bhnd_sprom_layout entries
	emit("/* SPROM layouts */\n")
	emit("const struct bhnd_sprom_layout bhnd_sprom_layouts[] = {\n")
	output_depth++
	for (_i = 0; _i < _nsrom_table; _i++) {
		_rev = array_get(_srom_table, _i)
		_layout = nvram_get_srom_layout(_nvram, _rev)
		write_data_srom_layout(_layout, _rev)
	}
	output_depth--
	emit("};\n")
	emit("const size_t bhnd_sprom_num_layouts = " _nsrom_table ";\n")
}

# Write a bhnd_nvram_vardef entry for the given variable
function write_data_nvram_vardefn(v, _desc, _help, _type, _fmt) {
	obj_assert_class(v, Var)

	_desc = get(v, p_desc)
	_help = get(v, p_help)
	_type = get(v, p_type)
	_fmt = var_get_fmt(v)

	emit("{\n")
	output_depth++
	emit(sprintf(".name = \"%s\",\n", get(v, p_name)))

	if (_desc != null)
		emit(sprintf(".desc = \"%s\",\n", get(_desc, p_value)))
	else
		emit(".desc = NULL,\n")

	if (_help != null)
		emit(sprintf(".help = \"%s\",\n", get(_help, p_value)))
	else
		emit(".help = NULL,\n")

	emit(".type = " type_get_const(_type) ",\n")
	emit(".nelem = " var_get_array_len(v) ",\n")
	emit(".fmt = &" get(_fmt, p_symbol) ",\n")
	emit(".flags = " gen_var_flags(v) ",\n")

	output_depth--
	emit("},\n")
}

# Write a top-level bhnd_sprom_layout entry for the given revision
# and layout definition
function write_data_srom_layout(layout, revision, _flags, _size,
    _sromcrc, _crc_seg, _crc_off,
    _sromsig, _sig_seg, _sig_offset, _sig_value,
    _sromrev, _rev_seg, _rev_off,
    _num_vars)
{
	_flags = array_new()

	# Calculate the size; it always follows the internal CRC variable
	_sromcrc = srom_layout_find_entry(layout, "<sromcrc>", revision)
	if (_sromcrc == null) {
		errorx("missing '<sromcrc>' entry for '"revision"' layout, " \
		    "cannot compute total size")
	} else {
		_crc_seg = srom_entry_get_single_segment(_sromcrc)
		_crc_off = get(_crc_seg, p_offset)
		_size = _crc_off
		_size += get(get(_crc_seg, p_type), p_width)
	}

	# Fetch signature definition
	_sromsig = srom_layout_find_entry(layout, "<sromsig>", revision)
	if (_sromsig == null) {
		array_append(_flags, "SPROM_LAYOUT_MAGIC_NONE")
	} else {
		_sig_seg = srom_entry_get_single_segment(_sromsig)

		_sig_offset = get(_sig_seg, p_offset)
		_sig_value = get(_sig_seg, p_value)
		if (_sig_value == "")
			errorc(get(_sromsig, p_line), "missing signature value")
	}

	# Fetch sromrev definition
	_sromrev = srom_layout_find_entry(layout, "sromrev", revision)
	if (_sromrev == null) {
		errorx("missing 'sromrev' entry for '"revision"' layout, " \
		    "cannot determine offset")
	} else {
		# Must be a u8 value
		if (!type_equal(get(_sromrev, p_type), UInt8)) {
			errorx("'sromrev' entry has non-u8 type '" \
			    type_to_string(get(_sromrev, p_type)))
		}

		_rev_seg = srom_entry_get_single_segment(_sromrev)
		_rev_off = get(_rev_seg, p_offset)
	}

	# Write layout entry
	emit("{\n")
	output_depth++
	emit(".size = "_size",\n")
	emit(".rev = "revision",\n")

	if (array_size(_flags) > 0) {
		emit(".flags = " array_join(_flags, "|") ",\n")
	} else {
		emit(".flags = 0,\n")
	}

	emit(".srev_offset = " _rev_off ",\n")

	if (_sromsig != null) {
		emit(".magic_offset = " _sig_offset ",\n")
		emit(".magic_value = " _sig_value ",\n")
	} else {
		emit(".magic_offset = 0,\n")
		emit(".magic_value = 0,\n")
	}

	emit(".crc_offset = " _crc_off ",\n")

	emit(".bindings = " srom_layout_get_variable_name(layout) ",\n")
	emit(".bindings_size = nitems(" \
	    srom_layout_get_variable_name(layout) "),\n")

	emit(".num_vars = " srom_layout_num_output_vars(layout, revision) ",\n")

	obj_delete(_flags)

	output_depth--
	emit("},\n");
}

# Create a new opstream encoding state instance for the given layout
function srom_ops_new(layout, _obj) {
	obj_assert_class(layout, SromLayout)

	_obj = obj_new(SromOpStream)
	set(_obj, p_layout, layout)
	set(_obj, p_revisions, get(layout, p_revisions))
	set(_obj, p_vid, 0)
	set(_obj, p_offset, 0)
	set(_obj, p_type, null)
	set(_obj, p_mask, null)
	set(_obj, p_shift, null)

	return (_obj)
}

# Return the current type width, or throw an error if no type is currently
# specified.
function srom_ops_get_type_width(opstream, _type)
{
	obj_assert_class(opstream, SromOpStream)

	_type = get(opstream, p_type)
	if (_type == null)
		errorx("no type value set")

	return (get(type_get_base(_type), p_width))
}

# Write a string to the SROM opcode stream, either buffering the write,
# or emitting it directly.
function srom_ops_emit(opstream, string, _pending_bind, _buffer) {
	obj_assert_class(opstream, SromOpStream)

	# Buffered?
	if ((_pending_bind = get(opstream, p_pending_bind)) != null) {
		_buffer = get(_pending_bind, p_buffer)
		array_append(_buffer, string)
		return
	}

	# Emit directly
	emit(string)
}

# Emit a SROM opcode followed by up to four optional bytes
function srom_ops_emit_opcode(opstream, opcode, arg0, arg1, arg2, arg3) {
	obj_assert_class(opstream, SromOpStream)

	srom_ops_emit(opstream, opcode",\n")
	if (arg0 != "") srom_ops_emit(opstream, arg0",\n")
	if (arg1 != "") srom_ops_emit(opstream, arg1",\n")
	if (arg2 != "") srom_ops_emit(opstream, arg2",\n")
	if (arg3 != "") srom_ops_emit(opstream, arg3",\n")
}

# Emit a SROM opcode and associated integer value, choosing the best
# SROM_OP_DATA variant for encoding the value.
#
# opc:		The standard opcode for non-IMM encoded data, or null if none
# opc_imm:	The IMM opcode, or null if none
# value:	The value to encode
# svalue:	Symbolic representation of value to include in output, or null
function srom_ops_emit_int_opcode(opstream, opc, opc_imm, value, svalue,
    _width, _offset, _delta)
{
	obj_assert_class(opstream, SromOpStream)

	# Fetch current type width
	_width = srom_ops_get_type_width(opstream)

	# Special cases:
	if (opc_imm == SPROM_OPCODE_SHIFT_IMM) {
		# SHIFT_IMM -- the imm value must be positive and divisible by
		# two (shift/2) to use the IMM form.
		if (value >= 0 && value % 2 == 0) {
			value = (value/2)
			opc = null
		} else {
			opc_imm = null
		}
	} else if (opc_imm == SPROM_OPCODE_OFFSET_REL_IMM) {
		# OFFSET_REL_IMM -- the imm value must be positive, divisible
		# by the type width, and relative to the last offset to use
		# the IMM form.

		# Assert that callers correctly flushed any pending bind before
		# attempting to set a relative offset
		if (get(opstream, p_pending_bind) != null)
			errorx("can't set relative offset with a pending bind")

		# Fetch current offset, calculate relative value and determine
		# whether we can issue an IMM opcode
		_offset = get(opstream, p_offset)
		_delta = value - _offset
		if (_delta >= 0 &&
		    _delta % _width == 0 &&
		    (_delta/_width) <= SPROM_OP_IMM_MAX)
		{
			srom_ops_emit(opstream,
			    sprintf("/* %#x + %#x -> %#x */\n", _offset,
			    _delta, value))
			value = (_delta / _width)
			opc = null
		} else {
			opc_imm = null
		}
	}

	# If no symbolic representation provided, write the raw value
	if (svalue == null)
		svalue = value

	# Try to encode as IMM value?
	if (opc_imm != null && value >= 0 && value <= SPROM_OP_IMM_MAX) {
		srom_ops_emit_opcode(opstream, "("opc_imm"|"svalue")")
		return
	}

	# Can't encode as immediate; do we have a non-immediate form?
	if (opc == null)
		errorx("can't encode '" value "' as immediate, and no " \
		    "non-immediate form was provided")

	# Determine and emit minimal encoding
	# We let the C compiler perform the bit operations, rather than
	# trying to wrestle awk's floating point arithmetic
	if (value < 0) {
		# Only Int8 is used
		 if (value < Int8Min)
			 errorx("cannot int8 encode '" value "'")

		srom_ops_emit_opcode(opstream,
		    "("opc"|"SPROM_OP_DATA_I8")", svalue)

	} else if (value <= UInt8Max) {

		srom_ops_emit_opcode(opstream,
		    "("opc"|"SPROM_OP_DATA_U8")", svalue)

	} else if (value % _width == 0 && (value / _width) <= UInt8Max) {

		srom_ops_emit_opcode(opstream,
		    "("opc"|"SPROM_OP_DATA_U8_SCALED")", svalue / _width)

	} else if (value <= UInt16Max) {

		srom_ops_emit_opcode(opstream,
		    "("opc"|"SPROM_OP_DATA_U16")", 
		    "("svalue" & 0xFF)",
		    "("svalue" >> 8)")

	} else if (value <= UInt32Max) {

		srom_ops_emit_opcode(opstream,
		    "("opc"|"SPROM_OP_DATA_U32")", 
		    "("svalue" & 0xFF)",
		    "(("svalue" >> 8) & 0xFF)",
		    "(("svalue" >> 16) & 0xFF)",
		    "(("svalue" >> 24) & 0xFF)")

	} else {
		errorx("can't encode '" value "' (too large)")
	}
}

# Emit initial OPCODE_VAR opcode and update opstream state
function srom_ops_reset_var(opstream, var, _vid_prev, _vid, _vid_name,
    _type, _base_type)
{
	obj_assert_class(opstream, SromOpStream)
	obj_assert_class(var, Var)

	# Flush any pending bind for the previous variable
	srom_ops_flush_bind(opstream, 1)

	# Fetch current state
	_vid_prev = get(opstream, p_vid)

	_vid = get(var, p_vid)
	_vid_name = var_get_macro_name(var, MTypeVarID)

	# Update state
	_type = get(var, p_type)
	set(opstream, p_vid, _vid)
	set(opstream, p_type, type_get_base(_type))
	set(opstream, p_nelem, var_get_array_len(var))
	set(opstream, p_mask, type_get_default_mask(_type))
	set(opstream, p_shift, 0)
	set(opstream, p_bind_total, 0)

	# Always provide a human readable comment
	srom_ops_emit(opstream, sprintf("/* %s (%#x) */\n", get(var, p_name),
	    get(opstream, p_offset)))

	# Prefer a single VAR_IMM byte
	if (_vid_prev == 0 || _vid <= SPROM_OP_IMM_MAX) {
		srom_ops_emit_int_opcode(opstream,
		    null, SPROM_OPCODE_VAR_IMM,
		    _vid, _vid_name)
		return
	}

	# Try encoding as a single VAR_REL_IMM byte
	if (_vid_prev <= _vid && (_vid - _vid_prev) <= SPROM_OP_IMM_MAX) {
		srom_ops_emit_int_opcode(opstream,
		    null, SPROM_OPCODE_VAR_REL_IMM,
		    _vid - _vid_prev, null)
		return
	}

	# Fall back on a multibyte encoding
	srom_ops_emit_int_opcode(opstream, SPROM_OPCODE_VAR, null, _vid,
	    _vid_name)
}

# Emit OPCODE_REV/OPCODE_REV_RANGE (if necessary) for a new revision range
function srom_ops_emit_revisions(opstream, revisions, _prev_revs,
    _start, _end)
{
	obj_assert_class(opstream, SromOpStream)
	_prev_revs = get(opstream, p_revisions)

	if (revrange_equal(_prev_revs, revisions))
		return;

	# Update stream state
	set(opstream, p_revisions, revisions)

	_start = get(revisions, p_start)
	_end = get(revisions, p_end)

	# Sanity-check range values
	if (_start < 0 || _end < 0)
		errorx("invalid range: " revrange_to_string(revisions))

	# If range covers a single revision, and can be encoded within
	# SROM_OP_IMM_MAX, we can use the single byte encoding
	if (_start == _end && _start <= SPROM_OP_IMM_MAX) {
		srom_ops_emit_int_opcode(opstream,
		    null, SPROM_OPCODE_REV_IMM, _start)
		return
	}

	# Otherwise, we need to use the two byte range encoding
	if (_start > SPROM_OP_REV_RANGE_MAX || _end > SPROM_OP_REV_RANGE_MAX) {
		errorx(sprintf("cannot encode range values %s (>= %u)",
		    revrange_to_string(revisions), SPROM_OP_REV_RANGE_MAX))
	}

	srom_ops_emit_opcode(opstream,
	    SPROM_OPCODE_REV_RANGE,
	    sprintf("(%u << %s) | (%u << %s)",
		_start, SPROM_OP_REV_START_SHIFT,
		_end, SPROM_OP_REV_END_SHIFT))
}

# Emit OPCODE_OFFSET (if necessary) for a new offset
function srom_ops_emit_offset(opstream, offset, _prev_offset, _rel_offset,
    _bind)
{
	obj_assert_class(opstream, SromOpStream)

	# Flush any pending bind before adjusting the offset
	srom_ops_flush_bind(opstream, 0)

	# Fetch current offset
	_prev_offset = get(opstream, p_offset)
	if (_prev_offset == offset)
		return

	# Encode (possibly a relative, 1-byte form) of the offset opcode
	srom_ops_emit_int_opcode(opstream, SPROM_OPCODE_OFFSET,
	    SPROM_OPCODE_OFFSET_REL_IMM, offset, null)

	# Update state
	set(opstream, p_offset, offset)
}

# Emit OPCODE_TYPE (if necessary) for a new type value; this also
# resets the mask to the type default.
function srom_ops_emit_type(opstream, type, _base_type, _prev_type, _prev_mask,
    _default_mask)
{
	obj_assert_class(opstream, SromOpStream)
	if (!obj_is_instanceof(type, ArrayType))
		obj_assert_class(type, Type)

	_default_mask = type_get_default_mask(type)
	_base_type = type_get_base(type)

	# If state already matches the requested type, nothing to be
	# done
	_prev_type = get(opstream, p_type)
	_prev_mask = get(opstream, p_mask)
	if (type_equal(_prev_type, _base_type) && _prev_mask == _default_mask)
		return

	# Update state
	set(opstream, p_type, _base_type)
	set(opstream, p_mask, _default_mask)

	# Emit opcode.
	if (type_get_const_val(_base_type) <= SPROM_OP_IMM_MAX) {
		# Single byte IMM encoding
		srom_ops_emit_opcode(opstream,
		    SPROM_OPCODE_TYPE_IMM "|" type_get_const(_base_type))
	} else {
		# Two byte encoding
		srom_ops_emit_opcode(opstream, SPROM_OPCODE_TYPE,
		    type_get_const(_base_type))
	}
}

# Emit OPCODE_MASK (if necessary) for a new mask value
function srom_ops_emit_mask(opstream, mask, _prev_mask) {
	obj_assert_class(opstream, SromOpStream)
	_prev_mask = get(opstream, p_mask)

	if (_prev_mask == mask)
		return

	set(opstream, p_mask, mask)
	srom_ops_emit_int_opcode(opstream,
	    SPROM_OPCODE_MASK, SPROM_OPCODE_MASK_IMM,
	    mask, sprintf("0x%x", mask))
}

# Emit OPCODE_SHIFT (if necessary) for a new shift value
function srom_ops_emit_shift(opstream, shift, _prev_shift) {
	obj_assert_class(opstream, SromOpStream)
	_prev_shift = get(opstream, p_shift)

	if (_prev_shift == shift)
		return

	set(opstream, p_shift, shift)
	srom_ops_emit_int_opcode(opstream,
	    SPROM_OPCODE_SHIFT, SPROM_OPCODE_SHIFT_IMM,
	    shift, null)
}

# Return true if a valid BIND/BINDN encoding exists for the given SKIP_IN
# value, false if the skip values exceed the limits of the bind opcode
# family.
function srom_ops_can_encode_skip_in(skip_in) {
	return (skip_in >= SPROM_OP_BIND_SKIP_IN_MIN &&
	    skip_in <= SPROM_OP_BIND_SKIP_IN_MAX)
}

# Return true if a valid BIND/BINDN encoding exists for the given SKIP_OUT
# value, false if the skip values exceed the limits of the bind opcode
# family.
function srom_ops_can_encode_skip_out(skip_out) {
	return (skip_in >= SPROM_OP_BIND_SKIP_IN_MIN &&
	    skip_in <= SPROM_OP_BIND_SKIP_IN_MAX)
}

# Return true if a valid BIND/BINDN encoding exists for the given skip
# values, false if the skip values exceed the limits of the bind opcode
# family.
function srom_ops_can_encode_skip(skip_in, skip_out) {
	return (srom_ops_can_encode_skip_in(skip_in) &&
	    srom_ops_can_encode_skip_out(skip_out))
}

# Create a new SromOpBind instance for the given segment
function srom_opbind_new(segment, skip_in, skip_out, _obj, _type, _width,
    _offset)
{
	obj_assert_class(segment, SromSegment)

	# Verify that an encoding exists for the skip values
	if (!srom_ops_can_encode_skip_in(skip_in)) {
		errorx(sprintf("cannot encode SKIP_IN=%d; maximum supported " \
		    "range %d-%d", skip_in,
		    SPROM_OP_BIND_SKIP_IN_MIN, SPROM_OP_BIND_SKIP_IN_MAX))
	}

	if (!srom_ops_can_encode_skip_out(skip_out)) {
		errorx(sprintf("cannot encode SKIP_OUT=%d; maximum supported " \
		    "range %d-%d", skip_out,
		    SPROM_OP_BIND_SKIP_OUT_MIN, SPROM_OP_BIND_SKIP_OUT_MAX))
	}

	# Fetch basic segment info
	_offset = get(segment, p_offset)
	_type = srom_segment_get_base_type(segment)
	_width = get(_type, p_width)

	# Construct new instance
	_obj = obj_new(SromOpBind)

	set(_obj, p_segment, segment)
	set(_obj, p_count, 1)
	set(_obj, p_offset, _offset)
	set(_obj, p_width, _width)
	set(_obj, p_skip_in, skip_in)
	set(_obj, p_skip_out, skip_out)
	set(_obj, p_buffer, array_new())

	return (_obj)
}

# Try to coalesce a BIND for the given segment with an existing bind request,
# returning true on success, or false if the two segments cannot be coalesced
# into the existing request
function srom_opbind_append(bind, segment, skip_out, _bind_seg, _bind_off,
    _width, _count, _skip_in, _seg_offset, _delta)
{
	obj_assert_class(bind, SromOpBind)
	obj_assert_class(segment, SromSegment)

	# Are the segments compatible?
	_bind_seg = get(bind, p_segment)
	if (!srom_segment_attributes_equal(_bind_seg, segment))
		return (0)

	# Are the output skip values compatible?
	if (get(bind, p_skip_out) != skip_out)
		return (0)

	# Find bind offset/count/width/skip
	_bind_off = get(bind, p_offset)
	_count = get(bind, p_count)
	_skip_in = get(bind, p_skip_in)
	_width = get(bind, p_width)

	# Fetch new segment's offset
	_seg_offset = get(segment, p_offset)

	# If there's only one segment in the bind op, we ned to compute the
	# skip value to be used for all later segments (including the
	# segment we're attempting to append)
	#
	# If there's already multiple segments, we just need to verify that
	# the bind_offset + (count * width * skip_in) produces the new
	# segment's offset
	if (_count == 1) {
		# Determine the delta between the two segment offsets. This
		# must be a multiple of the type width to be encoded
		# as a BINDN entry
		_delta = _seg_offset - _bind_off
		if ((_delta % _width) != 0)
			return (0)

		# The skip byte count is calculated as (type width * skip)
		_skip_in = _delta / _width

		# Is the skip encodable?
		if (!srom_ops_can_encode_skip_in(_skip_in))
			return (0)

		# Save required skip
		set(bind, p_skip_in, _skip_in)
	} else if (_count > 1) {
		# Get the final offset of the binding if we were to add
		# one additional segment
		_bind_off = _bind_off + (_width * _skip_in * (_count + 1))

		# If it doesn't match our segment's offset, we can't
		# append this segment
		if (_bind_off != _seg_offset)
			return (0)
	}

	# Success! Increment the bind count in the existing bind
	set(bind, p_count, _count + 1)
	return (1)
}

# Return true if the given binding operation can be omitted from the output
# if it would be immediately followed by a VAR, VAR_REL_IMM, or EOF opcode.
#
# The bind operatin must be configured with default count, skip_in, and 
# skip_out values of 1, and must contain no buffered post-BIND opcodes 
function srom_opbind_is_implicit_encodable(bind) {
	obj_assert_class(bind, SromOpBind)

	if (get(bind, p_count) != 1)
		return (0)

	if (get(bind, p_skip_in) != 1)
		return (0)

	if (get(bind, p_skip_out) != 1)
		return (0)

	if (array_size(get(bind, p_buffer)) != 0)
		return (0)

	return (1)
}


# Encode all segment settings for a single offset segment, followed by a bind
# request.
#
# opstream:	Opcode stream
# segment:	Segment to be written
# continued:	If this segment's value should be OR'd with the value of a
#		following segment
function srom_ops_emit_segment(opstream, segment, continued, _value,
    _bind, _skip_in, _skip_out)
{
	obj_assert_class(opstream, SromOpStream)
	obj_assert_class(segment, SromSegment)

	# Determine basic bind parameters
	_count = 1
	_skip_in = 1
	_skip_out = continued ? 0 : 1

	# Try to coalesce with a pending binding
	if ((_bind = get(opstream, p_pending_bind)) != null) {
		if (srom_opbind_append(_bind, segment, _skip_out))
			return
	}

	# Otherwise, flush any pending bind and enqueue our own
	srom_ops_flush_bind(opstream, 0)
	if (get(opstream, p_pending_bind))
		errorx("bind not flushed!")

	# Encode type
	_value = get(segment, p_type)
	srom_ops_emit_type(opstream, _value)

	# Encode offset
	_value = get(segment, p_offset)
	srom_ops_emit_offset(opstream, _value)

	# Encode mask
	_value = get(segment, p_mask)
	srom_ops_emit_mask(opstream, _value)

	# Encode shift
	_value = get(segment, p_shift)
	srom_ops_emit_shift(opstream, _value)

	# Enqueue binding with opstream
	_bind = srom_opbind_new(segment, _skip_in, _skip_out)
	set(opstream, p_pending_bind, _bind)
}

# (private) Adjust the stream's input offset by applying the given bind
# operation's skip_in * width * count.
function _srom_ops_apply_bind_offset(opstream, bind, _count, _offset, _width,
    _skip_in, _opstream_offset)
{
	obj_assert_class(opstream, SromOpStream)
	obj_assert_class(bind, SromOpBind)

	_opstream_offset = get(opstream, p_offset)
	_offset = get(bind, p_offset)
	if (_opstream_offset != _offset)
		errorx("stream/bind offset state mismatch")

	_count = get(bind, p_count)
	_width = get(bind, p_width)
	_skip_in = get(bind, p_skip_in)

	set(opstream, p_offset,
	    _opstream_offset + ((_width * _skip_in) * _count))
}

# (private) Write a bind instance and all buffered opcodes
function _srom_ops_emit_bind(opstream, bind, _count, _skip_in, _skip_out,
    _off_start, _width, _si_signbit, _written, _nbuffer, _buffer)
{
	obj_assert_class(opstream, SromOpStream)
	obj_assert_class(bind, SromOpBind)

	# Assert that any pending bind state has already been cleared
	if (get(opstream, p_pending_bind) != null)
		errorx("cannot flush bind with an existing pending_bind active")

	# Fetch (and assert valid) our skip values
	_skip_in = get(bind, p_skip_in)
	_skip_out = get(bind, p_skip_out)

	if (!srom_ops_can_encode_skip(_skip_in, _skip_out))
		errorx("invalid skip values in buffered bind")

	# Determine SKIP_IN sign bit
	_si_signbit = "0"
	if (_skip_in < 0)
		_si_signbit = SPROM_OP_BIND_SKIP_IN_SIGN

	# Emit BIND/BINDN opcodes until the full count is encoded
	_count = get(bind, p_count)
	while (_count > 0) {
		if (_count > 1 && _count <= SPROM_OP_IMM_MAX &&
		    _skip_in == 1 && _skip_out == 1)
		{
			# The one-byte BINDN form requires encoding the count
			# as a IMM, and has an implicit in/out skip of 1.
			srom_ops_emit_opcode(opstream,
			    "("SPROM_OPCODE_DO_BINDN_IMM"|"_count")")
			_count -= _count

		} else if (_count > 1) {
			# The two byte BINDN form can encode skip values and a
			# larger U8 count
			_written = min(_count, UInt8Max)

			srom_ops_emit_opcode(opstream,
			    sprintf("(%s|%s|(%u<<%s)|(%u<<%s))",
				SPROM_OPCODE_DO_BINDN,
				_si_signbit,
				abs(_skip_in), SPROM_OP_BIND_SKIP_IN_SHIFT,
				_skip_out, SPROM_OP_BIND_SKIP_OUT_SHIFT),
			    _written)
			_count -= _written

		} else {
			# The 1-byte BIND form can encode the same SKIP values
			# as the 2-byte BINDN, with a implicit count of 1
			srom_ops_emit_opcode(opstream,
			    sprintf("(%s|%s|(%u<<%s)|(%u<<%s))",
				SPROM_OPCODE_DO_BIND,
				_si_signbit,
				abs(_skip_in), SPROM_OP_BIND_SKIP_IN_SHIFT,
				_skip_out, SPROM_OP_BIND_SKIP_OUT_SHIFT))
			_count--
		}
	}

	# Update the stream's input offset
	_srom_ops_apply_bind_offset(opstream, bind)

	# Write any buffered post-BIND opcodes
	_buffer = get(bind, p_buffer)
	_nbuffer = array_size(_buffer)
	for (_i = 0; _i < _nbuffer; _i++)
		srom_ops_emit(opstream, array_get(_buffer, _i))
}

# Flush any buffered binding
function srom_ops_flush_bind(opstream, allow_implicit, _bind, _bind_total)
{
	obj_assert_class(opstream, SromOpStream)

	# If no pending bind, nothing to flush
	if ((_bind = get(opstream, p_pending_bind)) == null)
		return

	# Check the per-variable bind count to determine whether
	# we can encode an implicit bind.
	#
	# If there have been any explicit bind statements, implicit binding
	# cannot be used.
	_bind_total = get(opstream, p_bind_total)
	if (allow_implicit && _bind_total > 0) {
		# Disable implicit encoding; explicit bind statements have
		# been issued for this variable previously.
		allow_implicit = 0
	}

	# Increment bind count
	set(opstream, p_bind_total, _bind_total + 1)

	# Clear the property value
	set(opstream, p_pending_bind, null)

	# If a pending bind operation can be encoded as an implicit bind,
	# emit a descriptive comment and update the stream state.
	#
	# Otherwise, emit the full set of bind opcode(s)
	_base_off = get(opstream, p_offset)
	if (allow_implicit && srom_opbind_is_implicit_encodable(_bind)) {
		# Update stream's input offset
		_srom_ops_apply_bind_offset(opstream, _bind)
	} else {
		_srom_ops_emit_bind(opstream, _bind)
	}

	# Provide bind information as a comment
	srom_ops_emit(opstream,
	    sprintf("/* bind (%s @ %#x -> %#x) */\n",
		type_to_string(get(opstream, p_type)),
		_base_off, get(opstream, p_offset)))

	# Clean up
	obj_delete(_bind)
}

# Write OPCODE_EOF after flushing any buffered writes
function srom_ops_emit_eof(opstream) {
	obj_assert_class(opstream, SromOpStream)

	# Flush any buffered writes
	srom_ops_flush_bind(opstream, 1)

	# Emit an explicit VAR_END opcode for the last entry
	srom_ops_emit_opcode(opstream, SPROM_OPCODE_VAR_END)

	# Emit EOF
	srom_ops_emit_opcode(opstream, SPROM_OPCODE_EOF)
}

# Write the SROM offset segment bindings to the opstream
function write_srom_offset_bindings(opstream, offsets,
    _noffsets, _offset, _segs, _nsegs, _segment, _cont,
    _i, _j)
{
	_noffsets = array_size(offsets)
	for (_i = 0; _i < _noffsets; _i++) {
		# Encode each segment in this offset 
		_offset = array_get(offsets, _i)
		_segs = get(_offset, p_segments)
		_nsegs = array_size(_segs)

		for (_j = 0; _j < _nsegs; _j++) {
			_segment = array_get(_segs, _j)
			_cont = 0
			
			# Should this value be OR'd with the next segment?
			if (_j+1 < _nsegs)
				_cont = 1 

			# Encode segment
			srom_ops_emit_segment(opstream, _segment, _cont)
		}
	}
}

# Write the SROM entry stream for a SROM entry to the output file
function write_srom_entry_bindings(entry, opstream, _var, _vid,
    _var_type, _entry_type, _offsets, _noffsets)
{
	_var = get(entry, p_var)
	_vid = get(_var, p_vid)

	# Encode revision switch. This resets variable state, so must
	# occur before any variable definitions to which it applies
	srom_ops_emit_revisions(opstream, get(entry, p_revisions))

	# Encode variable ID
	srom_ops_reset_var(opstream, _var, _vid)
	output_depth++

	# Write entry-specific array length (SROM layouts may define array
	# mappings with fewer elements than in the variable definition)
	if (srom_entry_has_array_type(entry)) {
		_var_type = get(_var, p_type)
		_entry_type = get(entry, p_type)

		# If the array length differs from the variable default,
		# write an OPCODE_EXT_NELEM entry
		if (type_get_nelem(_var_type) != type_get_nelem(_entry_type)) {
			srom_ops_emit_opcode(opstream, SPROM_OPCODE_NELEM,
			    srom_entry_get_array_len(entry))
		}
	}

	# Write offset segment bindings
	_offsets = get(entry, p_offsets)
	write_srom_offset_bindings(opstream, _offsets)
	output_depth--
}

# Write a SROM layout binding opcode table to the output file
function write_srom_bindings(layout, _varname, _var, _all_entries,
    _nall_entries, _entries, _nentries, _entry, _opstream, _i)
{
	_varname = srom_layout_get_variable_name(layout)
	_all_entries = get(layout, p_entries)
	_opstream = srom_ops_new(layout)

	#
	# Collect all entries to be included in the output, and then
	# sort by their variable's assigned ID (ascending).
	#
	# The variable IDs were previously assigned in lexigraphical sort
	# order; since the variable *offsets* tend to match this order, this
	# works out well for our compact encoding, allowing us to make use of
	# compact relative encoding of both variable IDs and variable offsets.
	#
	_entries = array_new()
	_nall_entries = array_size(_all_entries)
	for (_i = 0; _i < _nall_entries; _i++) {
		_entry = array_get(_all_entries, _i)
		_var = get(_entry, p_var)

		# Skip internal variables
		if (var_is_internal(_var))
			continue

		# Sanity check variable ID assignment
		if (get(_var, p_vid) == "")
			errorx("missing variable ID for " obj_to_string(_var))
	
		array_append(_entries, _entry)
	}

	# Sort entries by (variable ID, revision range), ascending
	array_sort(_entries, prop_path_create(p_var, p_vid),
	    prop_path_create(p_revisions, p_start),
	    prop_path_create(p_revisions, p_end))

	# Emit all entry binding opcodes
	emit("static const uint8_t " _varname "[] = {\n")
	output_depth++

	_nentries = array_size(_entries)
	for (_i = 0; _i < _nentries; _i++) {
		_entry = array_get(_entries, _i)
		write_srom_entry_bindings(_entry, _opstream)
	}

	# Flush and write EOF
	srom_ops_emit_eof(_opstream)

	output_depth--
	emit("};\n")

	obj_delete(_opstream)
	obj_delete(_entries)
}

# Write the BHND_NVAR_<NAME>_ID #defines to the output file
function write_data_defines(output_vars, _noutput_vars, _tab_align, _var,
    _macro, _macros, _num_macros, _i)
{
	# Produce our array of #defines
	_num_macros = 0
	_noutput_vars = array_size(output_vars)
	for (_i = 0; _i < _noutput_vars; _i++) {
		_var = array_get(output_vars, _i)

		# Variable ID
		_macro = var_get_macro(_var, MTypeVarID, get(_var, p_vid))
		_macros[_num_macros++] = _macro
	}

	# Calculate value tab alignment position for our macros
	_tab_align = macros_get_tab_alignment(_macros, _num_macros)

	# Write the #defines
	emit("/* ID constants provide an index into the variable array */\n")
	for (_i = 0; _i < _num_macros; _i++)
		write_macro_define(_macros[_i], _tab_align)
	emit("\n\n");
}

# Calculate the common tab alignment to be used with a set of prefix strings
# with the given maximum length
function tab_alignment(max_len, _tab_align) {
	_tab_align = max_len
	_tab_align += (TAB_WIDTH - (_tab_align % TAB_WIDTH)) % TAB_WIDTH
	_tab_align /= TAB_WIDTH

	return (_tab_align)
}

# Generate and return a tab string that can be appended to a string of
# `strlen` to pad the column out to `align_to`
#
# Note: If the string from which strlen was derived contains tabs, the result
# is undefined
function tab_str(strlen, align_to, _lead, _pad, _result, _i) {
	_lead = strlen
	_lead -= (_lead % TAB_WIDTH);
	_lead /= TAB_WIDTH;

	# Determine required padding to reach the desired alignment
	if (align_to >= _lead)
		_pad = align_to - _lead;
	else
		_pad = 1;

	for (_i = 0; _i < _pad; _i++)
		_result = _result "\t"

	return (_result)
}


# Write a MacroDefine constant, padding the constant out to `align_to`
function write_macro_define(macro, align_to, _tabstr, _i) {
	# Determine required padding to reach the desired alignment
	_tabstr = tab_str(length(get(macro, p_name)), align_to)

	emit("#define\t" get(macro, p_name) _tabstr get(macro, p_value) "\n")
}

# Calculate the tab alignment to be used with a given integer-indexed array
# of Macro instances.
function macros_get_tab_alignment(macros, macros_len, _macro, _max_len, _i) {
	_max_len = 0
	for (_i = 0; _i < macros_len; _i++) {
		_macro = macros[_i]
		_max_len = max(_max_len, length(get(_macro, p_name)))
	}

	return (tab_alignment(_max_len))
}

# Variable group block
$1 == "group" && in_parser_context(NVRAM) {
	parse_variable_group()
}

# Variable definition
(($1 ~ VACCESS_REGEX && $2 ~ TYPES_REGEX) || $1 ~ TYPES_REGEX) &&
    in_parser_context(SymbolContext) \
{
	parse_variable_defn()
}

# Variable "fmt" parameter
$1 == "fmt" && in_parser_context(Var) {
	parse_variable_param($1)
	next
}

# Variable "all1" parameter
$1 == "all1" && in_parser_context(Var) {
	parse_variable_param($1)
	next
}

# Variable desc/help parameters
($1 == "desc" || $1 == "help") && in_parser_context(Var) {
	parse_variable_param($1)
	next
}

# SROM layout block
$1 == "srom" && in_parser_context(NVRAM) {
	parse_srom_layout()
}


# SROM layout revision filter block
$1 == "srom" && in_parser_context(SromLayout) {
	parse_srom_layout_filter()
}

# SROM layout variable entry
$1 ~ "("OFF_REGEX"):$" && \
    (in_parser_context(SromLayout) || in_parser_context(SromLayoutFilter)) \
{
	parse_srom_variable_entry()
}


# SROM entry segment
$1 ~ "("REL_OFF_REGEX"|"OFF_REGEX")[:,|]?" && in_parser_context(SromEntry) {
	parse_srom_entry_segments()
}

# Skip comments and blank lines
/^[ \t]*#/ || /^$/ {
	next
}

# Close blocks
/}/ && !in_parser_context(NVRAM) {
	while (!in_parser_context(NVRAM) && $0 ~ "}") {
		parser_state_close_block();
	}
	next
}

# Report unbalanced '}'
/}/ && in_parser_context(NVRAM) {
	error("extra '}'")
}

# Invalid variable type
$1 && in_parser_context(SymbolContext) {
	error("unknown type '" $1 "'")
}

# Generic parse failure
{
	error("unrecognized statement")
}

# Create a class instance with the given name
function class_new(name, superclass, _class) {
	if (_class != null)
		errorx("class_get() must be called with one or two arguments")

	# Look for an existing class instance
	if (name in _g_class_names)
		errorx("redefining class: " name)

	# Create and register the class object
	_class = obj_new(superclass)
	_g_class_names[name] = _class
	_g_obj[_class,OBJ_IS_CLS] = 1
	_g_obj[_class,CLS_NAME] = name

	return (_class)
}

# Return the class instance with the given name
function class_get(name) {
	if (name in _g_class_names)
		return (_g_class_names[name])

	errorx("no such class " name)
}

# Return the name of cls
function class_get_name(cls) {
	if (cls == null) {
		warnx("class_get_name() called with null class")
		return "<null>"
	}

	if (!obj_is_class(cls))
		errorx(cls " is not a class object")

	return (_g_obj[cls,CLS_NAME])
}

# Return true if the given property property ID is defined on class
function class_has_prop_id(class, prop_id, _super) {
	if (_super != null)
		errorx("class_has_prop_id() must be called with two arguments")

	if (class == null)
		return (0)

	if (prop_id == null)
		return (0)

	# Check class<->prop cache
	if ((class, prop_id) in _g_class_prop_cache)
		return (1)

	# Otherwise, slow path
	if (!obj_is_class(class))
		errorx(class " is not a class object")

	if (_super != null)
		errorx("class_has_prop_id() must be called with two arguments")

	for (_super = class; _super != null; _super = obj_get_class(_super)) {
		if (!((_super,CLS_PROP,prop_id) in _g_obj))
			continue

		# Found; add to class<->prop cache
		_g_class_prop_cache[class,prop_id] = 1
		return (1)
	}

	return (0)
}

# Return true if the given property prop is defined on class
function class_has_property(class, prop) {
	if (!(PROP_ID in prop))
		return (0)

	return (class_has_prop_id(class, prop[PROP_ID]))
}

# Define a `prop` on `class` with the given `name` string
function class_add_prop(class, prop, name, _prop_id) {
	if (_prop_id != null)
		errorx("class_add_prop() must be called with three arguments")

	# Check for duplicate property definition
	if (class_has_property(class, prop))
		errorx("property " prop[PROP_NAME] " already defined on " \
		    class_get_name(class))

	# Init property IDs
	if (_g_prop_ids == null)
		_g_prop_ids = 1

	# Get (or create) new property entry
	if (name in _g_prop_names) {
		_prop_id = _g_prop_names[name]
	} else {
		_prop_id = _g_prop_ids++
		_g_prop_names[name] = _prop_id
		_g_props[_prop_id] = name

		prop[PROP_NAME]	= name
		prop[PROP_ID]	= _prop_id
	}

	# Add to class definition
	_g_obj[class,CLS_PROP,prop[PROP_ID]] = name
	return (name)
}

# Return the property ID for a given class-defined property
function class_get_prop_id(class, prop) {
	if (class == null)
		errorx("class_get_prop_id() on null class")

	if (!class_has_property(class, prop)) {
		errorx("requested undefined property '" prop[PROP_NAME] "on " \
		    class_get_name(class))
	}

	return (prop[PROP_ID])
}

# Return the property ID for a given class-defined property name
function class_get_named_prop_id(class, name, _prop_id) {
	if (class == null)
		errorx("class_get_prop_id() on null class")

	if (!(name in _g_prop_names))
		errorx("requested undefined property '" name "'")

	_prop_id = _g_prop_names[name]

	if (!class_has_prop_id(class, _prop_id)) {
		errorx("requested undefined property '" _g_props[_prop_id] \
		    "' on " class_get_name(class))
	}

	return (_prop_id)
}

# Create a new instance of the given class
function obj_new(class, _obj) {
	if (_obj != null)
		errorx("obj_new() must be called with one argument")

	if (_g_obj_ids == null)
		_g_obj_ids = 1

	# Assign ID and set superclass
	_obj = _g_obj_ids++
	_g_obj[_obj,OBJ_SUPER] = class

	return (_obj)
}

# obj_delete() support for Map instances
function _obj_delete_map(obj, _prefix, _key) {
	obj_assert_class(obj, Map)
	_prefix = "^" obj SUBSEP
	for (_key in _g_maps) {
		if (!match(_key, _prefix) && _key != obj)
			continue
		delete _g_maps[_key]
	}
}

# obj_delete() support for Array instances
function _obj_delete_array(obj, _size, _i) {
	obj_assert_class(obj, Array)
	_size = array_size(obj)

	for (_i = 0; _i < _size; _i++)
		delete _g_arrays[obj,OBJ_PROP,_i]
}

# Destroy all metadata associated with the given object
function obj_delete(obj, _prop_id, _prop_name, _prefix, _key, _size, _i) {
	if (obj_is_class(obj))
		errorx("cannot delete class objects")

	# Handle classes that use external global array storage
	# for effeciency
	if (obj_is_instanceof(obj, Map)) {
		_obj_delete_map(obj)
	} else if (obj_is_instanceof(obj, Array)) {
		_obj_delete_array(obj)
	}

	# Delete all object properties
	for (_prop_name in _g_prop_names) {
		if (!obj_has_prop_id(obj, _prop_id))
			continue

		_prop_id = _g_prop_names[_prop_name]
		delete _g_obj[obj,OBJ_PROP,_prop_id]
		delete _g_obj_nr[obj,OBJ_PROP,_prop_id]
	}

	# Delete instance state
	delete _g_obj[obj,OBJ_IS_CLS]
	delete _g_obj[obj,OBJ_SUPER]
}

# Print an object's unique ID, class, and properties to
# stdout
function obj_dump(obj, _pname, _prop_id, _prop_val) {
	print(class_get_name(obj_get_class(obj)) "<" obj ">:")

	# Dump all properties
	for (_pname in _g_prop_names) {
		_prop_id = _g_prop_names[_pname]

		if (!obj_has_prop_id(obj, _prop_id))
			continue

		_prop_val = prop_get(obj, _prop_id)
		printf("\t%s: %s\n", _pname, _prop_val)
	}
}

# Return true if obj is a class object
function obj_is_class(obj) {
	return (_g_obj[obj,OBJ_IS_CLS] == 1)
}

# Return the class of obj, if any.
function obj_get_class(obj) {
	if (obj == null)
		errorx("obj_get_class() on null object")
	return (_g_obj[obj,OBJ_SUPER])
}

# Return true if obj is an instance of the given class
function obj_is_instanceof(obj, class, _super) {
	if (_super != null)
		errorx("obj_is_instanceof() must be called with two arguments")

	if (!obj_is_class(class))
		errorx(class " is not a class object")

	if (obj == null) {
		errorx("obj_is_instanceof() called with null obj (class " \
		    class_get_name(class) ")")
	}

	for (_super = obj_get_class(obj); _super != null;
	     _super = obj_get_class(_super))
	{
		if (_super == class)
			return (1)
	}

	return (0)
}

# Default object shallow equality implementation. Returns true if the two
# objects share a common superclass and have identity equality across all defined
# properties.
function obj_trivially_equal(lhs, rhs, _class, _pname, _prop_id) {
	# Simple case
	if (lhs == rhs)
		return (1)

	# Must share a common superclass
	_class = obj_get_class(lhs)
	if (_class != obj_get_class(rhs))
		return (0)

	# Compare all properties
	_prop_count = 0
	for (_pname in _g_prop_names) {
		_prop_id = _g_prop_names[_pname]

		if (!class_has_prop_id(_class, _prop_id))
			continue

		if (prop_get(lhs, _prop_id) != prop_get(rhs, _prop_id))
			return (0)
	}

	# All properties are trivially equal
	return (1)
}


# Return a debug string representation of an object's unique ID, class, and
# properties
function obj_to_string(obj, _pname, _prop_id, _prop_val, _prop_count, _result) {
	_result = class_get_name(obj_get_class(obj)) "<" obj ">: { "

	# Fetch all properties
	_prop_count = 0
	for (_pname in _g_prop_names) {
		_prop_id = _g_prop_names[_pname]

		if (!obj_has_prop_id(obj, _prop_id))
			continue

		if (_prop_count >= 0)
			_result = _result ", "

		_result = _result sprintf("\t%s: %s\n", _pname, _prop_val)
		_prop_count++
	}

	return (_result " }")
}

# Assert that obj is an instance of the given class
function obj_assert_class(obj, class) {
	if (!obj_is_instanceof(obj, class)) {
		errorx(class_get_name(obj_get_class(obj)) "<" obj "> is not " \
		    "an instance of " class_get_name(class))
	}
}

# Return true if the given property prop is defined by the object's superclass
function obj_has_property(obj, prop, _class) {
	if (obj == null)
		errorx("obj_has_property() on null object")

	_class = obj_get_class(obj)
	return (class_has_property(_class, prop))
}

# Return true if the given property ID is defined by the object's superclass
function obj_has_prop_id(obj, prop_id, _class) {
	if (obj == null)
		errorx("obj_has_prop_id() on null object")

	_class = obj_get_class(obj)
	return (class_has_prop_id(_class, prop_id))
}

# Return the line (NR) at which a given property ID was set on the object
# Will throw an error if the property has not been set on obj
function obj_get_prop_id_nr(obj, prop_id) {
	if (obj == null)
		errorx("obj_get_prop_id_nr() on null object")

	if (!obj_has_prop_id(obj, prop_id)) {
		errorx("requested undefined property '" _g_props[prop_id] \
		    "' (" prop_id ") on " obj_to_string(obj))
	}

	# Fetch NR
	if ((obj,OBJ_PROP,prop_id) in _g_obj_nr)
		return (_g_obj_nr[obj,OBJ_PROP,prop_id])

	errorx("property '" _g_props[prop_id] "' (" prop_id ") not " \
	    "previously set on " obj_to_string(obj))
}

# Return the line (NR) at which a given property was set on the object
# Will throw an error if the property has not been set on obj
function obj_get_prop_nr(obj, prop) {
	return (obj_get_prop_id_nr(obj, prop[PROP_ID]))
}

# Return an abstract property ID for a given property
function obj_get_prop_id(obj, prop) {
	if (obj == null)
		errorx("obj_get_prop_id() on null object")

	return (class_get_prop_id(obj_get_class(obj), prop))
}


# Return the property ID for a given property name
function obj_get_named_prop_id(obj, name) {
	if (obj == null)
		errorx("obj_get_named_prop_id() on null object")

	return (class_get_named_prop_id(obj_get_class(obj), name))
}

# Set a property on obj
function set(obj, prop, value, _class) {
	return (prop_set(obj, prop[PROP_ID], value))
}

# Get a property value defined on obj
function get(obj, prop, _class) {
	return (prop_get(obj, prop[PROP_ID]))
}

# Set a property on obj, using a property ID returned by obj_get_prop_id() or
# class_get_prop_id()
function prop_set(obj, prop_id, value, _class) {
	if (obj == null) {
		errorx("setting property '" _g_props[prop_id] \
		    "' on null object")
	}

	_class = obj_get_class(obj)
	if (_class == null)
		errorx(obj " has no superclass")

	if (!class_has_prop_id(_class, prop_id)) {
		errorx("requested undefined property '" _g_props[prop_id] \
		    "' (" prop_id ") on " class_get_name(_class))
	}

	# Track the line on which the property was set
	_g_obj_nr[obj,OBJ_PROP,prop_id] = NR
	_g_obj[obj,OBJ_PROP,prop_id] = value
}

# Convert a property ID to a property path.
function prop_id_to_path(prop_id) {
	if (!(prop_id in _g_props))
		errorx("'" prop_id "' is not a property ID")

	# Convert to path string representation
	return (""prop_id)
}

# Convert a property to a property path.
function prop_to_path(prop) {
	if (!(PROP_ID in prop))
		errorx("prop_to_path() called with non-property head")

	return (prop_id_to_path(prop[PROP_ID]))
}

# Create a property path from head and tail properties
# Additional properties may be appended via prop_path_append() or
# prop_path_append_id()
function prop_path_create(head, tail) {
	if (!(PROP_ID in head))
		errorx("prop_path() called with non-property head")

	if (!(PROP_ID in tail))
		errorx("prop_path() called with non-property tail")

	return (head[PROP_ID] SUBSEP tail[PROP_ID])
}

# Append a property to the given property path
function prop_path_append(path, tail) {
	if (!(PROP_ID in tail))
		errorx("prop_path_append() called with non-property tail")

	return (prop_path_append_id(path, tail[PROP_ID]))
}

# Append a property ID to the given property path
function prop_path_append_id(path, tail_id) {
	if (!(tail_id in _g_props))
		errorx("'" tail_id "' is not a property ID")

	return (path SUBSEP tail_id)
}

# Fetch a value from obj using a property path previously returned by
# prop_path_create(), prop_to_path(), etc.
function prop_get_path(obj, prop_path, _class, _prop_ids, _nprop_ids, _next,
    _prop_head, _prop_len, _prop_tail)
{
	if (obj == null) {
		errorx("requested property path '" \
		    gsub(SUBSEP, ".", prop_path)  "' on null object")
	}

	# Try the cache first
	_class = obj_get_class(obj)
	if ((_class,prop_path,PPATH_HEAD) in _g_ppath_cache) {
		_prop_head = _g_ppath_cache[_class,prop_path,PPATH_HEAD]
		_next = prop_get(obj, _prop_head)

		if ((_class,prop_path,PPATH_TAIL) in _g_ppath_cache) {
			_prop_tail = _g_ppath_cache[_class,prop_path,PPATH_TAIL]
			return (prop_get_path(_next, _prop_tail))
		}

		return (_next)
	}

	# Parse the head/tail of the property path and add to cache
	_nprop_ids = split(prop_path, _prop_ids, SUBSEP)
	if (_nprop_ids == 0)
		errorx("empty property path")
	_prop_head = _prop_ids[1]
	_g_ppath_cache[_class,prop_path,PPATH_HEAD] = _prop_head

	if (_nprop_ids > 1) {
		_prop_len = length(_prop_head)
		_prop_tail = substr(prop_path, _prop_len+2)

		# Add to cache
		_g_ppath_cache[_class,prop_path,PPATH_TAIL] = _prop_tail
	}

	# Recursively call out implementation, this time fetching from
	# cache
	return (prop_get_path(obj, prop_path))
}

# Fetch a value property value from obj, using a property ID returned by
# obj_get_prop_id() or class_get_prop_id()
function prop_get(obj, prop_id, _class) {
	if (obj == null) {
		errorx("requested property '" _g_props[prop_id] \
		    "' on null object")
	}

	_class = obj_get_class(obj)
	if (_class == null)
		errorx(obj " has no superclass")

	if (!class_has_prop_id(_class, prop_id)) {
		errorx("requested undefined property '" _g_props[prop_id] \
		    "' (" prop_id ") on " class_get_name(_class))
	}

	return (_g_obj[obj,OBJ_PROP,prop_id])
}

# Create a new MacroType instance
function macro_type_new(name, const_suffix, _obj) {
	_obj = obj_new(MacroType)

	set(_obj, p_name, name)
	set(_obj, p_const_suffix, const_suffix)

	return (_obj)
}

# Create a new MacroDefine instance
function macro_new(name, value, _obj) {
	_obj = obj_new(MacroDefine)
	set(_obj, p_name, name)
	set(_obj, p_value, value)

	return (_obj)
}

# Create an empty array; this uses _g_arrays to store integer
# keys/values under the object's property prefix.
function array_new(_obj) {
	_obj = obj_new(Array)
	set(_obj, p_count, 0)

	return (_obj)
}

# Return the number of elements in the array
function array_size(array) {
	obj_assert_class(array, Array)
	return (get(array, p_count))
}

# Return true if the array is empty
function array_empty(array) {
	return (array_size(array) == 0)
}

# Append a value to the array
function array_append(array, value, _i) {
	obj_assert_class(array, Array)

	_i = get(array, p_count)
	_g_arrays[array,OBJ_PROP,_i] = value
	set(array, p_count, _i+1)
}

# Set an array value
# An error will be thrown if the idx is outside array bounds
function array_set(array, idx, value) {
	obj_assert_class(array, Array)

	if (!((array,OBJ_PROP,idx) in _g_arrays))
		errorx(idx " out of range of array " obj_to_string(array))

	_g_arrays[array,OBJ_PROP,idx] = value
}

# Return value at the given index from the array
# An error will be thrown if 'idx' is outside the array bounds
function array_get(array, idx) {
	obj_assert_class(array, Array)

	if (!((array,OBJ_PROP,idx) in _g_arrays))
		errorx(idx " out of range of array " obj_to_string(array))

	return (_g_arrays[array,OBJ_PROP,idx])
}


#
# Sort an array, using standard awk comparison operators over its values.
#
# If `prop_path*` is non-NULL, the corresponding property path (or property ID)
# will be fetched from each array element and used as the sorting value.
#
# If multiple property paths are specified, the array is first sorted by
# the first path, and then any equal values are sorted by the second path,
# and so on.
#
function array_sort(array, prop_path0, prop_path1, prop_path2, _size) {
	obj_assert_class(array, Array)

	if (_size != null)
		errorx("no more than three property paths may be specified")

	_size = array_size(array)
	if (_size <= 1)
		return

	_qsort(array, prop_path0, prop_path1, prop_path2, 0, _size-1)
}

function _qsort_get_key(array, idx, prop_path, _v) {
	_v = array_get(array, idx)
	
	if (prop_path == null)
		return (_v)

	return (prop_get_path(_v, prop_path))
}

function _qsort_compare(array, lhs_idx, rhs_val, ppath0, ppath1, ppath2,
    _lhs_val, _rhs_prop_val)
{
	_lhs_val = _qsort_get_key(array, lhs_idx, ppath0)
	if (ppath0 == null)
		_rhs_prop_val = rhs_val
	else
		_rhs_prop_val = prop_get_path(rhs_val, ppath0)

	if (_lhs_val == _rhs_prop_val && ppath1 != null) {
		_lhs_val = _qsort_get_key(array, lhs_idx, ppath1)
		_rhs_prop_val = prop_get_path(rhs_val, ppath1)

		if (_lhs_val == _rhs_prop_val && ppath2 != null) {
			_lhs_val = _qsort_get_key(array, lhs_idx, ppath2)
			_rhs_prop_val = prop_get_path(rhs_val, ppath2)
		}
	}

	if (_lhs_val < _rhs_prop_val)
		return (-1)
	else if (_lhs_val > _rhs_prop_val)
		return (1)
	else
		return (0)
}

function _qsort(array, ppath0, ppath1, ppath2, first, last, _qpivot,
    _qleft, _qleft_val, _qright, _qright_val)
{
	if (first >= last)
		return

	# select pivot element
	_qpivot = int(first + int((last-first+1) * rand()))
	_qleft = first
	_qright = last

	_qpivot_val = array_get(array, _qpivot)

	# partition
	while (_qleft <= _qright) {
		while (_qsort_compare(array, _qleft, _qpivot_val, ppath0, ppath1,
		    ppath2) < 0)
		{
			_qleft++
		}

		while (_qsort_compare(array, _qright, _qpivot_val, ppath0, ppath1,
		    ppath2) > 0)
		{
			_qright--
		}

		# swap
		if (_qleft <= _qright) {
			_qleft_val = array_get(array, _qleft)
			_qright_val = array_get(array, _qright)
			
			array_set(array, _qleft, _qright_val)
			array_set(array, _qright, _qleft_val)

			_qleft++
			_qright--
		}
	}

	# sort the partitions
	_qsort(array, ppath0, ppath1, ppath2, first, _qright)
	_qsort(array, ppath0, ppath1, ppath2, _qleft, last)
}


#
# Join all array values with the given separator
#
# If `prop_path` is non-NULL, the corresponding property path (or property ID)
# will be fetched from each array value and included in the result, rather than
# immediate array value
#
function array_join(array, sep, prop_path, _i, _size, _value, _result) {
	obj_assert_class(array, Array)

	_result = ""
	_size = array_size(array)
	for (_i = 0; _i < _size; _i++) {
		# Fetch the value (and optionally, a target property)
		_value = array_get(array, _i)
		if (prop_path != null)
			_value = prop_get_path(_value, prop_path)

		if (_i+1 < _size)
			_result = _result _value sep
		else
			_result = _result _value
	}

	return (_result)
}

# Return the first value in the array, or null if empty
function array_first(array) {
	obj_assert_class(array, Array)

	if (array_size(array) == 0)
		return (null)
	else 
		return (array_get(array, 0))
}

# Return the last value in the array, or null if empty
function array_tail(list, _size) {
	obj_assert_class(array, Array)

	_size = array_size(array)
	if (_size == 0)
		return (null)
	else 
		return (array_get(array, _size-1))
}

# Create an empty hash table; this uses the _g_maps array to store arbitrary
# keys/values under the object's property prefix.
function map_new(_obj) {
	_obj = obj_new(Map)
	return (_obj)
}

# Add `key` with `value` to `map`
function map_set(map, key, value) {
	obj_assert_class(map, Map)
	_g_maps[map,OBJ_PROP,key] = value
}

# Remove `key` from the map
function map_remove(map, key) {
	obj_assert_class(map, Map)
	delete _g_maps[map,OBJ_PROP,key]
}

# Return true if `key` is found in `map`, false otherwise
function map_contains(map, key) {
	obj_assert_class(map, Map)
	return ((map,OBJ_PROP,key) in _g_maps)
}

# Fetch the value of `key` from the map. Will throw an error if the
# key does not exist
function map_get(map, key) {
	obj_assert_class(map, Map)
	return _g_maps[map,OBJ_PROP,key]
}

# Create and return a new list containing all defined values in `map`
function map_to_array(map, _key, _prefix, _values) {
	obj_assert_class(map, Map)

	_values = array_new()
	_prefix = "^" map SUBSEP OBJ_PROP SUBSEP
	for (_key in _g_maps) {
		if (!match(_key, _prefix))
			continue

		array_append(_values, _g_maps[_key])
	}

	return (_values)
}

# Create a new Type instance
function type_new(name, width, signed, constant, array_constant, fmt, mask,
    constant_value, array_constant_value, _obj)
{
	obj_assert_class(fmt, Fmt)

	_obj = obj_new(Type)
	set(_obj, p_name, name)
	set(_obj, p_width, width)
	set(_obj, p_signed, signed)
	set(_obj, p_const, constant)
	set(_obj, p_const_val, constant_value)
	set(_obj, p_array_const, array_constant)
	set(_obj, p_array_const_val, array_constant_value)
	set(_obj, p_default_fmt, fmt)
	set(_obj, p_mask, mask)

	return (_obj)
}

# Return true if two types are equal
function type_equal(lhs, rhs) {
	# Simple case
	if (lhs == rhs)
		return (1)

	# Must share a common class
	if (obj_get_class(lhs) != obj_get_class(rhs))
		return (0)

	# Handle ArrayType equality
	if (obj_is_instanceof(lhs, ArrayType)) {
		# Size must be equal
		if (get(lhs, p_count) != get(rhs, p_count))
			return (0)

		# The base types must be equal
		return (type_equal(type_get_base(lhs), type_get_base(rhs)))
	}

	# Handle Type equality -- we just check for trivial identity
	# equality of all members
	obj_assert_class(lhs, Type)
	return (obj_trivially_equal(lhs, rhs))
}

# Return the type's default value mask. If the type is an array type,
# the default mask of the base type will be returned.
function type_get_default_mask(type) {
	if (obj_is_instanceof(type, ArrayType))
		return (type_get_default_mask(type_get_base(type)))

	obj_assert_class(type, Type)
	return (get(type, p_mask))
}

# Return the type's C constant representation
function type_get_const(type) {
	if (obj_is_instanceof(type, ArrayType))
		return (get(type_get_base(type), p_array_const))

	obj_assert_class(type, Type)
	return (get(type, p_const))
}

# Return the type's C constant integer value
function type_get_const_val(type) {
	if (obj_is_instanceof(type, ArrayType))
		return (get(type_get_base(type), p_array_const_val))

	obj_assert_class(type, Type)
	return (get(type, p_const_val))
}

# Return an array type's element count, or 1 if the type is not
# an array type
function type_get_nelem(type) {
	if (obj_is_instanceof(type, ArrayType))
		return (get(type, p_count))

	obj_assert_class(type, Type)
	return (1)
}

# Return the base type for a given type instance.
function type_get_base(type) {
	if (obj_is_instanceof(type, ArrayType))
		return (type_get_base(get(type, p_type)))

	obj_assert_class(type, Type)
	return (type)
}

# Return the default fmt for a given type instance
function type_get_default_fmt(type, _base, _fmt, _array_fmt) {
	_base = type_get_base(type)
	_fmt = get(_base, p_default_fmt)

	if (obj_is_instanceof(type, ArrayType)) {
		_array_fmt = get(_fmt, p_array_fmt)
		if (_array_fmt != null)
			_fmt = _array_fmt
	}

	return (_fmt)
}

# Return a string representation of the given type
function type_to_string(type, _base_type) {
	if (obj_is_instanceof(type, ArrayType)) {
		_base_type = type_get_base(type)
		return (type_to_string(_base_type) "[" get(type, p_count) "]")
	}
	return get(type, p_name)
}

# Return true if type `rhs` is can be coerced to type `lhs` without data
# loss
function type_can_represent(lhs, rhs) {
	# Must be of the same class (Type or ArrayType)
	if (obj_get_class(lhs) != obj_get_class(rhs))
		return (0)

	if (obj_is_instanceof(lhs, ArrayType)) {
		# The base types must have a representable relationship
		if (!type_can_represent(type_get_base(lhs), type_get_base(rhs)))
			return (0)

		# The lhs type must be able to represent -at least- as
		# many elements as the RHS type
		if (get(lhs, p_count) < get(rhs, p_count))
			return (0)

		return (1)
	}

	# A signed type could represent the full range of a smaller unsigned
	# type, but we don't bother; the two should agree when used in a SROM
	# layout. Instead simply assert that both are signed or unsigned.
	if (get(lhs, p_signed) != get(rhs, p_signed))
		return (0)

	# The `rhs` type must be equal or smaller in width to the `lhs` type
	if (get(lhs, p_width) < get(rhs, p_width))
		return (0)

	return (1)
}

# Create a new ArrayType instance
function array_type_new(type, count, _obj) {
	_obj = obj_new(ArrayType)
	set(_obj, p_type, type)
	set(_obj, p_count, count)

	return (_obj)
}

#
# Parse a type string to either the Type, ArrayType, or null if
# the type is not recognized.
#
function parse_type_string(str, _base, _count) {
	if (match(str, ARRAY_REGEX"$") > 0) {
		# Extract count and base type
		_count = substr(str, RSTART+1, RLENGTH-2)
		sub(ARRAY_REGEX"$", "", str)

		# Look for base type
		if ((_base = type_named(str)) == null)
			return (null)

		return (array_type_new(_base, int(_count)))
	} else {
		return (type_named(str))
	}
}

#
# Parse a variable name in the form of 'name' or 'name[len]', returning
# either the provided base_type if no array specifiers are found, or
# the fully parsed ArrayType.
#
function parse_array_type_specifier(str, base_type, _count) {
	if (match(str, ARRAY_REGEX"$") > 0) {
		# Extract count
		_count = substr(str, RSTART+1, RLENGTH-2)
		return (array_type_new(base_type, int(_count)))
	} else {
		return (base_type)
	}
}

# Return the type constant for `name`, if any
function type_named(name, _n, _type) {
	if (name == null)
		errorx("called type_named() with null name")

	if (map_contains(BaseTypes, name))
		return (map_get(BaseTypes, name))

	return (null)	
}

# Create a new Fmt instance
function fmt_new(name, symbol, array_fmt, _obj) {
	_obj = obj_new(Fmt)
	set(_obj, p_name, name)
	set(_obj, p_symbol, symbol)

	if (array_fmt != null)
		set(_obj, p_array_fmt, array_fmt)

	return (_obj)
}


# Return the Fmt constant for `name`, if any
function fmt_named(name, _n, _fmt) {
	if (map_contains(ValueFormats, name))
		return (map_get(ValueFormats, name))

	return (null)
}

# Create a new VFlag instance
function vflag_new(name, constant, _obj) {
	_obj = obj_new(VFlag)
	set(_obj, p_name, name)
	set(_obj, p_const, constant)

	return (_obj)
}

# Create a new StringConstant AST node
function stringconstant_new(value, continued, _obj) {
	_obj = obj_new(StringConstant)
	set(_obj, p_value, value)
	set(_obj, p_continued, continued)
	set(_obj, p_line, NR)

	return (_obj)
}

# Create an empty StringConstant AST node to which additional lines
# may be appended
function stringconstant_empty(_obj) {
	return (stringconstant_new("", 1))
}

# Parse an input string and return a new string constant
# instance
function stringconstant_parse_line(line, _obj) {
	_obj = stringconstant_empty()
	stringconstant_append_line(_obj, line)
	return (_obj)
}

# Parse and apend an additional line to this string constant
function stringconstant_append_line(str, line, _cont, _strbuf, _regex, _eol) {
	obj_assert_class(str, StringConstant)

	# Must be marked for continuation
	if (!get(str, p_continued)) {
		errorx("can't append to non-continuation string '" \
		    get(str, p_value) "'")
	}

	_strbuf = get(str, p_value)

	# If start of string, look for (and remove) initial double quote
	if (_strbuf == null) {
		_regex = "^[ \t]*\""
		if (!sub(_regex, "", line)) {
			error("expected quoted string")
		}
	}

	# Look for a terminating double quote
	_regex = "([^\"\\\\]*(\\\\.[^\"\\\\]*)*)\""

	_eol = match(line, _regex)
	if (_eol > 0) {
		# Drop everything following the terminating quote
		line = substr(line, 1, RLENGTH-1)
		_cont = 0
	} else {
		# No terminating quote found, continues on next line
		_cont = 1
	}

	# Trim leading and trailing whitespace
	sub(/(^[ \t]+|[ \t]+$)/, "", line)

	# Append to existing buffer
	if ((_strbuf = get(str, p_value)) == NULL)
		set(str, p_value, line)
	else
		set(str, p_value, _strbuf " " line)

	# Update line continuation setting
	set(str, p_continued, _cont)
}

# Create a new RevRange instance
function revrange_new(start, end, _obj) {
	_obj = obj_new(RevRange)
	set(_obj, p_start, start)
	set(_obj, p_end, end)
	set(_obj, p_line, NR)

	return (_obj)
}

# Return true if the two revision ranges are equal
function revrange_equal(lhs, rhs) {
	if (get(lhs, p_start) != get(rhs, p_start))
		return (0)

	if (get(lhs, p_end) != get(rhs, p_end))
		return (0)

	return (1)
}

# Return true if the requested rev is covered by revrange, false otherwise
function revrange_contains(range, rev) {
	obj_assert_class(range, RevRange)

	if (rev < get(range, p_start))
		return (0)
	else if (rev > get(range, p_end)) {
		return (0)
	} else {
		return (1)
	}
}

#
# Return a string representation of the given revision range
#
function revrange_to_string(revs, _start, _end) {
	obj_assert_class(revs, RevRange)

	_start = get(revs, p_start)
	_end = get(revs, p_end)

	if (_start == 0)
		return ("<= " _end)
	else if (_end == REV_MAX)
		return (">= " _start)
	else
		return (_start "-" _end)
}

# Create a new VarGroup instance
function var_group_new(name, _obj) {
	_obj = obj_new(VarGroup)
	set(_obj, p_name, name)
	set(_obj, p_vars, array_new())
	set(_obj, p_line, NR)

	return (_obj)
}

# Create a new NVRAM instance
function nvram_new(_obj, _vars, _v) {
	_obj = obj_new(NVRAM)
	_vars = array_new()
	set(_obj, p_vars, _vars)
	set(_obj, p_var_groups, array_new())
	set(_obj, p_srom_layouts, array_new())
	set(_obj, p_srom_table, map_new())

	#
	# Register our implicit variable definitions
	#

	# SROM signature offset
	_v = var_new(VAccessInternal, "<sromsig>", UInt16)
	array_append(_vars, _v)
	_g_var_names[get(_v, p_name)] = _v

	# SROM CRC8 offset
	_v = var_new(VAccessInternal, "<sromcrc>", UInt8)
	array_append(_vars, _v)
	_g_var_names[get(_v, p_name)] = _v

	return (_obj)
}

# Register a new SROM layout instance
# An error will be thrown if the layout overlaps any revisions covered
# by an existing instance.
function nvram_add_srom_layout(nvram, layout, _table, _revs, _start, _end, _i) {
	obj_assert_class(nvram, NVRAM)
	obj_assert_class(layout, SromLayout)

	# revision:layout hash table
	_table = get(nvram, p_srom_table)

	# register the layout's revisions
	_revs = get(layout, p_revisions)
	_start = get(_revs, p_start)
	_end = get(_revs, p_end)

	for (_i = _start; _i <= _end; _i++) {
		if (map_contains(_table, _i)) {
			error("SROM layout redeclares layout for revision '" \
			    _i "' (originally declared on line " \
			    get(map_get(_table, _i), p_line) ")")
		}

		map_set(_table, _i, layout)
	}

	# append to srom_layouts
	array_append(get(nvram, p_srom_layouts), layout)
}

# Return the first SROM layout registered for a given SROM revision,
# or null if no matching layout is found
function nvram_get_srom_layout(nvram, revision, _layouts, _nlayouts, _layout,
    _i)
{
	obj_assert_class(nvram, NVRAM)

	_layouts = get(nvram, p_srom_layouts)
	_nlayouts = array_size(_layouts)
	for (_i = 0; _i < _nlayouts; _i++) {
		_layout = array_get(_layouts, _i)

		if (srom_layout_has_rev(_layout, revision))
			return (_layout)
	}

	# Not found
	return (null)
}

# Create a new Var instance
function var_new(access, name, type, _obj) {
	obj_assert_class(access, VAccess)

	# Validate the variable identifier
	#
	# The access modifier dictates the permitted identifier format.
	#   VAccessInternal:		<ident>
	#   VAccess(Public|Private):	ident
	if (access != VAccessInternal && name ~ SVAR_IDENT_REGEX) {
		error("invalid identifier '"name"'; did you mean to " \
		    "mark this variable as internal?")
	} else if (access == VAccessInternal) {
		if (name !~ SVAR_IDENT_REGEX)
			error("invalid identifier '"name"' for internal " \
			"variable; did you mean '<" name ">'?")
	} else if (name !~ VAR_IDENT_REGEX) {
		error("invalid identifier '"name"'")
	}

	_obj = obj_new(Var)
	set(_obj, p_access, access)
	set(_obj, p_name, name)
	set(_obj, p_type, type)
	set(_obj, p_line, NR)

	return (_obj)
}

# Return true if var is internal-only, and should not be included
# in any output (e.g. has an access specifier of VAccessInternal).
function var_is_internal(var) {
	return (get(var, p_access) == VAccessInternal)
}

# Return true if `var` has an array type
function var_has_array_type(var, _vtype) {
	obj_assert_class(var, Var)
	_vtype = get(var, p_type)
	return (obj_is_instanceof(_vtype, ArrayType))
}

# Return the number of array elements defined by this variable's type,
# or 1 if the variable does not have an array type.
function var_get_array_len(var) {
	obj_assert_class(var, Var)
	return (type_get_nelem(get(var, p_type)))
}

# Return the fmt for var. If not explicitly set on var, will return then
# return of calling type_get_default_fmt() with the variable's type
function var_get_fmt(var, _fmt) {
	obj_assert_class(var, Var)

	# If defined on var, return it
	if ((_fmt = get(var, p_fmt)) != null)
		return (_fmt)

	# Fall back on the type's default
	return (type_get_default_fmt(get(var, p_type)))
}

# Return a new MacroDefine instance for the given variable, macro type,
# and value
function var_get_macro(var, macro_type, value, _macro) {
	obj_assert_class(var, Var)
	obj_assert_class(macro_type, MacroType)

	return (macro_new(var_get_macro_name(var, macro_type), value))
}

# Return the preprocessor constant name to be used with `var` for the given
# macro_type
function var_get_macro_name(var, macro_type, _var_name, _suffix) {
	obj_assert_class(var, Var)
	obj_assert_class(macro_type, MacroType)

	_var_name = get(var, p_name)
	_suffix = get(macro_type, p_const_suffix)

	return("BHND_NVAR_" toupper(_var_name) _suffix)
}

# Create a new SromLayout instance
function srom_layout_new(rev_desc, _obj)
{
	_obj = obj_new(SromLayout)
	set(_obj, p_revisions, rev_desc)
	set(_obj, p_entries, array_new())
	set(_obj, p_revmap, map_new())
	set(_obj, p_output_var_counts, map_new())
	set(_obj, p_line, NR)

	return (_obj)
}

# Register a new entry with the srom layout
function srom_layout_add_entry(layout, entry, _revmap, _name, _rev_start,
    _rev_end, _var, _prev_entry, _count, _i)
{
	obj_assert_class(layout, SromLayout)
	obj_assert_class(entry, SromEntry)

	_layout_revmap = get(layout, p_revmap)
	_layout_var_count = get(layout, p_output_var_counts)

	_var = get(entry, p_var)
	_name = get(_var, p_name)

	# Add to revision array
	array_append(get(layout, p_entries), entry)

	# Add to the revision map tables
	_rev_start = get(get(entry, p_revisions), p_start)
	_rev_end = get(get(entry, p_revisions), p_end)

	for (_i = _rev_start; _i <= _rev_end; _i++) {
		# Check for existing entry
		_prev_entry = srom_layout_find_entry(layout, _name, _i)
		if (_prev_entry != null) {
			error("redefinition of variable '" _name "' for SROM " \
			    "revision " _i " (previously defined on line " \
			    get(_prev_entry, p_line) ")")
		}

		# Add to the (varname,revision) map
		map_set(_layout_revmap, (_name SUBSEP _i), entry)

		# If an output variable, set or increment the output variable
		# count
		if (!srom_entry_should_output(entry, _i))
			continue

		if (!map_contains(_layout_var_count, _i)) {
			map_set(_layout_var_count, _i, 1)
		} else {
			_count = map_get(_layout_var_count, _i)
			map_set(_layout_var_count, _i, _count + 1)
		}
	}
}


# Return the variable name to be used when emitting C declarations
# for this SROM layout
#
# The name is gauranteed to be unique across SROM layouts with non-overlapping
# revision ranges
function srom_layout_get_variable_name(layout, _revs) {
	obj_assert_class(layout, SromLayout)

	_revs = get(layout, p_revisions)

	return ("bhnd_sprom_layout_r" get(_revs, p_start) \
	    "_r" get(_revs, p_end))
}

# Return true if the given SROM revision is defined by the layout, false
# otherwise
function srom_layout_has_rev(layout, rev) {
	obj_assert_class(layout, SromLayout)
	return (revrange_contains(get(layout, p_revisions), rev))
}


# Return the total number of output variables (variables to be included
# in the SROM layout bindings) for the given SROM revision
function srom_layout_num_output_vars(layout, rev, _counts)
{
	obj_assert_class(layout, SromLayout)

	_counts = get(layout, p_output_var_counts)
	if (!map_contains(_counts, rev))
		return (0)

	return (map_get(_counts, rev))
}

# Return the SromEntry defined for the given variable name and SROM revision,
# or null if none
function srom_layout_find_entry(layout, vname, revision, _key, _srom_revmap) {
	obj_assert_class(layout, SromLayout)

	_srom_revmap = get(layout, p_revmap)

	# SromEntry are mapped by name,revision composite keys
	_key = vname SUBSEP revision
	if (!map_contains(_srom_revmap, _key))
		return (null)

	return (map_get(_srom_revmap, _key))
	
}

# Create a new SromLayoutFilter instance, checking that `revs`
# falls within the parent's revision range
function srom_layout_filter_new(parent, revs, _obj, _start, _end, _parent_revs) {
	obj_assert_class(parent, SromLayout)
	obj_assert_class(revs, RevRange)

	# Fetch our parent's revision range, confirm that we're
	# a strict subset
	_start = get(revs, p_start)
	_end = get(revs, p_end)
	_parent_revs = get(parent, p_revisions)

	if (!revrange_contains(_parent_revs, _start))
		error("'" _start "' is outside of parent range")

	if (!revrange_contains(_parent_revs, _end))
		error("'" _end "' is outside of parent range")

	if (revrange_equal(revs, _parent_revs)) {
		error("srom range '" revrange_to_string(revs) "' is " \
		    "identical to parent range of '" \
		        revrange_to_string(_parent_revs) "'")
	}

	# Construct and return new filter instance
	_obj = obj_new(SromLayoutFilter)
	set(_obj, p_parent, parent)
	set(_obj, p_revisions, revs)
	set(_obj, p_line, NR)

	return (_obj)
}

#
# Create a new SromEntry instance
#
# var:		The variable referenced by this entry
# revisions:	The SROM revisions to which this entry applies
# base_offset:	The SROM entry offset; any relative segment offsets will be
#		calculated relative to the base offset
# type:		The SROM's value type; this may be a subtype of the variable
#		type, and defines the data (width, sign, etc) to be read from
#		SROM.
# 
function srom_entry_new(var, revisions, base_offset, type, _obj) {
	obj_assert_class(var, Var)
	if (revisions != null)
		obj_assert_class(revisions, RevRange)

	_obj = obj_new(SromEntry)
	set(_obj, p_var, var)
	set(_obj, p_revisions, revisions)
	set(_obj, p_base_offset, base_offset)
	set(_obj, p_type, type)
	set(_obj, p_offsets, array_new())
	set(_obj, p_line, NR)

	return (_obj)
}

# Return true if the SromEntry has an array type
function srom_entry_has_array_type(entry) {
	obj_assert_class(entry, SromEntry)

	return (obj_is_instanceof(get(entry, p_type), ArrayType))
}

# Return the number of array elements defined by this SromEntry's type,
# or 1 if the entry does not have an array type.
function srom_entry_get_array_len(entry, _type) {
	obj_assert_class(entry, SromEntry)

	return (type_get_nelem(get(entry, p_type)))
}

#
# Return true if the given entry should be included in the output bindings
# generated for the given revision, false otherwise.
#
function srom_entry_should_output(entry, rev, _var, _revs)
{
	obj_assert_class(entry, SromEntry)

	_var = get(entry, p_var)
	_revs = get(entry, p_revisions)

	# Exclude internal variables
	if (var_is_internal(_var))
		return (0)

	# Exclude inapplicable entry revisions
	if (!revrange_contains(_revs, rev))
		return (0)

	return (1)
}

#
# Return the single, non-shifted, non-masked offset/segment for the given
# SromEntry, or throw an error if the entry contains multiple offsets/segments.
#
# This is used to fetch special-cased variable definitions that are required
# to present a single simple offset.
#
function srom_entry_get_single_segment(entry, _offsets, _segments, _seg,
    _base_type, _default_mask)
{
	obj_assert_class(entry, SromEntry)

	# Fetch the single offset's segment list
	_offsets = get(entry, p_offsets)
	if (array_size(_offsets) != 1)
		errorc(get(entry, p_line), "unsupported offset count")

	_segments = get(array_first(_offsets), p_segments)
	if (array_size(_segments) != 1)
		errorc(get(entry, p_line), "unsupported segment count")

	# Fetch the single segment
	_seg = array_first(_segments)
	_base_type = srom_segment_get_base_type(_seg)
	_default_mask = get(_base_type, p_mask)

	# Must not be shifted/masked
	if (get(_seg, p_shift) != 0)
		errorc(obj_get_prop_nr(_seg, p_mask), "shift unsupported")

	if (get(_seg, p_mask) != _default_mask)
		errorc(obj_get_prop_nr(_seg, p_mask), "mask unsupported")	

	return  (_seg)
}

# Create a new SromOffset instance
function srom_offset_new(_obj) {
	_obj = obj_new(SromOffset)
	set(_obj, p_segments, array_new())
	set(_obj, p_line, NR)

	return (_obj)
}

# Return the number of SromSegment instances defined by this offset.
function srom_offset_segment_count(offset) {
	obj_assert_class(offset, SromOffset)
	return (array_size(get(offset, p_segments)))
}

# Return the idx'th segment. Will throw an error if idx falls outside
# the number of available segments.
function srom_offset_get_segment(offset, idx, _segments, _seg) {
	obj_assert_class(offset, SromOffset)

	return (array_get(get(offset, p_segments), idx))
}

# Create a new SromSegment instance
function srom_segment_new(offset, type, mask, shift, value, _obj) {
	_obj = obj_new(SromSegment)
	set(_obj, p_offset, offset)
	set(_obj, p_type, type)
	set(_obj, p_mask, mask)
	set(_obj, p_shift, shift)
	set(_obj, p_value, value)
	set(_obj, p_line, NR)

	return (_obj)
}

# Return true if the segment has an array type
function srom_segment_has_array_type(seg, _type) {
	_type = srom_segment_get_type(seg)
	return (obj_is_instanceof(_type, ArrayType))
}

# Return the array count of the segment, or 1 if the segment does not have
# an array type 
function srom_segment_get_array_len(seg, _type) {
	if (!srom_segment_has_array_type(seg))
		return (1)

	_type = srom_segment_get_type(seg)
	return (get(_type, p_count))
}

# Return the type of the segment
function srom_segment_get_type(seg) {
	obj_assert_class(seg, SromSegment)
	return (get(seg, p_type))

}

# Return the base type of the segment
function srom_segment_get_base_type(seg) {
	return (type_get_base(srom_segment_get_type(seg)))
}

# Return true if the two segments have identical types and attributes (i.e.
# differing only by offset)
function srom_segment_attributes_equal(lhs, rhs) {
	obj_assert_class(lhs, SromSegment)
	obj_assert_class(rhs, SromSegment)

	# type
	if (!type_equal(get(lhs, p_type), get(rhs, p_type)))
		return (0)

	# mask
	if (get(lhs, p_mask) != get(rhs, p_mask))
		return (0)

	# shift
	if (get(lhs, p_shift) != get(rhs, p_shift))
		return (0)

	# value
	if (get(lhs, p_value) != get(rhs, p_value))
		return (0)

	return (1)
}

# Return a human-readable representation of a Segment instance
function segment_to_string(seg, _str, _t, _m, _s,  _attrs, _attr_str) {
	_attrs = array_new()

	# include type (if specified)
	if ((_t = get(seg, p_type)) != null)
		_str = (type_to_string(_t) " ")

	# include offset
	_str = (_str sprintf("0x%X", get(seg, p_offset)))

	# append list of attributes
	if ((_m = get(seg, p_mask)) != null)
		array_append(_attrs, ("&" _m))

	if ((_s = get(seg, p_shift)) != null) {
		if (_s > 0)
			_s = ">>" _s
		else
			_s = "<<" _s
		array_append(_attrs, _s)
	}

	_attr_str = array_join(_attrs, ", ")
	obj_delete(_attrs)

	if (_attr_str == "")
		return (_str)
	else
		return (_str " (" _attr_str ")")
}

# return the flag definition for variable `v`
function gen_var_flags(v, _type, _flags, _flag, _str)
{
	_num_flags = 0;
	_type = get(v, p_type)
	_flags = array_new()

	# VF_PRIVATE
	if (get(v, p_access) == VAccessPrivate)
		array_append(_flags, VFlagPrivate)

	# VF_IGNALL1
	if (get(v, p_ignall1))
		array_append(_flags, VFlagIgnoreAll1)

	# If empty, return empty flag value
	if (array_size(_flags) == 0) {
		obj_delete(_flags)
		return ("0")
	}

	# Join all flag constants with |
	_str = array_join(_flags, "|", class_get_prop_id(VFlag, p_const))

	# Clean up
	obj_delete(_flags)

	return (_str)
}

#
# Return the absolute value
#
function abs(i) {
	return (i < 0 ? -i : i)
}

#
# Return the minimum of two values
#
function min(lhs, rhs) {
	return (lhs < rhs ? lhs : rhs)
}

#
# Return the maximum of two values
#
function max(lhs, rhs) {
	return (lhs > rhs ? lhs : rhs)
}

#
# Parse a hex string
#
function parse_hex_string(str, _hex_pstate, _out, _p, _count) {
	if (!AWK_REQ_HEX_PARSING)
		return (str + 0)

	# Populate hex parsing lookup table on-demand
	if (!("F" in _g_hex_table)) {
		for (_p = 0; _p < 16; _p++) {
			_g_hex_table[sprintf("%X", _p)] = _p
			_g_hex_table[sprintf("%x", _p)] = _p
		}
	}

	# Split input into an array
	_count = split(toupper(str), _hex_pstate, "")
	_p = 1

	# Skip leading '0x'
	if (_count >= 2 && _hex_pstate[1] == "0") {
		if (_hex_pstate[2] == "x" || _hex_pstate[2] == "X")
			_p += 2
	}

	# Parse the hex_digits
	_out = 0
	for (; _p <= _count; _p++)
		_out = (_out * 16) + _g_hex_table[_hex_pstate[_p]]

	return (_out)
}

#
# Return the integer representation of an unsigned decimal, hexadecimal, or
# octal string
#
function parse_uint_string(str) {
	if (str ~ UINT_REGEX)
		return (int(str))
	else if (str ~ HEX_REGEX)
		return (parse_hex_string(str))
	else
		error("invalid integer value: '" str "'")
}

#
# Parse an offset string, stripping any leading '+' or trailing ':' or ','
# characters
#
# +0x0:
# 0x0,
# ...
#
function parse_uint_offset(str) {
	# Drop any leading '+'
	sub(/^\+/, "", str)

	# Drop any trailing ':', ',', or '|'
	sub("[,|:]$", "", str)

	# Parse the cleaned up string
	return (parse_uint_string(str))
}

#
# Print msg to output file, without indentation
#
function emit_ni(msg) {
	printf("%s", msg) >> OUTPUT_FILE
}

#
# Print msg to output file, indented for the current `output_depth`
#
function emit(msg, _ind) {
	for (_ind = 0; _ind < output_depth; _ind++)
		emit_ni("\t")

	emit_ni(msg)
}

#
# Print a warning to stderr
#
function warn(msg) {
	print "warning:", msg, "at", FILENAME, "line", NR > "/dev/stderr"
}

#
# Print an warning message without including the source line information
#
function warnx(msg) {
	print "warning:", msg > "/dev/stderr"
}

#
# Print a compiler error to stderr with a caller supplied
# line number
#
function errorc(line, msg) {
	errorx(msg " at " FILENAME " line " line)
}

#
# Print a compiler error to stderr
#
function error(msg) {
	errorx(msg " at " FILENAME " line " NR ":\n\t" $0)
}

#
# Print an error message without including the source line information
#
function errorx(msg) {
	print "error:", msg > "/dev/stderr"
	_EARLY_EXIT=1
	exit 1
}

#
# Print a debug output message
#
function debug(msg, _i) {
	if (!DEBUG)
		return
	for (_i = 1; _i < _g_parse_stack_depth; _i++)
		printf("\t") > "/dev/stderr"
	print msg > "/dev/stderr"
}

#
# Advance to the next non-comment input record
#
function next_line(_result) {
	do {
		_result = getline
	} while (_result > 0 && $0 ~ /^[ \t]*#.*/) # skip comment lines
	return (_result)
}

#
# Advance to the next input record and verify that it matches @p regex
#
function getline_matching(regex, _result) {
	_result = next_line()
	if (_result <= 0)
		return (_result)

	if ($0 ~ regex)
		return (1)

	return (-1)
}

#
# Shift the current fields left by `n`.
#
# If all fields are consumed and the optional do_getline argument is true,
# read the next line.
#
function shiftf(n, do_getline, _i) {
	if (n > NF)
		error("shift past end of line")

	if (n == NF) {
		# If shifting the entire line, just reset the line value
		$0 = ""
	} else {
		for (_i = 1; _i <= NF-n; _i++) {
			$(_i) = $(_i+n)
		}
		NF = NF - n
	}

	if (NF == 0 && do_getline)
		next_line()
}

# Push a new parser state.
function parser_state_push(ctx, is_block, _state) {
	_state = obj_new(ParseState)
	set(_state, p_ctx, ctx)
	set(_state, p_is_block, is_block)
	set(_state, p_line, NR)

	_g_parse_stack_depth++
	_g_parse_stack[_g_parse_stack_depth] = _state
}

# Fetch the current parser state
function parser_state_get() {
	if (_g_parse_stack_depth == 0)
		errorx("parser_state_get() called with empty parse stack")

	return (_g_parse_stack[_g_parse_stack_depth])
}

# Pop the current parser state
function parser_state_pop(_block_state, _closes_block) {
	if (_g_parse_stack_depth == 0)
		errorx("parser_state_pop() called with empty parse stack")

	_closes_block = get(parser_state_get(), p_is_block)

	delete _g_parse_stack[_g_parse_stack_depth]
	_g_parse_stack_depth--

	if (_closes_block)
		debug("}")
}

# Fetch the current context object associated with this parser state
# The object will be asserted as being an instance of the given class.
function parser_state_get_context(class, _ctx_obj) {
	_ctx_obj = get(parser_state_get(), p_ctx)
	obj_assert_class(_ctx_obj, class)

	return (_ctx_obj)
}

# Walk the parser state stack until a context object of the given class
# is found. If the top of the stack is reached without finding a context object
# of the requested type, an error will be thrown.
function parser_state_find_context(class, _state, _ctx, _i) {
	if (class == null)
		errorx("parser_state_find_context() called with null class")

	# Find the first context instance inheriting from `class`
	for (_i = 0; _i < _g_parse_stack_depth; _i++) {
		_state = _g_parse_stack[_g_parse_stack_depth - _i]
		_ctx = get(_state, p_ctx)

		# Check for match
		if (obj_is_instanceof(_ctx, class))
			return (_ctx)
	}

	# Not found
	errorx("no context instance of type '" class_get_name(class) "' " \
	    "found in parse stack")
}

#
# Find opening brace and push a new parser state for a brace-delimited block.
#
function parser_state_open_block(ctx) {
	if ($0 ~ "{" || getline_matching("^[ \t]*{") > 0) {
		parser_state_push(ctx, 1)
		sub("^[^{]*{", "", $0)
		return
	}

	error("found '"$1 "' instead of expected '{'")
}

#
# Find closing brace and pop parser states until the first
# brace-delimited block is discarded.
#
function parser_state_close_block(_next_state, _found_block) {
	if ($0 !~ "}")
		error("internal error - no closing brace")

	# pop states until we exit the first enclosing block
	do {
		_next_state = parser_state_get()
		_found_block = get(_next_state, p_is_block)
		parser_state_pop()
	} while (!_found_block)

	# strip everything prior to the block closure
	sub("^[^}]*}", "", $0)
}

# Evaluates to true if the current parser state is defined with a context of
# the given class
function in_parser_context(class, _ctx) {
	if (class == null)
		errorx("called in_parser_context() with null class")

	_ctx = get(parser_state_get(), p_ctx)
	return (obj_is_instanceof(_ctx, class))
}

#
# Parse and return a revision range from the current line.
#
# 4
# 4-10	# revisions 4-10, inclusive
# > 4
# < 4
# >= 4
# <= 4
#
function parse_revrange(_start, _end, _robj) {
	_start = 0
	_end = 0

	if ($2 ~ "[0-9]*-[0-9*]") {
		split($2, _g_rev_range, "[ \t]*-[ \t]*")
		_start = int(_g_rev_range[1])
		_end = int(_g_rev_range[2])
	} else if ($2 ~ "(>|>=|<|<=)" && $3 ~ "[1-9][0-9]*") {
		if ($2 == ">") {
			_start = int($3)+1
			_end = REV_MAX
		} else if ($2 == ">=") {
			_start = int($3)
			_end = REV_MAX
		} else if ($2 == "<" && int($3) > 0) {
			_start = 0
			_end = int($3)-1
		} else if ($2 == "<=") {
			_start = 0
			_end = int($3)-1
		} else {
			error("invalid revision descriptor")
		}
	} else if ($2 ~ "[1-9][0-9]*") {
		_start = int($2)
		_end = int($2)
	} else {
		error("invalid revision descriptor")
	}

	return (revrange_new(_start, _end))
}

# 
# Parse a variable group block starting at the current line
#
# group "Group Name" {
# 	u8	var_name[10] {
#		...
#	}
#	...
# }
#
function parse_variable_group(_ctx, _groups, _group, _group_name) {
	_ctx = parser_state_get_context(NVRAM)

	# Seek to the start of the name string
	shiftf(1)

	# Parse the first line
	_group_name = stringconstant_parse_line($0)

	# Incrementally parse line continuations
	while (get(_group_name, p_continued)) {
		getline
		stringconstant_append_line(_group_name, $0)
	}

	debug("group \"" get(_group_name, p_value) "\" {")

	# Register the new variable group
	_groups = get(_ctx, p_var_groups)
	_group = var_group_new(_group_name)
	array_append(_groups, _group)

	# Push our variable group block
	parser_state_open_block(_group)
}


#
# Parse a variable definition block starting at the current line
#
# u8	var_name[10] {
#	all1	ignore
#	desc	...
# }
#
function parse_variable_defn(_ctx, _vaccess, _type, _name, _fmt, _var,
    _var_list)
{
	_ctx = parser_state_get_context(SymbolContext)

	# Check for access modifier
	if ($1 == "private") {
		_vaccess = VAccessPrivate
		shiftf(1)
	} else if ($1 == "internal") {
		_vaccess = VAccessInternal
		shiftf(1)
	} else {
		_vaccess = VAccessPublic
	}

	# Find the base type
	if ((_type = type_named($1)) == null)
		error("unknown type '" $1 "'")

	# Parse (and trim) any array specifier from the variable name
	_name = $2
	_type = parse_array_type_specifier(_name, _type)
	sub(ARRAY_REGEX"$", "", _name)

	# Look for an existing variable definition
	if (_name in _g_var_names) {
		error("variable identifier '" _name "' previously defined at " \
		    "line " get(_g_var_names[_name], p_line))
	}

	# Construct new variable instance
	_var = var_new(_vaccess, _name, _type)
	debug((_private ? "private " : "") type_to_string(_type) " " _name " {")

	# Register in global name table
	_g_var_names[_name] = _var

	# Add to our parent context
	_var_list = get(_ctx, p_vars)
	array_append(_var_list, _var)

	# Push our variable definition block
	parser_state_open_block(_var)
}


#
# Return a string containing the human-readable list of valid Fmt names
#
function fmt_get_human_readable_list(_result, _fmts, _fmt, _nfmts, _i)
{
	# Build up a string listing the valid formats
	_fmts = map_to_array(ValueFormats)
	_result = ""

	_nfmts = array_size(_fmts)
	for (_i = 0; _i < _nfmts; _i++) {
		_fmt = array_get(_fmts, _i)
		if (_i+1 == _nfmts)
			_result = _result "or "

		_result = _name_str \
		    "'" get(_fmt, p_name) "'"

		if (_i+1 < _nfmts)
			_result = _result ", "
	}

	obj_delete(_fmts)
	return (_result)
}

#
# Parse a variable parameter from the current line
#
# fmt	(decimal|hex|macaddr|...)
# all1	ignore
# desc	"quoted string"
# help	"quoted string"
#
function parse_variable_param(param_name, _var, _vprops, _prop_id, _pval) {
	_var = parser_state_get_context(Var)

	if (param_name == "fmt") {
		debug($1 " " $2)

		# Check for an existing definition
		if ((_pval = get(_var, p_fmt)) != null) {
			error("fmt previously specified on line " \
			    obj_get_prop_nr(_var, p_fmt))
		}

		# Validate arguments
		if (NF != 2) {
			error("'" $1 "' requires a single parameter value of " \
			    fmt_get_human_readable_list())
		}

		if ((_pval = fmt_named($2)) == null) {
			error("'" $1 "' value '" $2 "' unrecognized. Must be " \
			    "one of " fmt_get_human_readable_list())
		}

		# Set fmt reference
		set(_var, p_fmt, _pval)
	} else if (param_name == "all1") {
		debug($1 " " $2)
		
		# Check for an existing definition
		if ((_pval = get(_var, p_ignall1)) != null) {
			error("all1 previously specified on line " \
			    obj_get_prop_nr(_var, p_ignall1))
		}

		# Check argument
		if (NF != 2)
			error("'" $1 "'requires a single 'ignore' argument")
		else if ($2 != "ignore")
			error("unknown "$1" value '"$2"', expected 'ignore'")

		# Set variable property
		set(_var, p_ignall1, 1)
	} else if (param_name == "desc" || param_name == "help") {
		# Fetch an indirect property reference for either the 'desc'
		# or 'help' property
		_prop_id = obj_get_named_prop_id(_var, param_name)

		# Check for an existing definition
		if ((_pval = prop_get(_var, _prop_id)) != null) {
			error(get(_var, p_name) " '" $1 "' redefined " \
			    "(previously defined on line " \
			    obj_get_prop_id_nr(_var, _prop_id) ")")
		}

		# Seek to the start of the desc/help string
		shiftf(1)

		# Parse the first line
		_pval = stringconstant_parse_line($0)

		# Incrementally parse line continuations
		while (get(_pval, p_continued)) {
			getline
			stringconstant_append_line(_pval, $0)
		}

		debug(param_name " \"" get(_pval, p_value) "\"")

		# Add to the var object
		prop_set(_var, _prop_id, _pval)
	} else {
		error("unknown variable property type: '" param_name "'")
	}
}


#
# Parse a top-level SROM layout block starting at the current line
#
# srom 4-7 {
#     0x000: ...
# }
#
function parse_srom_layout(_nvram, _srom_layouts, _revs, _layout) {
	_nvram = parser_state_get_context(NVRAM)
	_srom_layouts = get(_nvram, p_srom_layouts)

	# Parse revision descriptor and register SROM
	# instance
	_revs = parse_revrange()
	_layout = srom_layout_new(_revs)
	nvram_add_srom_layout(_nvram, _layout)

	debug("srom " revrange_to_string(_revs) " {")

	# Push new SROM parser state
	parser_state_open_block(_layout)
}


#
# Parse a nested srom range filter block starting at the current line
# srom 4-7 {
#	# Filter block
# 	srom 5 {
#		0x000: ...
#	}
# }
#
function parse_srom_layout_filter(_parent, _revs, _filter) {
	_parent = parser_state_get_context(SromLayout)

	# Parse revision descriptor
	_revs = parse_revrange()

	# Construct the filter (which also validates the revision range)
	_filter = srom_layout_filter_new(_parent, _revs)

	debug("srom " revrange_to_string(_revs) " {")

	# Push new SROM parser state
	parser_state_open_block(_filter)	
}


#
# Parse a SROM offset segment's attribute list from the current line
#
# <empty line>
# (&0xF0, >>4, =0x5340)
# ()
#
# Attribute designators:
#	&0xF	Mask value with 0xF
#	<<4	Shift left 4 bits
#	>>4	Shift right 4 bits
#	=0x53	The parsed value must be equal to this constant value
#
# May be followed by a | indicating that this segment should be OR'd with the
# segment that follows, or a terminating , indicating that a new offset's
# list of segments may follow.
#
function parse_srom_segment_attributes(offset, type, _attrs, _num_attr, _attr,
    _mask, _shift, _value, _i)
{
	# seek to offset (attributes...) or end of the offset expr (|,)
	sub("^[^,(|){}]+", "", $0)

	# defaults
	_mask = type_get_default_mask(type)
	_shift = 0

	# parse attributes
	if ($1 ~ "^\\(") {
		# extract attribute list
		if (match($0, /\([^|\(\)]*\)/) <= 0)
			error("expected attribute list")

		_attrs = substr($0, RSTART+1, RLENGTH-2)

		# drop attribute list from the input line
		$0 = substr($0, RSTART+RLENGTH, length($0) - RSTART+RLENGTH)

		# parse attributes
		_num_attr = split(_attrs, _g_attrs, ",[ \t]*")
		for (_i = 1; _i <= _num_attr; _i++) {
			_attr = _g_attrs[_i]
	
			if (sub("^&[ \t]*", "", _attr) > 0) {
				_mask = parse_uint_string(_attr)
			} else if (sub("^<<[ \t]*", "", _attr) > 0) {
				_shift = - parse_uint_string(_attr)
			} else if (sub("^>>[ \t]*", "", _attr) > 0) {
				_shift = parse_uint_string(_attr)
			} else if (sub("^=[ \t]*", "", _attr) > 0) {
				_value = _attr
			} else {
				error("unknown attribute '" _attr "'")
			}
		}
	}

	return (srom_segment_new(offset, type, _mask, _shift, _value))
}

#
# Parse a SROM offset's segment declaration from the current line
#
# +0x0:	u8 (&0xF0, >>4)		# read 8 bits at +0x0 (relative to srom entry
#				# offset, apply 0xF0 mask, shift >> 4
# 0x10:	u8 (&0xF0, >>4)		# identical to above, but perform the read at
#				# absolute offset 0x10
#
# +0x0: u8			# no attributes
# 0x10: u8
#
# +0x0				# simplified forms denoted by lack of ':'; the
# 0x0				# type is inherited from the parent SromEntry
#
#
function parse_srom_segment(base_offset, base_type, _simple, _type, _type_str,
    _offset, _attrs, _num_attr, _attr, _mask, _shift, _off_desc)
{
	# Fetch the offset value
	_offset = $1

	# Offset string must be one of:
	# 	simplified entry: <offset|+reloff>
	#		Provides only the offset, with the type inherited
	#		from the original variable definition
	#	standard entry: <offset|+reloff>:
	#		Provides the offset, followed by a type
	#
	# We differentiate the two by looking for (and simultaneously removing)
	# the trailing ':'
	if (!sub(/:$/, "", _offset))
		_simple = 1

	# The offset may either be absolute (e.g. 0x180) or relative (e.g.
	# +0x01).
	#
	# If we find a relative offset definition, we must trim the leading '+'
	# and then add the base offset
	if (sub(/^\+/, "", _offset)) {
		_offset = base_offset + parse_uint_offset(_offset)
	} else {
		
		_offset = parse_uint_offset(_offset)
	}

	# If simplified form, use the base type of the SROM entry. Otherwise,
	# we need to parse the type.
	if (_simple) {
		_type = base_type
	} else {
		_type_str = $2
		sub(/,$/, "", _type_str) # trim trailing ',', if any

		if ((_type = parse_type_string(_type_str)) == null)
			error("unknown type '" _type_str "'")
	}

	# Parse the trailing (... attributes ...), if any
	return (parse_srom_segment_attributes(_offset, _type))
}

#
# Parse a SROM variable entry from the current line
# <offset>: <type> <varname><array spec> ...
#
function parse_srom_variable_entry(_srom, _srom_revs, _rev_start, _rev_end,
    _srom_entries, _srom_revmap, _prev_entry, _ctx, _base_offset, _name,
    _stype, _var, _entry, _offset, _seg, _i)
{
	# Fetch our parent context
	_ctx = parser_state_get_context(SromContext)
	_srom_revs = get(_ctx, p_revisions)
	_rev_start = get(_srom_revs, p_start)
	_rev_end = get(_srom_revs, p_end)

	# Locate our enclosing layout
	_srom = parser_state_find_context(SromLayout)
	_srom_entries = get(_srom, p_entries)
	_srom_revmap = get(_srom, p_revmap)

	# Verify argument count
	if (NF < 3) {
		error("unrecognized srom entry syntax; must specify at " \
		    "least \"<offset>: <type> <variable name>\"")
	}

	# Parse the base offset
	_base_offset = parse_uint_offset($1)

	# Parse the base type
	if ((_stype = type_named($2)) == null)
		error("unknown type '" $2 "'")

	# Parse (and trim) any array specifier from the variable name
	_name = $3
	_stype = parse_array_type_specifier(_name, _stype)
	sub(ARRAY_REGEX"$", "", _name)

	# Locate the variable definition
	if (!(_name in _g_var_names))
		error("no definition found for variable '" _name "'")
	_var = _g_var_names[_name]

	# The SROM entry type must be a subtype of the variable's declared
	# type
	if (!type_can_represent(get(_var, p_type), _stype)) {
		error("'" type_to_string(_stype) "' SROM value cannot be " \
		    "coerced to '" type_to_string(get(_var, p_type)) " " _name \
		    "' variable")
	}

	# Create and register our new offset entry
	_entry = srom_entry_new(_var, _srom_revs, _base_offset, _stype)
	srom_layout_add_entry(_srom, _entry)

	# Seek to either the block start ('{'), or the attributes to be
	# used for a single offset/segment entry at `offset`
	shiftf(3)

	# Using the block syntax? */
	if ($1 == "{") {
		debug(sprintf("0x%03x: %s %s {", _base_offset,
		    type_to_string(_stype), _name))
		parser_state_open_block(_entry)
	} else {
		# Otherwise, we're using the simplified syntax -- create and
		# register our implicit SromOffset
		_offset = srom_offset_new()
		array_append(get(_entry, p_offsets), _offset)

		# Parse and register simplified segment syntax
		_seg = parse_srom_segment_attributes(_base_offset, _stype)
		array_append(get(_offset, p_segments), _seg)

		debug(sprintf("0x%03x: %s %s { %s }", _base_offset,
		    type_to_string(_stype), _name, segment_to_string(_seg)))
	}
}

#
# Parse all SromSegment entry segments readable starting at the current line
#
# <offset|+reloff>[,|]?
# <offset|+reloff>: <type>[,|]?
# <offset|+reloff>: <type> (<attributes>)[,|]?
#
function parse_srom_entry_segments(_entry, _base_off, _base_type, _offs,
    _offset, _segs, _seg, _more_seg, _more_vals)
{
	_entry = parser_state_get_context(SromEntry)
	_base_off = get(_entry, p_base_offset)
	_offs = get(_entry, p_offsets)

	_base_type = get(_entry, p_type)
	_base_type = type_get_base(_base_type)

	# Parse all offsets
	do {
		# Create a SromOffset
		_offset = srom_offset_new()
		_segs = get(_offset, p_segments)

		array_append(_offs, _offset)

		# Parse all segments
		do {
			_seg = parse_srom_segment(_base_off, _base_type)
			array_append(_segs, _seg)

			# Do more segments follow?
			_more_seg = ($1 == "|")
			if (_more_seg)
				shiftf(1, 1)

			if (_more_seg)
				debug(segment_to_string(_seg) " |")
			else
				debug(segment_to_string(_seg))
		} while (_more_seg)

		# Do more offsets follow?
		_more_vals = ($1 == ",")
		if (_more_vals)
			shiftf(1, 1)
	} while (_more_vals)
}
