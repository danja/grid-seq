#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_root}/build"
bundle_dir="${HOME}/.lv2/grid-seq.lv2"

if [[ ! -d "${build_dir}" ]]; then
  meson setup "${build_dir}"
fi

meson compile -C "${build_dir}"

mkdir -p "${bundle_dir}"
cp "${build_dir}/grid_seq.so" "${bundle_dir}/"
cp "${build_dir}/grid_seq_ui.so" "${bundle_dir}/"
cp "${project_root}/ttl/manifest.ttl" "${project_root}/ttl/grid_seq.ttl" "${bundle_dir}/"

echo "Installed to ${bundle_dir}"
