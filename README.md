# Modified base image for Fedora Kinoite

[![Build Ostree Container Image](https://github.com/karuboniru/karuboniru-workstation/actions/workflows/docker-publish.yml/badge.svg)](https://github.com/karuboniru/karuboniru-workstation/actions/workflows/docker-publish.yml)

Personal custom Fedora Kinoite (KDE Plasma, bootc) base image configuration without Firefox.

Images are built via GitHub Actions and pushed to `ghcr.io/karuboniru/karuboniru-workstation`.

## Building

For local testing on a Fedora machine with podman and rpm-ostree installed:

```sh
podman unshare -- rpm-ostree compose tree --repo repo --cachedir=cache --unified-core karuboniru-packages.yaml
```

Production builds happen in GitHub Actions using `rpm-ostree compose image`.

## Repository structure

- `karuboniru-packages.yaml` — root treefile defining this custom image
- `*.repo` — DNF repository definitions
- `manifests/base/` — Fedora Atomic base manifests, using the Kinoite desktop
- `manifests/features/` — optional feature yamls (cvmfs, evtgen, root-pythia6, etc.)
- `etc/` — config files embedded into the image via `add-files`

See [CLAUDE.md](CLAUDE.md) for detailed architecture documentation.

## Upstream

Forked from [fedora-atomic-desktops](https://gitlab.com/fedora/ostree/sig).
