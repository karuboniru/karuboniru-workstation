#!/usr/bin/env bash
set -euo pipefail

# The workflows provide the authenticated registry, image name, tag and treefile.
remote="docker://${registry}/${image}"
target="${remote}:${tag}"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

is_chunked() {
    jq -e '
        .mediaType == "application/vnd.oci.image.manifest.v1+json" and
        (.layers | length > 0 and all(.[];
            .mediaType == "application/vnd.oci.image.layer.v1.tar+zstd" and
            (.annotations["io.github.containers.zstd-chunked.manifest-checksum"] // "" | test("^sha256:[0-9a-f]{64}$")) and
            (.annotations["io.github.containers.zstd-chunked.manifest-position"] // "" | length > 0)
        ))
    ' "$1" >/dev/null
}

# Follow compose image's change detection, versioning and layer packing, with
# an explicit skopeo copy so we can select the compression used on the wire.
tree_args=("$@")
packing_args=()
if skopeo inspect --raw --retry-times 4 "$target" >"$work/manifest" 2>"$work/inspect-error"; then
    previous="$remote@$(skopeo manifest-digest "$work/manifest")"
    skopeo inspect --config --retry-times 4 "$previous" >"$work/config"
    inputhash=$(jq -er '.config.Labels["rpmostree.inputhash"]' "$work/config")
    version=$(jq -r '.config.Labels | .["org.opencontainers.image.version"] // .version // empty' "$work/config")
    tree_args+=(--previous-inputhash="$inputhash")
    if [[ -n $version ]]; then
        tree_args+=(--previous-version="$version")
    fi
    packing_args+=(--previous-build-manifest="$work/manifest")
elif ! grep -Eiq '(manifest|name)[ _]unknown' "$work/inspect-error"; then
    cat "$work/inspect-error" >&2
    exit 1
fi

mkdir -p cache/v0
ostree init --repo=repo --mode=archive
ostree init --repo=cache/repo --mode=bare-user
rpm-ostree compose tree --unified-core --repo=cache/repo --layer-repo=repo --cachedir=cache/v0 \
    --write-commitid-to="$work/commit" --touch-if-changed="$work/changed" "${tree_args[@]}" "$composefile"

if [[ -e $work/changed ]]; then
    source="oci:$work/image"
    rpm-ostree compose container-encapsulate --repo=cache/repo --max-layers=256 \
        "${packing_args[@]}" "$(<"$work/commit")" "$source"
    skopeo inspect --raw "$source" >"$work/manifest"
elif is_chunked "$work/manifest"; then
    echo "Inputs unchanged and $target already uses zstd:chunked."
    exit 0
else
    # Migrate an unchanged legacy image without creating another OSTree commit.
    source=${previous:?Missing previous image}
fi

skopeo copy --retry-times 4 --format oci --dest-compress \
    --dest-compress-format=zstd:chunked --dest-force-compress-format \
    "$source" "dir:$work/chunked"

# Recompression replaces layer annotations. Keep rpm-ostree's layer metadata
# (including ostree.components for future builds) alongside the new TOC data.
manifest="$work/chunked/manifest.json"
jq --slurpfile src "$work/manifest" '
    if .config.digest != $src[0].config.digest or (.layers | length) != ($src[0].layers | length)
    then error("Image configuration or layer count changed during compression") else . end
    | .layers |= (to_entries | map(
        .key as $i | .value | .annotations = (
            ($src[0].layers[$i].annotations // {} | with_entries(
                select(.key | startswith("io.github.containers.zstd-chunked.") | not)
            )) + .annotations
        )
    ))
' "$manifest" >"$work/merged"
mv "$work/merged" "$manifest"
if ! is_chunked "$manifest"; then
    echo "Expected zstd:chunked metadata on every layer of $target" >&2
    exit 1
fi

skopeo copy --retry-times 4 --preserve-digests "dir:$work/chunked" "$target"
skopeo inspect --raw --retry-times 4 "$target" >"$work/published"
cmp "$manifest" "$work/published"
