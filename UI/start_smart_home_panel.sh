#!/bin/sh

APP_DIR="$(cd "$(dirname "$0")" && pwd)"

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-linuxfb}"
export QT_QPA_FONTDIR="${QT_QPA_FONTDIR:-/usr/share/fonts}"

cd "$APP_DIR" || exit 1
exec ./smart_home_panel
