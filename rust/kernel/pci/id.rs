// SPDX-License-Identifier: GPL-2.0

//! PCI device identifiers and related types.
//!
//! This module contains PCI class codes, Vendor IDs, and supporting types.

use crate::{bindings, error::code::EINVAL, error::Error, prelude::*};
use core::fmt;

/// PCI device class codes.
///
/// Each entry contains the full 24-bit PCI class code (base class in bits
/// 23-16, subclass in bits 15-8, programming interface in bits 7-0).
///
/// # Examples
///
/// ```
/// # use kernel::{device::Core, pci::{self, Class}, prelude::*};
/// fn probe_device(pdev: &pci::Device<Core>) -> Result {
///     let pci_class = pdev.pci_class();
///     dev_info!(
///         pdev.as_ref(),
///         "Detected PCI class: {}\n",
///         pci_class
///     );
///     Ok(())
/// }
/// ```
#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct Class(u32);

/// PCI class mask constants for matching [`Class`] codes.
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ClassMask {
    /// Match the full 24-bit class code.
    Full = 0xffffff,
    /// Match the upper 16 bits of the class code (base class and subclass only)
    ClassSubclass = 0xffff00,
}

macro_rules! define_all_pci_classes {
    (
        $($variant:ident = $binding:expr,)+
    ) => {
        impl Class {
            $(
                #[allow(missing_docs)]
                pub const $variant: Self = Self(Self::to_24bit_class($binding));
            )+
        }

        impl fmt::Display for Class {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                match self {
                    $(
                        &Self::$variant => write!(f, stringify!($variant)),
                    )+
                    _ => <Self as fmt::Debug>::fmt(self, f),
                }
            }
        }
    };
}

/// Once constructed, a [`Class`] contains a valid PCI class code.
impl Class {
    /// Create a [`Class`] from a raw 24-bit class code.
    #[inline]
    pub(super) fn from_raw(class_code: u32) -> Self {
        Self(class_code)
    }

    /// Get the raw 24-bit class code value.
    #[inline]
    pub const fn as_raw(self) -> u32 {
        self.0
    }

    // Converts a PCI class constant to 24-bit format.
    //
    // Many device drivers use only the upper 16 bits (base class and subclass),
    // but some use the full 24 bits. In order to support both cases, store the
    // class code as a 24-bit value, where 16-bit values are shifted up 8 bits.
    const fn to_24bit_class(val: u32) -> u32 {
        if val > 0xFFFF {
            val
        } else {
            val << 8
        }
    }
}

impl fmt::Debug for Class {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "0x{:06x}", self.0)
    }
}

impl ClassMask {
    /// Get the raw mask value.
    #[inline]
    pub const fn as_raw(self) -> u32 {
        self as u32
    }
}

impl TryFrom<u32> for ClassMask {
    type Error = Error;

    fn try_from(value: u32) -> Result<Self, Self::Error> {
        match value {
            0xffffff => Ok(ClassMask::Full),
            0xffff00 => Ok(ClassMask::ClassSubclass),
            _ => Err(EINVAL),
        }
    }
}

/// PCI vendor IDs.
///
/// Each entry contains the 16-bit PCI vendor ID as assigned by the PCI SIG.
#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct Vendor(u16);

macro_rules! define_all_pci_vendors {
    (
        $($variant:ident = $binding:expr,)+
    ) => {
        impl Vendor {
            $(
                #[allow(missing_docs)]
                pub const $variant: Self = Self($binding as u16);
            )+
        }

        impl fmt::Display for Vendor {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                match self {
                    $(
                        &Self::$variant => write!(f, stringify!($variant)),
                    )+
                    _ => <Self as fmt::Debug>::fmt(self, f),
                }
            }
        }
    };
}

/// Once constructed, a `Vendor` contains a valid PCI Vendor ID.
impl Vendor {
    /// Create a Vendor from a raw 16-bit vendor ID.
    #[inline]
    pub(super) fn from_raw(vendor_id: u16) -> Self {
        Self(vendor_id)
    }

    /// Get the raw 16-bit vendor ID value.
    #[inline]
    pub const fn as_raw(self) -> u16 {
        self.0
    }
}

impl fmt::Debug for Vendor {
    #[inline]
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "0x{:04x}", self.0)
    }
}

define_all_pci_classes! {
    NOT_DEFINED                = bindings::PCI_CLASS_NOT_DEFINED,                // 0x000000
    NOT_DEFINED_VGA            = bindings::PCI_CLASS_NOT_DEFINED_VGA,            // 0x000100

    STORAGE_SCSI               = bindings::PCI_CLASS_STORAGE_SCSI,               // 0x010000
    STORAGE_IDE                = bindings::PCI_CLASS_STORAGE_IDE,                // 0x010100
    STORAGE_FLOPPY             = bindings::PCI_CLASS_STORAGE_FLOPPY,             // 0x010200
    STORAGE_IPI                = bindings::PCI_CLASS_STORAGE_IPI,                // 0x010300
    STORAGE_RAID               = bindings::PCI_CLASS_STORAGE_RAID,               // 0x010400
    STORAGE_SATA               = bindings::PCI_CLASS_STORAGE_SATA,               // 0x010600
    STORAGE_SATA_AHCI          = bindings::PCI_CLASS_STORAGE_SATA_AHCI,          // 0x010601
    STORAGE_SAS                = bindings::PCI_CLASS_STORAGE_SAS,                // 0x010700
    STORAGE_EXPRESS            = bindings::PCI_CLASS_STORAGE_EXPRESS,            // 0x010802
    STORAGE_OTHER              = bindings::PCI_CLASS_STORAGE_OTHER,              // 0x018000

    NETWORK_ETHERNET           = bindings::PCI_CLASS_NETWORK_ETHERNET,           // 0x020000
    NETWORK_TOKEN_RING         = bindings::PCI_CLASS_NETWORK_TOKEN_RING,         // 0x020100
    NETWORK_FDDI               = bindings::PCI_CLASS_NETWORK_FDDI,               // 0x020200
    NETWORK_ATM                = bindings::PCI_CLASS_NETWORK_ATM,                // 0x020300
    NETWORK_OTHER              = bindings::PCI_CLASS_NETWORK_OTHER,              // 0x028000

    DISPLAY_VGA                = bindings::PCI_CLASS_DISPLAY_VGA,                // 0x030000
    DISPLAY_XGA                = bindings::PCI_CLASS_DISPLAY_XGA,                // 0x030100
    DISPLAY_3D                 = bindings::PCI_CLASS_DISPLAY_3D,                 // 0x030200
    DISPLAY_OTHER              = bindings::PCI_CLASS_DISPLAY_OTHER,              // 0x038000

    MULTIMEDIA_VIDEO           = bindings::PCI_CLASS_MULTIMEDIA_VIDEO,           // 0x040000
    MULTIMEDIA_AUDIO           = bindings::PCI_CLASS_MULTIMEDIA_AUDIO,           // 0x040100
    MULTIMEDIA_PHONE           = bindings::PCI_CLASS_MULTIMEDIA_PHONE,           // 0x040200
    MULTIMEDIA_HD_AUDIO        = bindings::PCI_CLASS_MULTIMEDIA_HD_AUDIO,        // 0x040300
    MULTIMEDIA_OTHER           = bindings::PCI_CLASS_MULTIMEDIA_OTHER,           // 0x048000

    MEMORY_RAM                 = bindings::PCI_CLASS_MEMORY_RAM,                 // 0x050000
    MEMORY_FLASH               = bindings::PCI_CLASS_MEMORY_FLASH,               // 0x050100
    MEMORY_CXL                 = bindings::PCI_CLASS_MEMORY_CXL,                 // 0x050200
    MEMORY_OTHER               = bindings::PCI_CLASS_MEMORY_OTHER,               // 0x058000

    BRIDGE_HOST                = bindings::PCI_CLASS_BRIDGE_HOST,                // 0x060000
    BRIDGE_ISA                 = bindings::PCI_CLASS_BRIDGE_ISA,                 // 0x060100
    BRIDGE_EISA                = bindings::PCI_CLASS_BRIDGE_EISA,                // 0x060200
    BRIDGE_MC                  = bindings::PCI_CLASS_BRIDGE_MC,                  // 0x060300
    BRIDGE_PCI_NORMAL          = bindings::PCI_CLASS_BRIDGE_PCI_NORMAL,          // 0x060400
    BRIDGE_PCI_SUBTRACTIVE     = bindings::PCI_CLASS_BRIDGE_PCI_SUBTRACTIVE,     // 0x060401
    BRIDGE_PCMCIA              = bindings::PCI_CLASS_BRIDGE_PCMCIA,              // 0x060500
    BRIDGE_NUBUS               = bindings::PCI_CLASS_BRIDGE_NUBUS,               // 0x060600
    BRIDGE_CARDBUS             = bindings::PCI_CLASS_BRIDGE_CARDBUS,             // 0x060700
    BRIDGE_RACEWAY             = bindings::PCI_CLASS_BRIDGE_RACEWAY,             // 0x060800
    BRIDGE_OTHER               = bindings::PCI_CLASS_BRIDGE_OTHER,               // 0x068000

    COMMUNICATION_SERIAL       = bindings::PCI_CLASS_COMMUNICATION_SERIAL,       // 0x070000
    COMMUNICATION_PARALLEL     = bindings::PCI_CLASS_COMMUNICATION_PARALLEL,     // 0x070100
    COMMUNICATION_MULTISERIAL  = bindings::PCI_CLASS_COMMUNICATION_MULTISERIAL,  // 0x070200
    COMMUNICATION_MODEM        = bindings::PCI_CLASS_COMMUNICATION_MODEM,        // 0x070300
    COMMUNICATION_OTHER        = bindings::PCI_CLASS_COMMUNICATION_OTHER,        // 0x078000

    SYSTEM_PIC                 = bindings::PCI_CLASS_SYSTEM_PIC,                 // 0x080000
    SYSTEM_PIC_IOAPIC          = bindings::PCI_CLASS_SYSTEM_PIC_IOAPIC,          // 0x080010
    SYSTEM_PIC_IOXAPIC         = bindings::PCI_CLASS_SYSTEM_PIC_IOXAPIC,         // 0x080020
    SYSTEM_DMA                 = bindings::PCI_CLASS_SYSTEM_DMA,                 // 0x080100
    SYSTEM_TIMER               = bindings::PCI_CLASS_SYSTEM_TIMER,               // 0x080200
    SYSTEM_RTC                 = bindings::PCI_CLASS_SYSTEM_RTC,                 // 0x080300
    SYSTEM_PCI_HOTPLUG         = bindings::PCI_CLASS_SYSTEM_PCI_HOTPLUG,         // 0x080400
    SYSTEM_SDHCI               = bindings::PCI_CLASS_SYSTEM_SDHCI,               // 0x080500
    SYSTEM_RCEC                = bindings::PCI_CLASS_SYSTEM_RCEC,                // 0x080700
    SYSTEM_OTHER               = bindings::PCI_CLASS_SYSTEM_OTHER,               // 0x088000

    INPUT_KEYBOARD             = bindings::PCI_CLASS_INPUT_KEYBOARD,             // 0x090000
    INPUT_PEN                  = bindings::PCI_CLASS_INPUT_PEN,                  // 0x090100
    INPUT_MOUSE                = bindings::PCI_CLASS_INPUT_MOUSE,                // 0x090200
    INPUT_SCANNER              = bindings::PCI_CLASS_INPUT_SCANNER,              // 0x090300
    INPUT_GAMEPORT             = bindings::PCI_CLASS_INPUT_GAMEPORT,             // 0x090400
    INPUT_OTHER                = bindings::PCI_CLASS_INPUT_OTHER,                // 0x098000

    DOCKING_GENERIC            = bindings::PCI_CLASS_DOCKING_GENERIC,            // 0x0a0000
    DOCKING_OTHER              = bindings::PCI_CLASS_DOCKING_OTHER,              // 0x0a8000

    PROCESSOR_386              = bindings::PCI_CLASS_PROCESSOR_386,              // 0x0b0000
    PROCESSOR_486              = bindings::PCI_CLASS_PROCESSOR_486,              // 0x0b0100
    PROCESSOR_PENTIUM          = bindings::PCI_CLASS_PROCESSOR_PENTIUM,          // 0x0b0200
    PROCESSOR_ALPHA            = bindings::PCI_CLASS_PROCESSOR_ALPHA,            // 0x0b1000
    PROCESSOR_POWERPC          = bindings::PCI_CLASS_PROCESSOR_POWERPC,          // 0x0b2000
    PROCESSOR_MIPS             = bindings::PCI_CLASS_PROCESSOR_MIPS,             // 0x0b3000
    PROCESSOR_CO               = bindings::PCI_CLASS_PROCESSOR_CO,               // 0x0b4000

    SERIAL_FIREWIRE            = bindings::PCI_CLASS_SERIAL_FIREWIRE,            // 0x0c0000
    SERIAL_FIREWIRE_OHCI       = bindings::PCI_CLASS_SERIAL_FIREWIRE_OHCI,       // 0x0c0010
    SERIAL_ACCESS              = bindings::PCI_CLASS_SERIAL_ACCESS,              // 0x0c0100
    SERIAL_SSA                 = bindings::PCI_CLASS_SERIAL_SSA,                 // 0x0c0200
    SERIAL_USB_UHCI            = bindings::PCI_CLASS_SERIAL_USB_UHCI,            // 0x0c0300
    SERIAL_USB_OHCI            = bindings::PCI_CLASS_SERIAL_USB_OHCI,            // 0x0c0310
    SERIAL_USB_EHCI            = bindings::PCI_CLASS_SERIAL_USB_EHCI,            // 0x0c0320
    SERIAL_USB_XHCI            = bindings::PCI_CLASS_SERIAL_USB_XHCI,            // 0x0c0330
    SERIAL_USB_CDNS            = bindings::PCI_CLASS_SERIAL_USB_CDNS,            // 0x0c0380
    SERIAL_USB_DEVICE          = bindings::PCI_CLASS_SERIAL_USB_DEVICE,          // 0x0c03fe
    SERIAL_FIBER               = bindings::PCI_CLASS_SERIAL_FIBER,               // 0x0c0400
    SERIAL_SMBUS               = bindings::PCI_CLASS_SERIAL_SMBUS,               // 0x0c0500
    SERIAL_IPMI_SMIC           = bindings::PCI_CLASS_SERIAL_IPMI_SMIC,           // 0x0c0700
    SERIAL_IPMI_KCS            = bindings::PCI_CLASS_SERIAL_IPMI_KCS,            // 0x0c0701
    SERIAL_IPMI_BT             = bindings::PCI_CLASS_SERIAL_IPMI_BT,             // 0x0c0702

    WIRELESS_RF_CONTROLLER     = bindings::PCI_CLASS_WIRELESS_RF_CONTROLLER,     // 0x0d1000
    WIRELESS_WHCI              = bindings::PCI_CLASS_WIRELESS_WHCI,              // 0x0d1010

    INTELLIGENT_I2O            = bindings::PCI_CLASS_INTELLIGENT_I2O,            // 0x0e0000

    SATELLITE_TV               = bindings::PCI_CLASS_SATELLITE_TV,               // 0x0f0000
    SATELLITE_AUDIO            = bindings::PCI_CLASS_SATELLITE_AUDIO,            // 0x0f0100
    SATELLITE_VOICE            = bindings::PCI_CLASS_SATELLITE_VOICE,            // 0x0f0300
    SATELLITE_DATA             = bindings::PCI_CLASS_SATELLITE_DATA,             // 0x0f0400

    CRYPT_NETWORK              = bindings::PCI_CLASS_CRYPT_NETWORK,              // 0x100000
    CRYPT_ENTERTAINMENT        = bindings::PCI_CLASS_CRYPT_ENTERTAINMENT,        // 0x100100
    CRYPT_OTHER                = bindings::PCI_CLASS_CRYPT_OTHER,                // 0x108000

    SP_DPIO                    = bindings::PCI_CLASS_SP_DPIO,                    // 0x110000
    SP_OTHER                   = bindings::PCI_CLASS_SP_OTHER,                   // 0x118000

    ACCELERATOR_PROCESSING     = bindings::PCI_CLASS_ACCELERATOR_PROCESSING,     // 0x120000

    OTHERS                     = bindings::PCI_CLASS_OTHERS,                     // 0xff0000
}

define_all_pci_vendors! {
    PCI_SIG                  = bindings::PCI_VENDOR_ID_PCI_SIG,                  // 0x0001
    LOONGSON                 = bindings::PCI_VENDOR_ID_LOONGSON,                 // 0x0014
    SOLIDIGM                 = bindings::PCI_VENDOR_ID_SOLIDIGM,                 // 0x025e
    TTTECH                   = bindings::PCI_VENDOR_ID_TTTECH,                   // 0x0357
    DYNALINK                 = bindings::PCI_VENDOR_ID_DYNALINK,                 // 0x0675
    UBIQUITI                 = bindings::PCI_VENDOR_ID_UBIQUITI,                 // 0x0777
    BERKOM                   = bindings::PCI_VENDOR_ID_BERKOM,                   // 0x0871
    ITTIM                    = bindings::PCI_VENDOR_ID_ITTIM,                    // 0x0b48
    COMPAQ                   = bindings::PCI_VENDOR_ID_COMPAQ,                   // 0x0e11
    LSI_LOGIC                = bindings::PCI_VENDOR_ID_LSI_LOGIC,                // 0x1000
    ATI                      = bindings::PCI_VENDOR_ID_ATI,                      // 0x1002
    VLSI                     = bindings::PCI_VENDOR_ID_VLSI,                     // 0x1004
    ADL                      = bindings::PCI_VENDOR_ID_ADL,                      // 0x1005
    NS                       = bindings::PCI_VENDOR_ID_NS,                       // 0x100b
    TSENG                    = bindings::PCI_VENDOR_ID_TSENG,                    // 0x100c
    WEITEK                   = bindings::PCI_VENDOR_ID_WEITEK,                   // 0x100e
    DEC                      = bindings::PCI_VENDOR_ID_DEC,                      // 0x1011
    CIRRUS                   = bindings::PCI_VENDOR_ID_CIRRUS,                   // 0x1013
    IBM                      = bindings::PCI_VENDOR_ID_IBM,                      // 0x1014
    UNISYS                   = bindings::PCI_VENDOR_ID_UNISYS,                   // 0x1018
    COMPEX2                  = bindings::PCI_VENDOR_ID_COMPEX2,                  // 0x101a
    WD                       = bindings::PCI_VENDOR_ID_WD,                       // 0x101c
    AMI                      = bindings::PCI_VENDOR_ID_AMI,                      // 0x101e
    AMD                      = bindings::PCI_VENDOR_ID_AMD,                      // 0x1022
    TRIDENT                  = bindings::PCI_VENDOR_ID_TRIDENT,                  // 0x1023
    AI                       = bindings::PCI_VENDOR_ID_AI,                       // 0x1025
    DELL                     = bindings::PCI_VENDOR_ID_DELL,                     // 0x1028
    MATROX                   = bindings::PCI_VENDOR_ID_MATROX,                   // 0x102B
    MOBILITY_ELECTRONICS     = bindings::PCI_VENDOR_ID_MOBILITY_ELECTRONICS,     // 0x14f2
    CT                       = bindings::PCI_VENDOR_ID_CT,                       // 0x102c
    MIRO                     = bindings::PCI_VENDOR_ID_MIRO,                     // 0x1031
    NEC                      = bindings::PCI_VENDOR_ID_NEC,                      // 0x1033
    FD                       = bindings::PCI_VENDOR_ID_FD,                       // 0x1036
    SI                       = bindings::PCI_VENDOR_ID_SI,                       // 0x1039
    HP                       = bindings::PCI_VENDOR_ID_HP,                       // 0x103c
    HP_3PAR                  = bindings::PCI_VENDOR_ID_HP_3PAR,                  // 0x1590
    PCTECH                   = bindings::PCI_VENDOR_ID_PCTECH,                   // 0x1042
    ASUSTEK                  = bindings::PCI_VENDOR_ID_ASUSTEK,                  // 0x1043
    DPT                      = bindings::PCI_VENDOR_ID_DPT,                      // 0x1044
    OPTI                     = bindings::PCI_VENDOR_ID_OPTI,                     // 0x1045
    ELSA                     = bindings::PCI_VENDOR_ID_ELSA,                     // 0x1048
    STMICRO                  = bindings::PCI_VENDOR_ID_STMICRO,                  // 0x104A
    BUSLOGIC                 = bindings::PCI_VENDOR_ID_BUSLOGIC,                 // 0x104B
    TI                       = bindings::PCI_VENDOR_ID_TI,                       // 0x104c
    SONY                     = bindings::PCI_VENDOR_ID_SONY,                     // 0x104d
    WINBOND2                 = bindings::PCI_VENDOR_ID_WINBOND2,                 // 0x1050
    ANIGMA                   = bindings::PCI_VENDOR_ID_ANIGMA,                   // 0x1051
    EFAR                     = bindings::PCI_VENDOR_ID_EFAR,                     // 0x1055
    MOTOROLA                 = bindings::PCI_VENDOR_ID_MOTOROLA,                 // 0x1057
    PROMISE                  = bindings::PCI_VENDOR_ID_PROMISE,                  // 0x105a
    FOXCONN                  = bindings::PCI_VENDOR_ID_FOXCONN,                  // 0x105b
    UMC                      = bindings::PCI_VENDOR_ID_UMC,                      // 0x1060
    PICOPOWER                = bindings::PCI_VENDOR_ID_PICOPOWER,                // 0x1066
    MYLEX                    = bindings::PCI_VENDOR_ID_MYLEX,                    // 0x1069
    APPLE                    = bindings::PCI_VENDOR_ID_APPLE,                    // 0x106b
    YAMAHA                   = bindings::PCI_VENDOR_ID_YAMAHA,                   // 0x1073
    QLOGIC                   = bindings::PCI_VENDOR_ID_QLOGIC,                   // 0x1077
    CYRIX                    = bindings::PCI_VENDOR_ID_CYRIX,                    // 0x1078
    CONTAQ                   = bindings::PCI_VENDOR_ID_CONTAQ,                   // 0x1080
    OLICOM                   = bindings::PCI_VENDOR_ID_OLICOM,                   // 0x108d
    SUN                      = bindings::PCI_VENDOR_ID_SUN,                      // 0x108e
    NI                       = bindings::PCI_VENDOR_ID_NI,                       // 0x1093
    CMD                      = bindings::PCI_VENDOR_ID_CMD,                      // 0x1095
    BROOKTREE                = bindings::PCI_VENDOR_ID_BROOKTREE,                // 0x109e
    SGI                      = bindings::PCI_VENDOR_ID_SGI,                      // 0x10a9
    WINBOND                  = bindings::PCI_VENDOR_ID_WINBOND,                  // 0x10ad
    PLX                      = bindings::PCI_VENDOR_ID_PLX,                      // 0x10b5
    MADGE                    = bindings::PCI_VENDOR_ID_MADGE,                    // 0x10b6
    THREECOM                 = bindings::PCI_VENDOR_ID_3COM,                     // 0x10b7
    AL                       = bindings::PCI_VENDOR_ID_AL,                       // 0x10b9
    NEOMAGIC                 = bindings::PCI_VENDOR_ID_NEOMAGIC,                 // 0x10c8
    TCONRAD                  = bindings::PCI_VENDOR_ID_TCONRAD,                  // 0x10da
    ROHM                     = bindings::PCI_VENDOR_ID_ROHM,                     // 0x10db
    NVIDIA                   = bindings::PCI_VENDOR_ID_NVIDIA,                   // 0x10de
    IMS                      = bindings::PCI_VENDOR_ID_IMS,                      // 0x10e0
    AMCC                     = bindings::PCI_VENDOR_ID_AMCC,                     // 0x10e8
    AMPERE                   = bindings::PCI_VENDOR_ID_AMPERE,                   // 0x1def
    INTERG                   = bindings::PCI_VENDOR_ID_INTERG,                   // 0x10ea
    REALTEK                  = bindings::PCI_VENDOR_ID_REALTEK,                  // 0x10ec
    XILINX                   = bindings::PCI_VENDOR_ID_XILINX,                   // 0x10ee
    INIT                     = bindings::PCI_VENDOR_ID_INIT,                     // 0x1101
    CREATIVE                 = bindings::PCI_VENDOR_ID_CREATIVE,                 // 0x1102
    TTI                      = bindings::PCI_VENDOR_ID_TTI,                      // 0x1103
    SIGMA                    = bindings::PCI_VENDOR_ID_SIGMA,                    // 0x1105
    VIA                      = bindings::PCI_VENDOR_ID_VIA,                      // 0x1106
    SIEMENS                  = bindings::PCI_VENDOR_ID_SIEMENS,                  // 0x110A
    VORTEX                   = bindings::PCI_VENDOR_ID_VORTEX,                   // 0x1119
    EF                       = bindings::PCI_VENDOR_ID_EF,                       // 0x111a
    IDT                      = bindings::PCI_VENDOR_ID_IDT,                      // 0x111d
    FORE                     = bindings::PCI_VENDOR_ID_FORE,                     // 0x1127
    PHILIPS                  = bindings::PCI_VENDOR_ID_PHILIPS,                  // 0x1131
    EICON                    = bindings::PCI_VENDOR_ID_EICON,                    // 0x1133
    CISCO                    = bindings::PCI_VENDOR_ID_CISCO,                    // 0x1137
    ZIATECH                  = bindings::PCI_VENDOR_ID_ZIATECH,                  // 0x1138
    SYSKONNECT               = bindings::PCI_VENDOR_ID_SYSKONNECT,               // 0x1148
    DIGI                     = bindings::PCI_VENDOR_ID_DIGI,                     // 0x114f
    XIRCOM                   = bindings::PCI_VENDOR_ID_XIRCOM,                   // 0x115d
    SERVERWORKS              = bindings::PCI_VENDOR_ID_SERVERWORKS,              // 0x1166
    ALTERA                   = bindings::PCI_VENDOR_ID_ALTERA,                   // 0x1172
    SBE                      = bindings::PCI_VENDOR_ID_SBE,                      // 0x1176
    TOSHIBA                  = bindings::PCI_VENDOR_ID_TOSHIBA,                  // 0x1179
    TOSHIBA_2                = bindings::PCI_VENDOR_ID_TOSHIBA_2,                // 0x102f
    ATTO                     = bindings::PCI_VENDOR_ID_ATTO,                     // 0x117c
    RICOH                    = bindings::PCI_VENDOR_ID_RICOH,                    // 0x1180
    DLINK                    = bindings::PCI_VENDOR_ID_DLINK,                    // 0x1186
    ARTOP                    = bindings::PCI_VENDOR_ID_ARTOP,                    // 0x1191
    ZEITNET                  = bindings::PCI_VENDOR_ID_ZEITNET,                  // 0x1193
    FUJITSU_ME               = bindings::PCI_VENDOR_ID_FUJITSU_ME,               // 0x119e
    MARVELL                  = bindings::PCI_VENDOR_ID_MARVELL,                  // 0x11ab
    MARVELL_EXT              = bindings::PCI_VENDOR_ID_MARVELL_EXT,              // 0x1b4b
    V3                       = bindings::PCI_VENDOR_ID_V3,                       // 0x11b0
    ATT                      = bindings::PCI_VENDOR_ID_ATT,                      // 0x11c1
    SPECIALIX                = bindings::PCI_VENDOR_ID_SPECIALIX,                // 0x11cb
    ANALOG_DEVICES           = bindings::PCI_VENDOR_ID_ANALOG_DEVICES,           // 0x11d4
    ZORAN                    = bindings::PCI_VENDOR_ID_ZORAN,                    // 0x11de
    COMPEX                   = bindings::PCI_VENDOR_ID_COMPEX,                   // 0x11f6
    MICROSEMI                = bindings::PCI_VENDOR_ID_MICROSEMI,                // 0x11f8
    RP                       = bindings::PCI_VENDOR_ID_RP,                       // 0x11fe
    CYCLADES                 = bindings::PCI_VENDOR_ID_CYCLADES,                 // 0x120e
    ESSENTIAL                = bindings::PCI_VENDOR_ID_ESSENTIAL,                // 0x120f
    O2                       = bindings::PCI_VENDOR_ID_O2,                       // 0x1217
    THREEDX                  = bindings::PCI_VENDOR_ID_3DFX,                     // 0x121a
    AVM                      = bindings::PCI_VENDOR_ID_AVM,                      // 0x1244
    STALLION                 = bindings::PCI_VENDOR_ID_STALLION,                 // 0x124d
    AT                       = bindings::PCI_VENDOR_ID_AT,                       // 0x1259
    ASIX                     = bindings::PCI_VENDOR_ID_ASIX,                     // 0x125b
    ESS                      = bindings::PCI_VENDOR_ID_ESS,                      // 0x125d
    SATSAGEM                 = bindings::PCI_VENDOR_ID_SATSAGEM,                 // 0x1267
    ENSONIQ                  = bindings::PCI_VENDOR_ID_ENSONIQ,                  // 0x1274
    TRANSMETA                = bindings::PCI_VENDOR_ID_TRANSMETA,                // 0x1279
    ROCKWELL                 = bindings::PCI_VENDOR_ID_ROCKWELL,                 // 0x127A
    ITE                      = bindings::PCI_VENDOR_ID_ITE,                      // 0x1283
    ALTEON                   = bindings::PCI_VENDOR_ID_ALTEON,                   // 0x12ae
    NVIDIA_SGS               = bindings::PCI_VENDOR_ID_NVIDIA_SGS,               // 0x12d2
    PERICOM                  = bindings::PCI_VENDOR_ID_PERICOM,                  // 0x12D8
    AUREAL                   = bindings::PCI_VENDOR_ID_AUREAL,                   // 0x12eb
    ELECTRONICDESIGNGMBH     = bindings::PCI_VENDOR_ID_ELECTRONICDESIGNGMBH,     // 0x12f8
    ESDGMBH                  = bindings::PCI_VENDOR_ID_ESDGMBH,                  // 0x12fe
    CB                       = bindings::PCI_VENDOR_ID_CB,                       // 0x1307
    SIIG                     = bindings::PCI_VENDOR_ID_SIIG,                     // 0x131f
    RADISYS                  = bindings::PCI_VENDOR_ID_RADISYS,                  // 0x1331
    MICRO_MEMORY             = bindings::PCI_VENDOR_ID_MICRO_MEMORY,             // 0x1332
    DOMEX                    = bindings::PCI_VENDOR_ID_DOMEX,                    // 0x134a
    INTASHIELD               = bindings::PCI_VENDOR_ID_INTASHIELD,               // 0x135a
    QUATECH                  = bindings::PCI_VENDOR_ID_QUATECH,                  // 0x135C
    SEALEVEL                 = bindings::PCI_VENDOR_ID_SEALEVEL,                 // 0x135e
    HYPERCOPE                = bindings::PCI_VENDOR_ID_HYPERCOPE,                // 0x1365
    DIGIGRAM                 = bindings::PCI_VENDOR_ID_DIGIGRAM,                 // 0x1369
    KAWASAKI                 = bindings::PCI_VENDOR_ID_KAWASAKI,                 // 0x136b
    CNET                     = bindings::PCI_VENDOR_ID_CNET,                     // 0x1371
    LMC                      = bindings::PCI_VENDOR_ID_LMC,                      // 0x1376
    NETGEAR                  = bindings::PCI_VENDOR_ID_NETGEAR,                  // 0x1385
    APPLICOM                 = bindings::PCI_VENDOR_ID_APPLICOM,                 // 0x1389
    MOXA                     = bindings::PCI_VENDOR_ID_MOXA,                     // 0x1393
    CCD                      = bindings::PCI_VENDOR_ID_CCD,                      // 0x1397
    EXAR                     = bindings::PCI_VENDOR_ID_EXAR,                     // 0x13a8
    MICROGATE                = bindings::PCI_VENDOR_ID_MICROGATE,                // 0x13c0
    THREEWARE                = bindings::PCI_VENDOR_ID_3WARE,                    // 0x13C1
    IOMEGA                   = bindings::PCI_VENDOR_ID_IOMEGA,                   // 0x13ca
    ABOCOM                   = bindings::PCI_VENDOR_ID_ABOCOM,                   // 0x13D1
    SUNDANCE                 = bindings::PCI_VENDOR_ID_SUNDANCE,                 // 0x13f0
    CMEDIA                   = bindings::PCI_VENDOR_ID_CMEDIA,                   // 0x13f6
    ADVANTECH                = bindings::PCI_VENDOR_ID_ADVANTECH,                // 0x13fe
    MEILHAUS                 = bindings::PCI_VENDOR_ID_MEILHAUS,                 // 0x1402
    LAVA                     = bindings::PCI_VENDOR_ID_LAVA,                     // 0x1407
    TIMEDIA                  = bindings::PCI_VENDOR_ID_TIMEDIA,                  // 0x1409
    ICE                      = bindings::PCI_VENDOR_ID_ICE,                      // 0x1412
    MICROSOFT                = bindings::PCI_VENDOR_ID_MICROSOFT,                // 0x1414
    OXSEMI                   = bindings::PCI_VENDOR_ID_OXSEMI,                   // 0x1415
    CHELSIO                  = bindings::PCI_VENDOR_ID_CHELSIO,                  // 0x1425
    EDIMAX                   = bindings::PCI_VENDOR_ID_EDIMAX,                   // 0x1432
    ADLINK                   = bindings::PCI_VENDOR_ID_ADLINK,                   // 0x144a
    SAMSUNG                  = bindings::PCI_VENDOR_ID_SAMSUNG,                  // 0x144d
    GIGABYTE                 = bindings::PCI_VENDOR_ID_GIGABYTE,                 // 0x1458
    AMBIT                    = bindings::PCI_VENDOR_ID_AMBIT,                    // 0x1468
    MYRICOM                  = bindings::PCI_VENDOR_ID_MYRICOM,                  // 0x14c1
    MEDIATEK                 = bindings::PCI_VENDOR_ID_MEDIATEK,                 // 0x14c3
    TITAN                    = bindings::PCI_VENDOR_ID_TITAN,                    // 0x14D2
    PANACOM                  = bindings::PCI_VENDOR_ID_PANACOM,                  // 0x14d4
    SIPACKETS                = bindings::PCI_VENDOR_ID_SIPACKETS,                // 0x14d9
    AFAVLAB                  = bindings::PCI_VENDOR_ID_AFAVLAB,                  // 0x14db
    AMPLICON                 = bindings::PCI_VENDOR_ID_AMPLICON,                 // 0x14dc
    BCM_GVC                  = bindings::PCI_VENDOR_ID_BCM_GVC,                  // 0x14a4
    BROADCOM                 = bindings::PCI_VENDOR_ID_BROADCOM,                 // 0x14e4
    TOPIC                    = bindings::PCI_VENDOR_ID_TOPIC,                    // 0x151f
    MAINPINE                 = bindings::PCI_VENDOR_ID_MAINPINE,                 // 0x1522
    ENE                      = bindings::PCI_VENDOR_ID_ENE,                      // 0x1524
    SYBA                     = bindings::PCI_VENDOR_ID_SYBA,                     // 0x1592
    MORETON                  = bindings::PCI_VENDOR_ID_MORETON,                  // 0x15aa
    VMWARE                   = bindings::PCI_VENDOR_ID_VMWARE,                   // 0x15ad
    ZOLTRIX                  = bindings::PCI_VENDOR_ID_ZOLTRIX,                  // 0x15b0
    MELLANOX                 = bindings::PCI_VENDOR_ID_MELLANOX,                 // 0x15b3
    DFI                      = bindings::PCI_VENDOR_ID_DFI,                      // 0x15bd
    QUICKNET                 = bindings::PCI_VENDOR_ID_QUICKNET,                 // 0x15e2
    ADDIDATA                 = bindings::PCI_VENDOR_ID_ADDIDATA,                 // 0x15B8
    PDC                      = bindings::PCI_VENDOR_ID_PDC,                      // 0x15e9
    FARSITE                  = bindings::PCI_VENDOR_ID_FARSITE,                  // 0x1619
    ARIMA                    = bindings::PCI_VENDOR_ID_ARIMA,                    // 0x161f
    BROCADE                  = bindings::PCI_VENDOR_ID_BROCADE,                  // 0x1657
    SIBYTE                   = bindings::PCI_VENDOR_ID_SIBYTE,                   // 0x166d
    ATHEROS                  = bindings::PCI_VENDOR_ID_ATHEROS,                  // 0x168c
    NETCELL                  = bindings::PCI_VENDOR_ID_NETCELL,                  // 0x169c
    CENATEK                  = bindings::PCI_VENDOR_ID_CENATEK,                  // 0x16CA
    SYNOPSYS                 = bindings::PCI_VENDOR_ID_SYNOPSYS,                 // 0x16c3
    USR                      = bindings::PCI_VENDOR_ID_USR,                      // 0x16ec
    VITESSE                  = bindings::PCI_VENDOR_ID_VITESSE,                  // 0x1725
    LINKSYS                  = bindings::PCI_VENDOR_ID_LINKSYS,                  // 0x1737
    ALTIMA                   = bindings::PCI_VENDOR_ID_ALTIMA,                   // 0x173b
    CAVIUM                   = bindings::PCI_VENDOR_ID_CAVIUM,                   // 0x177d
    TECHWELL                 = bindings::PCI_VENDOR_ID_TECHWELL,                 // 0x1797
    BELKIN                   = bindings::PCI_VENDOR_ID_BELKIN,                   // 0x1799
    RDC                      = bindings::PCI_VENDOR_ID_RDC,                      // 0x17f3
    GLI                      = bindings::PCI_VENDOR_ID_GLI,                      // 0x17a0
    LENOVO                   = bindings::PCI_VENDOR_ID_LENOVO,                   // 0x17aa
    QCOM                     = bindings::PCI_VENDOR_ID_QCOM,                     // 0x17cb
    CDNS                     = bindings::PCI_VENDOR_ID_CDNS,                     // 0x17cd
    ARECA                    = bindings::PCI_VENDOR_ID_ARECA,                    // 0x17d3
    S2IO                     = bindings::PCI_VENDOR_ID_S2IO,                     // 0x17d5
    SITECOM                  = bindings::PCI_VENDOR_ID_SITECOM,                  // 0x182d
    TOPSPIN                  = bindings::PCI_VENDOR_ID_TOPSPIN,                  // 0x1867
    COMMTECH                 = bindings::PCI_VENDOR_ID_COMMTECH,                 // 0x18f7
    SILAN                    = bindings::PCI_VENDOR_ID_SILAN,                    // 0x1904
    RENESAS                  = bindings::PCI_VENDOR_ID_RENESAS,                  // 0x1912
    SOLARFLARE               = bindings::PCI_VENDOR_ID_SOLARFLARE,               // 0x1924
    TDI                      = bindings::PCI_VENDOR_ID_TDI,                      // 0x192E
    NXP                      = bindings::PCI_VENDOR_ID_NXP,                      // 0x1957
    PASEMI                   = bindings::PCI_VENDOR_ID_PASEMI,                   // 0x1959
    ATTANSIC                 = bindings::PCI_VENDOR_ID_ATTANSIC,                 // 0x1969
    JMICRON                  = bindings::PCI_VENDOR_ID_JMICRON,                  // 0x197B
    KORENIX                  = bindings::PCI_VENDOR_ID_KORENIX,                  // 0x1982
    HUAWEI                   = bindings::PCI_VENDOR_ID_HUAWEI,                   // 0x19e5
    NETRONOME                = bindings::PCI_VENDOR_ID_NETRONOME,                // 0x19ee
    QMI                      = bindings::PCI_VENDOR_ID_QMI,                      // 0x1a32
    AZWAVE                   = bindings::PCI_VENDOR_ID_AZWAVE,                   // 0x1a3b
    REDHAT_QUMRANET          = bindings::PCI_VENDOR_ID_REDHAT_QUMRANET,          // 0x1af4
    ASMEDIA                  = bindings::PCI_VENDOR_ID_ASMEDIA,                  // 0x1b21
    REDHAT                   = bindings::PCI_VENDOR_ID_REDHAT,                   // 0x1b36
    WCHIC                    = bindings::PCI_VENDOR_ID_WCHIC,                    // 0x1c00
    SILICOM_DENMARK          = bindings::PCI_VENDOR_ID_SILICOM_DENMARK,          // 0x1c2c
    AMAZON_ANNAPURNA_LABS    = bindings::PCI_VENDOR_ID_AMAZON_ANNAPURNA_LABS,    // 0x1c36
    CIRCUITCO                = bindings::PCI_VENDOR_ID_CIRCUITCO,                // 0x1cc8
    AMAZON                   = bindings::PCI_VENDOR_ID_AMAZON,                   // 0x1d0f
    ZHAOXIN                  = bindings::PCI_VENDOR_ID_ZHAOXIN,                  // 0x1d17
    ROCKCHIP                 = bindings::PCI_VENDOR_ID_ROCKCHIP,                 // 0x1d87
    HYGON                    = bindings::PCI_VENDOR_ID_HYGON,                    // 0x1d94
    META                     = bindings::PCI_VENDOR_ID_META,                     // 0x1d9b
    FUNGIBLE                 = bindings::PCI_VENDOR_ID_FUNGIBLE,                 // 0x1dad
    HXT                      = bindings::PCI_VENDOR_ID_HXT,                      // 0x1dbf
    TEKRAM                   = bindings::PCI_VENDOR_ID_TEKRAM,                   // 0x1de1
    RPI                      = bindings::PCI_VENDOR_ID_RPI,                      // 0x1de4
    ALIBABA                  = bindings::PCI_VENDOR_ID_ALIBABA,                  // 0x1ded
    CXL                      = bindings::PCI_VENDOR_ID_CXL,                      // 0x1e98
    TEHUTI                   = bindings::PCI_VENDOR_ID_TEHUTI,                   // 0x1fc9
    SUNIX                    = bindings::PCI_VENDOR_ID_SUNIX,                    // 0x1fd4
    HINT                     = bindings::PCI_VENDOR_ID_HINT,                     // 0x3388
    THREEDLABS               = bindings::PCI_VENDOR_ID_3DLABS,                   // 0x3d3d
    NETXEN                   = bindings::PCI_VENDOR_ID_NETXEN,                   // 0x4040
    AKS                      = bindings::PCI_VENDOR_ID_AKS,                      // 0x416c
    WCHCN                    = bindings::PCI_VENDOR_ID_WCHCN,                    // 0x4348
    ACCESSIO                 = bindings::PCI_VENDOR_ID_ACCESSIO,                 // 0x494f
    S3                       = bindings::PCI_VENDOR_ID_S3,                       // 0x5333
    DUNORD                   = bindings::PCI_VENDOR_ID_DUNORD,                   // 0x5544
    DCI                      = bindings::PCI_VENDOR_ID_DCI,                      // 0x6666
    GLENFLY                  = bindings::PCI_VENDOR_ID_GLENFLY,                  // 0x6766
    INTEL                    = bindings::PCI_VENDOR_ID_INTEL,                    // 0x8086
    WANGXUN                  = bindings::PCI_VENDOR_ID_WANGXUN,                  // 0x8088
    SCALEMP                  = bindings::PCI_VENDOR_ID_SCALEMP,                  // 0x8686
    COMPUTONE                = bindings::PCI_VENDOR_ID_COMPUTONE,                // 0x8e0e
    KTI                      = bindings::PCI_VENDOR_ID_KTI,                      // 0x8e2e
    ADAPTEC                  = bindings::PCI_VENDOR_ID_ADAPTEC,                  // 0x9004
    ADAPTEC2                 = bindings::PCI_VENDOR_ID_ADAPTEC2,                 // 0x9005
    HOLTEK                   = bindings::PCI_VENDOR_ID_HOLTEK,                   // 0x9412
    NETMOS                   = bindings::PCI_VENDOR_ID_NETMOS,                   // 0x9710
    THREECOM_2               = bindings::PCI_VENDOR_ID_3COM_2,                   // 0xa727
    SOLIDRUN                 = bindings::PCI_VENDOR_ID_SOLIDRUN,                 // 0xd063
    DIGIUM                   = bindings::PCI_VENDOR_ID_DIGIUM,                   // 0xd161
    TIGERJET                 = bindings::PCI_VENDOR_ID_TIGERJET,                 // 0xe159
    XILINX_RME               = bindings::PCI_VENDOR_ID_XILINX_RME,               // 0xea60
    XEN                      = bindings::PCI_VENDOR_ID_XEN,                      // 0x5853
    OCZ                      = bindings::PCI_VENDOR_ID_OCZ,                      // 0x1b85
    NCUBE                    = bindings::PCI_VENDOR_ID_NCUBE,                    // 0x10ff
}
