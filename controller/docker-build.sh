#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="${IMAGE_NAME:-pico-switch-controller-build}"

docker build -t "${IMAGE_NAME}" "${ROOT_DIR}"

docker run --rm \
  -v "${ROOT_DIR}:/workspace" \
  -w /workspace \
  "${IMAGE_NAME}" \
  cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w

docker run --rm \
  -v "${ROOT_DIR}:/workspace" \
  -w /workspace \
  "${IMAGE_NAME}" \
  cmake --build build --target pico_switch_controller
