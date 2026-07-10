#include "ship/controller/physicaldevice/ControllerAssignmentStore.h"

#include <sstream>

#include "libultraship/bridge/consolevariablebridge.h"

namespace {

constexpr uint8_t kMaxControllerPorts = 4;
constexpr const char* kCVarKeyConfigured = "gControllers.PortAssignments.Configured";

std::string CVarKeyForPort(uint8_t portIndex) {
    return "gControllers.PortAssignments.Port" + std::to_string(portIndex);
}

// Composite device keys may contain USB path characters — we use `;` as the
// between-key delimiter for robustness (Windows HID paths use `#` internally).
constexpr char kBetweenKeyDelimiter = ';';

} // namespace

namespace Ship {

ControllerAssignmentStore::ControllerAssignmentStore() {
    LoadFromConfig();
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

bool ControllerAssignmentStore::PortHasDeviceKeyAssigned(uint8_t portIndex, const std::string& deviceKey) const {
    auto it = mEnabledDeviceKeysByPort.find(portIndex);
    return it != mEnabledDeviceKeysByPort.end() && it->second.contains(deviceKey);
}

bool ControllerAssignmentStore::AnyPortConfigured() const {
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

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

void ControllerAssignmentStore::AssignDeviceKeyToPort(uint8_t portIndex, const std::string& deviceKey) {
    if (deviceKey.empty()) {
        return;
    }
    mEnabledDeviceKeysByPort[portIndex].insert(deviceKey);
    mUserHasConfigured = true;
}

void ControllerAssignmentStore::UnassignDeviceKeyFromPort(uint8_t portIndex, const std::string& deviceKey) {
    mUserHasConfigured = true;
    auto it = mEnabledDeviceKeysByPort.find(portIndex);
    if (it == mEnabledDeviceKeysByPort.end()) {
        return;
    }
    it->second.erase(deviceKey);
}

void ControllerAssignmentStore::ResetAllAssignments() {
    mEnabledDeviceKeysByPort.clear();
    mUserHasConfigured = false;
}

void ControllerAssignmentStore::SeedFromConnectedDevicesIfUnconfigured(
    const std::vector<std::string>& connectedDeviceKeys) {
    if (AnyPortConfigured()) {
        return;
    }
    // Legacy default UI state: port 0 shows every connected controller as
    // "checked" (enabled); ports 1..3 show none. Snapshot port 0 so a
    // subsequent uncheck-of-one leaves the other port-0 controllers intact.
    for (const auto& key : connectedDeviceKeys) {
        if (!key.empty()) {
            mEnabledDeviceKeysByPort[0].insert(key);
        }
    }
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void ControllerAssignmentStore::LoadFromConfig() {
    mEnabledDeviceKeysByPort.clear();
    mUserHasConfigured = (CVarGetInteger(kCVarKeyConfigured, 0) != 0);
    for (uint8_t port = 0; port < kMaxControllerPorts; port++) {
        auto cvarKey = CVarKeyForPort(port);
        const char* raw = CVarGetString(cvarKey.c_str(), "");
        if (raw == nullptr || raw[0] == '\0') {
            continue;
        }
        std::stringstream ss(raw);
        std::string deviceKey;
        while (std::getline(ss, deviceKey, kBetweenKeyDelimiter)) {
            if (!deviceKey.empty()) {
                mEnabledDeviceKeysByPort[port].insert(deviceKey);
            }
        }
    }
}

void ControllerAssignmentStore::SaveToConfig() {
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
                joined.push_back(kBetweenKeyDelimiter);
            }
            joined.append(deviceKey);
        }
        CVarSetString(cvarKey.c_str(), joined.c_str());
    }
    CVarSave();
}

} // namespace Ship
