#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <SDL2/SDL.h>

namespace Ship {

/**
 * @brief Tracks connected SDL game controllers and manages per-port ignore lists.
 *
 * ConnectedPhysicalDeviceManager maintains a live map of SDL game controllers that
 * are currently connected to the system. It also supports per-port ignore lists so
 * that specific controllers can be excluded from a given controller port's input
 * processing.
 */
class ConnectedPhysicalDeviceManager {
  public:
    /** @brief Constructs the manager and performs an initial scan of connected gamepads. */
    ConnectedPhysicalDeviceManager();
    ~ConnectedPhysicalDeviceManager();

    /**
     * @brief Returns the connected SDL gamepads available for the given port.
     *
     * Gamepads on the port's ignore list are excluded from the result.
     *
     * @param portIndex Zero-based controller port index.
     * @return Map of SDL joystick instance ID to SDL_GameController pointer.
     */
    std::unordered_map<int32_t, SDL_GameController*> GetConnectedSDLGamepadsForPort(uint8_t portIndex);

    /**
     * @brief Returns the display names of all connected SDL gamepads.
     * @return Map of SDL joystick instance ID to human-readable gamepad name.
     */
    std::unordered_map<int32_t, std::string> GetConnectedSDLGamepadNames();

    /**
     * @brief Returns the set of SDL joystick instance IDs ignored for a port.
     * @param portIndex Zero-based controller port index.
     * @return Set of ignored joystick instance IDs.
     */
    std::unordered_set<int32_t> GetIgnoredInstanceIdsForPort(uint8_t portIndex);

    /**
     * @brief Checks whether a specific joystick instance is being ignored on a port.
     * @param portIndex  Zero-based controller port index.
     * @param instanceId SDL joystick instance ID.
     * @return true if the instance is ignored for the given port.
     */
    bool PortIsIgnoringInstanceId(uint8_t portIndex, int32_t instanceId);

    /**
     * @brief Adds a joystick instance to a port's ignore list.
     * @param portIndex  Zero-based controller port index.
     * @param instanceId SDL joystick instance ID to ignore.
     */
    void IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId);

    /**
     * @brief Removes a joystick instance from a port's ignore list.
     * @param portIndex  Zero-based controller port index.
     * @param instanceId SDL joystick instance ID to stop ignoring.
     */
    void UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId);

    /**
     * @brief Handles an SDL device-added event by opening the new controller.
     * @param sdlDeviceIndex SDL device index from the SDL_CONTROLLERDEVICEADDED event.
     */
    void HandlePhysicalDeviceConnect(int32_t sdlDeviceIndex);

    /**
     * @brief Handles an SDL device-removed event by closing the controller.
     * @param sdlJoystickInstanceId SDL joystick instance ID from the SDL_CONTROLLERDEVICEREMOVED event.
     */
    void HandlePhysicalDeviceDisconnect(int32_t sdlJoystickInstanceId);

    /** @brief Re-scans all connected SDL gamepads and rebuilds the internal maps. */
    void RefreshConnectedSDLGamepads();

    /**
     * @brief Adds a controller (by SDL joystick GUID) to a port's enable-list and persists.
     *
     * Once ANY port has one or more assignments, strict mode is active for all ports:
     * only enable-listed GUIDs are permitted per port. When every port is empty, the
     * legacy default applies (port 0 accepts everything; ports 1-3 accept nothing).
     *
     * @param portIndex Zero-based controller port index.
     * @param guid      SDL joystick GUID string (32-char hex, as returned by
     *                  SDL_JoystickGetGUIDString).
     */
    void AssignGuidToPort(uint8_t portIndex, const std::string& guid);

    /**
     * @brief Removes a controller (by SDL joystick GUID) from a port's enable-list and persists.
     * @param portIndex Zero-based controller port index.
     * @param guid      SDL joystick GUID string.
     */
    void UnassignGuidFromPort(uint8_t portIndex, const std::string& guid);

    /**
     * @brief Checks whether a GUID is in the given port's enable-list.
     * @param portIndex Zero-based controller port index.
     * @param guid      SDL joystick GUID string.
     * @return true if the port explicitly permits this GUID.
     */
    bool PortHasGuidAssigned(uint8_t portIndex, const std::string& guid);

    /**
     * @brief Returns the SDL joystick GUID string for a connected instance ID.
     *
     * The GUID is captured during RefreshConnectedSDLGamepads and cached, so this
     * avoids the re-query-SDL failure modes that hit `SDL_JoystickFromInstanceID`
     * on some builds.
     *
     * @param instanceId SDL joystick instance ID.
     * @return GUID string (32-char hex), or empty string if the instance is not
     *         currently tracked as a connected gamepad.
     */
    std::string GetGuidForInstanceId(int32_t instanceId);

    /**
     * @brief Returns the composite persistence key for a connected instance ID.
     *
     * Combines the GUID with SDL_JoystickPath (preferred), joystick serial, or
     * a name+occurrence-index fallback. Two physically identical controllers
     * plugged into different USB ports produce distinct keys via their paths.
     *
     * @param instanceId SDL joystick instance ID.
     * @return Composite device key string, or empty string if the instance is
     *         not currently tracked.
     */
    std::string GetDeviceKeyForInstanceId(int32_t instanceId);

    /** @brief Loads per-port GUID enable-lists from persisted CVars. */
    void LoadAssignmentsFromConfig();

    /** @brief Saves per-port GUID enable-lists to persisted CVars and flushes to disk. */
    void SaveAssignmentsToConfig();

    /**
     * @brief Rebuilds mIgnoredInstanceIds from mEnabledGuidsByPort and the current
     *        connected-device set.
     *
     * Called by RefreshConnectedSDLGamepads() and by Assign/UnassignGuidToPort().
     */
    void RebuildIgnoredInstanceIds();

  private:
    /** @brief Returns true if any port has one or more enable-list entries. */
    bool AnyPortConfigured() const;

    /**
     * @brief On first transition out of legacy-default mode, snapshot the currently
     *        DISPLAYED port-0 enable-list (every connected controller) into
     *        mEnabledGuidsByPort[0] so a subsequent uncheck removes only the intended
     *        controller instead of collapsing port 0 to empty.
     *
     * No-op once any port is already configured.
     */
    void SeedFromLegacyDisplayedStateIfUnconfigured();

    std::unordered_map<int32_t, SDL_GameController*> mConnectedSDLGamepads;
    std::unordered_map<int32_t, std::string> mConnectedSDLGamepadNames;
    std::unordered_map<uint8_t, std::unordered_set<int32_t>> mIgnoredInstanceIds;

    // Persistence layer (Plans/controller_port_persistence_plan.md, libultraship#2).
    // Enable-list of composite device keys permitted per controller port.
    // Composite key = GUID + `#` + path (or serial or name+index — see BuildDeviceKey).
    // Sourced from gControllers.PortAssignments.Port{0..3} CVars.
    std::unordered_map<uint8_t, std::unordered_set<std::string>> mEnabledDeviceKeysByPort;
    // instanceId → GUID string, populated during RefreshConnectedSDLGamepads.
    std::unordered_map<int32_t, std::string> mConnectedGuids;
    // instanceId → composite device key, populated during RefreshConnectedSDLGamepads
    // so RebuildIgnoredInstanceIds can filter without re-walking SDL.
    std::unordered_map<int32_t, std::string> mConnectedDeviceKeys;
    // Sticky "user has taken ownership of controller assignments" flag. Sourced from
    // gControllers.PortAssignments.Configured. Once set, we never fall back to the
    // legacy-default (port-0-accepts-all / ports-1..3-accept-none) rule.
    bool mUserHasConfigured = false;
};
} // namespace Ship
