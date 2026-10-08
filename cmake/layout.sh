
set -euo pipefail
export LC_ALL=C

ROOT=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
SKIPPED=(.git .idea .vscode .ruff_cache .test-venv .venv __pycache__ CMakeCache.txt CMakeFiles backups build deps dist releases)

files=()
problems=()
skip_paths=()
declare -A binary=()

skip_inside() {
    local path
    path=$(realpath -m -- "$1")
    if [[ $path == "$ROOT"/?* ]]; then
        skip_paths+=("./${path#"$ROOT"/}")
    fi
}

literal_path() {
    local path=${1//\\/\\\\}
    path=${path//\*/\\*}
    path=${path//\?/\\?}
    printf '%s' "${path//\[/\\[}"
}

project_files() {
    local name path prune=()
    for name in "${SKIPPED[@]}"; do
        prune+=(-o -name "$name")
    done
    while IFS= read -r -d '' path; do
        path=${path%/*}
        if [[ $path != . ]]; then
            skip_paths+=("$path")
        fi
    done < <(find . -mindepth 1 -type d -name CMakeFiles -print0 -prune -o \( "${prune[@]:1}" \) -prune \
        -o -type f -name .setup-stamp -print0)
    for path in "${skip_paths[@]}"; do
        prune+=(-o -path "$(literal_path "$path")")
    done
    find . -mindepth 1 \( "${prune[@]:1}" \) -prune -o -type f -printf '%P\0' |
        tr / '\001' | sort -z | tr '\001' /
}

matching() {
    local status=0
    grep -lZ "$@" || status=$?
    return $((status == 1 ? 0 : status))
}

find_binary() {
    local path found=()
    mapfile -d '' -t found < <(
        matching -aP '\x00' -- "${files[@]/#/./}" &&
            LC_ALL=C.UTF-8 matching -axv '.*' -- "${files[@]/#/./}"
    )
    wait "$!"
    for path in "${found[@]}"; do
        binary[${path#./}]=1
    done
}

check_names() {
    local path=$1 name=${1##*/}
    name=${name#"${name%%[!.]*}"}
    if [[ $name == ?*.h ]]; then
        problems+=("$path: headers must use .hpp")
    fi
    case $path in
        src/*/* | tests/*/* | tools/*/*) ;;
        src/* | tests/* | tools/*) problems+=("$path: move it into a subdirectory of ${path%%/*}/") ;;
    esac
}

framing_error() {
    local text=$1
    error=
    if [[ $text != *[!$'\r\n']* ]]; then
        error='has no content'
    elif [[ $text == *$'\r'* ]]; then
        error='uses CR line endings'
    elif [[ $text != $'\n'* || $text == $'\n\n'* ]]; then
        error='must start with exactly one empty line'
    elif [[ $text != *$'\n\n' || $text == *$'\n\n\n' ]]; then
        error='must end with exactly two empty lines'
    fi
}

framed() {
    local body=${1//$'\r\n'/$'\n'}
    body=${body#"${body%%[!$'\n']*}"}
    body=${body%"${body##*[!$'\n']}"}
    printf '\n%s\n\n' "$body"
}

check_framing() {
    local path=$1 fix=$2 text error
    IFS= read -r -d '' text <"$path" || true
    framing_error "$text"
    if [[ -n $error && $fix == 1 && $error != 'has no content' ]]; then
        framed "$text" >"$path"
        printf 'fixed %s\n' "$path"
    elif [[ -n $error ]]; then
        problems+=("$path: $error")
        if [[ $error != 'has no content' ]]; then
            fixable=1
        fi
    fi
}

main() {
    local fix=0 fixable=0 path problem
    while (($#)); do
        case $1 in
            --fix) fix=1 ;;
            --skip)
                if (($# < 2)); then
                    printf 'layout guard: --skip needs a folder\n' >&2
                    return 2
                fi
                skip_inside "$2"
                shift
                ;;
            *)
                printf 'layout guard: unknown argument %s\n' "$1" >&2
                return 2
                ;;
        esac
        shift
    done
    if [[ -n ${CIV_DEPS:-} ]]; then
        skip_inside "$CIV_DEPS"
    fi
    if [[ -n ${CIV_MSVC_SDK:-} ]]; then
        skip_inside "$CIV_MSVC_SDK"
    fi
    cd "$ROOT"
    mapfile -d '' -t files < <(project_files)
    wait "$!"
    find_binary
    for path in "${files[@]}"; do
        check_names "$path"
        if [[ -z ${binary[$path]:-} ]]; then
            check_framing "$path" "$fix"
        fi
    done
    for problem in "${problems[@]}"; do
        printf 'layout guard: %s\n' "$problem" >&2
    done
    if ((fixable)); then
        printf 'layout guard: %d problem(s); bash cmake/layout.sh --fix reframes the files\n' "${#problems[@]}" >&2
    elif ((${#problems[@]})); then
        printf 'layout guard: %d problem(s)\n' "${#problems[@]}" >&2
    fi
    if ((${#problems[@]})); then
        return 1
    fi
}

main "$@"

