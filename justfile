set shell := ["bash", "-euo", "pipefail", "-c"]

# List available recipes
default:
    @just --list --unsorted

# Evaluate the Nix flake
eval:
    nix flake check --no-build

# Configure the Meson build directory
setup:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ -d build ]; then
        meson setup build --prefix=/usr --reconfigure
    else
        meson setup build --prefix=/usr
    fi

# Build the plugin
build: setup
    meson compile -C build

# Run the Meson tests
test: build
    meson test -C build --print-errorlogs

# Check C++ formatting with the Wayfire configuration
lint:
    git ls-files -z -- '*.cpp' '*.hpp' | xargs -0 -r uncrustify -c "$WAYFIRE_UNCRUSTIFY_CONFIG" --check

# Run all static checks
check: eval test lint install-staged

# Install into a local staging directory
install-staged: build
    #!/usr/bin/env bash
    set -euo pipefail
    if [ -e staged ]; then
        rm -rf -- staged
    fi
    meson install -C build --destdir "$PWD/staged"
    for language in es_ES ro zh_CN; do
        test -f "$PWD/staged/usr/share/locale/$language/LC_MESSAGES/wf-plugin-vecdecor.mo"
    done
    ! find "$PWD/staged" -name 'wf-plugin-pixdecor.mo' -print -quit | grep -q .

# Remove generated build and staging directories
clean:
    rm -rf -- build staged
