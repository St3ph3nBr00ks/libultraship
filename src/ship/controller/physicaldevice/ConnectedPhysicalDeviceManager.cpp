#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <unordered_map>
#include "libultraship/bridge/consolevariablebridge.h"

namespace {
constexpr uint8_t kMaxControllerPorts = 4;
constexpr const char* kCVarKeyConfigured = "gControllers.PortAssignments.Configured";

std::string CVarKeyForPort(uint8_t portIndex) {
    return "gControllers.PortAssignments.Port" + std::to_string(portIndex);
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
ConnectedPhysicalDeviceManager::ConnectedPhysicalDeviceManager() {
    LoadAssignmentsFromConfig();
}

ConnectedPhysicalDeviceManager::~ConnectedPhysicalDeviceManager() {
}

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

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceConnect(int32_t sdlDeviceIndex) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceDisconnect(int32_t sdlJoystickInstanceId) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::RefreshConnectedSDLGamepads() {
    mConnectedSDLGamepads.clear();
    mConnectedSDLGamepadNames.clear();
    mConnectedGuids.clear();
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

        SPDLOG_INFO("[ControllerPersistence] Device instanceId={} name=\"{}\" guid={} path=\"{}\" serial=\"{}\" key=\"{}\"",
                    instanceId, gamepadName, deviceGuidCStr,
                    joystickPath ? joystickPath : "",
                    joystickSerial ? joystickSerial : "",
                    deviceKey);

        mConnectedSDLGamepads[instanceId] = gamepad;
        mConnectedSDLGamepadNames[instanceId] = gamepadName;
        mConnectedGuids[instanceId] = deviceGuidCStr;
        mConnectedDeviceKeys[instanceId] = deviceKey;
    }

    RebuildIgnoredInstanceIds();
}

// -----------------------------------------------------------------------------
// Per-port GUID persistence (Plans/controller_port_persistence_plan.md,
// libultraship#2).
// -----------------------------------------------------------------------------

bool ConnectedPhysicalDeviceManager::AnyPortConfigured() const {
    if (mUserHasConfigured) {
        return true;
    }
    for (const auto& [port, keys] : mEnabledDeviceKeysByPort) {
        if (!keys.empty()) {
            return true;
        }
    }
    return false;
}

void ConnectedPhysicalDeviceManager::SeedFromLegacyDisplayedStateIfUnconfigured() {
    if (AnyPortConfigured()) {
        return;
    }
    // Legacy default UI state: port 0 shows every connected controller as
    // "checked" (enabled); ports 1..3 show none. Snapshot port 0 so a subsequent
    // uncheck-of-one leaves the other port-0 controllers intact.
    for (const auto& [instanceId, key] : mConnectedDeviceKeys) {
        mEnabledDeviceKeysByPort[0].insert(key);
    }
}

void ConnectedPhysicalDeviceManager::AssignGuidToPort(uint8_t portIndex, const std::string& deviceKey) {
    if (deviceKey.empty()) {
        SPDLOG_INFO("[ControllerPersistence] AssignGuidToPort port={} key=(empty) — rejected", portIndex);
        return;
    }
    SeedFromLegacyDisplayedStateIfUnconfigured();
    mEnabledDeviceKeysByPort[portIndex].insert(deviceKey);
    mUserHasConfigured = true;
    SPDLOG_INFO("[ControllerPersistence] AssignGuidToPort port={} key=\"{}\" portSize={}", portIndex, deviceKey,
                mEnabledDeviceKeysByPort[portIndex].size());
    RebuildIgnoredInstanceIds();
}

void ConnectedPhysicalDeviceManager::UnassignGuidFromPort(uint8_t portIndex, const std::string& deviceKey) {
    SeedFromLegacyDisplayedStateIfUnconfigured();
    auto it = mEnabledDeviceKeysByPort.find(portIndex);
    if (it == mEnabledDeviceKeysByPort.end()) {
        // Even a "port had nothing to unassign" toggle counts as user configuring,
        // so the sticky flag flips.
        mUserHasConfigured = true;
        SPDLOG_INFO("[ControllerPersistence] UnassignGuidFromPort port={} key=\"{}\" — port not in map", portIndex,
                    deviceKey);
        return;
    }
    it->second.erase(deviceKey);
    mUserHasConfigured = true;
    SPDLOG_INFO("[ControllerPersistence] UnassignGuidFromPort port={} key=\"{}\" portSize={}", portIndex, deviceKey,
                it->second.size());
    RebuildIgnoredInstanceIds();
}

bool ConnectedPhysicalDeviceManager::PortHasGuidAssigned(uint8_t portIndex, const std::string& deviceKey) {
    auto it = mEnabledDeviceKeysByPort.find(portIndex);
    return it != mEnabledDeviceKeysByPort.end() && it->second.contains(deviceKey);
}

std::string ConnectedPhysicalDeviceManager::GetGuidForInstanceId(int32_t instanceId) {
    auto it = mConnectedGuids.find(instanceId);
    if (it == mConnectedGuids.end()) {
        return "";
    }
    return it->second;
}

std::string ConnectedPhysicalDeviceManager::GetDeviceKeyForInstanceId(int32_t instanceId) {
    auto it = mConnectedDeviceKeys.find(instanceId);
    if (it == mConnectedDeviceKeys.end()) {
        return "";
    }
    return it->second;
}

void ConnectedPhysicalDeviceManager::LoadAssignmentsFromConfig() {
    mEnabledDeviceKeysByPort.clear();
    mUserHasConfigured = (CVarGetInteger(kCVarKeyConfigured, 0) != 0);
    for (uint8_t port = 0; port < kMaxControllerPorts; port++) {
        auto cvarKey = CVarKeyForPort(port);
        const char* raw = CVarGetString(cvarKey.c_str(), "");
        if (raw == nullptr || raw[0] == '\0') {
            continue;
        }
        // Composite device keys contain the GUID separator `#` and possibly USB
        // paths with `,` inside (Linux paths don't, Windows paths shouldn't, but
        // if a future path contained a comma it would collide with our delimiter).
        // Use `;` as the between-key delimiter for robustness.
        std::stringstream ss(raw);
        std::string deviceKey;
        while (std::getline(ss, deviceKey, ';')) {
            if (!deviceKey.empty()) {
                mEnabledDeviceKeysByPort[port].insert(deviceKey);
            }
        }
    }
}

void ConnectedPhysicalDeviceManager::SaveAssignmentsToConfig() {
    CVarSetInteger(kCVarKeyConfigured, mUserHasConfigured ? 1 : 0);
    for (uint8_t port = 0; port < kMaxControllerPorts; port++) {
        auto cvarKey = CVarKeyForPort(port);
        auto it = mEnabledDeviceKeysByPort.find(port);
        if (it == mEnabledDeviceKeysByPort.end() || it->second.empty()) {
            CVarClear(cvarKey.c_str());
            continue;
        }
        std::string joined;
        for (const auto& deviceKey : it->second) {
            if (!joined.empty()) {
                joined.push_back(';');
            }
            joined.append(deviceKey);
        }
        CVarSetString(cvarKey.c_str(), joined.c_str());
    }
    CVarSave();
}

void ConnectedPhysicalDeviceManager::RebuildIgnoredInstanceIds() {
    mIgnoredInstanceIds.clear();

    const bool strict = AnyPortConfigured();
    SPDLOG_INFO("[ControllerPersistence] RebuildIgnoredInstanceIds strict={} connectedDevices={} userConfigured={}",
                strict, mConnectedDeviceKeys.size(), mUserHasConfigured);

    for (const auto& [instanceId, deviceKey] : mConnectedDeviceKeys) {
        for (uint8_t port = 0; port < kMaxControllerPorts; port++) {
            bool ignore;
            if (strict) {
                // Strict mode: only enable-listed device keys are permitted per port.
                auto it = mEnabledDeviceKeysByPort.find(port);
                ignore = (it == mEnabledDeviceKeysByPort.end() || !it->second.contains(deviceKey));
            } else {
                // Legacy default (never configured): port 0 accepts every controller;
                // ports 1..3 reject every controller. Matches pre-libultraship#2 behaviour.
                ignore = (port != 0);
            }
            if (ignore) {
                mIgnoredInstanceIds[port].insert(instanceId);
            }
        }
    }
}
} // namespace Ship
