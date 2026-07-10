#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL2/SDL.h>

namespace Ship {

class ControllerAssignmentStore;

/**
 * @brief Tracks connected SDL game controllers and manages per-port ignore lists.
 *
 * ConnectedPhysicalDeviceManager maintains a live map of SDL game controllers
 * that are currently connected to the system. It also supports per-port ignore
 * lists so that specific controllers can be excluded from a given controller
 * port's input processing.
 *
 * The persistent per-port enable-list (which composite device keys are permitted
 * per port, plus the sticky "user has configured" flag) lives in a separate
 * ControllerAssignmentStore. This class owns SDL device tracking; the store
 * owns policy. Assignment / query methods on this class delegate to the store
 * and rebuild `mIgnoredInstanceIds` as needed.
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
     *
     * Session-only; not persisted. Preferred call path is
     * AssignDeviceKeyToPort / UnassignDeviceKeyFromPort which persists across
     * launches. Kept for backward-compatibility with external consumers and
     * as a fallback when the composite-key cache lookup fails mid-toggle.
     *
     * @param portIndex  Zero-based controller port index.
     * @param instanceId SDL joystick instance ID to ignore.
     */
    void IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId);

    /** @brief Session-only sibling of IgnoreInstanceIdForPort. */
    void UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId);

    /** @brief Handles an SDL device-added event by opening the new controller. */
    void HandlePhysicalDeviceConnect(int32_t sdlDeviceIndex);

    /** @brief Handles an SDL device-removed event by closing the controller. */
    void HandlePhysicalDeviceDisconnect(int32_t sdlJoystickInstanceId);

    /** @brief Re-scans all connected SDL gamepads and rebuilds the internal maps. */
    void RefreshConnectedSDLGamepads();

    // ---------------------------------------------------------------
    // Persistence-backed per-port assignment API (libultraship#2)
    // ---------------------------------------------------------------

    /**
     * @brief Adds a composite device key to a port's persistent enable-list.
     *
     * The composite key combines the SDL joystick GUID with the joystick path
     * (or serial, or name+occurrence-index) so identical-model controllers
     * plugged into different USB ports remain distinguishable across launches.
     * Callers should still invoke SaveAssignmentsToConfig() to flush to disk.
     *
     * @param portIndex Zero-based controller port index.
     * @param deviceKey Composite device key string (typically obtained from
     *                  GetDeviceKeyForInstanceId).
     */
    void AssignDeviceKeyToPort(uint8_t portIndex, const std::string& deviceKey);

    /** @brief Sibling of AssignDeviceKeyToPort for the un-check UI path. */
    void UnassignDeviceKeyFromPort(uint8_t portIndex, const std::string& deviceKey);

    /**
     * @brief Clears every port's persistent enable-list AND the user-configured
     *        flag. After a save, next launch is indistinguishable from a fresh
     *        install.
     */
    void ResetAllAssignments();

    /**
     * @brief Checks whether a composite key is in the given port's enable-list.
     * @param portIndex Zero-based controller port index.
     * @param deviceKey Composite device key string.
     */
    bool PortHasDeviceKeyAssigned(uint8_t portIndex, const std::string& deviceKey);

    /**
     * @brief Returns the composite persistence key for a connected instance ID.
     *
     * Composite key = GUID + `#` + (path OR `S:`+serial OR `N:`+name+`@`+index).
     * Two physically identical controllers plugged into different USB ports
     * produce distinct keys via their paths.
     *
     * @param instanceId SDL joystick instance ID.
     * @return Composite device key string, or empty string if not tracked.
     */
    std::string GetDeviceKeyForInstanceId(int32_t instanceId);

    /** @brief Flushes the underlying assignment store to CVars + disk. */
    void SaveAssignmentsToConfig();

  private:
    /**
     * @brief Rebuilds mIgnoredInstanceIds from the assignment store and the
     *        current connected-device set. Called from RefreshConnectedSDLGamepads
     *        and from any mutation of the store.
     */
    void RebuildIgnoredInstanceIds();

    std::unordered_map<int32_t, SDL_GameController*> mConnectedSDLGamepads;
    std::unordered_map<int32_t, std::string> mConnectedSDLGamepadNames;
    // instanceId → composite device key, populated during RefreshConnectedSDLGamepads
    // so RebuildIgnoredInstanceIds can filter without re-walking SDL.
    std::unordered_map<int32_t, std::string> mConnectedDeviceKeys;
    // Ports whose keys are session-only-ignored; separate from the persistence
    // store so the strict-mode rebuild can OR the two together.
    std::unordered_map<uint8_t, std::unordered_set<int32_t>> mIgnoredInstanceIds;
    // Persistence layer (libultraship#2). Owns per-port enable-lists.
    std::unique_ptr<ControllerAssignmentStore> mAssignmentStore;
};

} // namespace Ship
