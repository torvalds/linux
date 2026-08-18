use zerocopy_derive::*;

// The only valid value of this type are the bytes `0xC0C0`.
#[derive(TryFromBytes, KnownLayout, Immutable, IntoBytes)]
#[repr(u16)]
pub enum C0C0 {
    _XC0C0 = 0xC0C0,
}

macro_rules! define_packet {
    ($name: ident, $trait: ident, $leading_field: ty) => {
        #[derive($trait, KnownLayout, Immutable, IntoBytes, SplitAt)]
        #[repr(C, align(2))]
        pub struct $name {
            magic_number: $leading_field,
            mug_size: u8,
            temperature: u8,
            marshmallows: [[u8; 2]],
        }
    };
}

/// Packet begins with bytes 0xC0C0.
define_packet!(CocoPacket, TryFromBytes, C0C0);

/// Packet begins with any two bytes.
define_packet!(LocoPacket, FromBytes, [u8; 2]);
