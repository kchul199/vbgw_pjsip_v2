#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
PJPROJECT_ROOT="${PJPROJECT_ROOT:-${ROOT_DIR}/third_party/pjproject/install}"

PC_FILE="${PJPROJECT_ROOT}/lib/pkgconfig/libpjproject.pc"
if [[ ! -f "${PC_FILE}" ]]; then
  echo "Custom PJPROJECT prefix not found: ${PC_FILE}" >&2
  echo "Run ./scripts/build_local_pjproject.sh first, or set PJPROJECT_ROOT." >&2
  exit 1
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja -UPJPROJECT_* \
  -DPJPROJECT_ROOT="${PJPROJECT_ROOT}" "$@"

echo
echo "Configured build directory: ${BUILD_DIR}"
echo "Using custom PJPROJECT root: ${PJPROJECT_ROOT}"
echo "Build with: cmake --build ${BUILD_DIR}"
