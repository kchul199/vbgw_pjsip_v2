#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PJPROJECT_VERSION="${PJPROJECT_VERSION:-2.16}"
PJPROJECT_URL="${PJPROJECT_URL:-https://github.com/pjsip/pjproject/archive/refs/tags/${PJPROJECT_VERSION}.tar.gz}"

PJPROJECT_WORK_DIR="${PJPROJECT_WORK_DIR:-${ROOT_DIR}/third_party/pjproject}"
PJPROJECT_CACHE_DIR="${PJPROJECT_CACHE_DIR:-${ROOT_DIR}/third_party/cache}"
PJPROJECT_TARBALL="${PJPROJECT_TARBALL:-${PJPROJECT_CACHE_DIR}/pjproject-${PJPROJECT_VERSION}.tar.gz}"
PJPROJECT_SRC_PARENT="${PJPROJECT_SRC_PARENT:-${PJPROJECT_WORK_DIR}/src}"
PJPROJECT_SRC_DIR="${PJPROJECT_SRC_PARENT}/pjproject-${PJPROJECT_VERSION}"
PJPROJECT_PREFIX="${PJPROJECT_PREFIX:-${PJPROJECT_WORK_DIR}/install}"

PJ_MAX_CALLS="${PJ_MAX_CALLS:-256}"
PJ_MAX_ACC="${PJ_MAX_ACC:-32}"
PJ_MAX_CONF_PORTS="${PJ_MAX_CONF_PORTS:-1024}"
PJ_MAX_TSX_COUNT="${PJ_MAX_TSX_COUNT:-4095}"
PJ_IOQUEUE_MAX_HANDLES="${PJ_IOQUEUE_MAX_HANDLES:-1024}"
PJ_MAKE_JOBS="${PJ_MAKE_JOBS:-1}"

OPENSSL_PREFIX="${OPENSSL_PREFIX:-}"
if [[ -z "${OPENSSL_PREFIX}" ]]; then
  if command -v brew >/dev/null 2>&1; then
    OPENSSL_PREFIX="$(brew --prefix openssl@3 2>/dev/null || true)"
  fi
fi
if [[ -z "${OPENSSL_PREFIX}" ]]; then
  if [[ -d /opt/homebrew/opt/openssl@3 ]]; then
    OPENSSL_PREFIX="/opt/homebrew/opt/openssl@3"
  elif [[ -d /usr/local/opt/openssl@3 ]]; then
    OPENSSL_PREFIX="/usr/local/opt/openssl@3"
  fi
fi

if [[ -z "${OPENSSL_PREFIX}" || ! -d "${OPENSSL_PREFIX}" ]]; then
  echo "OpenSSL 3 prefix not found. Set OPENSSL_PREFIX explicitly." >&2
  exit 1
fi

mkdir -p "${PJPROJECT_CACHE_DIR}" "${PJPROJECT_SRC_PARENT}" "${PJPROJECT_WORK_DIR}"

if [[ ! -f "${PJPROJECT_TARBALL}" ]]; then
  echo "[1/6] Downloading PJPROJECT ${PJPROJECT_VERSION} source tarball..."
  curl -fL "${PJPROJECT_URL}" -o "${PJPROJECT_TARBALL}"
else
  echo "[1/6] Reusing cached tarball: ${PJPROJECT_TARBALL}"
fi

echo "[2/6] Extracting source tree..."
rm -rf "${PJPROJECT_SRC_DIR}"
tar -xzf "${PJPROJECT_TARBALL}" -C "${PJPROJECT_SRC_PARENT}"

CONFIG_SITE_PATH="${PJPROJECT_SRC_DIR}/pjlib/include/pj/config_site.h"
echo "[3/6] Writing custom config_site.h to raise compile-time limits..."
cat > "${CONFIG_SITE_PATH}" <<EOF
#ifndef __PJ_CONFIG_SITE_H__
#define __PJ_CONFIG_SITE_H__

#define PJSUA_MAX_CALLS ${PJ_MAX_CALLS}
#define PJSUA_MAX_ACC ${PJ_MAX_ACC}
#define PJSUA_MAX_CONF_PORTS ${PJ_MAX_CONF_PORTS}
#define PJSIP_MAX_TSX_COUNT ${PJ_MAX_TSX_COUNT}
#define PJ_IOQUEUE_MAX_HANDLES ${PJ_IOQUEUE_MAX_HANDLES}

#endif
EOF

pushd "${PJPROJECT_SRC_DIR}" >/dev/null

echo "[4/6] Configuring PJPROJECT..."
make distclean >/dev/null 2>&1 || true
OPENSSL_CFLAGS="-I${OPENSSL_PREFIX}/include"
OPENSSL_LDFLAGS="-L${OPENSSL_PREFIX}/lib"
export CPPFLAGS="${OPENSSL_CFLAGS}${CPPFLAGS:+ ${CPPFLAGS}}"
export CFLAGS="${OPENSSL_CFLAGS}${CFLAGS:+ ${CFLAGS}}"
export CXXFLAGS="${OPENSSL_CFLAGS}${CXXFLAGS:+ ${CXXFLAGS}}"
export LDFLAGS="${OPENSSL_LDFLAGS}${LDFLAGS:+ ${LDFLAGS}}"
export PKG_CONFIG_PATH="${OPENSSL_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
./configure --prefix="${PJPROJECT_PREFIX}" --disable-shared --with-ssl="${OPENSSL_PREFIX}"

echo "[5/6] Building PJPROJECT (serial build for stability)..."
make dep
make -j"${PJ_MAKE_JOBS}"

echo "[6/6] Installing to ${PJPROJECT_PREFIX}..."
make install
popd >/dev/null

PC_FILE="${PJPROJECT_PREFIX}/lib/pkgconfig/libpjproject.pc"
if [[ ! -f "${PC_FILE}" ]]; then
  echo "Build finished but ${PC_FILE} was not generated." >&2
  exit 1
fi

echo
echo "PJPROJECT custom build complete."
echo "  Prefix: ${PJPROJECT_PREFIX}"
echo "  libpjproject.pc: ${PC_FILE}"
echo "  PJSUA_MAX_CALLS: ${PJ_MAX_CALLS}"
echo "  PJSUA_MAX_CONF_PORTS: ${PJ_MAX_CONF_PORTS}"
echo "  PJSIP_MAX_TSX_COUNT: ${PJ_MAX_TSX_COUNT}"
echo "  PJ_IOQUEUE_MAX_HANDLES: ${PJ_IOQUEUE_MAX_HANDLES}"
echo
echo "Next step:"
echo "  BUILD_DIR=build-local ./scripts/configure_with_local_pjproject.sh -DCMAKE_BUILD_TYPE=Release"
