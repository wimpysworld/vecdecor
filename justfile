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
        meson setup build --prefix=/usr --buildtype=debugoptimized --reconfigure
    else
        meson setup build --prefix=/usr --buildtype=debugoptimized
    fi

# Build the plugin
build: setup
    meson compile -C build

# Run the Meson tests
test: build
    meson test -C build --print-errorlogs

# Check C++ formatting with the Wayfire configuration
lint:
    #!/usr/bin/env bash
    set -euo pipefail
    git ls-files -z --cached --others --exclude-standard --deduplicate -- '*.cpp' '*.hpp' |
        while IFS= read -r -d '' path; do
            if [[ -f "$path" ]]; then
                printf './%s\0' "$path"
            fi
        done |
        xargs -0 -r uncrustify -c "$WAYFIRE_UNCRUSTIFY_CONFIG" --check

# Run the full CI check suite
check: eval test lint check-locales install-staged

# Check translation syntax and metadata coverage
check-locales:
    #!/usr/bin/env bash
    set -euo pipefail
    expected=$(mktemp)
    trap 'rm -f -- "$expected"' EXIT
    awk '/<_[[:alnum:]_]+>.*<\/_[[:alnum:]_]+>/ {
        sub(/^.*<_[^>]+>/, "")
        sub(/<\/_[^>]+>.*$/, "")
        print
    }' metadata/vecdecor.xml | LC_ALL=C sort -u > "$expected"
    for catalogue in locale/*/LC_MESSAGES/wf-plugin-vecdecor.po; do
        msgfmt --check --output-file=/dev/null "$catalogue"
        if ! diff -u "$expected" <(
            msgattrib --no-obsolete --no-wrap "$catalogue" |
                sed -n 's/^msgid "\(.*\)"$/\1/p' |
                sed '/^$/d' |
                LC_ALL=C sort -u
        ); then
            echo "Translation catalogue does not match metadata: $catalogue" >&2
            exit 1
        fi
    done

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
