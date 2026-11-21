"""Extract pinned official development packages locally. No installers or registry writes."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1] / ".deps" / "graphics"
PACKAGES = {
    "vulkan-sdk.exe": ("https://sdk.lunarg.com/sdk/download/1.4.363.1/windows/vulkansdk-windows-X64-1.4.363.1.exe",
        "c02932bd9a968b22d404536108adaacac908c081cd6b0113f5bfefc523dd0a6a"),
    "slang.zip": ("https://github.com/shader-slang/slang/releases/download/v2026.22.1/slang-2026.22.1-windows-x86_64.zip",
        "e72344f7b0323cec8b039c534caaf4680e7ba629489fc796ea4c7f7adb7deb68"),
    "sdl.zip": ("https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-devel-3.4.20-VC.zip",
        "1ec3009655ee37b1f73e52b3b120acc5afd8abaf9532f3303b1bf1dfe91b5969"),
    "vma.zip": ("https://codeload.github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/zip/refs/tags/v3.5.0",
        "9674db0a02fa45a55894c1d277d8474a4e0064459dc8b44afbbe83ce0a0dda46"),
}

# These archive slices and paths belong to the exact SHA-256-verified IFW SDK above.
# Extract SDK data only. Registering a global loader or layer would affect other apps.
SDK_ARCHIVES = (
    (34765651, 169636880, ["Bin/spirv-val.exe", "Bin/spirv-dis.exe",
        "Bin/VkLayer_khronos_validation.dll", "Bin/VkLayer_khronos_validation.json"]),
    (238792439, 1994543, ["Include/vulkan", "Include/vk_video"]),
    (240787022, 61453879, ["Lib/vulkan-1.lib"]),
)
REQUIRED = ("sdk/Include/vulkan/vulkan.h", "sdk/Lib/vulkan-1.lib", "sdk/Bin/spirv-val.exe",
    "sdk/Bin/spirv-dis.exe", "sdk/Bin/VkLayer_khronos_validation.dll", "sdk/Bin/VkLayer_khronos_validation.json",
    "slang/bin/slangc.exe", "slang/bin/slang-compiler.dll", "vma/VulkanMemoryAllocator-3.5.0/include/vk_mem_alloc.h",
    "sdl/SDL3-3.4.20/lib/x64/SDL3.dll", "sdl/SDL3-3.4.20/lib/x64/SDL3.lib")

def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

def obtain(name, url, digest):
    dest = ROOT / name
    if dest.exists() and sha256(dest) == digest:
        return dest
    with tempfile.NamedTemporaryFile(dir=ROOT, delete=False) as tmp:
        staging = Path(tmp.name)
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "PLATZ-development-bootstrap/1"})
        with urllib.request.urlopen(request, timeout=90) as response, staging.open("wb") as out:
            shutil.copyfileobj(response, out)
        if sha256(staging) != digest:
            raise ValueError(f"SHA-256 mismatch for {name}")
        os.replace(staging, dest)
    finally:
        staging.unlink(missing_ok=True)
    return dest

def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    marker = ROOT / "ready.txt"
    version = sha256(Path(__file__))
    if marker.is_file() and marker.read_text() == version and all((ROOT / item).is_file() for item in REQUIRED):
        return
    for name, (url, digest) in PACKAGES.items():
        archive = obtain(name, url, digest)
        if name == "vulkan-sdk.exe":
            sdk = ROOT / "sdk"
            sdk.mkdir(exist_ok=True)
            with archive.open("rb") as file, tempfile.TemporaryDirectory(dir=ROOT) as work:
                archive_slice = Path(work) / "content.7z"
                for offset, size, members in SDK_ARCHIVES:
                    file.seek(offset)
                    archive_slice.write_bytes(file.read(size))
                    subprocess.run(["tar.exe", "-xf", archive_slice, "-C", sdk, *members], check=True)
        else:
            with zipfile.ZipFile(archive) as bundle:
                bundle.extractall(ROOT / archive.stem)
    for item in REQUIRED:
        if not (ROOT / item).is_file():
            raise RuntimeError(f"Missing extracted component: {item}")
    marker.write_text(version)
    print("Vulkan SDK 1.4.363.1, Slang 2026.22.1, SDL 3.4.20 and VMA 3.5.0 are ready.")

if __name__ == "__main__":
    main()
