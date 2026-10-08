
set -euo pipefail
LC_ALL=C
shopt -u patsub_replacement 2>/dev/null || true

SCRIPT=$(realpath -- "${BASH_SOURCE[0]}")
ROOT=${SCRIPT%/*/*}
DEPS=$(realpath -ms -- "${CIV_DEPS:-$ROOT/deps}")
MODAPI_REPO=https://github.com/Spore-Community/Spore-ModAPI.git
MODAPI_TAG=v2.5.559
MODAPI_COMMIT=261a09e0400537058dd132c2c3c7ea7967dcf3df
DLLS_URL=https://github.com/Spore-Community/Spore-ModAPI/releases/download/v2.5.559/SporeModAPIdlls.zip
DLLS_SHA256=990347c254dfce439b98083ccd779411696af615ad6acd6b98fde9c9e1d138c1
LIB_SHA256=7bd8d73f082949c029c6c2a96b7431a5f82a0862dece5070eeb72dd57e5c7425
DETOURS_REPO=https://github.com/microsoft/Detours.git
DETOURS_TAG=v4.0.1
DETOURS_COMMIT=e4bfd6b03e50de46b47abfbd1e46b384f0c5f833
XWIN_VERSION=0.10.0
declare -A XWIN_SHA256=(
    [x86_64]=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88
    [aarch64]=6d56d28537a86f37aa3d041318898f25ee3100c6b6ec332ad873c28faf37be23
)
MSVC_CRT_VERSION=14.44.17.14
WINDOWS_SDK_VERSION=10.0.26100

INPUT_METHODS=$'inline void GameInput::OnKeyDown(int vkCode, KeyModifiers modifiers) {
\t((void(__thiscall*)(GameInput*, int, KeyModifiers))(GetAddress(GameInput, OnKeyDown)))(this, vkCode, modifiers);
}
inline void GameInput::OnKeyUp(int vkCode, KeyModifiers modifiers) {
\t((void(__thiscall*)(GameInput*, int, KeyModifiers))(GetAddress(GameInput, OnKeyUp)))(this, vkCode, modifiers);
}

inline bool GameInput::IsMouseButtonDown(MouseButton button) {
\treturn ((bool(*)(MouseButton))(GetAddress(GameInput, IsMouseButtonDown)))(button);
}'

LICENSE_MESSAGE='the MSVC CRT and Windows SDK are under the Microsoft license and are not redistributed.
Re-run with --accept-msvc-license to download them with xwin (https://github.com/Jake-Shadle/xwin),
or pass --msvc-sdk PATH to reuse an existing xwin "splat" tree.'

ACCEPT_LICENSE=
MSVC_SDK=
XWIN=
XWIN_CACHE=
DOWNLOAD_DIR=

die() {
    printf 'setup: %s\n' "$*" >&2
    exit 1
}

run() {
    printf '+ %s\n' "$*"
    "$@"
}

usage() {
    cat <<TEXT
Fetches and prepares everything the Linux cross-build needs into deps/ (or \$CIV_DEPS):
Spore ModAPI SDK v2.5.559 with its headers patched for clang-cl, Microsoft Detours v4.0.1, SporeModAPI.lib
and the MSVC CRT 14.44 with the Windows SDK 10.0.26100 for x86 (Microsoft license) from xwin 0.10.0.

Usage: bash cmake/setup.sh [options]
  --accept-msvc-license  download the MSVC CRT and Windows SDK with xwin
  --xwin PATH            the xwin executable (default: xwin $XWIN_VERSION from PATH, else downloaded into deps/.xwin)
  --xwin-cache PATH      keep the xwin download cache there (default: deps/.xwin-cache, removed after success)
  --msvc-sdk PATH        reuse an existing xwin splat tree instead of downloading
  -h, --help             show this text
TEXT
}

need_value() {
    (( $# > 1 )) || die "argument $1: expected one argument"
}

parse_args() {
    while (( $# )); do
        if [[ $1 == --*=* ]]; then
            set -- "${1%%=*}" "${1#*=}" "${@:2}"
        fi
        case $1 in
            --accept-msvc-license) ACCEPT_LICENSE=1; shift ;;
            --msvc-sdk) need_value "$@"; MSVC_SDK=$2; shift 2 ;;
            --xwin) need_value "$@"; XWIN=$2; shift 2 ;;
            --xwin-cache) need_value "$@"; XWIN_CACHE=$2; shift 2 ;;
            -h | --help) usage; exit 0 ;;
            *) die "unrecognized argument: $1 (see --help)" ;;
        esac
    done
}

sha256() {
    local sum
    sum=$(sha256sum < "$1")
    printf '%s' "${sum%% *}"
}

fetch() {
    local name=$1 repo=$2 tag=$3 commit=$4 target=$DEPS/$1 head eol
    if [[ -d $target/.git ]]; then
        head=$(git -C "$target" rev-parse HEAD 2>/dev/null) || head=
        eol=$(git -C "$target" ls-files --eol 2>/dev/null) || eol=w/crlf
        if [[ $head == "$commit" && $eol != *w/crlf* ]] && git -C "$target" diff --quiet HEAD -- 2>/dev/null; then
            printf '%s %s is already in %s\n' "$name" "$tag" "$target"
            return
        fi
    fi
    rm -rf -- "$target"
    run git -c advice.detachedHead=false -c core.autocrlf=false -c core.eol=lf clone --depth 1 --branch "$tag" "$repo" "$target" ||
        die "cannot clone $repo $tag"
    head=$(git -C "$target" rev-parse HEAD)
    [[ $head == "$commit" ]] || die "$name $tag resolved to $head, expected $commit"
}

occurrences() {
    local rest=${1//"$2"/}
    printf '%s' "$(( (${#1} - ${#rest}) / ${#2} ))"
}

shown() {
    local text=${1:0:60}
    text=${text//$'\n'/'\n'}
    printf "'%s'" "${text//$'\t'/'\t'}"
}

patch() {
    local file=$DEPS/sdk/$1 old=$2 new=$3 count=${4:-1} text found
    IFS= read -r -d '' text < "$file" || true
    found=$(occurrences "$text" "$old")
    [[ $found == "$count" ]] || die "$1: expected $count occurrence(s) of $(shown "$old"), found $found"
    printf '%s' "${text//"$old"/"$new"}" > "$file"
}

patch_input() {
    local file=$DEPS/sdk/Spore/Input.h nl=$'\n' text before upto rest tail=
    local first=$nl'inline auto_METHOD_VOID(GameInput, OnKeyDown' last=$nl'inline auto_STATIC_METHOD(GameInput'
    IFS= read -r -d '' text < "$file" || true
    text=$nl$text
    before=${text%%"$first"*}
    upto=${text%%"$last"*}
    if [[ $(occurrences "$text" "$first") != 1 || $(occurrences "$text" "$last") != 1 ]] ||
        (( ${#upto} < ${#before} )); then
        die 'Spore/Input.h: GameInput method block not found exactly once'
    fi
    rest=${text:${#upto}+1}
    if [[ $rest == *"$nl"* ]]; then
        tail=$nl${rest#*"$nl"}
    fi
    text=$before$nl$INPUT_METHODS$tail
    printf '%s' "${text:1}" > "$file"
}

copy_sdk() {
    local upstream=$1 sdk=$2
    rm -rf -- "$sdk"
    mkdir -p -- "$sdk"
    cp -a -- "$upstream/Spore ModAPI/Spore" "$sdk/Spore"
    cp -a -- "$upstream/EASTL-3.02.01/include/EASTL" "$sdk/EASTL"
    cp -a -- "$upstream/EASTL-3.02.01/test/packages/EABase/include/Common/EABase" "$sdk/EABase"
}

fix_includes() {
    LC_ALL=C find "$1" -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.inl' \) -print0 |
        LC_ALL=C xargs -0 -r sed -i -E '/^[[:space:]]*#[[:space:]]*include/s|\\([A-Za-z])|/\1|g'
}

patch_math_utils() {
    local decl=$'\t\tbool Intersect(const BoundingBox& other, BoundingBox& dst/* = BoundingBox()*/) const;'
    patch Spore/MathUtils.h \
        $'#ifdef SDK_TO_GHIDRA\n'"$decl  // commented the default parameter as it doesn't work in GCC"$'\n#else\n' \
        $'#if defined(SDK_TO_GHIDRA) || defined(__clang__)\n'"$decl"$'\n#else\n'
}

patch_model_world() {
    local constructor default
    constructor=$'\tinline FilterSettings::FilterSettings()\n\t\t: requiredGroupFlags()\n\t\t, excludedGroupFlags()\n'
    constructor+=$'\t\t, filterFunction(nullptr)\n\t\t, collisionMode(CollisionMode::MeshCluster)\n'
    constructor+=$'\t\t, flags(kUseModelCollisionMode)\n\t{\n\t}\n'
    default=$'\n\tinline FilterSettings& DefaultFilterSettings()\n\t{\n\t\tthread_local FilterSettings settings;\n'
    default+=$'\t\tsettings = FilterSettings();\n\t\treturn settings;\n\t}\n'
    patch Spore/Graphics/IModelWorld.h "$constructor" "$constructor$default"
    patch Spore/Graphics/IModelWorld.h \
        'FilterSettings& settings = FilterSettings()' 'FilterSettings& settings = DefaultFilterSettings()' 11
}

patch_static_extremum() {
    patch EASTL/type_traits.h \
        "{ static const size_t value = ((I0 $2 I1) ? $1<I0, IN...>::value : $1<I1, IN...>::value); };" \
        "{ static const size_t value = (I0 $2 I1) ? ($1<I0, IN...>::value) : ($1<I1, IN...>::value); };"
}

patch_utfwin() {
    local defaults=$'\t\t, smoothness(0.0003f)\n\t\t, saturation(0.5f)\n\t\t, color(Math::Color(0x00000000u))\n'
    patch Spore/UTFWin/OutlineFormat.h $'\t\t, sizeY(0)\n\t{\n\t}\n' $'\t\t, sizeY(0)\n'"$defaults"$'\t{\n\t}\n'
    patch Spore/UTFWin/OutlineFormat.h $'\t\t, saturation(other.saturation)\n\t{\n\t}\n' \
        $'\t\t, saturation(other.saturation)\n\t\t, color(other.color)\n\t{\n\t}\n'
    patch Spore/UTFWin/SporeStdDrawableImageInfo.h \
        'SetStrokeShadow(const OutlineFormat& value) { mHaloShadow = value; }' \
        'SetStrokeShadow(const OutlineFormat& value) { mStrokeShadow = value; }'
}

generate_sdk() {
    local sdk=$DEPS/sdk
    copy_sdk "$1" "$sdk"
    fix_includes "$sdk"
    patch_math_utils
    patch_model_world
    patch_static_extremum static_min '<='
    patch_static_extremum static_max '>='
    patch_utfwin
    patch_input
    printf 'Patched SDK headers are in %s\n' "$sdk"
}

fetch_modapi_lib() {
    local target=$DEPS/modapi/SporeModAPI.lib
    if [[ -f $target && $(sha256 "$target") == "$LIB_SHA256" ]]; then
        printf 'SporeModAPI.lib is already in %s\n' "${target%/*}"
        return
    fi
    printf '+ download %s\n' "$DLLS_URL"
    mkdir -p -- "${target%/*}"
    DOWNLOAD_DIR=$(mktemp -d "${target%/*}/.download.XXXXXX")
    trap 'rm -rf -- "$DOWNLOAD_DIR"' EXIT
    local zip=$DOWNLOAD_DIR/dlls.zip lib=$DOWNLOAD_DIR/SporeModAPI.lib
    curl -fsSL --connect-timeout 120 -o "$zip" "$DLLS_URL" || die "cannot download $DLLS_URL"
    [[ $(sha256 "$zip") == "$DLLS_SHA256" ]] || die 'SporeModAPIdlls.zip does not match the expected SHA-256'
    unzip -p "$zip" SporeModAPI.lib > "$lib" || die 'SporeModAPIdlls.zip has no SporeModAPI.lib'
    [[ $(sha256 "$lib") == "$LIB_SHA256" ]] || die 'SporeModAPI.lib does not match the expected SHA-256'
    mv -f -- "$lib" "$target"
    rm -rf -- "$DOWNLOAD_DIR"
}

msvc_complete() {
    [[ -d $1/crt/include && -d $1/sdk/lib/um/x86 ]]
}

msvc_ready() {
    local target=$DEPS/msvc-sdk versions
    msvc_complete "$target" || return 1
    if [[ -L $target ]]; then
        return 0
    fi
    [[ -f $target/.civ-versions ]] || return 1
    versions=$(<"$target/.civ-versions")
    [[ $versions == "$MSVC_CRT_VERSION $WINDOWS_SDK_VERSION" ]]
}

check_msvc_tree() {
    MSVC_SDK=$(realpath -m -- "$MSVC_SDK")
    msvc_complete "$MSVC_SDK" || die "$MSVC_SDK is not an xwin-style tree (crt/include, sdk/lib/um/x86)"
}

check_deps_folder() {
    if [[ -e $DEPS && ! -e $DEPS/.setup-stamp && -n $(ls -A -- "$DEPS" 2>/dev/null) ]]; then
        die "$DEPS is not empty and was not prepared by this script; CIV_DEPS must be a folder used only for these dependencies"
    fi
}

link_msvc_sdk() {
    local target=$1
    if [[ $(realpath -m -- "$target") == "$MSVC_SDK" ]]; then
        printf 'MSVC CRT/SDK is already in %s\n' "$target"
    elif [[ -d $target && ! -L $target ]]; then
        printf 'setup: warning: %s is a folder, not a link; it is kept and %s is not linked\n' "$target" "$MSVC_SDK" >&2
    else
        rm -f -- "$target"
        ln -sT -- "$MSVC_SDK" "$target"
        printf 'MSVC CRT/SDK: %s -> %s\n' "$target" "$MSVC_SDK"
    fi
}

xwin_version() {
    local version words
    version=$("$1" --version 2>/dev/null) || return 1
    read -r -d '' -a words <<< "$version" || true
    printf '%s' "${words[*]: -1}"
}

download_xwin() {
    local arch name url sha dir=$DEPS/.xwin
    arch=$(uname -m)
    sha=${XWIN_SHA256[$arch]:-}
    [[ -n $sha ]] || die "no pinned xwin $XWIN_VERSION build for $arch; pass --xwin PATH"
    name=xwin-$XWIN_VERSION-$arch-unknown-linux-musl
    XWIN=$dir/$name/xwin
    if [[ -x $XWIN ]]; then
        return
    fi
    url=https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VERSION/$name.tar.gz
    printf '+ download %s\n' "$url"
    mkdir -p -- "$dir"
    DOWNLOAD_DIR=$(mktemp -d "$dir/.download.XXXXXX")
    trap 'rm -rf -- "$DOWNLOAD_DIR"' EXIT
    curl -fsSL --connect-timeout 120 -o "$DOWNLOAD_DIR/xwin.tar.gz" "$url" || die "cannot download $url"
    [[ $(sha256 "$DOWNLOAD_DIR/xwin.tar.gz") == "$sha" ]] || die "$name.tar.gz does not match the expected SHA-256"
    tar -xzf "$DOWNLOAD_DIR/xwin.tar.gz" -C "$DOWNLOAD_DIR" "$name/xwin" || die "cannot unpack $name.tar.gz"
    rm -rf -- "${dir:?}/$name"
    mv -- "$DOWNLOAD_DIR/$name" "$dir/$name"
    rm -rf -- "$DOWNLOAD_DIR"
}

resolve_xwin() {
    local found
    if [[ -n $XWIN ]]; then
        found=$(xwin_version "$XWIN") || die "$XWIN --version failed"
        if [[ $found != "$XWIN_VERSION" ]]; then
            printf 'setup: warning: tested with xwin %s, found %s\n' "$XWIN_VERSION" "${found:-an unknown version}" >&2
        fi
    elif found=$(command -v xwin) && [[ $(xwin_version "$found") == "$XWIN_VERSION" ]]; then
        XWIN=$found
    else
        download_xwin
    fi
}

run_xwin() {
    local target=$1 partial=$DEPS/.msvc-sdk.partial cache=$DEPS/.xwin-cache
    resolve_xwin
    if [[ -n $XWIN_CACHE ]]; then
        cache=$(realpath -m -- "$XWIN_CACHE")
    fi
    rm -rf -- "$partial"
    run "$XWIN" --accept-license --arch x86 --manifest-version 17 --crt-version "$MSVC_CRT_VERSION" \
        --sdk-version "$WINDOWS_SDK_VERSION" --cache-dir "$cache" splat --output "$partial" || die 'xwin failed'
    msvc_complete "$partial" || die "xwin left no crt/include and sdk/lib/um/x86 in $partial"
    printf '%s %s\n' "$MSVC_CRT_VERSION" "$WINDOWS_SDK_VERSION" > "$partial/.civ-versions"
    if [[ -L $target || -f $target ]]; then
        rm -f -- "$target"
    else
        rm -rf -- "$target"
    fi
    mv -T -- "$partial" "$target"
}

prepare_msvc_sdk() {
    local target=$DEPS/msvc-sdk
    if [[ -n $MSVC_SDK ]]; then
        link_msvc_sdk "$target"
    elif msvc_ready; then
        printf 'MSVC CRT/SDK is already in %s\n' "$target"
    else
        run_xwin "$target"
    fi
    remove_leftovers
}

remove_leftovers() {
    local path resolved
    for path in "$DEPS/.msvc-sdk.partial" "$DEPS/.xwin-cache"; do
        resolved=$(realpath -m -- "$path")
        if [[ -n $MSVC_SDK && $MSVC_SDK/ == "$resolved"/* ]]; then
            continue
        fi
        if [[ -n $XWIN_CACHE && $(realpath -m -- "$XWIN_CACHE")/ == "$resolved"/* ]]; then
            continue
        fi
        rm -rf -- "$path"
    done
}

main() {
    parse_args "$@"
    if [[ -n $MSVC_SDK ]]; then
        check_msvc_tree
    elif [[ -z $ACCEPT_LICENSE ]] && ! msvc_ready; then
        die "$LICENSE_MESSAGE"
    fi
    check_deps_folder
    mkdir -p -- "$DEPS"
    printf 'incomplete\n' > "$DEPS/.setup-stamp"
    fetch Spore-ModAPI "$MODAPI_REPO" "$MODAPI_TAG" "$MODAPI_COMMIT"
    generate_sdk "$DEPS/Spore-ModAPI"
    fetch Detours "$DETOURS_REPO" "$DETOURS_TAG" "$DETOURS_COMMIT"
    fetch_modapi_lib
    prepare_msvc_sdk
    printf '%s\n' "$(sha256 "$SCRIPT")" > "$DEPS/.setup-stamp"
    printf 'Dependencies are ready in %s\n' "$DEPS"
}

main "$@"

