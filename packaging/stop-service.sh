#!/bin/sh
set -e

SERVICE_NAME=nmos-daemon.service

if [ "$(id -u)" -ne 0 ]; then
    echo "[ERROR] Please run this script as root." >&2
    exit 1
fi

echo "[STOP] Stopping ${SERVICE_NAME}..."
systemctl stop "${SERVICE_NAME}"
systemctl status "${SERVICE_NAME}" --no-pager --lines=5 || true

echo "[SUCCESS] Service stop requested: ${SERVICE_NAME}"
