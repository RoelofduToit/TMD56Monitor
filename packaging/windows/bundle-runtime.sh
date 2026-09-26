#!/usr/bin/env bash
# Copy the GTK/GLib runtime this executable actually needs into the install prefix.
# Run from an MSYS2 UCRT64 shell after cmake --install. Do not copy all of /ucrt64.
#
# Windows loads an implicit DLL from the executable's directory before PATH, so the
# bundle is flat: tmd56-monitor.exe and its DLLs share one directory. Modules that
# GTK opens later (pixbuf loaders, GIO modules) live under lib/ and are checked too.
set -euo pipefail

prefix=${1:?install prefix}
exe="$prefix/tmd56-monitor.exe"
mingw_prefix=${MINGW_PREFIX:-/ucrt64}

if [[ ! -f "$exe" ]]; then
    echo "tmd56: $exe was not installed" >&2
    exit 1
fi
if ! command -v ntldd >/dev/null 2>&1; then
    echo "tmd56: ntldd is required (mingw-w64-ucrt-x86_64-ntldd)" >&2
    exit 1
fi

skip_dll() {
    local base
    base=$(basename "$1" | tr '[:upper:]' '[:lower:]')
    case "$base" in
        api-ms-*|ext-ms-*|kernel32.dll|user32.dll|gdi32.dll|advapi32.dll|shell32.dll|\
        ole32.dll|oleaut32.dll|comdlg32.dll|ws2_32.dll|msvcrt.dll|ntdll.dll|ucrtbase.dll|\
        bcrypt.dll|crypt32.dll|secur32.dll|shlwapi.dll|imm32.dll|setupapi.dll|version.dll|\
        winmm.dll|iphlpapi.dll|dwmapi.dll|uxtheme.dll|comctl32.dll|usp10.dll|rpcrt4.dll|\
        hid.dll|opengl32.dll|winspool.drv|cfgmgr32.dll|dnsapi.dll|mpr.dll|netapi32.dll|\
        userenv.dll|wtsapi32.dll|imagehlp.dll|dbghelp.dll)
            return 0
            ;;
    esac
    case "$1" in
        [A-Za-z]:/Windows/*|[A-Za-z]:/WINDOWS/*|*/Windows/System32/*|*/Windows/SysWOW64/*)
            return 0
            ;;
    esac
    return 1
}

to_unix() {
    local path=$1
    path=${path//$'\r'/}
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -u "$path"
    else
        printf '%s\n' "$path"
    fi
}

# ntldd follows imports into System32. Only MinGW DLLs belong in the package.
is_mingw_runtime() {
    local src=$1
    local mixed mingw_mixed
    case "$(basename "$src" | tr '[:upper:]' '[:lower:]')" in
        *.dll) ;;
        *) return 1 ;;
    esac
    if ! command -v cygpath >/dev/null 2>&1; then
        case "$src" in
            "$mingw_prefix"/*) return 0 ;;
            *) return 1 ;;
        esac
    fi
    mixed=$(cygpath -m "$src" | tr '[:upper:]' '[:lower:]' | tr '\\' '/')
    mingw_mixed=$(cygpath -m "$mingw_prefix" | tr '[:upper:]' '[:lower:]' | tr '\\' '/')
    mingw_mixed=${mingw_mixed%/}
    case "$mixed" in
        "$mingw_mixed"/*) return 0 ;;
        *) return 1 ;;
    esac
}

copy_dll() {
    local src=$1
    local base dest
    src=$(to_unix "$src")
    [[ -f "$src" ]] || return 0
    if skip_dll "$src" || ! is_mingw_runtime "$src"; then
        return 0
    fi
    base=$(basename "$src")
    dest="$prefix/$base"
    if [[ ! -f "$dest" ]]; then
        cp -a "$src" "$dest"
        echo "dll $base"
    fi
}

# Make loader paths relative to the cache file: "loaders/name.dll".
rewrite_loaders_cache() {
    local cache=$1
    local tmp line rest
    tmp=$(mktemp)
    while IFS= read -r line || [[ -n "$line" ]]; do
        line=${line//$'\r'/}
        line=${line//\\//}
        while [[ "$line" == *'//'* ]]; do
            line=${line//'//'/'/'}
        done
        if [[ "$line" == \#* ]] && [[ "$line" == *ucrt64* || "$line" == *msys64* || "$line" == *mingw64* || "$line" == *mingw32* ]]; then
            continue
        fi
        if [[ "$line" == *'/loaders/'* ]]; then
            rest=${line#*/loaders/}
            if [[ "$line" == \"* ]]; then
                line="\"loaders/${rest}"
            else
                line="loaders/${rest}"
            fi
        fi
        printf '%s\n' "$line"
    done < "$cache" > "$tmp"
    mv "$tmp" "$cache"
    if grep -E -q 'ucrt64|msys64|mingw64|mingw32' "$cache"; then
        echo "tmd56: loaders.cache still points at the build machine:" >&2
        grep -E -n 'ucrt64|msys64|mingw64|mingw32' "$cache" >&2
        return 1
    fi
}

# ntldd -R lists the import closure, including DLLs that are not direct imports.
collect_from() {
    local target=$1
    local line path
    while IFS= read -r line; do
        case "$line" in
            *'=>'* )
                path=${line#*=> }
                path=${path%% (*}
                path=${path//$'\r'/}
                [[ -n "$path" && "$path" != "not found" ]] || continue
                copy_dll "$path"
                ;;
        esac
    done < <(ntldd -R "$target")
}

collect_tree_dlls() {
    local root=$1
    [[ -d "$root" ]] || return 0
    local dll
    while IFS= read -r -d '' dll; do
        collect_from "$dll"
    done < <(find "$root" -type f -name '*.dll' -print0)
}

collect_from "$exe"

# GLib looks for these beside the GIO DLL when it needs to spawn a helper.
shopt -s nullglob
for helper in "$mingw_prefix"/bin/gspawn-*-helper.exe "$mingw_prefix"/bin/gspawn-*-helper-console.exe; do
    cp -a "$helper" "$prefix/"
    collect_from "$helper"
done
shopt -u nullglob

# Pixbuf loaders are dlopen'd, so ntldd does not see them from the executable.
loader_dir="$mingw_prefix/lib/gdk-pixbuf-2.0"
if [[ -d "$loader_dir" ]]; then
    mkdir -p "$prefix/lib"
    cp -a "$loader_dir" "$prefix/lib/"
    collect_tree_dlls "$prefix/lib/gdk-pixbuf-2.0"
    dest_cache=$(find "$prefix/lib/gdk-pixbuf-2.0" -name loaders.cache -print | head -n 1 || true)
    if [[ -z "$dest_cache" ]]; then
        echo "tmd56: gdk-pixbuf loaders.cache was not found" >&2
        exit 1
    fi
    rewrite_loaders_cache "$dest_cache"
else
    echo "tmd56: gdk-pixbuf loaders were not found under $loader_dir" >&2
    exit 1
fi

# GIO modules (TLS and similar). The shipped tree drops giomodule.cache because
# that file contains absolute build-machine paths; GLib scans the directory.
gio_src="$mingw_prefix/lib/gio/modules"
if [[ -d "$gio_src" ]]; then
    mkdir -p "$prefix/lib/gio"
    cp -a "$gio_src" "$prefix/lib/gio/"
    rm -f "$prefix/lib/gio/modules/giomodule.cache"
    collect_tree_dlls "$prefix/lib/gio/modules"
fi

# Pango module DLLs, when this GTK build still ships them separately.
# The modules index is not copied: it records absolute MSYS paths.
if [[ -d "$mingw_prefix/lib/pango" ]]; then
    mkdir -p "$prefix/lib/pango"
    cp -a "$mingw_prefix/lib/pango/." "$prefix/lib/pango/"
    find "$prefix/lib/pango" -name '*.modules' -delete
    collect_tree_dlls "$prefix/lib/pango"
fi

schema_src="$mingw_prefix/share/glib-2.0/schemas"
if [[ -d "$schema_src" ]]; then
    mkdir -p "$prefix/share/glib-2.0/schemas"
    cp -a "$schema_src"/*.xml "$prefix/share/glib-2.0/schemas/"
    glib-compile-schemas "$prefix/share/glib-2.0/schemas"
else
    echo "tmd56: GLib schemas were not found" >&2
    exit 1
fi

mkdir -p "$prefix/etc/gtk-4.0"
if [[ -d "$mingw_prefix/etc/gtk-4.0" ]]; then
    cp -a "$mingw_prefix/etc/gtk-4.0/." "$prefix/etc/gtk-4.0/"
fi
if [[ ! -e "$prefix/etc/gtk-4.0/settings.ini" ]]; then
    printf '%s\n' '[Settings]' 'gtk-theme-name=Adwaita' > "$prefix/etc/gtk-4.0/settings.ini"
fi

# A private fontconfig file so SVG icons do not look for the MSYS prefix.
# WINDOWSFONTDIR is fontconfig's token for the Windows font directory.
if compgen -G "$prefix/libfontconfig*.dll" > /dev/null; then
    mkdir -p "$prefix/etc/fonts"
    cat > "$prefix/etc/fonts/fonts.conf" << 'EOF'
<?xml version="1.0"?>
<fontconfig>
  <dir>WINDOWSFONTDIR</dir>
  <cachedir prefix="xdg">fontconfig</cachedir>
</fontconfig>
EOF
fi

# GTK widgets need a small icon theme. Adwaita is the theme GTK 4 expects.
# Locales, manuals, headers and print backends are not copied.
if [[ -d "$mingw_prefix/share/icons/Adwaita" ]]; then
    mkdir -p "$prefix/share/icons"
    cp -a "$mingw_prefix/share/icons/Adwaita" "$prefix/share/icons/"
fi
if [[ -d "$mingw_prefix/share/icons/hicolor" ]]; then
    mkdir -p "$prefix/share/icons"
    cp -a "$mingw_prefix/share/icons/hicolor" "$prefix/share/icons/"
fi

license_dest="$prefix/THIRD-PARTY-LICENSES"
mkdir -p "$license_dest"
if [[ -d "$mingw_prefix/share/licenses" ]]; then
    for name in gtk4 glib2 cairo pango harfbuzz gdk-pixbuf2 libserialport zlib libpng libffi pcre2 gettext fontconfig freetype librsvg; do
        if [[ -d "$mingw_prefix/share/licenses/$name" ]]; then
            cp -a "$mingw_prefix/share/licenses/$name" "$license_dest/"
        fi
    done
fi

# ntldd's own fallback search looks beside ntldd.exe, which lives in the MSYS
# prefix. Run a copy from an empty directory so a missing DLL cannot be
# satisfied by the build machine.
audit_bin=$(mktemp -d)
cleanup() {
    rm -rf "$audit_bin"
}
trap cleanup EXIT
cp "$(command -v ntldd)" "$audit_bin/ntldd.exe"
while IFS= read -r line; do
    case "$line" in
        *'=>'* )
            path=${line#*=> }
            path=${path%% (*}
            path=${path//$'\r'/}
            [[ -n "$path" && "$path" != "not found" ]] || continue
            path=$(to_unix "$path")
            [[ -f "$path" ]] || continue
            if skip_dll "$path" || ! is_mingw_runtime "$path"; then
                continue
            fi
            cp -a "$path" "$audit_bin/$(basename "$path")"
            ;;
    esac
done < <(ntldd -R "$audit_bin/ntldd.exe")

# Fail only when a DLL we ship has an unresolved import. ntldd also walks
# System32, and those trees contain optional Windows DLLs that are not part
# of this package.
audit() {
    local target=$1
    local out missing
    if ! out=$(cd "$audit_bin" && PATH="$audit_bin:$prefix" ./ntldd.exe -R "$target"); then
        echo "tmd56: ntldd could not read $target" >&2
        return 1
    fi
    missing=$(printf '%s\n' "$out" | TMD_AUDIT_PREFIX="$prefix" TMD_AUDIT_BIN="$audit_bin" awk '
        function indent_of(s,    i) {
            i = 1
            while (substr(s, i, 1) == " " || substr(s, i, 1) == "\t") {
                i++
            }
            return i
        }
        function norm(s,    out, i, c) {
            out = ""
            s = tolower(s)
            for (i = 1; i <= length(s); i++) {
                c = substr(s, i, 1)
                if (c == "\\") {
                    c = "/"
                }
                out = out c
            }
            if (substr(out, length(out), 1) == "/") {
                out = substr(out, 1, length(out) - 1)
            }
            return out
        }
        function is_ours(path,    low, root) {
            low = norm(path)
            root = norm(ENVIRON["TMD_AUDIT_PREFIX"])
            if (root != "" && (index(low, root "/") == 1 || low == root)) {
                return 1
            }
            root = norm(ENVIRON["TMD_AUDIT_BIN"])
            if (root != "" && (index(low, root "/") == 1 || low == root)) {
                return 1
            }
            return 0
        }
        BEGIN { ours[0] = 1; depth[0] = 0; n = 0 }
        {
            line = $0
            sub(/\r$/, "", line)
            if (index(line, "=>") == 0) {
                next
            }
            ind = indent_of(line)
            while (n > 0 && depth[n] >= ind) {
                n--
            }
            parent = ours[n]
            split_at = index(line, "=>")
            left = substr(line, 1, split_at - 1)
            right = substr(line, split_at + 2)
            gsub(/^[ \t]+|[ \t]+$/, "", left)
            if (right ~ /not found/) {
                lname = tolower(left)
                if (parent && lname !~ /^api-ms-/ && lname !~ /^ext-ms-/) {
                    print left
                }
            } else {
                path = right
                sub(/^[ \t]+/, "", path)
                sub(/ \(.*$/, "", path)
                n++
                depth[n] = ind
                ours[n] = is_ours(path)
            }
        }
    ')
    if [[ -n "$missing" ]]; then
        echo "tmd56: unresolved imports in $target" >&2
        printf '%s\n' "$missing" >&2
        return 1
    fi
}

audit "$exe"
shopt -s nullglob
for helper in "$prefix"/gspawn-*-helper.exe "$prefix"/gspawn-*-helper-console.exe; do
    audit "$helper"
done
shopt -u nullglob
while IFS= read -r -d '' extra; do
    audit "$extra"
done < <(find "$prefix/lib" -type f -name '*.dll' -print0)

for required in \
    share/glib-2.0/schemas/gschemas.compiled \
    lib/gdk-pixbuf-2.0 \
    LICENSE \
    THIRD_PARTY_NOTICES.md
do
    if [[ ! -e "$prefix/$required" ]]; then
        echo "tmd56: bundle is missing $required" >&2
        exit 1
    fi
done
if ! find "$prefix/lib/gdk-pixbuf-2.0" -name loaders.cache -print | grep -q .; then
    echo "tmd56: bundle is missing gdk-pixbuf loaders.cache" >&2
    exit 1
fi

dll_count=$(find "$prefix" -name '*.dll' -type f | wc -l)
echo "tmd56: runtime bundle ready in $prefix ($dll_count DLLs)"
