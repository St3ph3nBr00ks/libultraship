#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"

#include <spdlog/spdlog.h>
#include <unordered_map>

#include "libultraship/bridge/consolevariablebridge.h"
#include "ship/controller/physicaldevice/ControllerAssignmentStore.h"

namespace {

constexpr uint8_t kMaxControllerPorts = 4;

// Gate the per-Refresh / per-Rebuild diagnostic logs behind a CVar so they
// don't spam default logs at INFO level. Kept accessible for troubleshooting.
constexpr const char* kCVarDebugKey = "gDeveloperTools.ControllerPersistenceDebug";

bool DiagnosticsEnabled() {
    return CVarGetInteger(kCVarDebugKey, 0) != 0;
}

// Build a per-device composite key that identifies a physical controller as
// stably as SDL will allow. Preference order:
//   1. GUID + `#` + SDL_JoystickPath  — stable per USB port on Windows/Linux;
//      distinguishes XInput vs DirectInput enumerations of the same device.
//   2. GUID + `#S:` + SDL_JoystickGetSerial — used when path is empty and the
//      device exposes a serial (many controllers don't).
//   3. GUID + `#N:` + name + `@` + occurrence-index — final fallback for
//      identical-model controllers with no path or serial. Order-of-plug
//      stable but not USB-port stable.
std::string BuildDeviceKey(const std::string& guid, const char* path, const char* serial, const std::string& name,
                           int occurrenceIndex) {
    if (path != nullptr && path[0] != '\0') {
        return guid + "#" + path;
    }
    if (serial != nullptr && serial[0] != '\0') {
        return guid + "#S:" + serial;
    }
    return guid + "#N:" + name + "@" + std::to_string(occurrenceIndex);
}

} // namespace

namespace Ship {

ConnectedPhysicalDeviceManager::ConnectedPhysicalDeviceManager()
    : mAssignmentStore(std::make_unique<ControllerAssignmentStore>()) {
}

ConnectedPhysicalDeviceManager::~ConnectedPhysicalDeviceManager() = default;

// ---------------------------------------------------------------------------
// SDL device tracking + ignore-list queries
// ---------------------------------------------------------------------------

std::unordered_map<int32_t, SDL_GameController*>
ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadsForPort(uint8_t portIndex) {
    std::unordered_map<int32_t, SDL_GameController*> result;
    for (const auto& [instanceId, gamepad] : mConnectedSDLGamepads) {
        if (!PortIsIgnoringInstanceId(portIndex, instanceId)) {
            result[instanceId] = gamepad;
        }
    }
    return result;
}

std::unordered_map<int32_t, std::string> ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadNames() {
    return mConnectedSDLGamepadNames;
}

std::unordered_set<int32_t> ConnectedPhysicalDeviceManager::GetIgnoredInstanceIdsForPort(uint8_t portIndex) {
    return mIgnoredInstanceIds[portIndex];
}

bool ConnectedPhysicalDeviceManager::PortIsIgnoringInstanceId(uint8_t portIndex, int32_t instanceId) {
    return GetIgnoredInstanceIdsForPort(portIndex).contains(instanceId);
}

void ConnectedPhysicalDeviceManager::IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {
    mIgnoredInstanceIds[portIndex].insert(instanceId);
}

void ConnectedPhysicalDeviceManager::UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {
    mIgnoredInstanceIds[portIndex].erase(instanceId);
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceConnect(int32_t /* sdlDeviceIndex */) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceDisconnect(int32_t /* sdlJoystickInstanceId */) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::RefreshConnectedSDLGamepads() {
    mConnectedSDLGamepads.clear();
    mConnectedSDLGamepadNames.clear();
    mConnectedDeviceKeys.clear();
    static SDL_JoystickGUID sZeroGuid;

    // Occurrence-index counter — used only when neither path nor serial is
    // available and we have to fall back to name+index.
    std::unordered_map<std::string, int> occurrenceByGuidName;

    for (int32_t i = 0; i < SDL_NumJoysticks(); i++) {

        SDL_JoystickGUID deviceGUID = SDL_JoystickGetDeviceGUID(i);
        if (SDL_memcmp(&deviceGUID, &sZeroGuid, sizeof(deviceGUID)) == 0) {
            SPDLOG_WARN(
                "Calling SDL JoystickGetDeviceGUID with index ({:d}) returned zero GUID. This is likely due to an "
                "invalid index. Refer to https://wiki.libsdl.org/SDL2/SDL_JoystickGetDeviceGUID for more information.",
                i);
            continue;
        }

        char deviceGuidCStr[33] = "";
        SDL_JoystickGetGUIDString(deviceGUID, deviceGuidCStr, sizeof(deviceGuidCStr));

        if (!SDL_IsGameController(i)) {
            SPDLOG_WARN("SDL Joystick (GUID: {}) not recognized as gamepad."
                        "This is likely due to a missing mapping string in gamecontrollerdb.txt."
                        "Refer to https://github.com/mdqinc/SDL_GameControllerDB for more information.",
                        deviceGuidCStr);
            continue;
        }

        auto gamepad = SDL_GameControllerOpen(i);
        if (gamepad == nullptr) {
            SPDLOG_ERROR("SDL GameControllerOpen error (GUID: {}): {}", deviceGuidCStr, SDL_GetError());
            continue;
        }

        SDL_Joystick* joystick = SDL_GameControllerGetJoystick(gamepad);
        auto instanceId = SDL_JoystickInstanceID(joystick);
        if (instanceId < 0) {
            SPDLOG_ERROR("SDL JoystickInstanceID error (GUID: {}): {}", deviceGuidCStr, SDL_GetError());
            continue;
        }

        std::string gamepadName;
        auto name = SDL_GameControllerName(gamepad);
        if (name == nullptr) {
            gamepadName = deviceGuidCStr;
            SPDLOG_WARN("SDL_GameControllerName returned null. Setting name to GUID \"{}\" instead.", gamepadName);
        } else {
            gamepadName = name;
        }

        // Resolve stable-across-launches identifiers for the composite device key.
        const char* joystickPath = SDL_JoystickPath(joystick);          // SDL 2.24+
        const char* joystickSerial = SDL_JoystickGetSerial(joystick);   // SDL 2.0.14+
        int occurrenceIdx = occurrenceByGuidName[std::string(deviceGuidCStr) + "|" + gamepadName]++;
        std::string deviceKey = BuildDeviceKey(deviceGuidCStr, joystickPath, joystickSerial, gamepadName,
                                               occurrenceIdx);

        if (DiagnosticsEnabled()) {
            SPDLOG_INFO(
                "[ControllerPersistence] Device instanceId={} name=\"{}\" guid={} path=\"{}\" serial=\"{}\" key=\"{}\"",
                instanceId, gamepadName, deviceGuidCStr, joystickPath ? joystickPath : "",
                joystickSerial ? joystickSerial : "", deviceKey);
        }

        mConnectedSDLGamepads[instanceId] = gamepad;
        mConnectedSDLGamepadNames[instanceId] = gamepadName;
        mConnectedDeviceKeys[instanceId] = deviceKey;
    }

    RebuildIgnoredInstanceIds();
}

// ---------------------------------------------------------------------------
// Persistence-backed assignment API — delegates to ControllerAssignmentStore
// ---------------------------------------------------------------------------

void ConnectedPhysicalDeviceManager::AssignDeviceKeyToPort(uint8_t portIndex, const std::string& deviceKey) {
    if (deviceKey.empty()) {
        if (DiagnosticsEnabled()) {
            SPDLOG_INFO("[ControllerPersistence] AssignDeviceKeyToPort port={} key=(empty) — rejected", portIndex);
        }
        return;
    }
    // Seed legacy state before the write so a subsequent uncheck-of-one leaves
    // the currently-displayed defaults intact. Requires the list of connected
    // device keys — the store is decoupled from SDL.
    std::vector<std::string> connectedKeys;
    connectedKeys.reserve(mConnectedDeviceKeys.size());
    for (const auto& [instanceId, key] : mConnectedDeviceKeys) {
        connectedKeys.push_back(key);
    }
    mAssignmentStore->SeedFromConnectedDevicesIfUnconfigured(connectedKeys);
    mAssignmentStore->AssignDeviceKeyToPort(portIndex, deviceKey);
    if (DiagnosticsEnabled()) {
        SPDLOG_INFO("[ControllerPersistence] AssignDeviceKeyToPort port={} key=\"{}\"", portIndex, deviceKey);
    }
    RebuildIgnoredInstanceIds();
}

void ConnectedPhysicalDeviceManager::UnassignDeviceKeyFromPort(uint8_t portIndex, const std::string& deviceKey) {
    std::vector<std::string> connectedKeys;
    connectedKeys.reserve(mConnectedDeviceKeys.size());
    for (const auto& [instanceId, key] : mConnectedDeviceKeys) {
        connectedKeys.push_back(key);
    }
    mAssignmentStore->SeedFromConnectedDevicesIfUnconfigured(connectedKeys);
    mAssignmentStore->UnassignDeviceKeyFromPort(portIndex, deviceKey);
    if (DiagnosticsEnabled()) {
        SPDLOG_INFO("[ControllerPersistence] UnassignDeviceKeyFromPort port={} key=\"{}\"", portIndex, deviceKey);
    }
    RebuildIgnoredInstanceIds();
}

void ConnectedPhysicalDeviceManager::ResetAllAssignments() {
    mAssignmentStore->ResetAllAssignments();
    mAssignmentStore->SaveToConfig();
    if (DiagnosticsEnabled()) {
        SPDLOG_INFO("[ControllerPersistence] ResetAllAssignments — cleared all port enable-lists and configured flag");
    }
    RebuildIgnoredInstanceIds();
}

bool ConnectedPhysicalDeviceManager::PortHasDeviceKeyAssigned(uint8_t portIndex, const std::string& deviceKey) {
    return mAssignmentStore->PortHasDeviceKeyAssigned(portIndex, deviceKey);
}

std::string ConnectedPhysicalDeviceManager::GetDeviceKeyForInstanceId(int32_t instanceId) {
    auto it = mConnectedDeviceKeys.find(instanceId);
    if (it == mConnectedDeviceKeys.end()) {
        return "";
    }
    return it->second;
}

void ConnectedPhysicalDeviceManager::SaveAssignmentsToConfig() {
    mAssignmentStore->SaveToConfig();
}

// ---------------------------------------------------------------------------
// Internal — derive mIgnoredInstanceIds from store + connected device set
// ---------------------------------------------------------------------------

void ConnectedPhysicalDeviceManager::RebuildIgnoredInstanceIds() {
    // Preserve any session-only ignores set via IgnoreInstanceIdForPort — we only
    // clear the persistence-derived portion. In practice both maps live in
    // mIgnoredInstanceIds and are indistinguishable, so we clear all here and
    // let the session-only path re-set as needed on subsequent toggles.
    mIgnoredInstanceIds.clear();

    const bool strict = mAssignmentStore->AnyPortConfigured();
    if (DiagnosticsEnabled()) {
        SPDLOG_INFO("[ControllerPersistence] RebuildIgnoredInstanceIds strict={} connectedDevices={} userConfigured={}",
                    strict, mConnectedDeviceKeys.size(), mAssignmentStore->UserHasConfigured());
    }

    for (const auto& [instanceId, deviceKey] : mConnectedDeviceKeys) {
        for (uint8_t port = 0; port < kMaxControllerPorts; port++) {
            bool ignore;
            if (strict) {
                ignore = !mAssignmentStore->PortHasDeviceKeyAssigned(port, deviceKey);
            } else {
                // Legacy default (never configured): port 0 accepts every
                // controller; ports 1..3 reject every controller. Matches
                // pre-libultraship#2 behaviour so a fresh config lands users
                // in the familiar out-of-box state.
                ignore = (port != 0);
            }
            if (ignore) {
                mIgnoredInstanceIds[port].insert(instanceId);
            }
        }
    }
}

} // namespace Ship
