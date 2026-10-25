#!/bin/bash
set -euo pipefail

# Stable, user-scoped Headless SonoBus installation. The official
# /Applications/SonoBus.app is never modified by this script.

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MODE="${1:-stage}"
BUILD_CONFIG="${SONOBUS_BUILD_CONFIG:-Debug}"
BUILD_DIR="${SONOBUS_BUILD_DIR:-$ROOT_DIR/build-headless}"
PRODUCT_APP="${SONOBUS_APP_PRODUCT:-$BUILD_DIR/SonoBus_artefacts/$BUILD_CONFIG/Standalone/SonoBus.app}"
STAGE_ROOT="$HOME/Library/Caches/SonoBusHeadless/staged"
STAGED_APP="$STAGE_ROOT/SonoBusHeadless.app"
APP_PATH="/Applications/SonoBusHeadless.app"
APP_EXECUTABLE="$APP_PATH/Contents/MacOS/SonoBus"
IDENTITY="Apple Development: fh8y51o@icloud.com (SMV6792LHZ)"
EXPECTED_AUTHORITY="Apple Development: fh8y51o@icloud.com (SMV6792LHZ)"
EXPECTED_AUTHORITY_CHAIN="Apple Development: fh8y51o@icloud.com (SMV6792LHZ)|Apple Worldwide Developer Relations Certification Authority|Apple Root CA"
EXPECTED_TEAM="25ZK5PSQ9K"
EXPECTED_BUNDLE_ID="com.jiajia.sonobusheadless"
LABEL="com.jiajia.sonobusheadless"
LEGACY_LABEL="com.sonobus.headless-direct"
SETUP_DIR="$HOME/Library/Application Support/SonoBusHeadless"
SETUP_FILE="$SETUP_DIR/SonoBus-Headless-Setup.xml"
AGENT_DIR="$HOME/Library/LaunchAgents"
AGENT_FILE="$AGENT_DIR/$LABEL.plist"
LOG_DIR="$SETUP_DIR/Logs"
ENTITLEMENTS="$ROOT_DIR/scripts/SonoBusHeadless-macos.entitlements"
CM="${SONOBUS_CMAKE:-}"

case "$MODE" in
    stage|prepare|deploy) ;;
    *) echo "Usage: $0 [stage|prepare|deploy]" >&2; exit 2 ;;
esac

if [[ -z "$CM" ]]; then
    CM="$(command -v cmake || true)"
fi
if [[ -z "$CM" && -x /opt/homebrew/bin/cmake ]]; then
    CM=/opt/homebrew/bin/cmake
fi
if [[ -z "$CM" && -x /usr/local/bin/cmake ]]; then
    CM=/usr/local/bin/cmake
fi
if [[ -z "$CM" && -x /private/tmp/sonobus-cmake-runtime/cmake/data/bin/cmake ]]; then
    CM=/private/tmp/sonobus-cmake-runtime/cmake/data/bin/cmake
fi

if [[ "${SONOBUS_SKIP_BUILD:-0}" != "1" ]]; then
    if [[ -z "$CM" ]]; then
        echo "CMake not found; set SONOBUS_CMAKE or install CMake." >&2
        exit 2
    fi
    "$CM" -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_CONFIG"
    "$CM" --build "$BUILD_DIR" --target SonoBus_Standalone --config "$BUILD_CONFIG" --parallel "${SONOBUS_BUILD_JOBS:-3}"
fi

if [[ ! -d "$PRODUCT_APP" ]]; then
    echo "Standalone app product not found: $PRODUCT_APP" >&2
    exit 2
fi
if [[ ! -f "$ENTITLEMENTS" ]]; then
    echo "Missing fixed entitlements: $ENTITLEMENTS" >&2
    exit 2
fi

mkdir -p "$STAGE_ROOT"
rm -rf "$STAGED_APP"
ditto "$PRODUCT_APP" "$STAGED_APP"

INFO_PLIST="$STAGED_APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier $EXPECTED_BUNDLE_ID" "$INFO_PLIST"
/usr/libexec/PlistBuddy -c "Set :CFBundleName SonoBusHeadless" "$INFO_PLIST"
/usr/libexec/PlistBuddy -c "Set :CFBundleDisplayName 'SonoBus Headless'" "$INFO_PLIST"
/usr/libexec/PlistBuddy -c "Set :NSMicrophoneUsageDescription 'SonoBus Headless captures microphone audio for live transmission.'" "$INFO_PLIST"
/usr/libexec/PlistBuddy -c "Add :NSLocalNetworkUsageDescription string 'SonoBus Headless connects to SonoBus peers on your local network.'" "$INFO_PLIST" 2>/dev/null || \
    /usr/libexec/PlistBuddy -c "Set :NSLocalNetworkUsageDescription 'SonoBus Headless connects to SonoBus peers on your local network.'" "$INFO_PLIST"

codesign --force --options runtime --timestamp=none --entitlements "$ENTITLEMENTS" --sign "$IDENTITY" "$STAGED_APP"
codesign --verify --deep --strict "$STAGED_APP"

codesign_details="$(codesign -dv --verbose=4 "$STAGED_APP" 2>&1)"
actual_bundle="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$INFO_PLIST")"
actual_identifier="$(printf '%s\n' "$codesign_details" | sed -n 's/^Identifier=//p' | head -1)"
actual_team="$(printf '%s\n' "$codesign_details" | sed -n 's/^TeamIdentifier=//p' | head -1)"
actual_authority="$(printf '%s\n' "$codesign_details" | sed -n 's/^Authority=//p' | head -1)"
actual_authority_chain="$(printf '%s\n' "$codesign_details" | sed -n 's/^Authority=//p' | paste -sd '|' -)"
actual_cdhash="$(printf '%s\n' "$codesign_details" | sed -n 's/^CDHash=//p' | head -1)"
actual_dr="$(codesign -d -r- "$STAGED_APP" 2>&1 | sed -n 's/^designated => //p' | head -1)"
ENTITLEMENTS_READBACK="$STAGE_ROOT/signed-entitlements.plist"
codesign -d --entitlements :- "$STAGED_APP" > "$ENTITLEMENTS_READBACK" 2>/dev/null
actual_audio_input="$(/usr/libexec/PlistBuddy -c 'Print :com.apple.security.device.audio-input' "$ENTITLEMENTS_READBACK" 2>/dev/null || true)"
expected_entitlements="$(plutil -convert json -o - "$ENTITLEMENTS")"
actual_entitlements="$(plutil -convert json -o - "$ENTITLEMENTS_READBACK")"
actual_mic_usage="$(/usr/libexec/PlistBuddy -c 'Print :NSMicrophoneUsageDescription' "$INFO_PLIST")"

if [[ "$actual_bundle" != "$EXPECTED_BUNDLE_ID" || "$actual_identifier" != "$EXPECTED_BUNDLE_ID" || \
      "$actual_team" != "$EXPECTED_TEAM" || "$actual_authority" != "$EXPECTED_AUTHORITY" || \
      "$actual_authority_chain" != "$EXPECTED_AUTHORITY_CHAIN" || "$actual_audio_input" != "true" || \
      "$actual_entitlements" != "$expected_entitlements" || -z "$actual_mic_usage" || \
      "$actual_dr" != "identifier \"$EXPECTED_BUNDLE_ID\" and anchor apple generic and certificate leaf[subject.CN] = \"$EXPECTED_AUTHORITY\" and certificate 1[field.1.2.840.113635.100.6.2.1] /* exists */" ]]; then
    echo "TCC_IDENTITY_CHANGED"
    echo "candidate bundle=$actual_bundle identifier=$actual_identifier team=$actual_team authority=$actual_authority authority_chain=$actual_authority_chain audio_input=$actual_audio_input" >&2
    echo "candidate DR=$actual_dr" >&2
    exit 73
fi

if [[ "$MODE" == "stage" ]]; then
    echo "A_IDENTITY_GATE=PASS"
    echo "BUNDLE_ID=$actual_bundle"
    echo "TEAM_IDENTIFIER=$actual_team"
    echo "AUTHORITY=$actual_authority"
    echo "DESIGNATED_REQUIREMENT=$actual_dr"
    echo "CDHASH=$actual_cdhash"
    echo "AUDIO_INPUT_ENTITLEMENT=$actual_audio_input"
    echo "STAGED_APP=$STAGED_APP"
    exit 0
fi

if [[ -e "$APP_PATH" ]]; then
    installed_details="$(codesign -dv --verbose=4 "$APP_PATH" 2>&1)"
    installed_bundle="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP_PATH/Contents/Info.plist")"
    installed_team="$(printf '%s\n' "$installed_details" | sed -n 's/^TeamIdentifier=//p' | head -1)"
    installed_authority="$(printf '%s\n' "$installed_details" | sed -n 's/^Authority=//p' | head -1)"
    installed_authority_chain="$(printf '%s\n' "$installed_details" | sed -n 's/^Authority=//p' | paste -sd '|' -)"
    installed_dr="$(codesign -d -r- "$APP_PATH" 2>&1 | sed -n 's/^designated => //p' | head -1)"
    installed_entitlements_file="$STAGE_ROOT/installed-entitlements.plist"
    codesign -d --entitlements :- "$APP_PATH" > "$installed_entitlements_file" 2>/dev/null
    installed_audio_input="$(/usr/libexec/PlistBuddy -c 'Print :com.apple.security.device.audio-input' "$installed_entitlements_file" 2>/dev/null || true)"
    installed_entitlements="$(plutil -convert json -o - "$installed_entitlements_file")"
    if [[ "$installed_bundle" != "$actual_bundle" || "$installed_team" != "$actual_team" || \
          "$installed_authority" != "$actual_authority" || "$installed_authority_chain" != "$actual_authority_chain" || \
          "$installed_dr" != "$actual_dr" || "$installed_audio_input" != "$actual_audio_input" || \
          "$installed_entitlements" != "$actual_entitlements" ]]; then
        echo "TCC_IDENTITY_CHANGED"
        echo "installed bundle=$installed_bundle team=$installed_team authority_chain=$installed_authority_chain" >&2
        echo "candidate bundle=$actual_bundle team=$actual_team authority_chain=$actual_authority_chain" >&2
        exit 73
    fi
    old_cdhash="$(printf '%s\n' "$installed_details" | sed -n 's/^CDHash=//p' | head -1)"
else
    old_cdhash="none"
fi

if [[ -n "${SONOBUS_SETUP_XML_SOURCE:-}" ]]; then
    if [[ ! -f "$SONOBUS_SETUP_XML_SOURCE" ]]; then
        echo "Setup XML source not found." >&2
        exit 2
    fi
    mkdir -p "$SETUP_DIR"
    install -m 600 "$SONOBUS_SETUP_XML_SOURCE" "$SETUP_FILE"
elif [[ ! -f "$SETUP_FILE" ]]; then
    echo "No persistent setup XML found; set SONOBUS_SETUP_XML_SOURCE to the verified setup file." >&2
    exit 2
fi

mkdir -p "$AGENT_DIR" "$LOG_DIR"
chmod 700 "$SETUP_DIR" "$LOG_DIR"
AGENT_TMP="$AGENT_FILE.tmp"
rm -f "$AGENT_TMP"
plutil -create xml1 "$AGENT_TMP"
/usr/libexec/PlistBuddy -c "Add :Label string $LABEL" "$AGENT_TMP"
/usr/libexec/PlistBuddy -c 'Add :ProgramArguments array' "$AGENT_TMP"
/usr/libexec/PlistBuddy -c "Add :ProgramArguments:0 string $APP_EXECUTABLE" "$AGENT_TMP"
/usr/libexec/PlistBuddy -c 'Add :ProgramArguments:1 string -q' "$AGENT_TMP"
/usr/libexec/PlistBuddy -c 'Add :ProgramArguments:2 string -d' "$AGENT_TMP"
/usr/libexec/PlistBuddy -c 'Add :ProgramArguments:3 string 192.168.1.4:51440' "$AGENT_TMP"
/usr/libexec/PlistBuddy -c 'Add :ProgramArguments:4 string -l' "$AGENT_TMP"
/usr/libexec/PlistBuddy -c "Add :ProgramArguments:5 string $SETUP_FILE" "$AGENT_TMP"
/usr/libexec/PlistBuddy -c 'Add :AssociatedBundleIdentifiers array' "$AGENT_TMP"
/usr/libexec/PlistBuddy -c "Add :AssociatedBundleIdentifiers:0 string $EXPECTED_BUNDLE_ID" "$AGENT_TMP"
/usr/libexec/PlistBuddy -c 'Add :RunAtLoad bool true' "$AGENT_TMP"
/usr/libexec/PlistBuddy -c 'Add :KeepAlive bool true' "$AGENT_TMP"
/usr/libexec/PlistBuddy -c 'Add :ProcessType string Interactive' "$AGENT_TMP"
/usr/libexec/PlistBuddy -c "Add :WorkingDirectory string $APP_PATH/Contents/MacOS" "$AGENT_TMP"
/usr/libexec/PlistBuddy -c "Add :StandardOutPath string $LOG_DIR/sonobus-headless.stdout.log" "$AGENT_TMP"
/usr/libexec/PlistBuddy -c "Add :StandardErrorPath string $LOG_DIR/sonobus-headless.stderr.log" "$AGENT_TMP"
plutil -lint "$AGENT_TMP" >/dev/null

if [[ "$MODE" == "prepare" ]]; then
    mv -f "$AGENT_TMP" "$AGENT_FILE"
    chmod 644 "$AGENT_FILE"
    echo "A_IDENTITY_GATE=PASS"
    echo "DEPLOY_PREPARED=YES"
    echo "LAUNCHAGENT_FILE=$AGENT_FILE"
    echo "APP_DESTINATION=$APP_PATH"
    echo "SETUP_XML_SHA256=$(shasum -a 256 "$SETUP_FILE" | awk '{print $1}')"
    exit 0
fi

if [[ -e "$APP_PATH" ]]; then
    backup_path="/private/tmp/SonoBusHeadless.predeploy.$(date +%Y%m%d-%H%M%S).app"
    ditto "$APP_PATH" "$backup_path"
    echo "PREVIOUS_APP_BACKUP=$backup_path"
fi

if launchctl print "gui/$(id -u)/$LABEL" >/dev/null 2>&1; then
    launchctl bootout "gui/$(id -u)/$LABEL"
fi
if launchctl print "gui/$(id -u)/$LEGACY_LABEL" >/dev/null 2>&1; then
    launchctl bootout "gui/$(id -u)/$LEGACY_LABEL"
fi
for attempt in {1..20}; do
    active_pids="$(ps -axo pid=,args= | awk -v executable="$APP_EXECUTABLE" '$2 == executable { print $1 }')"
    [[ -z "$active_pids" ]] && break
    sleep 0.25
done
if [[ -n "${active_pids:-}" ]]; then
    echo "DEPLOY_BLOCKED active SonoBusHeadless PID(s): $active_pids" >&2
    exit 74
fi
if [[ -e "$APP_PATH" ]]; then
    rm -rf "$APP_PATH"
fi
ditto "$STAGED_APP" "$APP_PATH"
chmod -R go-w "$APP_PATH"
codesign --verify --deep --strict "$APP_PATH"
mv -f "$AGENT_TMP" "$AGENT_FILE"
chmod 644 "$AGENT_FILE"
launchctl bootstrap "gui/$(id -u)" "$AGENT_FILE"

echo "DEPLOY_OK path=$APP_PATH bundle=$actual_bundle team=$actual_team cdhash=$actual_cdhash previous_cdhash=$old_cdhash"
echo "LAUNCHAGENT_BOOTSTRAPPED=$LABEL"
echo "SETUP_XML_SHA256=$(shasum -a 256 "$SETUP_FILE" | awk '{print $1}')"
