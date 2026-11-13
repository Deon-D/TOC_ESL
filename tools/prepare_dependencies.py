"""Prepare hash-verified pinned dependencies, optionally without network access."""
import argparse, hashlib, io, pathlib, tarfile, urllib.request
PACKAGES = {
    "spdlog": ("1.15.3", "https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.15.3", "15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67"),
    "tomlplusplus": ("3.4.0", "https://codeload.github.com/marzer/tomlplusplus/tar.gz/refs/tags/v3.4.0", "8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155"),
}
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dest", required=True)
    parser.add_argument("--cache", required=True)
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    cache = pathlib.Path(args.cache)
    cache.mkdir(parents=True, exist_ok=True)
    for name, (version, url, digest) in PACKAGES.items():
        archive = cache / (name + ".tar.gz")
        if not archive.exists():
            if args.offline:
                raise SystemExit(f"Missing offline archive: {archive}")
            archive.write_bytes(urllib.request.urlopen(url, timeout=60).read())
        data = archive.read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit(f"Checksum mismatch: {archive}")
        dest = (pathlib.Path(args.dest) / name).resolve()
        dest.mkdir(parents=True, exist_ok=True)
        with tarfile.open(fileobj=io.BytesIO(data)) as source:
            for entry in source:
                target = dest.joinpath(*pathlib.PurePosixPath(entry.name).parts[1:]).resolve()
                if not target.is_relative_to(dest) or entry.issym() or entry.islnk():
                    raise SystemExit(f"Unsafe archive member: {entry.name}")
                if entry.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                elif entry.isfile():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(source.extractfile(entry).read())
        (dest / '.esl-dependency').write_text(f'{version} {digest}', encoding='utf-8')
        print(f"{name} {version}: verified at {dest}")
