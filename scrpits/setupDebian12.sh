#!/usr/bin/env bash
# Prepare the pinned Debian toolchain for the ESL workspace.
set -euo pipefail

if [[ "$(id -u)" != 0 ]]; then
    echo 'Run through WSL with --user root.' >&2
    exit 1
fi
source /etc/os-release
if [[ "${ID:-}" != debian || "${VERSION_ID:-}" != 12 ]]; then
    echo 'This setup requires Debian 12 Bookworm.' >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
export LC_ALL=C.UTF-8
# Imported Docker rootfs images clear downloaded packages on every apt update.
# Keep the cache in this persistent WSL development environment so retries work.
if [[ -f /etc/apt/apt.conf.d/docker-clean ]]; then
    install -d -m 0755 /etc/apt/esl-backups
    if [[ -e /etc/apt/esl-backups/docker-clean ]]; then
        echo 'Inspect the existing docker-clean backup before retrying.' >&2
        exit 1
    fi
    mv -- /etc/apt/apt.conf.d/docker-clean /etc/apt/esl-backups/docker-clean
fi
apt-get -o Acquire::Retries=3 update
apt-get -o Acquire::Retries=3 install -y --no-install-recommends \
    build-essential gcc-12 g++-12 cmake ninja-build gdb git ca-certificates python3

case "$(g++-12 -dumpfullversion)" in
    12.2.*) ;;
    *) echo 'Expected G++ 12.2.x.' >&2; exit 1 ;;
esac

if ! id esl >/dev/null 2>&1; then
    useradd --create-home --shell /bin/bash esl
fi
# This dedicated imported distro uses an unprivileged default user.
# Package maintenance remains available via: wsl -d TOC-ESL-Debian12 -u root
python3 - <<'PY'
import configparser
from pathlib import Path
p = Path('/etc/wsl.conf')
config = configparser.ConfigParser()
if p.exists():
    config.read(p)
if not config.has_section('user'):
    config.add_section('user')
config.set('user', 'default', 'esl')
with p.open('w') as f:
    config.write(f)
PY

printf '\nInstalled tool versions:\n'
gcc-12 --version | sed -n '1p'
g++-12 --version | sed -n '1p'
cmake --version | sed -n '1p'
ctest --version | sed -n '1p'
ninja --version
gdb --version | sed -n '1p'
