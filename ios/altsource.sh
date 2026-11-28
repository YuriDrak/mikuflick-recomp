#!/bin/sh
# Writes an AltStore/SideStore/Feather source (altsource.json) for one IPA release.
# CI runs it when a GitHub release is published and uploads the JSON next to the IPA, so the
# source URL stays https://github.com/<owner>/<repo>/releases/latest/download/altsource.json.
#
#   sh ios/altsource.sh APP_DIR IPA_FILE DOWNLOAD_URL OUT_JSON
#
# APP_DIR is the packaged MikuFlick.app (version and bundle id are read from its Info.plist).
# Optional environment: REPO (owner/repo), NOTES (release notes), DATE (ISO 8601, default now).
# Needs jq and PlistBuddy (macOS).
set -eu
[ $# -eq 4 ] || { echo "usage: $0 APP_DIR IPA_FILE DOWNLOAD_URL OUT_JSON" >&2; exit 2; }
app=$1 ipa=$2 url=$3 out=$4
pb=/usr/libexec/PlistBuddy
bundle=$("$pb" -c 'Print CFBundleIdentifier' "$app/Info.plist")
version=$("$pb" -c 'Print CFBundleShortVersionString' "$app/Info.plist")
build=$("$pb" -c 'Print CFBundleVersion' "$app/Info.plist")
minos=$("$pb" -c 'Print MinimumOSVersion' "$app/Info.plist" 2>/dev/null || echo 26.0)
size=$(wc -c < "$ipa" | tr -d ' ')
repo=${REPO:-YuriDrak/mikuflick-recomp}
owner=${repo%%/*}
date=${DATE:-$(date -u +%Y-%m-%dT%H:%M:%SZ)}
notes=${NOTES:-"Miku Flick $version"}
icon="https://raw.githubusercontent.com/$repo/main/ios/App/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png"
about="A clean-room C11 + SDL2 port of Miku Flick/02. It ships no game data: on first start it imports your own legally obtained, decrypted Miku Flick/02 v1.1.5 IPA from Files. IPAs that include the DLC packs also give the DLC songs."

jq -n --arg bundle "$bundle" --arg version "$version" --arg build "$build" --arg minos "$minos" \
    --argjson size "$size" --arg url "$url" --arg date "$date" --arg notes "$notes" \
    --arg icon "$icon" --arg about "$about" --arg owner "$owner" \
    --arg website "https://github.com/$repo" --arg source_id "$bundle.source" '
{
  name: "Miku Flick",
  identifier: $source_id,
  subtitle: "Native port of Miku Flick/02",
  description: $about,
  iconURL: $icon,
  website: $website,
  tintColor: "39C5BB",
  apps: [{
    name: "Miku Flick",
    bundleIdentifier: $bundle,
    developerName: $owner,
    subtitle: "Flick rhythm game port (bring your own IPA)",
    localizedDescription: $about,
    iconURL: $icon,
    tintColor: "39C5BB",
    category: "games",
    versions: [{
      version: $version,
      buildVersion: $build,
      date: $date,
      localizedDescription: $notes,
      downloadURL: $url,
      size: $size,
      minOSVersion: $minos
    }],
    version: $version,
    versionDate: $date,
    versionDescription: $notes,
    downloadURL: $url,
    size: $size,
    appPermissions: { entitlements: [], privacy: {} }
  }],
  news: []
}' > "$out"
echo "wrote $out: $bundle $version ($size bytes)"
