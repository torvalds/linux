def _uapi_unpacker_impl(ctx):
    input_tar = ctx.file.kernel_uapi_headers
    out_dir = ctx.actions.declare_directory(ctx.label.name + "_uapi_headers")

    ctx.actions.run_shell(
        outputs = [out_dir],
        inputs = [input_tar],
        arguments = [input_tar.path, out_dir.path],
        progress_message = "Unpacking UAPI headers",
        command = """
          tar_file="${PWD}/$1"
          out_dir="${PWD}/$2"
          mkdir -p "$out_dir"
          cd "$out_dir"
          tar --strip-components=2 -xzf "$tar_file"
        """,
    )

    return [DefaultInfo(files = depset([out_dir]))]

uapi_unpacker = rule(
    implementation = _uapi_unpacker_impl,
    doc = """Unpack `kernel-uapi-headers.tar.gz`""",
    attrs = {
        "kernel_uapi_headers": attr.label(
            allow_single_file = True,
            mandatory = True,
            doc = "the kernel_uapi_headers tarball or label",
        ),
    },
)
