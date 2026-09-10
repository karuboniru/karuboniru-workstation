# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A personal custom Fedora Kinoite (KDE Plasma, rpm-ostree/bootc) base image configuration, forked from the official Fedora Atomic Desktops manifest repo. The active, maintained definition is **`karuboniru-packages.yaml`**. This branch uses Kinoite with Firefox excluded and gaze-kde for desktop integration.

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

## Repository structure

```
karuboniru-packages.yaml   # root treefile — the only file to edit for personal packages
*.repo                     # DNF repo definitions (all at root, same level as root treefile)
etc/                       # config files embedded into image via add-files
manifests/
  base/                    # Fedora Atomic manifests (kinoite.yaml, common.yaml, etc.)
  features/                # optional feature yamls (cvmfs, evtgen, root-pythia6, howdy, nvidia, …)
```

## Manifest include chain

`karuboniru-packages.yaml` → `manifests/base/kinoite.yaml` → `manifests/base/kinoite-common.yaml` → `manifests/base/common.yaml`, `kinoite-shared.yaml`, and `kinoite-packages.yaml`.

`common.yaml` conditionally includes `bootc.yaml` + `dnf5.yaml` (when `bootable_container == true`) and pulls in the other small component yamls (`bootupd`, `initramfs`, `composefs`, etc.). All files within `manifests/base/` include each other by relative filename.

`manifests/base/kinoite-packages.yaml` is auto-generated from Fedora Comps — do not edit manually (see header comment).

Feature yamls (`manifests/features/*.yaml`) are self-contained: each bundles its packages, any required repos (referenced by name, resolved from root), and postprocess scripts.

## Key files to edit

- **`karuboniru-packages.yaml`**: Add/remove packages, toggle features via `include:`, change repos, edit postprocess or `add-files`. Currently targets `releasever: 45`.
- **`.repo` files** at root: Edit when adding/updating DNF repository definitions.
- **`etc/`**: Overlay config files embedded into the image at compose time.

## Treefile conventions

- Commented-out packages (`# - package`) mean intentionally disabled but retained for reference.
- `add-files` entries are `[source, destination]` pairs copied into the image at compose time.
- `postprocess` scripts run as root inside the composed image rootfs — must be self-contained shell.
- `variables` block: `bootable_container: true` enables bootc/dnf5 path; `exclude_perl: false` keeps Perl (needed for TexLive tools in this image).

## Branching for new Fedora releases

When updating to a new Fedora release, update `releasever` in `common.yaml` and `karuboniru-packages.yaml`, and update the `ref:` lines in `kinoite-common.yaml` and `karuboniru-packages.yaml` (e.g., `ref: fedora/45/${basearch}/karuboniru-kinoite`).
