"""Fetch the public QOI corpus and build pinned, benchmark-only PNG dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import zipfile
import shutil
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    "zlib-1.3.2": (
        "https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz",
        "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"),
    "libpng-1.6.58": (
        "https://download.sourceforge.net/libpng/libpng-1.6.58.tar.gz",
        "8c9b05b675ca7301a458df2c2e46f26e1d41ff36b8863f8c33530bc58c2e6225"),
}


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def download(url, path, expected=None):
    if not path.exists():
        print(f"Downloading {url}", flush=True)
        temp = path.with_suffix(path.suffix + ".part")
        request = urllib.request.Request(url, headers={"User-Agent": "rg_image-benchmark/1"})
        with urllib.request.urlopen(request, timeout=120) as source, temp.open("wb") as target:
            total = 0
            reported = 0
            while chunk := source.read(1024 * 1024):
                target.write(chunk)
                total += len(chunk)
                if total - reported >= 128 * 1024 * 1024:
                    print(f"  {total // (1024 * 1024)} MiB", flush=True)
                    reported = total
        temp.replace(path)
    digest = sha256(path)
    if expected and digest != expected:
        raise RuntimeError(f"SHA256 mismatch for {path}: {digest}; expected {expected}")
    return digest


def extract(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as source:
        # Neither dependency builds nor the image corpus need archive symlinks.
        members = [m for m in source.getmembers() if m.isfile() or m.isdir()]
        source.extractall(destination, members=members, filter="data")


def run(*args):
    print(" ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def prepare_png(directory):
    prefix = directory / "prefix"
    metadata = {}
    for name, (url, expected) in SOURCES.items():
        archive = directory / (name + ".tar.gz")
        digest = download(url, archive, expected)
        source = directory / name
        if not source.exists():
            extract(archive, directory)
        binary = directory / (name + "-build")
        flags = [f"-DCMAKE_INSTALL_PREFIX={prefix}"]
        if os.name == "nt":
            flags += ["-A", "x64"]
        else:
            flags += ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_LIBDIR=lib"]
        if name.startswith("zlib"):
            flags += ["-DZLIB_BUILD_TESTING=OFF", "-DZLIB_BUILD_SHARED=OFF"]
        else:
            flags += [f"-DZLIB_ROOT={prefix}", "-DPNG_SHARED=OFF", "-DPNG_TESTS=OFF",
                      "-DPNG_TOOLS=OFF"]
            if os.name == "nt":
                flags += [f"-DZLIB_LIBRARY={prefix / 'lib' / 'zs.lib'}"]
        run("cmake", "-S", source, "-B", binary, *flags)
        run("cmake", "--build", binary, "--config", "Release", "--parallel", "4")
        run("cmake", "--install", binary, "--config", "Release")
        metadata[name] = {"url": url, "sha256": digest}
    (directory / "dependencies.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"PNG prefix: {prefix}", flush=True)


def prepare_corpus(directory):
    page = "https://qoiformat.org/benchmark/"
    with urllib.request.urlopen(page, timeout=60) as response:
        html = response.read().decode("utf-8")
    links = re.findall(r'href=[\"\x27]([^\"\x27]+)', html)
    link = next(x for x in links if "qoi_benchmark_suite.tar" in x)
    url = urllib.parse.urljoin(page, link)
    archive = directory / "qoi_benchmark_suite.tar"
    digest = download(url, archive)
    corpus = directory / "corpus"
    marker = directory / "corpus.json"
    old = json.loads(marker.read_text()) if marker.exists() else {}
    if old.get("archive_sha256") != digest and corpus.exists() and any(corpus.iterdir()):
        raise RuntimeError("Corpus archive differs from the extracted data (or extraction was interrupted); "
                           "use a fresh --directory to avoid mixing corpus versions")
    if old.get("archive_sha256") != digest or not corpus.exists():
        print("Extracting QOI corpus", flush=True)
        extract(archive, corpus)
    images = []
    for path in sorted(corpus.rglob("*.png")):
        if path.is_file() and not path.is_symlink():
            images.append({"path": path.relative_to(corpus).as_posix(), "bytes": path.stat().st_size,
                           "sha256": sha256(path)})
    marker.write_text(json.dumps({"source": page, "archive_url": url, "archive_sha256": digest,
                                 "image_count": len(images), "images": images}, indent=2) + "\n")
    print(f"Corpus: {corpus} ({len(images)} PNG files)", flush=True)


def prepare_sdl(directory):
    if os.name != "nt":
        raise RuntimeError("--sdl installs the Windows SDK; use a native SDL3 >= 3.4 SDK on other platforms")
    name = "SDL3-devel-3.4.14-VC.zip"
    url = "https://github.com/libsdl-org/SDL/releases/download/release-3.4.14/" + name
    archive = directory / name
    download(url, archive, "2fe279e70d426e9c644b625acb3083eb3cfb263a92f2c5718aff18d24a8b6e96")
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            target = (directory / entry.filename).resolve()
            if not target.is_relative_to(directory.resolve()):
                raise RuntimeError("SDL archive path escaped destination")
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.open(entry) as src, target.open("wb") as dst:
                    shutil.copyfileobj(src, dst)
    print(f"SDL3 prefix: {directory / 'SDL3-3.4.14'}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--png", action="store_true")
    parser.add_argument("--corpus", action="store_true")
    parser.add_argument("--sdl", action="store_true")
    parser.add_argument("--directory", type=Path, default=ROOT / "build" / "bench-deps")
    args = parser.parse_args()
    if not (args.png or args.corpus or args.sdl):
        parser.error("select --png, --corpus, and/or --sdl")
    directory = args.directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    if args.png:
        prepare_png(directory)
    if args.corpus:
        prepare_corpus(directory)
    if args.sdl:
        prepare_sdl(directory)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    main()
