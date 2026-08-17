use zerocopy_derive::*;

// The only valid value of this type are the bytes `0xC0C0`.
#[derive(TryFromBytes, KnownLayout, Immutable)]
#[repr(u16)]
pub enum C0C0 {
    _XC0C0 = 0xC0C0,
}

#[derive(FromBytes, KnownLayout, Immutable, SplitAt)]
#[repr(C, align(4))]
pub struct Packet<Magic> {
    magic_number: Magic,
    milk: u8,
    mug_size: u8,
    temperature: [u8; 5],
    marshmallows: [[u8; 3]],
}

/// A packet begining with the magic number `0xC0C0`.
pub type CocoPacket = Packet<C0C0>;

/// A packet beginning with any two initialized bytes.
pub type LocoPacket = Packet<[u8; 2]>;
