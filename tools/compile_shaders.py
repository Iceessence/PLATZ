"""Publish a single validated, reflection-backed shader header atomically."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

OPTIONS = ["-target", "spirv", "-profile", "spirv_1_6", "-emit-spirv-directly",
           "-matrix-layout-row-major", "-O2"]

def compile_stage(source, compiler, validator, work, entry, stage):
    binary = work / (stage + ".spv")
    reflection = work / (stage + ".json")
    subprocess.run([compiler, source, "-entry", entry, "-stage", stage, *OPTIONS,
                    "-reflection-json", reflection, "-o", binary], check=True)
    subprocess.run([validator, "--target-env", "vulkan1.4", binary], check=True)
    return binary.read_bytes(), json.loads(reflection.read_bytes())

def layout(reflection):
    params = reflection["parameters"]
    if len(params) != 1 or params[0]["name"] != "Scene" or params[0]["type"]["kind"] != "constantBuffer":
        raise ValueError("Renderer expects one Scene constant buffer")
    scene = params[0]
    desc = scene["binding"]
    block = scene["type"]["elementType"]
    return (desc.get("space", 0), desc["index"], block["sizes"][0]["value"],
            {f["name"]: f["binding"]["offset"] for f in block["fields"]})

def words(name, data):
    values = [f"0x{value:08x}u" for (value,) in struct.iter_unpack("<I", data)]
    return ("inline constexpr std::uint32_t " + name + "[] = {\n" +
        "\n".join("    " + ", ".join(values[i:i + 8]) + "," for i in range(0, len(values), 8)) + "\n};")

def build(source, output, compiler, validator):
    output.parent.mkdir(exist_ok=True, parents=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as scratch:
        work = Path(scratch)
        vert, vr = compile_stage(source, compiler, validator, work, "vsMain", "vertex")
        frag, fr = compile_stage(source, compiler, validator, work, "psMain", "fragment")
        if layout(vr) != layout(fr):
            raise ValueError("Vertex and fragment parameter layouts disagree")
        group, binding, size, offsets = layout(vr)
        header = ["#pragma once", "#include <cstdint>", "namespace platz::engine::shaders {",
            f"inline constexpr unsigned sceneSet={group}, sceneBinding={binding}, sceneSize={size};",
            "namespace offset {"]
        for field, offset in offsets.items():
            header.append(f"inline constexpr unsigned {field}={offset};")
        header += ["}", words("vertex", vert), words("fragment", frag), "}", ""]
        next_header = work / "scene_spv.hpp"
        next_header.write_bytes("\n".join(header).encode("ascii"))
        os.replace(next_header, output)
        print(f"Validated scene SPIR-V SHA256 vertex {hashlib.sha256(vert).hexdigest()}, fragment {hashlib.sha256(frag).hexdigest()}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--validator", type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.output, args.compiler, args.validator)
