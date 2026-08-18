#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_read_from_prefix_static_size(source: &[u8]) -> Option<format::LocoPacket> {
    match zerocopy::FromBytes::read_from_prefix(source) {
        Ok((packet, _rest)) => Some(packet),
        _ => None,
    }
}
