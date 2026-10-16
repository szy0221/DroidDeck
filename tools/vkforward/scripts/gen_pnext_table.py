#!/usr/bin/env python3
"""Generate the sType -> struct size table used to walk pNext chains.

Vulkan structs carry no size field, so a forwarded pNext chain cannot be walked
without knowing sizeof() for every sType. Instead of shipping a hand-written
lookup, we generate one straight from a vulkan_core.h so both halves of the
forwarder agree on sizes even if they are built against different toolchains.

Usage:
    python3 gen_pnext_table.py <path-to-vulkan_core.h> <output-dir>

The result is committed; regenerate only when the Vulkan headers move.
"""
import os
import re
import sys

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


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    header, outdir = sys.argv[1], sys.argv[2]
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

    os.makedirs(outdir, exist_ok=True)
    h = [
        "/* GENERATED FILE - do not edit. Regenerate with scripts/gen_pnext_table.py */",
        f"/* Source: Vulkan headers VK_HEADER_VERSION {version} */",
        "#ifndef VKFW_PNEXT_SIZES_H",
        "#define VKFW_PNEXT_SIZES_H",
        "",
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
        f"/* Source: Vulkan headers VK_HEADER_VERSION {version} */",
        "",
        '#define VK_NO_PROTOTYPES 1',
        '#include "vkfw_pnext_sizes.h"',
        "#include <vulkan/vulkan_core.h>",
        "",
        "#define VKFW_TYPE(name_) { VK_STRUCTURE_TYPE_##name_, sizeof(Vk##name_) }",
        "",
        "static struct { uint32_t sType; uint32_t size; } const kSizes[] = {",
    ]
    failures = []
    for value, name in rows:
        enum_name = name[2:]
        c.append(f"    {{ {value}, (uint32_t)sizeof({name}) }}, /* {enum_name} */")
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
    open(os.path.join(outdir, "vkfw_pnext_sizes.c"), "w", encoding="utf-8").write("\n".join(c))

    print(f"generated {len(rows)} entries from VK_HEADER_VERSION {version}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
