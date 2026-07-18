#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_read_from_bytes_static_size(source: &[u8]) -> Option<format::LocoPacket> {
    zerocopy::FromBytes::read_from_bytes(source).ok()
}
