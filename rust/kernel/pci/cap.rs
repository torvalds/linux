// SPDX-License-Identifier: GPL-2.0

//! PCI extended capability support.

use super::{
    io::ConfigSpaceBackend,
    ConfigSpace,
    Extended, //
};
use crate::{
    bindings,
    io::{
        Io,
        IoBackend,
        Region, //
    },
    num::{casts, Bounded},
    prelude::*,
};

/// Number of VF BAR register slots in an SR-IOV capability.
const NUM_VF_BARS: usize = casts::u32_as_usize(bindings::PCI_SRIOV_NUM_BARS);

/// PCI extended capability IDs.
#[repr(transparent)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ExtCapId(u16);

impl ExtCapId {
    /// Single Root I/O Virtualization.
    pub const SRIOV: Self = Self(casts::u32_into_u16::<{ bindings::PCI_EXT_CAP_ID_SRIOV }>());

    /// Returns the raw PCIe extended capability ID.
    #[inline]
    const fn as_raw(self) -> u16 {
        self.0
    }
}

/// A typed PCI extended capability register layout.
///
/// Implementors describe the register layout of one extended capability. The layout must start at
/// the extended capability header, and [`Self::ID`] must identify that layout.
pub trait ExtCapability: FromBytes + IntoBytes {
    /// PCI extended capability ID for this register layout.
    const ID: ExtCapId;
}

impl<'a> ConfigSpace<'a, Extended> {
    /// Finds and projects an extended capability into its typed register layout.
    ///
    /// Returns [`None`] if the device does not implement the capability.
    ///
    /// Returns an error if the capability is present but its register span is too small or
    /// insufficiently aligned for `C`.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// use kernel::{
    ///     device::Bound,
    ///     pci,
    ///     prelude::*,
    /// };
    ///
    /// fn probe_sriov(pdev: &pci::Device<Bound>) -> Result {
    ///     let Some(sriov) = pdev
    ///         .config_space_extended()?
    ///         .find_ext_capability::<pci::ExtSriovRegs>()?
    ///     else {
    ///         return Ok(());
    ///     };
    ///
    ///     let first_vf_offset = sriov.first_vf_offset();
    ///     let mut vf_bars = sriov.vf_bars()?;
    ///     let bar0 = vf_bars.next().ok_or(EINVAL)?;
    ///     let bar1 = vf_bars.next().ok_or(EINVAL)?;
    ///     let bar2 = vf_bars.next().ok_or(EINVAL)?;
    ///
    ///     Ok(())
    /// }
    /// ```
    pub fn find_ext_capability<C: ExtCapability>(&self) -> Result<Option<ConfigSpace<'a, C>>> {
        let offset = usize::from(
            // SAFETY: `self.pdev` is valid by the type invariant of `ConfigSpace`.
            unsafe {
                bindings::pci_find_ext_capability(self.pdev.as_raw(), i32::from(C::ID.as_raw()))
            },
        );

        if offset == 0 {
            return Ok(None);
        }

        let size = self.calculate_ext_cap_size(offset)?;

        let base = ConfigSpaceBackend::as_ptr(*self)
            .cast::<u8>()
            .wrapping_add(offset);
        let ptr = Region::<0>::ptr_try_from_raw_parts_mut(base, size)?;

        // SAFETY: `offset` was returned by `pci_find_ext_capability`, and
        // `calculate_ext_cap_size` bounds `ptr` at the next capability or the end of the extended
        // configuration space. `ptr_try_from_raw_parts_mut` verified the region layout.
        let capability = unsafe { ConfigSpaceBackend::project_view(*self, ptr) };

        capability.try_cast::<C>().map(Some)
    }

    /// Calculates the size of the extended capability at `offset`.
    ///
    /// The capability extends to the next extended capability, or to the end of the extended
    /// configuration space if it is the last one. `offset` must be a DWORD-aligned offset within
    /// the extended configuration space returned by `pci_find_ext_capability`. Returns an error if
    /// the capability header is outside the extended configuration space.
    fn calculate_ext_cap_size(&self, offset: usize) -> Result<usize> {
        let header = self.try_read32(offset)?;
        // SAFETY: Pure bit manipulation, no preconditions.
        let next = casts::u32_as_usize(unsafe { bindings::pci_ext_cap_next(header) });

        Ok(if next > offset {
            next - offset
        } else {
            self.size() - offset
        })
    }
}

/// SR-IOV register layout per PCIe spec (64 bytes starting at cap offset).
///
/// The raw registers are private because PCI core owns SR-IOV state management. Drivers should use
/// PCI core APIs for operations such as enabling VFs and querying their topology.
#[repr(C)]
#[derive(FromBytes, IntoBytes)]
pub struct ExtSriovRegs {
    /// Extended capability header.
    _header: u32,
    /// SR-IOV capabilities.
    _cap: u32,
    /// SR-IOV control.
    _ctrl: u16,
    /// SR-IOV status.
    _status: u16,
    /// Initial VFs.
    _initial_vfs: u16,
    /// Total VFs.
    _total_vfs: u16,
    /// Number of VFs.
    _num_vfs: u16,
    /// Function dependency link.
    _func_dep_link: u8,
    _reserved_0: u8,
    /// First VF offset.
    vf_offset: u16,
    /// VF stride.
    _vf_stride: u16,
    _reserved_1: u16,
    /// VF device ID.
    _vf_device_id: u16,
    /// Supported page sizes.
    _supported_page_sizes: u32,
    /// System page size.
    _system_page_size: u32,
    /// VF BARs (BAR0–BAR5).
    vf_bar: [u32; NUM_VF_BARS],
    /// VF migration state array offset.
    _migration_state: u32,
}

impl ExtCapability for ExtSriovRegs {
    const ID: ExtCapId = ExtCapId::SRIOV;
}

/// A typed view of an SR-IOV extended capability.
pub type ExtSriovCapability<'a> = ConfigSpace<'a, ExtSriovRegs>;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum BarMemoryType {
    Bits32,
    Bits64,
}

impl TryFrom<Bounded<u32, 2>> for BarMemoryType {
    type Error = Error;

    fn try_from(value: Bounded<u32, 2>) -> Result<Self> {
        match value.get() {
            0b00 => Ok(Self::Bits32),
            0b10 => Ok(Self::Bits64),
            _ => Err(EINVAL),
        }
    }
}

impl From<BarMemoryType> for Bounded<u32, 2> {
    fn from(value: BarMemoryType) -> Self {
        match value {
            BarMemoryType::Bits32 => Self::new::<0b00>(),
            BarMemoryType::Bits64 => Self::new::<0b10>(),
        }
    }
}

// TODO: Move the common BAR encoding definitions into the PCI core abstraction once it provides
// BAR register decoding.
crate::bitfield! {
    /// Low DWORD of an SR-IOV VF BAR.
    struct BarLow(u32) {
        /// Base address bits 31:4.
        31:4 address;
        /// Whether the address range is prefetchable.
        3:3 prefetchable => bool;
        /// Memory BAR type.
        2:1 memory_type ?=> BarMemoryType;
        /// Whether this is an I/O-space BAR.
        0:0 io_space => bool;
    }
}

/// A decoded VF BAR register encoding.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ExtSriovVfBar {
    /// The PCI bus address encoded by the BAR, without PCI attribute bits.
    ///
    /// This is not necessarily the CPU resource address.
    pub address: u64,

    /// Whether the BAR is 64-bit.
    pub is_64bit: bool,
}

impl ExtSriovCapability<'_> {
    /// Returns the current PCIe First VF Offset in Routing ID space.
    ///
    /// This value may change when NumVFs changes. Drivers should use PCI core helpers when they
    /// need to calculate a VF BDF; this accessor is intended for interfaces that require the First
    /// VF Offset itself.
    #[inline]
    pub fn first_vf_offset(&self) -> u16 {
        crate::io_read!(*self, .vf_offset)
    }

    /// Returns an iterator over decoded VF BAR register encodings.
    ///
    /// All six raw VF BAR register slots are read and decoded up front. A 32-bit encoding yields
    /// one entry; a 64-bit encoding combines two slots into one entry.
    ///
    /// A zero-valued low DWORD is yielded as a 32-bit BAR at address zero; this method does not
    /// probe whether a BAR is implemented.
    ///
    /// Returns [`EINVAL`] and logs an error if a BAR low DWORD does not encode a 32-bit or 64-bit
    /// memory BAR, or if a 64-bit encoding has no upper DWORD.
    pub fn vf_bars(&self) -> Result<impl Iterator<Item = ExtSriovVfBar>> {
        let slots: [u32; NUM_VF_BARS] =
            core::array::from_fn(|slot| crate::io_read!(*self, .vf_bar[panic: slot]));
        let mut slots = slots.into_iter();
        let mut bars = [None; NUM_VF_BARS];
        let mut count = 0;

        let mut decode = || {
            while let Some(low) = slots.next().map(BarLow::from) {
                // SR-IOV VF BARs describe memory-space apertures; an I/O-space encoding is not
                // valid for these registers.
                if low.io_space() {
                    return Err(EINVAL);
                }

                let low_address = u64::from(low.address()) << BarLow::ADDRESS_SHIFT;
                let bar = match low.memory_type()? {
                    BarMemoryType::Bits64 => ExtSriovVfBar {
                        address: (u64::from(slots.next().ok_or(EINVAL)?) << 32) | low_address,
                        is_64bit: true,
                    },
                    BarMemoryType::Bits32 => ExtSriovVfBar {
                        address: low_address,
                        is_64bit: false,
                    },
                };

                bars[count] = Some(bar);
                count += 1;
            }

            Ok(())
        };

        decode().inspect_err(|_| {
            dev_err!(self.pdev, "invalid VF BAR encoding in SR-IOV capability\n");
        })?;
        Ok(bars.into_iter().flatten())
    }
}
