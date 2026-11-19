#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026, UAB Kurokesu. All rights reserved.
#
# Uninstall Kurokesu camera driver (device tree overlay + kernel module via DKMS)

# Exit on errors
set -e

# Status line formatter (matches Makefile's PRINT)
print() { printf '  %-7s %s\n' "$1" "$2"; }

# Package identity and install paths
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DKMS_CONF="$SCRIPT_DIR/dkms.conf"
PACKAGE_NAME=$(grep '^PACKAGE_NAME=' "$DKMS_CONF" | cut -d'"' -f2)
SENSOR=$(grep '^BUILT_MODULE_NAME=' "$DKMS_CONF" | cut -d'"' -f2)
OVERLAY_DIR="/boot/overlay-user"
IQ_DIR="/etc/iqfiles"
MODULES_LOAD="/etc/modules-load.d/${SENSOR}.conf"
REMOVED=0

# Check required variables
if [ -z "$PACKAGE_NAME" ] || [ -z "$SENSOR" ]; then
	echo "Error: failed to parse $DKMS_CONF" >&2
	exit 1
fi

echo "Kurokesu Camera Driver Uninstaller"
echo "${PACKAGE_NAME}"
echo ""

# apt-owned trees are apt's to remove, refuse before touching anything
for SRC in /usr/src/*"$SENSOR"*; do
	[ -f "$SRC/dkms.conf" ] || continue
	if OWNER=$(dpkg -S "$SRC" 2>/dev/null); then
		echo "Error: ${OWNER%%:*} is installed from apt. Remove it with:" >&2
		echo "sudo apt remove ${OWNER%%:*}" >&2
		exit 1
	fi
done

# Remove DKMS registrations matching sensor name, same sweep as install.sh
if command -v dkms >/dev/null 2>&1; then
	ENTRIES=$(dkms status 2>/dev/null | sed 's/[,:].*//' | sort -u)
	for ENTRY in $ENTRIES; do
		case "${ENTRY%%/*}" in
		*"$SENSOR"*)
			REMOVED=1
			print DKMS "remove $ENTRY"
			if ! OUT=$(dkms remove "$ENTRY" --all 2>&1); then
				print WARN "could not fully remove $ENTRY" >&2
				printf '%s\n' "$OUT" >&2
			fi
			;;
		esac
	done
fi

# Source trees a failed install may have left behind
for SRC in /usr/src/*"$SENSOR"*; do
	[ -f "$SRC/dkms.conf" ] || continue
	REMOVED=1
	print CLEAN "$SRC"
	rm -rf "$SRC" || print WARN "could not remove $SRC" >&2
done

# Overlays built from this tree, color and mono per port
for DTS in "$SCRIPT_DIR"/*.dts; do
	NAME="$(basename "${DTS%.dts}")"
	for DTBO in "$OVERLAY_DIR/$NAME.dtbo" "$OVERLAY_DIR/${SENSOR}-mono-${NAME#"${SENSOR}"-}.dtbo"; do
		[ -f "$DTBO" ] || continue
		REMOVED=1
		print REMOVE "$DTBO"
		rm -f "$DTBO"
	done
done

# ISP tuning shipped by this tree
for IQ in "$SCRIPT_DIR"/tuning/*.json; do
	[ -f "$IQ_DIR/${IQ##*/}" ] || continue
	REMOVED=1
	print REMOVE "$IQ_DIR/${IQ##*/}"
	rm -f "$IQ_DIR/${IQ##*/}"
done

if [ -f "$MODULES_LOAD" ]; then
	REMOVED=1
	print REMOVE "$MODULES_LOAD"
	rm -f "$MODULES_LOAD"
fi

if [ "$REMOVED" -eq 0 ]; then
	echo "Nothing to remove, ${PACKAGE_NAME} not installed from source."
	exit 0
fi

echo ""
echo "Done. Remove user_overlays and initcall_blacklist from /boot/orangepiEnv.txt, then reboot."
