// SPDX-License-Identifier: GPL-2.0

//! Macro to define register layout and accessors.
//!
//! The [`register!`](kernel::io::register!) macro provides an intuitive and readable syntax for
//! defining a dedicated type for each register and accessing it using [`Io`](super::Io). Each such
//! type comes with its own field accessors that can return an error if a field's value is invalid.
//!
//! Note: most of the items in this module are public so they can be referenced by the macro, but
//! most are not to be used directly by users. Outside of the `register!` macro itself, the only
//! item you might want to import from this module is [`Array`].
//!
//! # Simple example
//!
//! ```no_run
//! use kernel::io::{
//!     register,
//!     Region,
//! };
//!
//! register! {
//!     base: Region<0x1000>;
//!
//!     /// Basic information about the chip.
//!     pub BOOT_0(u32) @ 0x00000100 {
//!         /// Vendor ID.
//!         15:8 vendor_id;
//!         /// Major revision of the chip.
//!         7:4 major_revision;
//!         /// Minor revision of the chip.
//!         3:0 minor_revision;
//!     }
//! }
//! ```
//!
//! This defines a 32-bit `BOOT_0` type which can be read from or written to offset `0x100` of an
//! `Io` region, with the described bitfields. For instance, `minor_revision` consists of the 4
//! least significant bits of the type.
//!
//! Fields are instances of [`Bounded`](kernel::num::Bounded) and can be read by calling their
//! getter method, which is named after them. They also have setter methods prefixed with `with_`
//! for runtime values and `with_const_` for constant values. All setters return the updated
//! register value.
//!
//! Fields can also be transparently converted from/to an arbitrary type by using the `=>` and
//! `?=>` syntaxes.
//!
//! If present, doc comments above register or fields definitions are added to the relevant item
//! they document (the register type itself, or the field's setter and getter methods).
//!
//! Note that multiple registers can be defined in a single `register!` invocation. This can be
//! useful to group related registers together.
//!
//! Here is how the register defined above can be used in code:
//!
//!
//! ```no_run
//! use kernel::{
//!     io::{
//!         register,
//!         Io,
//!         IoLoc,
//!         Region,
//!     },
//!     num::Bounded,
//! };
//! # use kernel::io::Mmio;
//! # register! {
//! #     base: Region<0x1000>;
//! #
//! #     pub BOOT_0(u32) @ 0x00000100 {
//! #         15:8 vendor_id;
//! #         7:4 major_revision;
//! #         3:0 minor_revision;
//! #     }
//! # }
//! # fn test(io: Mmio<'_, Region<0x1000>>) {
//! # fn obtain_vendor_id() -> u8 { 0xff }
//!
//! // Read from the register's defined offset (0x100).
//! let boot0 = io.read(BOOT_0);
//! pr_info!("chip revision: {}.{}", boot0.major_revision().get(), boot0.minor_revision().get());
//!
//! // Update some fields and write the new value back.
//! let new_boot0 = boot0
//!     // Constant values.
//!     .with_const_major_revision::<3>()
//!     .with_const_minor_revision::<10>()
//!     // Runtime value.
//!     .with_vendor_id(obtain_vendor_id());
//! io.write_reg(new_boot0);
//!
//! // Or, build a new value from zero and write it:
//! io.write_reg(BOOT_0::zeroed()
//!     .with_const_major_revision::<3>()
//!     .with_const_minor_revision::<10>()
//!     .with_vendor_id(obtain_vendor_id())
//! );
//!
//! // Or, read and update the register in a single step.
//! io.update(BOOT_0, |r| r
//!     .with_const_major_revision::<3>()
//!     .with_const_minor_revision::<10>()
//!     .with_vendor_id(obtain_vendor_id())
//! );
//!
//! // Constant values can also be built using the const setters.
//! const V: BOOT_0 = pin_init::zeroed::<BOOT_0>()
//!     .with_const_major_revision::<3>()
//!     .with_const_minor_revision::<10>();
//! # }
//! ```
//!
//! For more extensive documentation about how to define registers, see the
//! [`register!`](kernel::io::register!) macro.

use core::marker::PhantomData;

use crate::{
    build_assert::build_assert,
    io::IoLoc, //
};

/// Allows `()` to be used as the `location` parameter of [`Io::write`](super::Io::write) when
/// passing a [`FixedIoLoc`] value.
impl<Base: ?Sized, T> IoLoc<Base, T> for ()
where
    T: FixedIoLoc<Base>,
{
    #[inline(always)]
    fn offset(self) -> usize {
        T::LOCATION.offset()
    }
}

// Provides a `IoLoc` impl that for a fixed offset.
#[doc(hidden)]
pub struct OffsetLoc<Base: ?Sized, T>(usize, PhantomData<(T, Base)>);

impl<Base: ?Sized, T> OffsetLoc<Base, T> {
    #[inline]
    pub const fn new(offset: usize) -> Self {
        Self(offset, PhantomData)
    }

    #[inline]
    pub const fn const_offset(self) -> usize {
        self.0
    }
}

impl<Base: ?Sized, T> IoLoc<Base, T> for OffsetLoc<Base, T> {
    #[inline(always)]
    fn offset(self) -> usize {
        self.0
    }
}

/// Trait implemented by arrays of registers.
pub trait RegisterArray: Sized {
    /// Base type for this register.
    type Base: ?Sized;

    /// Start offset of the register.
    ///
    /// The interpretation of this offset depends on the type of the register.
    const OFFSET: usize;
    /// Number of elements in the registers array.
    const SIZE: usize;
    /// Number of bytes between the start of elements in the registers array.
    const STRIDE: usize;
}

/// Location of an array register.
pub struct RegisterArrayLoc<T: RegisterArray>(usize, PhantomData<T>);

impl<T: RegisterArray> RegisterArrayLoc<T> {
    /// Returns the location of register `T` at position `idx`, with build-time validation.
    #[inline(always)]
    pub fn new(idx: usize) -> Self {
        build_assert!(idx < T::SIZE);

        Self(idx, PhantomData)
    }

    /// Attempts to return the location of register `T` at position `idx`, with runtime validation.
    #[inline(always)]
    pub fn try_new(idx: usize) -> Option<Self> {
        if idx < T::SIZE {
            Some(Self(idx, PhantomData))
        } else {
            None
        }
    }
}

impl<Base: ?Sized, T> IoLoc<Base, T> for RegisterArrayLoc<T>
where
    T: RegisterArray<Base = Base>,
{
    #[inline(always)]
    fn offset(self) -> usize {
        T::OFFSET + self.0 * T::STRIDE
    }
}

/// Trait providing location builders for [`RegisterArray`]s.
pub trait Array {
    /// Returns the location of the register at position `idx`, with build-time validation.
    #[inline(always)]
    fn at(idx: usize) -> RegisterArrayLoc<Self>
    where
        Self: RegisterArray,
    {
        RegisterArrayLoc::new(idx)
    }

    /// Returns the location of the register at position `idx`, with runtime validation.
    #[inline(always)]
    fn try_at(idx: usize) -> Option<RegisterArrayLoc<Self>>
    where
        Self: RegisterArray,
    {
        RegisterArrayLoc::try_new(idx)
    }
}

/// Trait implemented by types that indicate there is a fixed I/O location for this given type.
///
/// Implementors can be used with [`Io::write_reg`](super::Io::write_reg).
pub trait FixedIoLoc<Base: ?Sized>: Sized {
    /// Type of [`FixedIoLoc::LOCATION`].
    type Location: IoLoc<Base, Self>;

    /// Location of this type within given base.
    const LOCATION: Self::Location;
}

/// Trait implemented by items that contain both a register value and the absolute I/O location at
/// which to write it.
///
/// Implementors can be used with [`Io::write_reg`](super::Io::write_reg).
pub trait LocatedRegister<Base: ?Sized> {
    /// Value to write.
    type Value;
    /// Full location information at which to write the value.
    type Location: IoLoc<Base, Self::Value>;

    /// Consumes `self` and returns a `(location, value)` tuple describing a valid I/O write
    /// operation.
    fn into_io_op(self) -> (Self::Location, Self::Value);
}

impl<Base: ?Sized, T> LocatedRegister<Base> for T
where
    T: FixedIoLoc<Base>,
{
    type Location = T::Location;
    type Value = T;

    #[inline(always)]
    fn into_io_op(self) -> (T::Location, T) {
        (T::LOCATION, self)
    }
}

/// Helper function for register element alias implementation.
///
/// This is used to enforce base matching and provide bounds checking.
#[doc(hidden)]
#[inline(always)] // for const eval only
pub const fn element_alias_offset<Base: ?Sized, Alias: RegisterArray<Base = Base>>(
    idx: usize,
) -> usize {
    assert!(idx < Alias::SIZE);
    Alias::OFFSET + idx * Alias::STRIDE
}

/// Defines a dedicated type for a register, including getter and setter methods for its fields and
/// methods to read and write it from an [`Io`](kernel::io::Io) region.
///
/// This documentation focuses on how to declare registers. See the [module-level
/// documentation](mod@kernel::io::register) for examples of how to access them.
///
/// Registers can either be fixed offset registers or arrays of registers.
///
/// ## Fixed offset registers
///
/// These are the simplest kind of registers. Their location is simply an offset inside the I/O
/// region. For instance:
///
/// ```ignore
/// register! {
///     pub FIXED_REG(u16) @ 0x80 {
///         ...
///     }
/// }
/// ```
///
/// This creates a 16-bit register named `FIXED_REG` located at offset `0x80` of an I/O region.
///
/// These registers' location can be built simply by referencing their name:
///
/// ```no_run
/// use kernel::{
///     io::{
///         register,
///         Io,
///         Region,
///     },
/// };
/// # use kernel::io::Mmio;
///
/// register! {
///     base: Region<0x1000>;
///
///     FIXED_REG(u32) @ 0x100 {
///         15:8 high_byte;
///         7:0  low_byte;
///     }
/// }
///
/// # fn test(io: Mmio<'_, Region<0x1000>>) {
/// let val = io.read(FIXED_REG);
///
/// // Write from an already-existing value.
/// io.write(FIXED_REG, val.with_low_byte(0xff));
///
/// // Create a register value from scratch.
/// let val2 = FIXED_REG::zeroed().with_high_byte(0x80);
///
/// // The location of fixed offset registers is already contained in their type. Thus, the
/// // `location` argument of `Io::write` is technically redundant and can be replaced by `()`.
/// io.write((), val2);
///
/// // Or, the single-argument `Io::write_reg` can be used.
/// io.write_reg(val2);
/// # }
///
/// ```
///
/// It is possible to create an alias of an existing register with new field definitions by using
/// the `=> ALIAS` syntax. This is useful for cases where a register's interpretation depends on
/// the context:
///
/// ```no_run
/// use kernel::io::{
///     register,
///     Region,
/// };
///
/// register! {
///     base: Region<0x1000>;
///
///     /// Scratch register.
///     pub SCRATCH(u32) @ 0x00000200 {
///         31:0 value;
///     }
///
///     /// Boot status of the firmware.
///     pub SCRATCH_BOOT_STATUS(u32) => SCRATCH {
///         0:0 completed;
///     }
/// }
/// ```
///
/// In this example, `SCRATCH_BOOT_STATUS` uses the same I/O address as `SCRATCH`, while providing
/// its own `completed` field.
///
/// If you do not wish to have a bitfield defined, you can also create a register using an existing
/// type.
///
/// ```no_run
/// # use kernel::io::*;
/// register! {
///     base: Region<0x1000>;
///
///     /// UART RX register.
///     pub UART_RX: u8 @ 0x100;
/// }
/// ```
///
/// In case there is a fixed register associated with a specific type in the base, you can apply
/// `#[unique]` attribute which enables `write_reg` shorthand. This is automatically applied to
/// bitfields instantiated via the `register!` macro.
///
/// This should only be used when types meaningfully represent a register. For example, in the
/// previous `UART_RX` example, even if only a single register is defined with `u8` type, it is a
/// bad idea to annotate it with `#[unique]`.
///
/// ```no_run
/// # use kernel::{bitfield, io::*};
///
/// bitfield! {
///     pub struct Reset(u32) {
///         0:0 reset;
///     }
/// }
///
/// register! {
///     base: Region<0x1000>;
///
///     pub RESET: #[unique] Reset @ 0x100;
/// }
///
/// # fn test(mmio: Mmio<'_, Region<0x1000>>) {
/// // let mmio: Mmio<'_, Region<0x1000>>;
/// mmio.write_reg(Reset::zeroed().with_const_reset::<1>());
/// # }
/// ```
///
/// ## Arrays of registers
///
/// Some I/O areas contain consecutive registers that share the same field layout. These areas can
/// be defined as an array of identical registers, allowing them to be accessed by index with
/// compile-time or runtime bound checking:
///
/// ```ignore
/// register! {
///     pub REGISTER_ARRAY(u8)[10, stride = 4] @ 0x100 {
///         ...
///     }
/// }
/// ```
///
/// This defines `REGISTER_ARRAY`, an array of 10 byte registers starting at offset `0x100`. Each
/// register is separated from its neighbor by 4 bytes.
///
/// The `stride` parameter is optional; if unspecified, the registers are placed consecutively from
/// each other.
///
/// A location for a register in a register array is built using the [`Array::at`] trait method.
/// All arrays of registers implement [`Array`].
///
/// ```no_run
/// use kernel::{
///     io::{
///         register,
///         register::Array,
///         Io,
///         Region,
///     },
/// };
/// # use kernel::io::Mmio;
/// # fn get_scratch_idx() -> usize {
/// #   0x15
/// # }
///
/// // Array of 64 consecutive registers with the same layout starting at offset `0x80`.
/// register! {
///     base: Region<0x1000>;
///
///     /// Scratch registers.
///     pub SCRATCH(u32)[64] @ 0x00000080 {
///         31:0 value;
///     }
/// }
///
/// # fn test(io: Mmio<'_, Region<0x1000>>)
/// #     -> Result<(), Error>{
/// // Read scratch register 0, i.e. I/O address `0x80`.
/// let scratch_0 = io.read(SCRATCH::at(0)).value();
///
/// // Write scratch register 15, i.e. I/O address `0x80 + (15 * 4)`.
/// io.write(Array::at(15), SCRATCH::from(0xffeeaabb));
///
/// // This is out of bounds and won't build.
/// // let scratch_128 = io.read(SCRATCH::at(128)).value();
///
/// // Runtime-obtained array index.
/// let idx = get_scratch_idx();
/// // Access on a runtime index returns an error if it is out-of-bounds.
/// let some_scratch = io.read(SCRATCH::try_at(idx).ok_or(EINVAL)?).value();
///
/// // Alias to a specific register in an array.
/// // Here `SCRATCH[8]` is used to convey the firmware exit code.
/// register! {
///     base: Region<0x1000>;
///
///     /// Firmware exit status code.
///     pub FIRMWARE_STATUS(u32) => SCRATCH[8] {
///         7:0 status;
///     }
/// }
///
/// let status = io.read(FIRMWARE_STATUS).status();
///
/// // Non-contiguous register arrays can be defined by adding a stride parameter.
/// // Here, each of the 16 registers of the array is separated by 8 bytes, meaning that the
/// // registers of the two declarations below are interleaved.
/// register! {
///     base: Region<0x1000>;
///
///     /// Scratch registers bank 0.
///     pub SCRATCH_INTERLEAVED_0(u32)[16, stride = 8] @ 0x000000c0 {
///         31:0 value;
///     }
///
///     /// Scratch registers bank 1.
///     pub SCRATCH_INTERLEAVED_1(u32)[16, stride = 8] @ 0x000000c4 {
///         31:0 value;
///     }
/// }
/// # Ok(())
/// # }
/// ```
///
/// ## Relative registers
///
/// There are cases where a register region is subdivided into small subregions, and you may wish to
/// have your register definition be relative to these subregions. This may be needed, for example,
/// if these subregions are instantiated several times, or you just want it for encapsulation
/// purpose.
///
/// For instance, imagine the following I/O space:
///
/// ```text
///           +-----------------------------+
///           |             ...             |
///           |                             |
///  0x100--->+------------CPU0-------------+
///           |                             |
///  0x110--->+-----------------------------+
///           |           CPU_CTL           |
///           +-----------------------------+
///           |             ...             |
///           |                             |
///           |                             |
///  0x200--->+------------CPU1-------------+
///           |                             |
///  0x210--->+-----------------------------+
///           |           CPU_CTL           |
///           +-----------------------------+
///           |             ...             |
///           +-----------------------------+
/// ```
///
/// `CPU0` and `CPU1` both have a `CPU_CTL` register that starts at offset `0x10` of their I/O
/// space segment. Since both instances of `CPU_CTL` share the same layout, we don't want to define
/// them twice and would prefer a way to select which one to use from a single definition.
///
/// This can be done by defining a new type for the subregion, and then defining registers that use
/// the new type as the base:
///
/// ```no_run
/// use kernel::{
///     io::{
///         io_project,
///         register,
///         Io,
///         Region,
///     },
/// };
/// # use kernel::io::Mmio;
///
/// // Subregion type. Make sure it has adequate size and alignment.
/// #[repr(align(4))]
/// #[derive(FromBytes, IntoBytes)]
/// pub struct CpuCtl([u8; 0x100]);
///
/// register! {
///     base: Region<0x1000>;
///
///     // Subregions can just be defined like normal registers.
///     CPU0: CpuCtl @ 0x100;
///     CPU1: CpuCtl @ 0x200;
/// }
///
/// // Then you can define new registers on the subregion.
/// register! {
///     base: CpuCtl;
///
///     /// CPU core control.
///     pub CPU_CTL(u32) @ 0x10 {
///         0:0 start;
///     }
/// }
///
/// # fn test(io: Mmio<'_, Region<0x1000>>) {
/// // Read the status of `Cpu0`.
/// let cpu0_started = io_project!(io, build: CPU0).read(CPU_CTL);
///
/// // Stop `Cpu0`.
/// io_project!(io, build: CPU0).write_reg(CPU_CTL::zeroed());
/// # }
/// ```
#[macro_export]
macro_rules! register {
    ($($tt:tt)*) => {
        $crate::macros::register!($($tt)*);
    };
}
