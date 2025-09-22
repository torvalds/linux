yaml2obj
========

yaml2obj takes a YAML description of an object file and converts it to a binary
file.

    $ yaml2obj input-file

.. program:: yaml2obj

Outputs the binary to stdout.

COFF Syntax
-----------

Here's a sample COFF file.

.. code-block:: yaml

  header:
    Machine: IMAGE_FILE_MACHINE_I386 # (0x14C)

  sections:
    - Name: .text
      Characteristics: [ IMAGE_SCN_CNT_CODE
                       , IMAGE_SCN_ALIGN_16BYTES
                       , IMAGE_SCN_MEM_EXECUTE
                       , IMAGE_SCN_MEM_READ
                       ] # 0x60500020
      SectionData:
        "\x83\xEC\x0C\xC7\x44\x24\x08\x00\x00\x00\x00\xC7\x04\x24\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x8B\x44\x24\x08\x83\xC4\x0C\xC3" # |....D$.......$...............D$.....|
    - Name: .rdata
      Characteristics: [ IMAGE_SCN_CNT_INITIALIZED_DATA, IMAGE_SCN_MEM_READ ]
      StructuredData:
        - Binary: {type: str}
        - UInt32: {type: int}
        - LoadConfig:
          Size: {type: int}
          TimeDateStamp: {type: int}
          MajorVersion: {type: int}
          MinorVersion: {type: int}
          GlobalFlagsClear: {type: int}
          GlobalFlagsSet: {type: int}
          CriticalSectionDefaultTimeout: {type: int}
          DeCommitFreeBlockThreshold: {type: int}
          DeCommitTotalFreeThreshold: {type: int}
          LockPrefixTable: {type: int}
          MaximumAllocationSize: {type: int}
          VirtualMemoryThreshold: {type: int}
          ProcessAffinityMask: {type: int}
          ProcessHeapFlags: {type: int}
          CSDVersion: {type: int}
          DependentLoadFlags: {type: int}
          EditList: {type: int}
          SecurityCookie: {type: int}
          SEHandlerTable: {type: int}
          SEHandlerCount: {type: int}
          GuardCFCheckFunction: {type: int}
          GuardCFCheckDispatch: {type: int}
          GuardCFFunctionTable: {type: int}
          GuardCFFunctionCount: {type: int}
          GuardFlags: {type: int}
          CodeIntegrity:
            Flags: {type: int}
            Catalog: {type: int}
            CatalogOffset: {type: int}
          GuardAddressTakenIatEntryTable: {type: int}
          GuardAddressTakenIatEntryCount: {type: int}
          GuardLongJumpTargetTable: {type: int}
          GuardLongJumpTargetCount: {type: int}
          DynamicValueRelocTable: {type: int}
          CHPEMetadataPointer: {type: int}
          GuardRFFailureRoutine: {type: int}
          GuardRFFailureRoutineFunctionPointer: {type: int}
          DynamicValueRelocTableOffset: {type: int}
          DynamicValueRelocTableSection: {type: int}
          GuardRFVerifyStackPointerFunctionPointer: {type: int}
          HotPatchTableOffset: {type: int}
          EnclaveConfigurationPointer: {type: int}
          VolatileMetadataPointer: {type: int}
          GuardEHContinuationTable: {type: int}
          GuardEHContinuationCount: {type: int}
          GuardXFGCheckFunctionPointer: {type: int}
          GuardXFGDispatchFunctionPointer: {type: int}
          GuardXFGTableDispatchFunctionPointer: {type: int}
          CastGuardOsDeterminedFailureMode: {type: int}

  symbols:
    - Name: .text
      Value: 0
      SectionNumber: 1
      SimpleType: IMAGE_SYM_TYPE_NULL # (0)
      ComplexType: IMAGE_SYM_DTYPE_NULL # (0)
      StorageClass: IMAGE_SYM_CLASS_STATIC # (3)
      NumberOfAuxSymbols: 1
      AuxiliaryData:
        "\x24\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00" # |$.................|

    - Name: _main
      Value: 0
      SectionNumber: 1
      SimpleType: IMAGE_SYM_TYPE_NULL # (0)
      ComplexType: IMAGE_SYM_DTYPE_NULL # (0)
      StorageClass: IMAGE_SYM_CLASS_EXTERNAL # (2)

Here's a simplified Kwalify_ schema with an extension to allow alternate types.

.. _Kwalify: http://www.kuwata-lab.com/kwalify/ruby/users-guide.html

.. code-block:: yaml

  type: map
    mapping:
      header:
        type: map
        mapping:
          Machine: [ {type: str, enum:
                                 [ IMAGE_FILE_MACHINE_UNKNOWN
                                 , IMAGE_FILE_MACHINE_AM33
                                 , IMAGE_FILE_MACHINE_AMD64
                                 , IMAGE_FILE_MACHINE_ARM
                                 , IMAGE_FILE_MACHINE_ARMNT
                                 , IMAGE_FILE_MACHINE_ARM64
                                 , IMAGE_FILE_MACHINE_EBC
                                 , IMAGE_FILE_MACHINE_I386
                                 , IMAGE_FILE_MACHINE_IA64
                                 , IMAGE_FILE_MACHINE_M32R
                                 , IMAGE_FILE_MACHINE_MIPS16
                                 , IMAGE_FILE_MACHINE_MIPSFPU
                                 , IMAGE_FILE_MACHINE_MIPSFPU16
                                 , IMAGE_FILE_MACHINE_POWERPC
                                 , IMAGE_FILE_MACHINE_POWERPCFP
                                 , IMAGE_FILE_MACHINE_R4000
                                 , IMAGE_FILE_MACHINE_SH3
                                 , IMAGE_FILE_MACHINE_SH3DSP
                                 , IMAGE_FILE_MACHINE_SH4
                                 , IMAGE_FILE_MACHINE_SH5
                                 , IMAGE_FILE_MACHINE_THUMB
                                 , IMAGE_FILE_MACHINE_WCEMIPSV2
                                 ]}
                   , {type: int}
                   ]
          Characteristics:
            - type: seq
              sequence:
                - type: str
                  enum: [ IMAGE_FILE_RELOCS_STRIPPED
                        , IMAGE_FILE_EXECUTABLE_IMAGE
                        , IMAGE_FILE_LINE_NUMS_STRIPPED
                        , IMAGE_FILE_LOCAL_SYMS_STRIPPED
                        , IMAGE_FILE_AGGRESSIVE_WS_TRIM
                        , IMAGE_FILE_LARGE_ADDRESS_AWARE
                        , IMAGE_FILE_BYTES_REVERSED_LO
                        , IMAGE_FILE_32BIT_MACHINE
                        , IMAGE_FILE_DEBUG_STRIPPED
                        , IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP
                        , IMAGE_FILE_NET_RUN_FROM_SWAP
                        , IMAGE_FILE_SYSTEM
                        , IMAGE_FILE_DLL
                        , IMAGE_FILE_UP_SYSTEM_ONLY
                        , IMAGE_FILE_BYTES_REVERSED_HI
                        ]
            - type: int
      sections:
        type: seq
        sequence:
          - type: map
            mapping:
              Name: {type: str}
              Characteristics:
                - type: seq
                  sequence:
                    - type: str
                      enum: [ IMAGE_SCN_TYPE_NO_PAD
                            , IMAGE_SCN_CNT_CODE
                            , IMAGE_SCN_CNT_INITIALIZED_DATA
                            , IMAGE_SCN_CNT_UNINITIALIZED_DATA
                            , IMAGE_SCN_LNK_OTHER
                            , IMAGE_SCN_LNK_INFO
                            , IMAGE_SCN_LNK_REMOVE
                            , IMAGE_SCN_LNK_COMDAT
                            , IMAGE_SCN_GPREL
                            , IMAGE_SCN_MEM_PURGEABLE
                            , IMAGE_SCN_MEM_16BIT
                            , IMAGE_SCN_MEM_LOCKED
                            , IMAGE_SCN_MEM_PRELOAD
                            , IMAGE_SCN_ALIGN_1BYTES
                            , IMAGE_SCN_ALIGN_2BYTES
                            , IMAGE_SCN_ALIGN_4BYTES
                            , IMAGE_SCN_ALIGN_8BYTES
                            , IMAGE_SCN_ALIGN_16BYTES
                            , IMAGE_SCN_ALIGN_32BYTES
                            , IMAGE_SCN_ALIGN_64BYTES
                            , IMAGE_SCN_ALIGN_128BYTES
                            , IMAGE_SCN_ALIGN_256BYTES
                            , IMAGE_SCN_ALIGN_512BYTES
                            , IMAGE_SCN_ALIGN_1024BYTES
                            , IMAGE_SCN_ALIGN_2048BYTES
                            , IMAGE_SCN_ALIGN_4096BYTES
                            , IMAGE_SCN_ALIGN_8192BYTES
                            , IMAGE_SCN_LNK_NRELOC_OVFL
                            , IMAGE_SCN_MEM_DISCARDABLE
                            , IMAGE_SCN_MEM_NOT_CACHED
                            , IMAGE_SCN_MEM_NOT_PAGED
                            , IMAGE_SCN_MEM_SHARED
                            , IMAGE_SCN_MEM_EXECUTE
                            , IMAGE_SCN_MEM_READ
                            , IMAGE_SCN_MEM_WRITE
                            ]
                - type: int
              SectionData: {type: str}
      symbols:
        type: seq
        sequence:
          - type: map
            mapping:
              Name: {type: str}
              Value: {type: int}
              SectionNumber: {type: int}
              SimpleType: [ {type: str, enum: [ IMAGE_SYM_TYPE_NULL
                                              , IMAGE_SYM_TYPE_VOID
                                              , IMAGE_SYM_TYPE_CHAR
                                              , IMAGE_SYM_TYPE_SHORT
                                              , IMAGE_SYM_TYPE_INT
                                              , IMAGE_SYM_TYPE_LONG
                                              , IMAGE_SYM_TYPE_FLOAT
                                              , IMAGE_SYM_TYPE_DOUBLE
                                              , IMAGE_SYM_TYPE_STRUCT
                                              , IMAGE_SYM_TYPE_UNION
                                              , IMAGE_SYM_TYPE_ENUM
                                              , IMAGE_SYM_TYPE_MOE
                                              , IMAGE_SYM_TYPE_BYTE
                                              , IMAGE_SYM_TYPE_WORD
                                              , IMAGE_SYM_TYPE_UINT
                                              , IMAGE_SYM_TYPE_DWORD
                                              ]}
                          , {type: int}
                          ]
              ComplexType: [ {type: str, enum: [ IMAGE_SYM_DTYPE_NULL
                                               , IMAGE_SYM_DTYPE_POINTER
                                               , IMAGE_SYM_DTYPE_FUNCTION
                                               , IMAGE_SYM_DTYPE_ARRAY
                                               ]}
                           , {type: int}
                           ]
              StorageClass: [ {type: str, enum:
                                          [ IMAGE_SYM_CLASS_END_OF_FUNCTION
                                          , IMAGE_SYM_CLASS_NULL
                                          , IMAGE_SYM_CLASS_AUTOMATIC
                                          , IMAGE_SYM_CLASS_EXTERNAL
                                          , IMAGE_SYM_CLASS_STATIC
                                          , IMAGE_SYM_CLASS_REGISTER
                                          , IMAGE_SYM_CLASS_EXTERNAL_DEF
                                          , IMAGE_SYM_CLASS_LABEL
                                          , IMAGE_SYM_CLASS_UNDEFINED_LABEL
                                          , IMAGE_SYM_CLASS_MEMBER_OF_STRUCT
                                          , IMAGE_SYM_CLASS_ARGUMENT
                                          , IMAGE_SYM_CLASS_STRUCT_TAG
                                          , IMAGE_SYM_CLASS_MEMBER_OF_UNION
                                          , IMAGE_SYM_CLASS_UNION_TAG
                                          , IMAGE_SYM_CLASS_TYPE_DEFINITION
                                          , IMAGE_SYM_CLASS_UNDEFINED_STATIC
                                          , IMAGE_SYM_CLASS_ENUM_TAG
                                          , IMAGE_SYM_CLASS_MEMBER_OF_ENUM
                                          , IMAGE_SYM_CLASS_REGISTER_PARAM
                                          , IMAGE_SYM_CLASS_BIT_FIELD
                                          , IMAGE_SYM_CLASS_BLOCK
                                          , IMAGE_SYM_CLASS_FUNCTION
                                          , IMAGE_SYM_CLASS_END_OF_STRUCT
                                          , IMAGE_SYM_CLASS_FILE
                                          , IMAGE_SYM_CLASS_SECTION
                                          , IMAGE_SYM_CLASS_WEAK_EXTERNAL
                                          , IMAGE_SYM_CLASS_CLR_TOKEN
                                          ]}
                            , {type: int}
                            ]
