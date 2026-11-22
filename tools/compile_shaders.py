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
    scene = next((p for p in reflection["parameters"] if p["name"] == "Scene"), None)
    if scene is None or scene["type"]["kind"] != "constantBuffer":
        raise ValueError("Renderer expects one Scene constant buffer")
    desc = scene["binding"]
    block = scene["type"]["elementType"]
    return (desc.get("space", 0), desc["index"], block["sizes"][0]["value"],
            {f["name"]: f["binding"]["offset"] for f in block["fields"]})

def texture_layout(reflection):
    resources = {p["name"]: p for p in reflection["parameters"]}
    if set(resources) != {"Scene", "colorTexture", "colorSampler"}:
        raise ValueError("Renderer expects one base-color texture and sampler")
    image = resources["colorTexture"]
    sampler = resources["colorSampler"]
    if image["type"].get("baseShape") != "texture2D" or sampler["type"]["kind"] != "samplerState":
        raise ValueError("Unexpected material image or sampler type")
    image_binding, sampler_binding = image["binding"], sampler["binding"]
    group = image_binding.get("space", 0)
    if group != sampler_binding.get("space", 0) or image_binding["index"] == sampler_binding["index"]:
        raise ValueError("Material image and sampler must share a set with distinct bindings")
    return (group, image_binding["index"], sampler_binding["index"])

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
        overlay_source = source.with_name("overlay.slang")
        overlay_vert, _ = compile_stage(overlay_source, compiler, validator, work, "overlayVS", "vertex")
        overlay_frag, _ = compile_stage(overlay_source, compiler, validator, work, "overlayPS", "fragment")
        if layout(vr) != layout(fr):
            raise ValueError("Vertex and fragment parameter layouts disagree")
        group, binding, size, offsets = layout(vr)
        texture_set, image_binding, sampler_binding = texture_layout(fr)
        header = ["#pragma once", "#include <cstdint>", "namespace platz::engine::shaders {",
            f"inline constexpr unsigned sceneSet={group}, sceneBinding={binding}, sceneSize={size};",
            f"inline constexpr unsigned textureSet={texture_set}, imageBinding={image_binding}, samplerBinding={sampler_binding};",
            "namespace offset {"]
        for field, offset in offsets.items():
            header.append(f"inline constexpr unsigned {field}={offset};")
        header += ["}", words("vertex", vert), words("fragment", frag), words("overlayVertex", overlay_vert),
                   words("overlayFragment", overlay_frag), "}", ""]
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
