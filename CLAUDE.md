# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A personal custom Fedora Silverblue (rpm-ostree/bootc) base image configuration, forked from the official Fedora Atomic Desktops manifest repo. The active, maintained definition is **`karuboniru-packages.yaml`** — most other variant YAMLs (kinoite, sway, budgie, etc.) have been removed from this fork and exist only in git history.

Images are built via GitHub Actions (`.github/workflows/docker-publish.yml`) and pushed to `ghcr.io`. The `COMPOSEFILE` GitHub Actions variable controls which compose file is used (currently `karuboniru-packages.yaml`).

## How builds work

The CI uses `rpm-ostree compose image` inside a privileged Fedora rawhide container:

```sh
rpm-ostree compose image --initialize-mode=if-not-exists \
  --format registry --layer-repo repo --cachedir=cache --copy-retry-times=4 \
  --max-layers 256 \
  $composefile \
  $registry/$image:$tag
```

For local testing on a Fedora machine with podman and rpm-ostree installed:

```sh
podman unshare -- rpm-ostree compose tree --repo repo --cachedir=cache --unified-core karuboniru-packages.yaml
```

Otherwise, builds happen in CI or require a privileged environment with `rpm-ostree` and `skopeo`.

## Manifest structure

`karuboniru-packages.yaml` is the root treefile. It uses rpm-ostree's `include:` key to compose layers:

- `silverblue.yaml` → `silverblue-common.yaml` → `common.yaml` (base Fedora Silverblue stack)
- `common.yaml` includes modular yamls: `common-packages.yaml`, `bootupd.yaml`, `initramfs.yaml`, `sysroot-ro.yaml`, `kernel-install.yaml`, `composefs.yaml`, `bootc.yaml`, `dnf5.yaml`
- `silverblue-packages.yaml` is auto-generated from Fedora Comps (do not edit manually — see header comment)
- Additional feature yamls included in `karuboniru-packages.yaml`: `root-pythia6.yaml`, `cvmfs.yaml`, `evtgen.yaml`

Each `.repo` file in the root is a DNF repository definition used during compose. Active repos in `karuboniru-packages.yaml`: `fedora`, `fedora-updates`, `fedora-updates-testing`, `vscode`, `dummy`, `pythia6`.

## Key files to edit

- **`karuboniru-packages.yaml`**: The only file to edit for package additions/removals, repo changes, postprocess scripts, and file overlays (`add-files`). Currently targets `releasever: 44`.
- **`.repo` files**: Edit when adding/updating repository definitions (e.g., `cernvm.repo`, `pythia6.repo`, `vscode.repo`).
- **`etc/`**: Overlay config files that get embedded into the image via `add-files` in `karuboniru-packages.yaml`.

## Treefile conventions

- Commented-out packages (`# - package`) mean intentionally disabled but retained for reference.
- `add-files` entries are `[source, destination]` pairs copied into the image at compose time.
- `postprocess` scripts run as root inside the composed image rootfs — must be self-contained shell.
- `variables` block: `bootable_container: true` enables bootc/dnf5 path; `exclude_perl: false` keeps Perl (needed for TexLive tools in this image).

## Branching for new Fedora releases

When updating to a new Fedora release, update `releasever` in `common.yaml` and `karuboniru-packages.yaml`, and update the `ref:` line in `karuboniru-packages.yaml` (e.g., `ref: fedora/44/${basearch}/karuboniru-silverblue`).
