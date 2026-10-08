
set -euo pipefail

ROOT=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)

tool() {
    local variable=$1 name=$2 found
    found=${!variable:-$(command -v "$name" || true)}
    if [[ -z $found ]]; then
        printf 'format: %s was not found; add it to PATH or set %s\n' "$name" "$variable" >&2
        return 1
    fi
    printf '%s\n' "$found"
}

existing_folders() {
    local name
    folders=()
    for name; do
        if [[ -d $name ]]; then
            folders+=("$name")
        fi
    done
}

format_native() {
    local folders sources=() clang_format
    existing_folders src tests tools
    ((${#folders[@]})) || return 0
    mapfile -d '' -t sources < <(find "${folders[@]}" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 | sort -z)
    wait "$!"
    ((${#sources[@]})) || return 0
    clang_format=$(tool CLANG_FORMAT clang-format)
    "$clang_format" -i "${sources[@]}"
}

main() {
    cd "$ROOT"
    format_native
    bash cmake/layout.sh --fix || true
    bash cmake/layout.sh
}

main "$@"

