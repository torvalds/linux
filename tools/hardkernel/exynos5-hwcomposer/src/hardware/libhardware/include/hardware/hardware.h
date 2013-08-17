/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_INCLUDE_HARDWARE_HARDWARE_H
#define ANDROID_INCLUDE_HARDWARE_HARDWARE_H

#include <stdint.h>
#include <sys/cdefs.h>

#include <cutils/native_handle.h>
#include <system/graphics.h>

__BEGIN_DECLS

/*
 * Value for the hw_module_t.tag field
 */

#define MAKE_TAG_CONSTANT(A,B,C,D) (((A) << 24) | ((B) << 16) | ((C) << 8) | (D))

#define HARDWARE_MODULE_TAG MAKE_TAG_CONSTANT('H', 'W', 'M', 'T')
#define HARDWARE_DEVICE_TAG MAKE_TAG_CONSTANT('H', 'W', 'D', 'T')

#define HARDWARE_MAKE_API_VERSION(maj,min) \
            ((((maj) & 0xff) << 8) | ((min) & 0xff))

#define HARDWARE_MAKE_API_VERSION_2(maj,min,hdr) \
            ((((maj) & 0xff) << 24) | (((min) & 0xff) << 16) | ((hdr) & 0xffff))
#define HARDWARE_API_VERSION_2_MAJ_MIN_MASK 0xffff0000
#define HARDWARE_API_VERSION_2_HEADER_MASK  0x0000ffff


/*
 * The current HAL API version.
 *
 * All module implementations must set the hw_module_t.hal_api_version field
 * to this value when declaring the module with HAL_MODULE_INFO_SYM.
 *
 * Note that previous implementations have always set this field to 0.
 * Therefore, libhardware HAL API will always consider versions 0.0 and 1.0
 * to be 100% binary compatible.
 *
 */
#define HARDWARE_HAL_API_VERSION HARDWARE_MAKE_API_VERSION(1, 0)

/*
 * Helper macros for module implementors.
 *
 * The derived modules should provide convenience macros for supported
 * versions so that implementations can explicitly specify module/device
 * versions at definition time.
 *
 * Use this macro to set the hw_module_t.module_api_version field.
 */
#define HARDWARE_MODULE_API_VERSION(maj,min) HARDWARE_MAKE_API_VERSION(maj,min)
#define HARDWARE_MODULE_API_VERSION_2(maj,min,hdr) HARDWARE_MAKE_API_VERSION_2(maj,min,hdr)

/*
 * Use this macro to set the hw_device_t.version field
 */
#define HARDWARE_DEVICE_API_VERSION(maj,min) HARDWARE_MAKE_API_VERSION(maj,min)
#define HARDWARE_DEVICE_API_VERSION_2(maj,min,hdr) HARDWARE_MAKE_API_VERSION_2(maj,min,hdr)

struct hw_module_t;
struct hw_module_methods_t;
struct hw_device_t;

/**
 * Every hardware module must have a data structure named HAL_MODULE_INFO_SYM
 * and the fields of this data structure must begin with hw_module_t
 * followed by module specific information.
 */
typedef struct hw_module_t {
    /** tag must be initialized to HARDWARE_MODULE_TAG */
    uint32_t tag;

    /**
     * The API version of the implemented module. The module owner is
     * responsible for updating the version when a module interface has
     * changed.
     *
     * The derived modules such as gralloc and audio own and manage this field.
     * The module user must interpret the version field to decide whether or
     * not to inter-operate with the supplied module implementation.
     * For example, SurfaceFlinger is responsible for making sure that
     * it knows how to manage different versions of the gralloc-module API,
     * and AudioFlinger must know how to do the same for audio-module API.
     *
     * The module API version should include a major and a minor component.
     * For example, version 1.0 could be represented as 0x0100. This format
     * implies that versions 0x0100-0x01ff are all API-compatible.
     *
     * In the future, libhardware will expose a hw_get_module_version()
     * (or equivalent) function that will take minimum/maximum supported
     * versions as arguments and would be able to reject modules with
     * versions outside of the supplied range.
     */
    uint16_t module_api_version;
#define version_major module_api_version
    /**
     * version_major/version_minor defines are supplied here for temporary
     * source code compatibility. They will be removed in the next version.
     * ALL clients must convert to the new version format.
     */

    /**
     * The API version of the HAL module interface. This is meant to
     * version the hw_module_t, hw_module_methods_t, and hw_device_t
     * structures and definitions.
     *
     * The HAL interface owns this field. Module users/implementations
     * must NOT rely on this value for version information.
     *
     * Presently, 0 is the only valid value.
     */
    uint16_t hal_api_version;
#define version_minor hal_api_version

    /** Identifier of module */
    const char *id;

    /** Name of this module */
    const char *name;

    /** Author/owner/implementor of the module */
    const char *author;

    /** Modules methods */
    struct hw_module_methods_t* methods;

    /** module's dso */
    void* dso;

    /** padding to 128 bytes, reserved for future use */
    uint32_t reserved[32-7];

} hw_module_t;

typedef struct hw_module_methods_t {
    /** Open a specific device */
    int (*open)(const struct hw_module_t* module, const char* id,
            struct hw_device_t** device);

} hw_module_methods_t;

/**
 * Every device data structure must begin with hw_device_t
 * followed by module specific public methods and attributes.
 */
typedef struct hw_device_t {
    /** tag must be initialized to HARDWARE_DEVICE_TAG */
    uint32_t tag;

    /**
     * Version of the module-specific device API. This value is used by
     * the derived-module user to manage different device implementations.
     *
     * The module user is responsible for checking the module_api_version
     * and device version fields to ensure that the user is capable of
     * communicating with the specific module implementation.
     *
     * One module can support multiple devices with different versions. This
     * can be useful when a device interface changes in an incompatible way
     * but it is still necessary to support older implementations at the same
     * time. One such example is the Camera 2.0 API.
     *
     * This field is interpreted by the module user and is ignored by the
     * HAL interface itself.
     */
    uint32_t version;

    /** reference to the module this device belongs to */
    struct hw_module_t* module;

    /** padding reserved for future use */
    uint32_t reserved[12];

    /** Close this device */
    int (*close)(struct hw_device_t* device);

} hw_device_t;

/**
 * Name of the hal_module_info
 */
#define HAL_MODULE_INFO_SYM         HMI

/**
 * Name of the hal_module_info as a string
 */
#define HAL_MODULE_INFO_SYM_AS_STR  "HMI"

/**
 * Get the module info associated with a module by id.
 *
 * @return: 0 == success, <0 == error and *module == NULL
 */
int hw_get_module(const char *id, const struct hw_module_t **module);

/**
 * Get the module info associated with a module instance by class 'class_id'
 * and instance 'inst'.
 *
 * Some modules types necessitate multiple instances. For example audio supports
 * multiple concurrent interfaces and thus 'audio' is the module class
 * and 'primary' or 'a2dp' are module interfaces. This implies that the files
 * providing these modules would be named audio.primary.<variant>.so and
 * audio.a2dp.<variant>.so
 *
 * @return: 0 == success, <0 == error and *module == NULL
 */
int hw_get_module_by_class(const char *class_id, const char *inst,
                           const struct hw_module_t **module);

__END_DECLS

#endif  /* ANDROID_INCLUDE_HARDWARE_HARDWARE_H */
