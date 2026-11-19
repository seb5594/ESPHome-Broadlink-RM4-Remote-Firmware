"""Works out which ESPHome releases the CI builds every device with.

The list is not written down anywhere: it is made from what is on PyPI and from
the `min_version` the project itself asks for, so a new ESPHome release is tested
by the next run (there is a weekly one, even when nobody pushes).

For every device config these are built:
  - the oldest release it supports, exactly (the `min_version` of the files it uses),
  - the newest patch release of each of the last few minor series (2026.10.2 for
    the 2026.10 series, and so on),
  - and the newest pre-release if it is ahead of every stable one. Pre-releases
    may fail without failing the run, they are an early warning.
A config that only differs in a detail (the MAC address) is built on the two ends
of that range only, to save build time.

Prints one line for the GitHub Actions output: matrix={"include": [...]}.
"""

import json
import re
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "esphome-broadlink-rm4-remote-firmware"

# The file with the chip settings sets the oldest ESPHome of every device, the
# radio feature asks for a newer one. Each config lists the files whose
# `min_version` counts for it, besides the chip file.
CHIP_FILE = "common/rtl8720c.yaml"
CONFIGS = {
    "ci-rm4-mini": {"files": [], "ends_only": False},
    "ci-rm4-mini-mac": {"files": [], "ends_only": True},
    "ci-rm4-pro-ir": {"files": [], "ends_only": False},
    "ci-rm4-pro": {"files": ["features/rf.yaml"], "ends_only": False},
}

# How many of the newest minor series are built besides the oldest release.
RECENT_MINORS = 4

PYPI_URL = "https://pypi.org/pypi/esphome/json"
VERSION = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:(a|b|rc)(\d+))?$")


def parse(text):
    """(year, minor, patch, stage, number) of a release, None for anything else.

    The stage sorts a pre-release before its final release: a, b, rc, then none.
    """
    found = VERSION.match(text)
    if found is None:
        return None
    year, minor, patch, stage, number = found.groups()
    rank = {"a": 0, "b": 1, "rc": 2, None: 3}[stage]
    return (int(year), int(minor), int(patch), rank, int(number or 0))


def is_final(version):
    return version[3] == 3


def text_of(version):
    year, minor, patch, rank, number = version
    stage = ["a", "b", "rc", ""][rank]
    return f"{year}.{minor}.{patch}" + (f"{stage}{number}" if stage else "")


def releases_from_pypi(url=PYPI_URL, attempts=3):
    """The releases of ESPHome that are not yanked, as parsed versions."""
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(url, timeout=30) as answer:
                data = json.load(answer)
            break
        except OSError:
            if attempt == attempts - 1:
                raise
            time.sleep(5 * (attempt + 1))
    versions = []
    for name, files in data["releases"].items():
        version = parse(name)
        if version is None or not files or all(f.get("yanked") for f in files):
            continue
        versions.append(version)
    return versions


def minimum_of(files):
    """The highest `min_version` among the given files, relative to the firmware folder."""
    highest = None
    for name in files:
        text = (FIRMWARE / name).read_text()
        found = re.search(r"^\s*min_version:\s*(\S+)", text, re.MULTILINE)
        if found is None:
            continue
        version = parse(found.group(1))
        if version is None:
            raise ValueError(f"{name}: cannot read min_version {found.group(1)!r}")
        if highest is None or version > highest:
            highest = version
    return highest


def versions_for(releases, minimum, ends_only):
    """The releases to build one config with, as (version, is_pre_release)."""
    stable = sorted(v for v in releases if is_final(v) and v >= minimum)
    newest_per_minor = {}
    for version in stable:
        newest_per_minor[version[:2]] = version  # sorted, so the last one wins
    recent = [newest_per_minor[m] for m in sorted(newest_per_minor)[-RECENT_MINORS:]]

    chosen = [(minimum, False)]
    if ends_only:
        recent = recent[-1:]
    chosen += [(v, False) for v in recent if v != minimum]

    # A pre-release is only interesting while it is ahead of everything stable.
    ahead = [v for v in releases if not is_final(v) and v >= minimum]
    if ahead and (not stable or max(ahead)[:2] > stable[-1][:2]):
        chosen.append((max(ahead), True))
    return chosen


def build_matrix(releases):
    chip = minimum_of([CHIP_FILE])
    include = []
    for config, options in CONFIGS.items():
        minimum = minimum_of([CHIP_FILE] + options["files"]) or chip
        for version, pre_release in versions_for(releases, minimum, options["ends_only"]):
            include.append(
                {"esphome": text_of(version), "config": config, "experimental": pre_release}
            )
    return {"include": include}


def main():
    matrix = build_matrix(releases_from_pypi())
    for entry in matrix["include"]:
        print(f"  {entry['esphome']:>12}  {entry['config']}" + ("  (pre-release)" if entry["experimental"] else ""), file=sys.stderr)
    print("matrix=" + json.dumps(matrix, separators=(",", ":")))


if __name__ == "__main__":
    main()
