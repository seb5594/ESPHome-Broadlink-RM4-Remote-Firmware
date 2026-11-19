# Releasing

A release is a tag `vX.Y.Z` on `main` plus a GitHub release whose text is the
section of that version in the [changelog](../CHANGELOG.md). The
[release workflow](../.github/workflows/release.yml) creates the release and checks
before that nothing is forgotten.

## What to do

1. Put the new version into `project_version` in
   [`common/base.yaml`](../esphome-broadlink-rm4-remote-firmware/common/base.yaml)
   (the device shows it in Home Assistant) and write a section `## X.Y.Z - <date>`
   at the top of the changelog.
2. Merge to `main` and wait until the **Validate** workflow is green on that commit.
3. Release, in one of two ways:
   - **In the browser:** *Actions → Release → Run workflow*, on `main`, with the
     version (`1.3.0`). The workflow creates the tag on the current head of `main`.
   - **With a tag:** `git tag v1.3.0 && git push origin v1.3.0`. The commit has to
     be on `main`.

## Which ESPHome versions the CI builds

The [Validate workflow](../.github/workflows/validate.yml) does not list ESPHome
versions. [`esphome_matrix.py`](../.github/tests/esphome_matrix.py) works them out
from PyPI at every run, and the workflow also runs every Monday, so a new ESPHome
release is built within a week without anybody touching the CI:

- **The oldest release a device supports**, exactly. It comes from the
  `min_version` of the files the device uses (`common/rtl8720c.yaml` for all,
  `features/rf.yaml` for the radio of the RM4 Pro), so raising a `min_version`
  moves the CI with it.
- **The newest patch release of each of the last four minor series** since then.
- **The newest pre-release**, as long as it is ahead of every stable release. A
  pre-release that does not build does not turn the run red, it is marked in the
  job name and shows the problem before the release is out.
- The config that only differs by the MAC address is built on the oldest and the
  newest release only.

The script uses nothing but the Python standard library and has tests that run
without network (`python -m unittest discover -s .github/tests -p 'test_*.py'`).
A device that is added to the CI goes into its `CONFIGS` table.

## What the workflow checks

It stops with a message, and publishes nothing, if

- the version is not of the form `1.3.0`, or a manual run is not on `main`,
- `project_version` in `common/base.yaml` is another version,
- the changelog has no section for the version, or it is empty,
- the tag or a release of that version exists already, or the tagged commit is not
  on `main`,
- there is no successful **Validate** run for the commit.

Then it publishes the release with the notes from the changelog and marks it as the
latest one. Users who pull the package with `ref: v1.3.0` get exactly this state.
