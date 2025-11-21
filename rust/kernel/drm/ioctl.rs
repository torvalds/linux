// SPDX-License-Identifier: GPL-2.0 OR MIT

//! DRM IOCTL definitions.
//!
//! C header: [`include/drm/drm_ioctl.h`](srctree/include/drm/drm_ioctl.h)

use crate::ioctl;

const BASE: u32 = uapi::DRM_IOCTL_BASE as u32;

/// Construct a DRM ioctl number with no argument.
#[allow(non_snake_case)]
#[inline(always)]
pub const fn IO(nr: u32) -> u32 {
    ioctl::_IO(BASE, nr)
}

/// Construct a DRM ioctl number with a read-only argument.
#[allow(non_snake_case)]
#[inline(always)]
pub const fn IOR<T>(nr: u32) -> u32 {
    ioctl::_IOR::<T>(BASE, nr)
}

/// Construct a DRM ioctl number with a write-only argument.
#[allow(non_snake_case)]
#[inline(always)]
pub const fn IOW<T>(nr: u32) -> u32 {
    ioctl::_IOW::<T>(BASE, nr)
}

/// Construct a DRM ioctl number with a read-write argument.
#[allow(non_snake_case)]
#[inline(always)]
pub const fn IOWR<T>(nr: u32) -> u32 {
    ioctl::_IOWR::<T>(BASE, nr)
}

/// Descriptor type for DRM ioctls. Use the `declare_drm_ioctls!{}` macro to construct them.
pub type DrmIoctlDescriptor = bindings::drm_ioctl_desc;

/// This is for ioctl which are used for rendering, and require that the file descriptor is either
/// for a render node, or if it’s a legacy/primary node, then it must be authenticated.
pub const AUTH: u32 = bindings::drm_ioctl_flags_DRM_AUTH;

/// This must be set for any ioctl which can change the modeset or display state. Userspace must
/// call the ioctl through a primary node, while it is the active master.
///
/// Note that read-only modeset ioctl can also be called by unauthenticated clients, or when a
/// master is not the currently active one.
pub const MASTER: u32 = bindings::drm_ioctl_flags_DRM_MASTER;

/// Anything that could potentially wreak a master file descriptor needs to have this flag set.
///
/// Current that’s only for the SETMASTER and DROPMASTER ioctl, which e.g. logind can call to
/// force a non-behaving master (display compositor) into compliance.
///
/// This is equivalent to callers with the SYSADMIN capability.
pub const ROOT_ONLY: u32 = bindings::drm_ioctl_flags_DRM_ROOT_ONLY;

/// This is used for all ioctl needed for rendering only, for drivers which support render nodes.
/// This should be all new render drivers, and hence it should be always set for any ioctl with
/// `AUTH` set. Note though that read-only query ioctl might have this set, but have not set
/// DRM_AUTH because they do not require authentication.
pub const RENDER_ALLOW: u32 = bindings::drm_ioctl_flags_DRM_RENDER_ALLOW;

/// Internal structures used by the `declare_drm_ioctls!{}` macro. Do not use directly.
#[doc(hidden)]
pub mod internal {
    pub use bindings::drm_device;
    pub use bindings::drm_file;
    pub use bindings::drm_ioctl_desc;
}

/// Declare the DRM ioctls for a driver.
///
/// Each entry in the list should have the form:
///
/// `(ioctl_number, argument_type, flags, user_callback),`
///
/// `argument_type` is the type name within the `bindings` crate.
/// `user_callback` should have the following prototype:
///
/// ```ignore
/// fn foo(device: &kernel::drm::Device<Self>,
///        data: &mut uapi::argument_type,
///        file: &kernel::drm::File<Self::File>,
/// ) -> Result<u32>
/// ```
/// where `Self` is the drm::drv::Driver implementation these ioctls are being declared within.
///
/// # Examples
///
/// ```ignore
/// kernel::declare_drm_ioctls! {
///     (FOO_GET_PARAM, drm_foo_get_param, ioctl::RENDER_ALLOW, my_get_param_handler),
/// }
/// ```
///
#[macro_export]
macro_rules! declare_drm_ioctls {
    ( $(($cmd:ident, $struct:ident, $flags:expr, $func:expr)),* $(,)? ) => {
        const IOCTLS: &'static [$crate::drm::ioctl::DrmIoctlDescriptor] = {
            use $crate::uapi::*;
            const _:() = {
                let i: u32 = $crate::uapi::DRM_COMMAND_BASE;
                // Assert that all the IOCTLs are in the right order and there are no gaps,
                // and that the size of the specified type is correct.
                $(
                    let cmd: u32 = $crate::macros::concat_idents!(DRM_IOCTL_, $cmd);
                    ::core::assert!(i == $crate::ioctl::_IOC_NR(cmd));
                    ::core::assert!(core::mem::size_of::<$crate::uapi::$struct>() ==
                                    $crate::ioctl::_IOC_SIZE(cmd));
                    let i: u32 = i + 1;
                )*
            };

            let ioctls = &[$(
                $crate::drm::ioctl::internal::drm_ioctl_desc {
                    cmd: $crate::macros::concat_idents!(DRM_IOCTL_, $cmd) as u32,
                    func: {
                        #[allow(non_snake_case)]
                        unsafe extern "C" fn $cmd(
                                raw_dev: *mut $crate::drm::ioctl::internal::drm_device,
                                raw_data: *mut ::core::ffi::c_void,
                                raw_file: *mut $crate::drm::ioctl::internal::drm_file,
                        ) -> core::ffi::c_int {
                            // SAFETY:
                            // - The DRM core ensures the device lives while callbacks are being
                            //   called.
                            // - The DRM device must have been registered when we're called through
                            //   an IOCTL.
                            //
                            // FIXME: Currently there is nothing enforcing that the types of the
                            // dev/file match the current driver these ioctls are being declared
                            // for, and it's not clear how to enforce this within the type system.
                            let dev = $crate::drm::device::Device::from_raw(raw_dev);
                            // SAFETY: The ioctl argument has size `_IOC_SIZE(cmd)`, which we
                            // asserted above matches the size of this type, and all bit patterns of
                            // UAPI structs must be valid.
                            // The `ioctl` argument is exclusively owned by the handler
                            // and guaranteed by the C implementation (`drm_ioctl()`) to remain
                            // valid for the entire lifetime of the reference taken here.
                            // There is no concurrent access or aliasing; no other references
                            // to this object exist during this call.
                            let data = unsafe { &mut *(raw_data.cast::<$crate::uapi::$struct>()) };
                            // SAFETY: This is just the DRM file structure
                            let file = unsafe { $crate::drm::File::from_raw(raw_file) };

                            match $func(dev, data, file) {
                                Err(e) => e.to_errno(),
                                Ok(i) => i.try_into()
                                            .unwrap_or($crate::error::code::ERANGE.to_errno()),
                            }
                        }
                        Some($cmd)
                    },
                    flags: $flags,
                    name: $crate::c_str!(::core::stringify!($cmd)).as_char_ptr(),
                }
            ),*];
            ioctls
        };
    };
}
