def define_top_level_config(target):
    """Define common top-level variables in build.config"""
    rule_name = "{}_top_level_config".format(target)
    native.genrule(
        name = rule_name,
        srcs = [],
        outs = ["build.config.bazel.top.level.{}".format(target)],
        cmd_bash = """
          cat << 'EOF' > "$@"
# === define_top_level_config ===
BUILDING_WITH_BAZEL=true
# === end define_top_level_config ===
EOF
        """,
    )

    return ":{}".format(rule_name)

def gen_config_without_source_lines(build_config, target):
    """Replace "." or "source" lines in build.config files with shell null operator"""
    rule_name = "{}.{}".format(target, build_config)
    out_file_name = rule_name + ".generated"
    native.genrule(
        name = rule_name,
        srcs = [build_config],
        outs = [out_file_name],
        cmd_bash = """
          sed -e 's/^ *\\. /: # &/' \
              -e 's/^ *source /: # &/' \
              $(location {}) > "$@"
        """.format(build_config),
    )

    return ":" + rule_name
