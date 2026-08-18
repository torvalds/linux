#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_read_from_suffix_static_size(source: &[u8]) -> Option<format::LocoPacket> {
    match zerocopy::FromBytes::read_from_suffix(source) {
        Ok((_rest, packet)) => Some(packet),
        _ => None,
    }
}
