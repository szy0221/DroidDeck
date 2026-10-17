#!/usr/bin/env python3
"""Generate the sType -> struct size table used to walk pNext chains.

Vulkan structs carry no size field, so a forwarded pNext chain cannot be walked
without knowing sizeof() for every sType. Instead of shipping a hand-written
lookup, we generate one straight from a vulkan_core.h so both halves of the
forwarder agree on sizes even if they are built against different toolchains.

The sizes are baked in as numbers, not as sizeof() expressions. That is the
whole point: the table then compiles anywhere, including against Vulkan headers
that renamed or dropped the structs it knows about, which an NDK or distro
header bump does often. Sizes are measured by compiling a probe once, here,
where the Vulkan headers are the ones we chose.

Usage:
    python3 gen_pnext_table.py <path-to-vulkan_core.h> <output-dir> [cc]

Sizes are LP64 (aarch64 and x86_64 agree); the tool refuses a compiler that is
not, because a 32-bit answer would silently halve pointers.

The result is committed; regenerate only when the Vulkan headers move.
"""
import os
import re
import subprocess
import sys
import tempfile

STypeRe = re.compile(r"VK_STRUCTURE_TYPE_([A-Z0-9_]+)\s*=\s*(\d+)")
StructRe = re.compile(r"^\s*typedef struct (Vk[A-Za-z0-9_]+) \{", re.M)

# Suffix pieces that stay uppercase in the struct name (extension tags and
# numeric suffixes such as the "2" in VkPhysicalDeviceProperties2).
KEEP = {"KHR", "EXT", "NV", "NVX", "AMD", "ARM", "ANDROID", "MVK", "KHX", "QCOM",
        "INTEL", "IMG", "FUCHSIA", "SEC", "VALVE", "GOOGLE", "HUAWEI", "MTL",
        "NN", "AMDX", "QNX", "OHOS", "MSFT", "NSOBJC"}


def camel(enum_suffix: str) -> str:
    parts = enum_suffix.split("_")
    out = []
    for part in parts:
        if part.isdigit() or not part:
            out.append(part)
        elif part in KEEP:
            out.append(part)
        else:
            out.append(part[:1] + part[1:].lower())
    return "".join(out)


def measure(header: str, names: list) -> list:
    """sizeof() every struct, by compiling a probe against this header."""
    include_dir = os.path.dirname(os.path.dirname(os.path.abspath(header)))
    lines = ["#define VK_NO_PROTOTYPES 1", "#include <vulkan/vulkan_core.h>",
             "#include <stdio.h>", "int main(void) {"]
    # The pointer size comes first: it is how we know the probe was built for
    # the ABI we are measuring for, and not for a 32-bit one.
    lines += ['    printf("%zu\\n", (size_t)sizeof(void *));']
    lines += [f'    printf("%zu\\n", (size_t)sizeof({name}));' for name in names]
    lines += ["    return 0;", "}"]
    return ["\n".join(lines), include_dir]


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(__doc__, file=sys.stderr)
        return 2
    header, outdir = sys.argv[1], sys.argv[2]
    cc = sys.argv[3] if len(sys.argv) == 4 else os.environ.get("CC", "cc")

    text = open(header, "r", encoding="utf-8", errors="replace").read()

    header_version = re.search(r"#define VK_HEADER_VERSION (\d+)", text)
    version = header_version.group(1) if header_version else "?"

    declared = {name for name in StructRe.findall(text)}
    rows = []
    for suffix, value in STypeRe.findall(text):
        name = "Vk" + camel(suffix)
        if name not in declared:
            continue  # loader-internal or non-core sType: not forwardable
        rows.append((int(value), name))

    seen = set()
    rows.sort()
    rows = [r for r in rows if not (r[0] in seen or seen.add(r[0]))]

    # The feature structs among them: VkPhysicalDevice*Features*, i.e. nodes
    # whose only payload is VkBool32 flags. Name-based, because the shape is
    # what the name says and there is no field list here to inspect.
    def is_feature(name: str) -> bool:
        return name.startswith("VkPhysicalDevice") and "Features" in name

    features = sorted(value for value, name in rows if is_feature(name))

    source, include_dir = measure(header, [name for _, name in rows])
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "probe.c")
        exe = os.path.join(tmp, "probe")
        open(src, "w", encoding="utf-8").write(source)
        try:
            subprocess.run([cc, f"-I{include_dir}", "-o", exe, src], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        except (OSError, subprocess.CalledProcessError) as e:
            detail = e.stderr.decode("utf-8", "replace") if getattr(e, "stderr", None) else str(e)
            print(f"cannot compile the size probe with {cc}: {detail}", file=sys.stderr)
            print("pass a 64-bit C compiler as the third argument, or set CC", file=sys.stderr)
            return 1
        try:
            out = subprocess.run([exe], check=True, stdout=subprocess.PIPE).stdout
        except (OSError, subprocess.CalledProcessError) as e:
            print(f"cannot run the size probe: {e}", file=sys.stderr)
            return 1

    sizes = [int(x) for x in out.decode().split()]
    if len(sizes) != len(rows) + 1:
        print(f"size probe printed {len(sizes)} sizes for {len(rows)} structs", file=sys.stderr)
        return 1
    if sizes[0] != 8:
        print(f"the probe compiler {cc} has {sizes[0]}-byte pointers; Vulkan sizes "
              "are LP64 here, so pass a 64-bit compiler", file=sys.stderr)
        return 1
    sizes = sizes[1:]

    # A Vulkan struct is never smaller than its sType/pNext header; if it were,
    # the probe was built for a different ABI than the one we are measuring for.
    bad = [(name, size) for (_, name), size in zip(rows, sizes) if size < 16]
    if bad:
        print("implausible sizes, is the probe compiler 32-bit?", file=sys.stderr)
        for name, size in bad[:5]:
            print(f"  {name}: {size}", file=sys.stderr)
        return 1

    os.makedirs(outdir, exist_ok=True)
    h = [
        "/* GENERATED FILE - do not edit. Regenerate with scripts/gen_pnext_table.py */",
        f"/* Source: Vulkan headers VK_HEADER_VERSION {version}, LP64 sizes */",
        "#ifndef VKFW_PNEXT_SIZES_H",
        "#define VKFW_PNEXT_SIZES_H",
        "",
        "#include <stdbool.h>",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"#define VKFW_PNEXT_TABLE_HEADER_VERSION {version}",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "/* 0 means 'unknown sType'; the caller must drop such nodes. */",
        "size_t vkfw_pnext_size(uint32_t sType);",
        "",
        "/* True for the VkPhysicalDevice*Features* structs: nodes made only of",
        " * VkBool32 flags. Knowing which nodes those are is what lets a caller",
        " * ask the driver what it really has and clear the flags it has not,",
        " * instead of handing over a chain the driver refuses as a whole. */",
        "bool vkfw_pnext_is_features(uint32_t sType);",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif /* VKFW_PNEXT_SIZES_H */",
        "",
    ]
    open(os.path.join(outdir, "vkfw_pnext_sizes.h"), "w", encoding="utf-8").write("\n".join(h))

    c = [
        "/* GENERATED FILE - do not edit. Regenerate with scripts/gen_pnext_table.py */",
        f"/* Source: Vulkan headers VK_HEADER_VERSION {version}, LP64 sizes */",
        "",
        '#include "vkfw_pnext_sizes.h"',
        "",
        "/*",
        " * Sizes, not sizeof(): this file is compiled against whatever Vulkan",
        " * headers the toolchain happens to carry, and those move. A struct the",
        " * local headers renamed or dropped used to break the build of a table",
        " * that only ever needed the numbers.",
        " */",
        "static struct { uint32_t sType; uint32_t size; } const kSizes[] = {",
    ]
    for (value, name), size in zip(rows, sizes):
        c.append(f"    {{ {value}, {size} }}, /* {name[2:]} */")
    c += [
        "};",
        "",
        "size_t vkfw_pnext_size(uint32_t sType)",
        "{",
        "    size_t lo = 0, hi = sizeof(kSizes) / sizeof(kSizes[0]);",
        "    while (lo < hi) {",
        "        size_t mid = lo + (hi - lo) / 2;",
        "        if (kSizes[mid].sType < sType) lo = mid + 1;",
        "        else hi = mid;",
        "    }",
        "    if (lo < sizeof(kSizes) / sizeof(kSizes[0]) && kSizes[lo].sType == sType)",
        "        return (size_t)kSizes[lo].size;",
        "    return 0;",
        "}",
        "",
    ]

    if not features:
        print("no feature structs found in this header; is it a vulkan_core.h?", file=sys.stderr)
        return 1

    c += [
        "/* sTypes of the VkPhysicalDevice*Features* structs, ascending. */",
        "static const uint32_t kFeatureTypes[] = {",
    ]
    c += [f"    {value}," for value in features]
    c += [
        "};",
        "",
        "bool vkfw_pnext_is_features(uint32_t sType)",
        "{",
        "    size_t lo = 0, hi = sizeof(kFeatureTypes) / sizeof(kFeatureTypes[0]);",
        "    while (lo < hi) {",
        "        size_t mid = lo + (hi - lo) / 2;",
        "        if (kFeatureTypes[mid] < sType) lo = mid + 1;",
        "        else hi = mid;",
        "    }",
        "    return lo < sizeof(kFeatureTypes) / sizeof(kFeatureTypes[0]) &&",
        "           kFeatureTypes[lo] == sType;",
        "}",
        "",
    ]
    open(os.path.join(outdir, "vkfw_pnext_sizes.c"), "w", encoding="utf-8").write("\n".join(c))

    print(f"generated {len(rows)} entries from VK_HEADER_VERSION {version} using {cc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
