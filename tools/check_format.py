"""Read-only check with clang-format 18."""
import argparse, pathlib, subprocess
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--formatter", default="clang-format")
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
version = subprocess.check_output([args.formatter, "--version"], text=True)
if "version 18." not in version:
    raise SystemExit("clang-format 18 is required")
files = sorted(p for folder in ["foundation", "kernel", "model", "top", "tests"]
               for p in (root / folder).rglob("*") if p.suffix in {".h", ".hpp", ".cpp"})
subprocess.run([args.formatter, "--dry-run", "--Werror", *map(str, files)], check=True)
print(f"Format check passed: {len(files)} files")
