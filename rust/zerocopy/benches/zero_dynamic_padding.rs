use zerocopy::*;

#[path = "formats/coco_dynamic_padding.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_zero_dynamic_padding(source: &mut format::LocoPacket) {
    source.zero()
}
