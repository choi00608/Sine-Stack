#!/bin/bash
# Sine Stack end-user installer. macOS Bash 3.2; no Python, Homebrew or build tools.
set -euo pipefail
umask 022

fail() { printf 'Sine Stack: %s\n' "$*" >&2; exit 1; }
usage() {
    cat <<'HELP'
Install or update AU, VST3 and Sine Stack.app together (macOS 11+, no sudo).

  bash install.sh --archive /path/SineStack-VERSION-macOS-Universal.tar.gz
  bash install.sh                         # Latest choi00608/Sine-Stack release
  bash install.sh --repo OWNER/REPOSITORY
  bash install.sh --repo OWNER/REPOSITORY --version 0.1.2

Options:
  --archive FILE        Offline release archive; SHA256SUMS.txt must be beside it
  --checksum-file FILE Use this checksum file for an offline archive
  --repo OWNER/REPO    Public GitHub repository; saved for subsequent updates
  --version VERSION   A specific vVERSION release instead of latest
  --check             Verify the release and show destinations without installing
  --allow-downgrade   Explicitly allow replacing a newer installed version
  --user-root DIR     Install into this existing user folder (default: your home)
  --help              Show this help

Online updates require a published GitHub Release with the archive and SHA256SUMS.txt.
Settings and presets are kept. Reopen the app and DAW after updating.
HELP
}
valid_version() { [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; }
valid_repo() { [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9][A-Za-z0-9_.-]*$ ]]; }
plist() { /usr/libexec/PlistBuddy -c "Print :$2" "$1" 2>/dev/null; }
is_newer() {
    /usr/bin/awk -v a="$1" -v b="$2" 'BEGIN {split(a,x,".");split(b,y,".");for(i=1;i<=3;i++){if(x[i]+0>y[i]+0)exit 0;if(x[i]+0<y[i]+0)exit 1}exit 1}'
}
fetch() {
    /usr/bin/curl --fail --location --silent --show-error --retry 2 \
        --connect-timeout 20 --max-time 600 --proto '=https' --proto-redir '=https' \
        --output "$2" "$1" || fail "Download failed. Check the repository, published release and network: $1"
}

archive=''; checksums=''; repository=''; requested_version=''; check_only=0; allow_downgrade=0
user_root="$HOME"
while [ "$#" -gt 0 ]; do
    case "$1" in
        --archive|--checksum-file|--repo|--version|--user-root)
            [ "$#" -ge 2 ] && [ -n "$2" ] || fail "Missing value for $1"
            case "$1" in
                --archive) archive="$2";; --checksum-file) checksums="$2";; --repo) repository="$2";;
                --version) requested_version="$2";; --user-root) user_root="$2";;
            esac
            shift 2;;
        --check) check_only=1; shift;;
        --allow-downgrade) allow_downgrade=1; shift;;
        --help|-h) usage; exit 0;;
        *) fail "Unknown option: $1 (use --help)";;
    esac
done
[ "$(/usr/bin/uname -s)" = Darwin ] || fail 'This release is for macOS.'
[ "$EUID" -ne 0 ] || fail 'Run as your normal user, without sudo.'
os_version=$(/usr/bin/sw_vers -productVersion)
[ "${os_version%%.*}" -ge 11 ] || fail 'macOS 11 or newer is required.'
[ -d "$user_root" ] || fail 'The --user-root folder must already exist.'
user_root=$(cd "$user_root" && pwd -P)
[ "$user_root" != / ] || fail 'The filesystem root is not a user folder.'
[ -z "$requested_version" ] || valid_version "$requested_version" || fail 'Use a numeric version such as 0.1.2.'
state_dir="$user_root/Library/Application Support/SineStack/Installer"
if [ -z "$repository" ] && [ -f "$state_dir/state.plist" ]; then
    repository=$(plist "$state_dir/state.plist" Repository || true)
fi
[ -n "$repository" ] || repository=choi00608/Sine-Stack
[ -z "$repository" ] || valid_repo "$repository" || fail 'Use --repo OWNER/REPOSITORY.'
[ -n "$archive" ] || [ -n "$repository" ] || fail 'No release repository configured. Use --archive FILE now, or --repo OWNER/REPOSITORY after publishing.'
[ -n "$archive" ] || [ -z "$checksums" ] || fail '--checksum-file is for offline archives only.'

work=$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/sinestack-install.XXXXXX")
stages=(); destinations=(); moved_old=(); installed_new=(); lock_owned=0; committed=0
lock_dir="$user_root/Library/Application Support/SineStack/.install-lock"
cleanup() {
    result=$?; trap - EXIT INT TERM; set +e
    for ((i=${#stages[@]}-1; i>=0; i--)); do
        preserve=0
        if [ "$committed" -eq 0 ]; then
            if [ "${installed_new[$i]}" -eq 1 ]; then
                /bin/mv "${destinations[$i]}" "${stages[$i]}/rejected" || preserve=1
            fi
            if [ "${moved_old[$i]}" -eq 1 ]; then
                /bin/mv "${stages[$i]}/previous" "${destinations[$i]}" || preserve=1
            fi
        fi
        if [ "$preserve" -eq 0 ]; then
            /bin/rm -rf "${stages[$i]}"
        else
            printf 'Recovery files retained at: %s\n' "${stages[$i]}" >&2
            result=1
        fi
    done
    [ "$lock_owned" -eq 0 ] || /bin/rm -rf "$lock_dir"
    /bin/rm -rf "$work"
    exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [ -n "$archive" ]; then
    [ -f "$archive" ] || fail "Archive not found: $archive"
    archive=$(cd "$(dirname "$archive")" && printf '%s/%s' "$(pwd -P)" "$(basename "$archive")")
    archive_name=$(basename "$archive")
    [[ "$archive_name" =~ ^SineStack-([0-9]+\.[0-9]+\.[0-9]+)-macOS-Universal\.tar\.gz$ ]] || fail 'Unexpected archive name.'
    version="${BASH_REMATCH[1]}"
    [ -z "$requested_version" ] || [ "$requested_version" = "$version" ] || fail 'Archive and requested version differ.'
    [ -n "$checksums" ] || checksums="$(dirname "$archive")/SHA256SUMS.txt"
else
    if [ -n "$requested_version" ]; then
        version="$requested_version"; tag="v$version"
    else
        fetch "https://api.github.com/repos/$repository/releases/latest" "$work/release.json"
        /usr/bin/plutil -convert xml1 -o "$work/release.plist" "$work/release.json" || fail 'Invalid GitHub release response.'
        tag=$(plist "$work/release.plist" tag_name) || fail 'Release has no tag.'
        [[ "$tag" = v* ]] || fail 'Release tag must be v followed by a numeric version.'
        version="${tag#v}"
        valid_version "$version" || fail 'Release tag must look like v0.1.2.'
    fi
    archive_name="SineStack-$version-macOS-Universal.tar.gz"
    archive="$work/$archive_name"; checksums="$work/SHA256SUMS.txt"
    base="https://github.com/$repository/releases/download/$tag"
    fetch "$base/SHA256SUMS.txt" "$checksums"
    fetch "$base/$archive_name" "$archive"
fi
[ -f "$checksums" ] || fail "Checksum file not found: $checksums"
expected=$(/usr/bin/awk -v name="$archive_name" 'length($1)==64 && $2==name {count++; hash=$1} END {if(count!=1)exit 1; print hash}' "$checksums") || fail 'Archive must have exactly one SHA-256 entry.'
[[ "$expected" =~ ^[0-9a-fA-F]{64}$ ]] || fail 'Invalid SHA-256 checksum.'
actual=$(/usr/bin/shasum -a 256 "$archive" | /usr/bin/awk '{print $1}')
[ "$(printf '%s' "$expected" | /usr/bin/tr 'A-F' 'a-f')" = "$actual" ] || fail 'Checksum mismatch; no installed files were changed.'

# Inspect paths and entry types before extraction. Our distribution contains no links.
release_root="SineStack-$version"
/usr/bin/tar -tzf "$archive" > "$work/members" || fail 'Unreadable release archive.'
[ -s "$work/members" ] || fail 'Empty release archive.'
while IFS= read -r member; do
    case "$member" in "$release_root"|"$release_root/"|"$release_root/"*) ;; *) fail 'Archive entry is outside the release folder.';; esac
    case "/${member%/}/" in *'/../'*|*'/./'*|*'//'*) fail 'Unsafe archive path.';; esac
    case "$member" in *'\'*|*$'\r'*) fail 'Unsupported archive path.';; esac
done < "$work/members"
/usr/bin/tar -tvzf "$archive" > "$work/types" || fail 'Could not inspect archive types.'
/usr/bin/awk 'substr($0,1,1)!="-" && substr($0,1,1)!="d" {exit 1}' "$work/types" || fail 'Archive links and special files are not supported.'
/bin/mkdir "$work/unpacked"
/usr/bin/tar -xzf "$archive" -C "$work/unpacked" --no-same-owner || fail 'Could not extract release.'
payload="$work/unpacked/$release_root"
manifest="$payload/release.plist"
[ "$(plist "$manifest" Format)" = SineStackRelease ] || fail 'Invalid release manifest.'
[ "$(plist "$manifest" FormatVersion)" = 1 ] || fail 'Unsupported release format.'
[ "$(plist "$manifest" Version)" = "$version" ] || fail 'Manifest version differs from the archive.'
fingerprint=$(plist "$manifest" SourceFingerprint); built_at=$(plist "$manifest" BuildUTC)
[[ "$fingerprint" =~ ^[0-9a-f]{64}$ ]] || fail 'Invalid source fingerprint.'
[[ "$built_at" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$ ]] || fail 'Invalid build date.'
[ -f "$payload/install.sh" ] || fail 'Release is missing its update tool.'

sources=("$payload/AU/Sine Stack.component" "$payload/VST3/Sine Stack.vst3" "$payload/Standalone/Sine Stack.app")
targets=("$user_root/Library/Audio/Plug-Ins/Components/Sine Stack.component" "$user_root/Library/Audio/Plug-Ins/VST3/Sine Stack.vst3" "$user_root/Applications/Sine Stack.app")
for ((i=0; i<3; i++)); do
    info="${sources[$i]}/Contents/Info.plist"
    [ "$(plist "$info" CFBundleIdentifier)" = audio.sinestack.synth ] || fail 'Unexpected bundle identifier.'
    [ "$(plist "$info" CFBundleExecutable)" = 'Sine Stack' ] || fail 'Unexpected bundle executable.'
    [ "$(plist "$info" CFBundleShortVersionString)" = "$version" ] && [ "$(plist "$info" CFBundleVersion)" = "$version" ] || fail 'Bundle versions do not match.'
    [ "$(plist "$info" SineStackSourceFingerprint)" = "$fingerprint" ] && [ "$(plist "$info" SineStackBuildUTC)" = "$built_at" ] || fail 'Bundle build identities do not match.'
    binary="${sources[$i]}/Contents/MacOS/Sine Stack"
    [ -x "$binary" ] || fail 'Bundle executable is missing or not executable.'
    description=$(/usr/bin/file -b "$binary")
    [[ "$description" = *'Mach-O universal binary'* && "$description" = *arm64* && "$description" = *x86_64* ]] || fail 'A Universal macOS binary is required.'
    /usr/bin/codesign --verify --deep --strict --all-architectures "${sources[$i]}" || fail 'Bundle signature verification failed.'
done
[ -n "$(plist "${sources[2]}/Contents/Info.plist" NSBluetoothAlwaysUsageDescription)" ] || fail 'The app is missing Bluetooth permissions metadata.'
printf 'Verified Sine Stack %s (%s): AU, VST3, Standalone\n' "$version" "$built_at"
if [ "$check_only" -eq 1 ]; then
    printf 'Destination: %s\n' "${targets[@]}"
    printf 'Verification complete; nothing installed.\n'; exit 0
fi

/bin/mkdir -p "$(dirname "$lock_dir")"
/bin/mkdir "$lock_dir" 2>/dev/null || fail "Another installation is in progress, or a stale lock remains: $lock_dir"
lock_owned=1
printf '%s\n' "$$" > "$lock_dir/pid"
for ((i=0; i<3; i++)); do
    destination="${targets[$i]}"
    [ ! -L "$destination" ] || fail "Refusing to replace a symlink: $destination"
    if [ -e "$destination" ]; then
        [ -d "$destination" ] && [ "$(plist "$destination/Contents/Info.plist" CFBundleIdentifier)" = audio.sinestack.synth ] || fail "Destination is not an existing Sine Stack bundle: $destination"
        previous=$(plist "$destination/Contents/Info.plist" CFBundleShortVersionString)
        valid_version "$previous" || fail 'Installed version is not recognised.'
        if [ "$allow_downgrade" -eq 0 ] && is_newer "$previous" "$version"; then
            fail "Installed $previous is newer than $version. Use --allow-downgrade only if intended."
        fi
    fi
done
# Bundle and updater stages live next to their destination, allowing rename/rollback.
/bin/mkdir "$work/updater"
/bin/cp "$payload/install.sh" "$work/updater/install.sh"
/bin/chmod 755 "$work/updater/install.sh"
cat > "$work/updater/state.plist" <<STATE
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>Version</key><string>$version</string><key>Repository</key><string>$repository</string><key>BuildUTC</key><string>$built_at</string></dict></plist>
STATE
sources+=("$work/updater"); targets+=("$state_dir")
for ((i=0; i<4; i++)); do
    destination="${targets[$i]}"
    [ ! -L "$destination" ] || fail "Refusing to replace a symlink: $destination"
    if [ -d "$destination" ] && /usr/bin/diff -qr "${sources[$i]}" "$destination" >/dev/null 2>&1; then
        printf 'Already current: %s\n' "$destination"; continue
    fi
    parent=$(dirname "$destination"); /bin/mkdir -p "$parent"
    stage=$(/usr/bin/mktemp -d "$parent/.SineStack-stage.XXXXXX")
    index=${#stages[@]}; stages[$index]="$stage"; destinations[$index]="$destination"; moved_old[$index]=0; installed_new[$index]=0
    /usr/bin/ditto "${sources[$i]}" "$stage/new"
    if [ "$i" -lt 3 ]; then /usr/bin/codesign --verify --deep --strict --all-architectures "$stage/new"; fi
done
for ((i=0; i<${#stages[@]}; i++)); do
    destination="${destinations[$i]}"; stage="${stages[$i]}"
    if [ -e "$destination" ]; then /bin/mv "$destination" "$stage/previous"; moved_old[$i]=1; fi
    /bin/mv "$stage/new" "$destination"; installed_new[$i]=1
    printf 'Installed: %s\n' "$destination"
done
committed=1
printf '\nSine Stack %s is installed. Your presets and settings were kept.\n' "$version"
printf 'Reopen Sine Stack and your DAW. The package installer uses separate system-wide locations.\n'
if [ -n "$repository" ]; then
    printf 'Next update: bash "%s/install.sh"\n' "$state_dir"
else
    printf 'After publishing: bash "%s/install.sh" --repo OWNER/REPOSITORY\n' "$state_dir"
fi
