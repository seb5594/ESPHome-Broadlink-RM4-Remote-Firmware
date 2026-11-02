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
