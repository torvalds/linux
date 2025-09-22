.. raw:: html

  <style type="text/css">
    .none { background-color: #FFCCCC }
    .part { background-color: #FFFF99 }
    .good { background-color: #CCFF99 }
  </style>

.. role:: none
.. role:: part
.. role:: good

.. contents::
   :local:

==============
AMDGPU Support
==============

Clang supports OpenCL, HIP and OpenMP on AMD GPU targets.


Predefined Macros
=================


.. list-table::
   :header-rows: 1

   * - Macro
     - Description
   * - ``__AMDGPU__``
     - Indicates that the code is being compiled for an AMD GPU.
   * - ``__AMDGCN__``
     - Defined if the GPU target is AMDGCN.
   * - ``__R600__``
     - Defined if the GPU target is R600.
   * - ``__<ArchName>__``
     - Defined with the name of the architecture (e.g., ``__gfx906__`` for the gfx906 architecture).
   * - ``__<GFXN>__``
     - Defines the GFX family (e.g., for gfx906, this macro would be ``__GFX9__``).
   * - ``__amdgcn_processor__``
     - Defined with the processor name as a string (e.g., ``"gfx906"``).
   * - ``__amdgcn_target_id__``
     - Defined with the target ID as a string.
   * - ``__amdgcn_feature_<feature-name>__``
     - Defined for each supported target feature. The value is 1 if the feature is enabled and 0 if it is disabled. Allowed feature names are sramecc and xnack.
   * - ``__AMDGCN_CUMODE__``
     - Defined as 1 if the CU mode is enabled and 0 if the WGP mode is enabled.
   * - ``__AMDGCN_UNSAFE_FP_ATOMICS__``
     - Defined if unsafe floating-point atomics are allowed.
   * - ``__AMDGCN_WAVEFRONT_SIZE__``
     - Defines the wavefront size. Allowed values are 32 and 64.
   * - ``__AMDGCN_WAVEFRONT_SIZE``
     - Alias to ``__AMDGCN_WAVEFRONT_SIZE__``. To be deprecated.
   * - ``__HAS_FMAF__``
     - Defined if FMAF instruction is available (deprecated).
   * - ``__HAS_LDEXPF__``
     - Defined if LDEXPF instruction is available (deprecated).
   * - ``__HAS_FP64__``
     - Defined if FP64 instruction is available (deprecated).

Please note that the specific architecture and feature names will vary depending on the GPU. Also, some macros are deprecated and may be removed in future releases.
