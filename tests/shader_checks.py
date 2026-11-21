"""Offline compiler qualification, including material ABI and last-good preservation."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

def main():
    root, graphics = map(Path, sys.argv[1:])
    root, graphics = root.resolve(), graphics.resolve()
    compiler = graphics / "slang/bin/slangc.exe"
    validator = graphics / "sdk/Bin/spirv-val.exe"
    disassembler = graphics / "sdk/Bin/spirv-dis.exe"
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        source = work / "scene.slang"
        include = work / "color.slangh"
        shutil.copy(root / "src/engine/scene.slang", source)
        shutil.copy(root / "src/engine/color.slangh", include)
        output = work / "scene_spv.hpp"
        compile_cmd = [sys.executable, root / "tools/compile_shaders.py", source, output,
                       "--compiler", compiler, "--validator", validator]
        subprocess.run(compile_cmd, check=True)
        original = output.read_bytes()
        subprocess.run(compile_cmd, check=True)
        assert output.read_bytes() == original, "The same compiler inputs yielded different output"
        for expected in ("sceneSet=0, sceneBinding=0, sceneSize=240", "baseColor=192", "meshKind=224"):
            assert expected.encode() in original, "Reflected material layout changed: " + expected
        include.write_text(include.read_text().replace("12.92", "12.50"))
        subprocess.run(compile_cmd, check=True)
        updated = output.read_bytes()
        assert updated != original, "A shader include edit did not change compiled output"
        source.write_text(source.read_text() + "\ninvalid_qualification_token;\n")
        bad = subprocess.run(compile_cmd, capture_output=True, text=True)
        assert bad.returncode and "scene.slang" in bad.stderr and "error" in bad.stderr.lower(), bad.stderr
        assert output.read_bytes() == updated, "A failed compile overwrote the last good header"

        # Compiler qualification only. This kernel is not a runtime feature or asset.
        compute = work / "subgroup.slang"
        compute.write_text('''#include "color.slangh"
[[vk::binding(0,0)]] RWStructuredBuffer<float> outputValues;
[shader("compute")][numthreads(32,1,1)]
void measure(uint3 tid : SV_DispatchThreadID) {
    outputValues[tid.x] = WaveActiveSum(linearToSrgb(float3(tid)).x);
}
''')
        binary = work / "compute.spv"
        deps = work / "compute.d"
        subprocess.run([compiler, compute, "-target", "spirv", "-profile", "spirv_1_6", "-entry",
            "measure", "-stage", "compute", "-capability", "spvGroupNonUniformArithmetic",
            "-emit-spirv-directly", "-g2", "-depfile", deps, "-o", binary], check=True)
        subprocess.run([validator, "--target-env", "vulkan1.4", binary], check=True)
        assembly = subprocess.check_output([disassembler, binary], text=True)
        assert "OpGroupNonUniformFAdd" in assembly, "Subgroup reduction missing"
        assert "compute.d" not in deps.read_text() and "color.slangh" in deps.read_text(), "Missing include dependency"
        assert "Debug" in assembly or "OpLine" in assembly, "Source-level debug information missing"
        print("Passed deterministic VS/FS, reflected material ABI, include change, atomic failure, "
              "file diagnostic, debug compute and subgroup SPIR-V validation")

if __name__ == "__main__":
    main()
