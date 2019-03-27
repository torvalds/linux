/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/






/**
 * @file
 *
 * File defining different Octeon model IDs and macros to
 * compare them.
 *
 * <hr>$Revision: 70338 $<hr>
 */

#ifndef __OCTEON_MODEL_H__
#define __OCTEON_MODEL_H__

#ifdef	__cplusplus
extern "C" {
#endif


/* NOTE: These must match what is checked in common-config.mk */
/* Defines to represent the different versions of Octeon.  */


/* IMPORTANT: When the default pass is updated for an Octeon Model,
** the corresponding change must also be made in the oct-sim script. */


/* The defines below should be used with the OCTEON_IS_MODEL() macro to
** determine what model of chip the software is running on.  Models ending
** in 'XX' match multiple models (families), while specific models match only
** that model.  If a pass (revision) is specified, then only that revision
** will be matched.  Care should be taken when checking for both specific
** models and families that the specific models are checked for first.
** While these defines are similar to the processor ID, they are not intended
** to be used by anything other that the OCTEON_IS_MODEL framework, and
** the values are subject to change at anytime without notice.
**
** NOTE: only the OCTEON_IS_MODEL() macro/function and the OCTEON_CN* macros
** should be used outside of this file.  All other macros are for internal
** use only, and may change without notice.
*/

#define OCTEON_FAMILY_MASK      0x00ffff00

/* Flag bits in top byte */
#define OM_IGNORE_REVISION        0x01000000      /* Ignores revision in model checks */
#define OM_CHECK_SUBMODEL         0x02000000      /* Check submodels */
#define OM_MATCH_PREVIOUS_MODELS  0x04000000      /* Match all models previous than the one specified */
#define OM_IGNORE_MINOR_REVISION  0x08000000      /* Ignores the minor revison on newer parts */
#define OM_FLAG_MASK              0xff000000

#define OM_MATCH_5XXX_FAMILY_MODELS     0x20000000 /* Match all cn5XXX Octeon models. */
#define OM_MATCH_6XXX_FAMILY_MODELS     0x40000000 /* Match all cn6XXX Octeon models. */
#define OM_MATCH_F7XXX_FAMILY_MODELS    0x80000000 /* Match all cnf7XXX Octeon models. */

/*
 * CNF7XXX models with new revision encoding
 */
#define OCTEON_CNF71XX_PASS1_0  0x000d9400

#define OCTEON_CNF71XX          (OCTEON_CNF71XX_PASS1_0 | OM_IGNORE_REVISION)
#define OCTEON_CNF71XX_PASS1_X  (OCTEON_CNF71XX_PASS1_0 | OM_IGNORE_MINOR_REVISION)

/*
 * CN6XXX models with new revision encoding
 */
#define OCTEON_CN68XX_PASS1_0   0x000d9100
#define OCTEON_CN68XX_PASS1_1   0x000d9101
#define OCTEON_CN68XX_PASS1_2   0x000d9102
#define OCTEON_CN68XX_PASS2_0   0x000d9108

#define OCTEON_CN68XX           (OCTEON_CN68XX_PASS2_0 | OM_IGNORE_REVISION)
#define OCTEON_CN68XX_PASS1_X   (OCTEON_CN68XX_PASS1_0 | OM_IGNORE_MINOR_REVISION)
#define OCTEON_CN68XX_PASS2_X   (OCTEON_CN68XX_PASS2_0 | OM_IGNORE_MINOR_REVISION)

#define OCTEON_CN68XX_PASS1	OCTEON_CN68XX_PASS1_X
#define OCTEON_CN68XX_PASS2	OCTEON_CN68XX_PASS2_X

#define OCTEON_CN66XX_PASS1_0   0x000d9200
#define OCTEON_CN66XX_PASS1_2   0x000d9202

#define OCTEON_CN66XX           (OCTEON_CN66XX_PASS1_0 | OM_IGNORE_REVISION)
#define OCTEON_CN66XX_PASS1_X   (OCTEON_CN66XX_PASS1_0 | OM_IGNORE_MINOR_REVISION)

#define OCTEON_CN63XX_PASS1_0   0x000d9000
#define OCTEON_CN63XX_PASS1_1   0x000d9001
#define OCTEON_CN63XX_PASS1_2   0x000d9002
#define OCTEON_CN63XX_PASS2_0   0x000d9008
#define OCTEON_CN63XX_PASS2_1   0x000d9009
#define OCTEON_CN63XX_PASS2_2   0x000d900a

#define OCTEON_CN63XX           (OCTEON_CN63XX_PASS2_0 | OM_IGNORE_REVISION)
#define OCTEON_CN63XX_PASS1_X   (OCTEON_CN63XX_PASS1_0 | OM_IGNORE_MINOR_REVISION)
#define OCTEON_CN63XX_PASS2_X   (OCTEON_CN63XX_PASS2_0 | OM_IGNORE_MINOR_REVISION)

#define OCTEON_CN61XX_PASS1_0   0x000d9300

#define OCTEON_CN61XX           (OCTEON_CN61XX_PASS1_0 | OM_IGNORE_REVISION)
#define OCTEON_CN61XX_PASS1_X   (OCTEON_CN61XX_PASS1_0 | OM_IGNORE_MINOR_REVISION)

/*
 * CN5XXX models with new revision encoding
 */
#define OCTEON_CN58XX_PASS1_0   0x000d0300
#define OCTEON_CN58XX_PASS1_1   0x000d0301
#define OCTEON_CN58XX_PASS1_2   0x000d0303
#define OCTEON_CN58XX_PASS2_0   0x000d0308
#define OCTEON_CN58XX_PASS2_1   0x000d0309
#define OCTEON_CN58XX_PASS2_2   0x000d030a
#define OCTEON_CN58XX_PASS2_3   0x000d030b

#define OCTEON_CN58XX           (OCTEON_CN58XX_PASS1_0 | OM_IGNORE_REVISION)
#define OCTEON_CN58XX_PASS1_X   (OCTEON_CN58XX_PASS1_0 | OM_IGNORE_MINOR_REVISION)
#define OCTEON_CN58XX_PASS2_X   (OCTEON_CN58XX_PASS2_0 | OM_IGNORE_MINOR_REVISION)
#define OCTEON_CN58XX_PASS1     OCTEON_CN58XX_PASS1_X
#define OCTEON_CN58XX_PASS2     OCTEON_CN58XX_PASS2_X

#define OCTEON_CN56XX_PASS1_0   0x000d0400
#define OCTEON_CN56XX_PASS1_1   0x000d0401
#define OCTEON_CN56XX_PASS2_0   0x000d0408
#define OCTEON_CN56XX_PASS2_1   0x000d0409

#define OCTEON_CN56XX           (OCTEON_CN56XX_PASS2_0 | OM_IGNORE_REVISION)
#define OCTEON_CN56XX_PASS1_X   (OCTEON_CN56XX_PASS1_0 | OM_IGNORE_MINOR_REVISION)
#define OCTEON_CN56XX_PASS2_X   (OCTEON_CN56XX_PASS2_0 | OM_IGNORE_MINOR_REVISION)
#define OCTEON_CN56XX_PASS1     OCTEON_CN56XX_PASS1_X
#define OCTEON_CN56XX_PASS2     OCTEON_CN56XX_PASS2_X

#define OCTEON_CN57XX           OCTEON_CN56XX
#define OCTEON_CN57XX_PASS1     OCTEON_CN56XX_PASS1
#define OCTEON_CN57XX_PASS2     OCTEON_CN56XX_PASS2

#define OCTEON_CN55XX           OCTEON_CN56XX
#define OCTEON_CN55XX_PASS1     OCTEON_CN56XX_PASS1
#define OCTEON_CN55XX_PASS2     OCTEON_CN56XX_PASS2

#define OCTEON_CN54XX           OCTEON_CN56XX
#define OCTEON_CN54XX_PASS1     OCTEON_CN56XX_PASS1
#define OCTEON_CN54XX_PASS2     OCTEON_CN56XX_PASS2

#define OCTEON_CN50XX_PASS1_0   0x000d0600

#define OCTEON_CN50XX           (OCTEON_CN50XX_PASS1_0 | OM_IGNORE_REVISION)
#define OCTEON_CN50XX_PASS1_X   (OCTEON_CN50XX_PASS1_0 | OM_IGNORE_MINOR_REVISION)
#define OCTEON_CN50XX_PASS1     OCTEON_CN50XX_PASS1_X

/* NOTE: Octeon CN5000F model is not identifiable using the OCTEON_IS_MODEL()
    functions, but are treated as CN50XX */

#define OCTEON_CN52XX_PASS1_0   0x000d0700
#define OCTEON_CN52XX_PASS2_0   0x000d0708

#define OCTEON_CN52XX           (OCTEON_CN52XX_PASS2_0 | OM_IGNORE_REVISION)
#define OCTEON_CN52XX_PASS1_X   (OCTEON_CN52XX_PASS1_0 | OM_IGNORE_MINOR_REVISION)
#define OCTEON_CN52XX_PASS2_X   (OCTEON_CN52XX_PASS2_0 | OM_IGNORE_MINOR_REVISION)
#define OCTEON_CN52XX_PASS1     OCTEON_CN52XX_PASS1_X
#define OCTEON_CN52XX_PASS2     OCTEON_CN52XX_PASS2_X

/*
 * CN3XXX models with old revision enconding
 */
//#define OCTEON_CN38XX_PASS1     0x000d0000    // is not supported
#define OCTEON_CN38XX_PASS2     0x000d0001
#define OCTEON_CN38XX_PASS3     0x000d0003
#define OCTEON_CN38XX           (OCTEON_CN38XX_PASS3 | OM_IGNORE_REVISION)

#define OCTEON_CN36XX           OCTEON_CN38XX
#define OCTEON_CN36XX_PASS2     OCTEON_CN38XX_PASS2
#define OCTEON_CN36XX_PASS3     OCTEON_CN38XX_PASS3

/* The OCTEON_CN31XX matches CN31XX models and the CN3020 */
#define OCTEON_CN31XX_PASS1     0x000d0100
#define OCTEON_CN31XX_PASS1_1   0x000d0102
#define OCTEON_CN31XX           (OCTEON_CN31XX_PASS1 | OM_IGNORE_REVISION)

/* This model is only used for internal checks, it
** is not a valid model for the OCTEON_MODEL environment variable.
** This matches the CN3010 and CN3005 but NOT the CN3020*/
#define OCTEON_CN30XX_PASS1     0x000d0200
#define OCTEON_CN30XX_PASS1_1   0x000d0202
#define OCTEON_CN30XX           (OCTEON_CN30XX_PASS1 | OM_IGNORE_REVISION)

#define OCTEON_CN3005_PASS1     (0x000d0210 | OM_CHECK_SUBMODEL)
#define OCTEON_CN3005_PASS1_0   (0x000d0210 | OM_CHECK_SUBMODEL)
#define OCTEON_CN3005_PASS1_1   (0x000d0212 | OM_CHECK_SUBMODEL)
#define OCTEON_CN3005           (OCTEON_CN3005_PASS1 | OM_IGNORE_REVISION | OM_CHECK_SUBMODEL)

#define OCTEON_CN3010_PASS1     (0x000d0200 | OM_CHECK_SUBMODEL)
#define OCTEON_CN3010_PASS1_0   (0x000d0200 | OM_CHECK_SUBMODEL)
#define OCTEON_CN3010_PASS1_1   (0x000d0202 | OM_CHECK_SUBMODEL)
#define OCTEON_CN3010           (OCTEON_CN3010_PASS1 | OM_IGNORE_REVISION | OM_CHECK_SUBMODEL)

#define OCTEON_CN3020_PASS1     (0x000d0110 | OM_CHECK_SUBMODEL)
#define OCTEON_CN3020_PASS1_0   (0x000d0110 | OM_CHECK_SUBMODEL)
#define OCTEON_CN3020_PASS1_1   (0x000d0112 | OM_CHECK_SUBMODEL)
#define OCTEON_CN3020           (OCTEON_CN3020_PASS1 | OM_IGNORE_REVISION | OM_CHECK_SUBMODEL)



/* This matches the complete family of CN3xxx CPUs, and not subsequent models */
#define OCTEON_CN3XXX           (OCTEON_CN58XX_PASS1_0 | OM_MATCH_PREVIOUS_MODELS | OM_IGNORE_REVISION)
#define OCTEON_CN5XXX           (OCTEON_CN58XX_PASS1_0 | OM_MATCH_5XXX_FAMILY_MODELS)
#define OCTEON_CN6XXX           (OCTEON_CN63XX_PASS1_0 | OM_MATCH_6XXX_FAMILY_MODELS)
#define OCTEON_CNF7XXX          (OCTEON_CNF71XX_PASS1_0 | OM_MATCH_F7XXX_FAMILY_MODELS)

/* The revision byte (low byte) has two different encodings.
** CN3XXX:
**
**     bits
**     <7:5>: reserved (0)
**     <4>:   alternate package
**     <3:0>: revision
**
** CN5XXX:
**
**     bits
**     <7>:   reserved (0)
**     <6>:   alternate package
**     <5:3>: major revision
**     <2:0>: minor revision
**
*/

/* Masks used for the various types of model/family/revision matching */
#define OCTEON_38XX_FAMILY_MASK      0x00ffff00
#define OCTEON_38XX_FAMILY_REV_MASK  0x00ffff0f
#define OCTEON_38XX_MODEL_MASK       0x00ffff10
#define OCTEON_38XX_MODEL_REV_MASK   (OCTEON_38XX_FAMILY_REV_MASK | OCTEON_38XX_MODEL_MASK)

/* CN5XXX and later use different layout of bits in the revision ID field */
#define OCTEON_58XX_FAMILY_MASK      OCTEON_38XX_FAMILY_MASK
#define OCTEON_58XX_FAMILY_REV_MASK  0x00ffff3f
#define OCTEON_58XX_MODEL_MASK       0x00ffffc0
#define OCTEON_58XX_MODEL_REV_MASK   (OCTEON_58XX_FAMILY_REV_MASK | OCTEON_58XX_MODEL_MASK)
#define OCTEON_58XX_MODEL_MINOR_REV_MASK (OCTEON_58XX_MODEL_REV_MASK & 0x00fffff8)
#define OCTEON_5XXX_MODEL_MASK       0x00ff0fc0


#define __OCTEON_MATCH_MASK__(x,y,z) (((x) & (z)) == ((y) & (z)))


/* NOTE: This for internal use only!!!!! */
#define __OCTEON_IS_MODEL_COMPILE__(arg_model, chip_model) \
    ((((arg_model & OCTEON_38XX_FAMILY_MASK) < OCTEON_CN58XX_PASS1_0)  && (\
     ((((arg_model) & (OM_FLAG_MASK)) == (OM_IGNORE_REVISION | OM_CHECK_SUBMODEL)) \
       && __OCTEON_MATCH_MASK__((chip_model), (arg_model), OCTEON_38XX_MODEL_MASK)) || \
     ((((arg_model) & (OM_FLAG_MASK)) == 0) \
       && __OCTEON_MATCH_MASK__((chip_model), (arg_model), OCTEON_38XX_FAMILY_REV_MASK)) || \
     ((((arg_model) & (OM_FLAG_MASK)) == OM_IGNORE_REVISION) \
       && __OCTEON_MATCH_MASK__((chip_model), (arg_model), OCTEON_38XX_FAMILY_MASK)) || \
     ((((arg_model) & (OM_FLAG_MASK)) == OM_CHECK_SUBMODEL) \
       && __OCTEON_MATCH_MASK__((chip_model), (arg_model), OCTEON_38XX_MODEL_REV_MASK)) || \
     ((((arg_model) & (OM_MATCH_PREVIOUS_MODELS)) == OM_MATCH_PREVIOUS_MODELS) \
       && (((chip_model) & OCTEON_38XX_MODEL_MASK) < ((arg_model) & OCTEON_38XX_MODEL_MASK))) \
    )) || \
    (((arg_model & OCTEON_38XX_FAMILY_MASK) >= OCTEON_CN58XX_PASS1_0)  && (\
     ((((arg_model) & (OM_FLAG_MASK)) == (OM_IGNORE_REVISION | OM_CHECK_SUBMODEL)) \
       && __OCTEON_MATCH_MASK__((chip_model), (arg_model), OCTEON_58XX_MODEL_MASK)) || \
     ((((arg_model) & (OM_FLAG_MASK)) == 0) \
       && __OCTEON_MATCH_MASK__((chip_model), (arg_model), OCTEON_58XX_FAMILY_REV_MASK)) || \
     ((((arg_model) & (OM_FLAG_MASK)) == OM_IGNORE_MINOR_REVISION) \
       && __OCTEON_MATCH_MASK__((chip_model), (arg_model), OCTEON_58XX_MODEL_MINOR_REV_MASK)) || \
     ((((arg_model) & (OM_FLAG_MASK)) == OM_IGNORE_REVISION) \
       && __OCTEON_MATCH_MASK__((chip_model), (arg_model), OCTEON_58XX_FAMILY_MASK)) || \
     ((((arg_model) & (OM_FLAG_MASK)) == OM_CHECK_SUBMODEL) \
       && __OCTEON_MATCH_MASK__((chip_model), (arg_model), OCTEON_58XX_MODEL_REV_MASK)) || \
     ((((arg_model) & (OM_MATCH_5XXX_FAMILY_MODELS)) == OM_MATCH_5XXX_FAMILY_MODELS) \
       && ((chip_model) >= OCTEON_CN58XX_PASS1_0) && ((chip_model) < OCTEON_CN63XX_PASS1_0)) || \
     ((((arg_model) & (OM_MATCH_6XXX_FAMILY_MODELS)) == OM_MATCH_6XXX_FAMILY_MODELS) \
       && ((chip_model) >= OCTEON_CN63XX_PASS1_0) && ((chip_model) < OCTEON_CNF71XX_PASS1_0)) || \
     ((((arg_model) & (OM_MATCH_F7XXX_FAMILY_MODELS)) == OM_MATCH_F7XXX_FAMILY_MODELS) \
       && ((chip_model) >= OCTEON_CNF71XX_PASS1_0)) || \
     ((((arg_model) & (OM_MATCH_PREVIOUS_MODELS)) == OM_MATCH_PREVIOUS_MODELS) \
       && (((chip_model) & OCTEON_58XX_MODEL_MASK) < ((arg_model) & OCTEON_58XX_MODEL_MASK))) \
    )))

#ifndef OCTEON_IS_MODEL
#if defined(USE_RUNTIME_MODEL_CHECKS) || defined(__U_BOOT__) || (defined(__linux__) && defined(__KERNEL__)) || defined(CVMX_BUILD_FOR_TOOLCHAIN) || (defined(__FreeBSD__) && defined(_KERNEL) && !defined(OCTEON_MODEL))

/* NOTE: This for internal use only!!!!! */
static inline int __octeon_is_model_runtime__(uint32_t model)
{
    uint32_t cpuid = cvmx_get_proc_id();

    /* Check for special case of mismarked 3005 samples. We only need to check
        if the sub model isn't being ignored */
    if ((model & OM_CHECK_SUBMODEL) == OM_CHECK_SUBMODEL)
    {
        if (cpuid == OCTEON_CN3010_PASS1 && (cvmx_read_csr(0x80011800800007B8ull) & (1ull << 34)))
            cpuid |= 0x10;
    }
    return(__OCTEON_IS_MODEL_COMPILE__(model, cpuid));
}

/* The OCTEON_IS_MODEL macro should be used for all Octeon model checking done
** in a program.
** This should be kept runtime if at all possible  and must be conditionalized
** with OCTEON_IS_COMMON_BINARY() if runtime checking support is required.
**
** Use of the macro in preprocessor directives ( #if OCTEON_IS_MODEL(...) )
** is NOT SUPPORTED, and should be replaced with CVMX_COMPILED_FOR()
** I.e.:
**  #if OCTEON_IS_MODEL(OCTEON_CN56XX)  ->  #if CVMX_COMPILED_FOR(OCTEON_CN56XX)
*/
#define OCTEON_IS_MODEL(x) __octeon_is_model_runtime__(x)
#define OCTEON_IS_COMMON_BINARY() 1
#undef OCTEON_MODEL
#else
#define CVMX_COMPILED_FOR(x) __OCTEON_IS_MODEL_COMPILE__(x, OCTEON_MODEL)

#define OCTEON_IS_MODEL(x) \
    (((x & (OM_IGNORE_REVISION | OM_IGNORE_MINOR_REVISION)) != 0) \
    ? __OCTEON_IS_MODEL_COMPILE__(x, OCTEON_MODEL) \
    : __OCTEON_IS_MODEL_COMPILE__(x, cvmx_get_proc_id()))

#define OCTEON_IS_COMMON_BINARY() 0
#endif
#endif

int octeon_model_version_check(uint32_t chip_id);
const char *octeon_model_get_string(uint32_t chip_id);
const char *octeon_model_get_string_buffer(uint32_t chip_id, char * buffer);

/**                                                                          
 * Return the octeon family, i.e., ProcessorID of the PrID register.
 *
 * @return the octeon family on success, ((unint32_t)-1) on error.
 */
static inline uint32_t cvmx_get_octeon_family(void)
{
#if defined(USE_RUNTIME_MODEL_CHECKS) || defined(__U_BOOT__) || (defined(__linux__) && defined(__KERNEL__)) || defined(CVMX_BUILD_FOR_TOOLCHAIN) || (defined(__FreeBSD__) && defined(_KERNEL))
    return (cvmx_get_proc_id() & OCTEON_FAMILY_MASK);
#else
    return (OCTEON_MODEL & OCTEON_FAMILY_MASK);
#endif
}

#ifdef	__cplusplus
}
#endif

#endif    /* __OCTEON_MODEL_H__ */
