use zerocopy::*;

#[path = "formats/coco_dynamic_padding.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_split_via_runtime_check_dynamic_padding(
    split: Split<&format::CocoPacket>,
) -> Option<(&format::CocoPacket, &[[u8; 3]])> {
    split.via_runtime_check().ok()
}
