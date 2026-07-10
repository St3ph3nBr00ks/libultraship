#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include "libultraship/bridge/consolevariablebridge.h"

namespace {
constexpr uint8_t kMaxControllerPorts = 4;

std::string CVarKeyForPort(uint8_t portIndex) {
    return "gControllers.PortAssignments.Port" + std::to_string(portIndex);
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
    static SDL_JoystickGUID sZeroGuid;

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

        auto instanceId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad));
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

        mConnectedSDLGamepads[instanceId] = gamepad;
        mConnectedSDLGamepadNames[instanceId] = gamepadName;
        mConnectedGuids[instanceId] = deviceGuidCStr;
    }

    RebuildIgnoredInstanceIds();
}

// -----------------------------------------------------------------------------
// Per-port GUID persistence (Plans/controller_port_persistence_plan.md,
// libultraship#2).
// -----------------------------------------------------------------------------

bool ConnectedPhysicalDeviceManager::AnyPortConfigured() const {
    for (const auto& [port, guids] : mEnabledGuidsByPort) {
        if (!guids.empty()) {
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
    for (const auto& [instanceId, guid] : mConnectedGuids) {
        mEnabledGuidsByPort[0].insert(guid);
    }
}

void ConnectedPhysicalDeviceManager::AssignGuidToPort(uint8_t portIndex, const std::string& guid) {
    if (guid.empty()) {
        return;
    }
    SeedFromLegacyDisplayedStateIfUnconfigured();
    mEnabledGuidsByPort[portIndex].insert(guid);
    RebuildIgnoredInstanceIds();
}

void ConnectedPhysicalDeviceManager::UnassignGuidFromPort(uint8_t portIndex, const std::string& guid) {
    SeedFromLegacyDisplayedStateIfUnconfigured();
    auto it = mEnabledGuidsByPort.find(portIndex);
    if (it == mEnabledGuidsByPort.end()) {
        return;
    }
    it->second.erase(guid);
    RebuildIgnoredInstanceIds();
}

bool ConnectedPhysicalDeviceManager::PortHasGuidAssigned(uint8_t portIndex, const std::string& guid) {
    auto it = mEnabledGuidsByPort.find(portIndex);
    return it != mEnabledGuidsByPort.end() && it->second.contains(guid);
}

void ConnectedPhysicalDeviceManager::LoadAssignmentsFromConfig() {
    mEnabledGuidsByPort.clear();
    for (uint8_t port = 0; port < kMaxControllerPorts; port++) {
        auto key = CVarKeyForPort(port);
        const char* raw = CVarGetString(key.c_str(), "");
        if (raw == nullptr || raw[0] == '\0') {
            continue;
        }
        std::stringstream ss(raw);
        std::string guid;
        while (std::getline(ss, guid, ',')) {
            if (!guid.empty()) {
                mEnabledGuidsByPort[port].insert(guid);
            }
        }
    }
}

void ConnectedPhysicalDeviceManager::SaveAssignmentsToConfig() {
    for (uint8_t port = 0; port < kMaxControllerPorts; port++) {
        auto key = CVarKeyForPort(port);
        auto it = mEnabledGuidsByPort.find(port);
        if (it == mEnabledGuidsByPort.end() || it->second.empty()) {
            CVarClear(key.c_str());
            continue;
        }
        std::string joined;
        for (const auto& guid : it->second) {
            if (!joined.empty()) {
                joined.push_back(',');
            }
            joined.append(guid);
        }
        CVarSetString(key.c_str(), joined.c_str());
    }
    CVarSave();
}

void ConnectedPhysicalDeviceManager::RebuildIgnoredInstanceIds() {
    mIgnoredInstanceIds.clear();

    const bool strict = AnyPortConfigured();

    for (const auto& [instanceId, guid] : mConnectedGuids) {
        for (uint8_t port = 0; port < kMaxControllerPorts; port++) {
            bool ignore;
            if (strict) {
                // Strict mode: only enable-listed GUIDs are permitted per port.
                auto it = mEnabledGuidsByPort.find(port);
                ignore = (it == mEnabledGuidsByPort.end() || !it->second.contains(guid));
            } else {
                // Legacy default (no assignments configured): port 0 accepts every
                // controller; ports 1..3 reject every controller. Matches pre-libultraship#2
                // behaviour so a fresh config lands users in the familiar out-of-box state.
                ignore = (port != 0);
            }
            if (ignore) {
                mIgnoredInstanceIds[port].insert(instanceId);
            }
        }
    }
}
} // namespace Ship
