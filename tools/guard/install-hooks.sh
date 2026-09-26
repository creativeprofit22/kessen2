#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copy the guard pre-commit hook into this clone's hooks directory.
# Does not change git config. Refuses to overwrite a different existing hook unless --force.
set -eu

top=$(git rev-parse --show-toplevel)
src="$top/tools/guard/pre-commit"
hooks=$(git rev-parse --git-path hooks)
dst="$hooks/pre-commit"

mkdir -p "$hooks"
if [ -e "$dst" ] && ! cmp -s "$src" "$dst"; then
    if [ "${1:-}" != "--force" ]; then
        echo "install-hooks: $dst exists and differs; re-run with --force to replace it" >&2
        exit 1
    fi
    cp "$dst" "$dst.bak"
    echo "install-hooks: previous hook saved to $dst.bak"
fi
cp "$src" "$dst"
chmod +x "$dst"
echo "install-hooks: installed $dst"
