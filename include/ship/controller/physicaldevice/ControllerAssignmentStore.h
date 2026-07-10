#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Ship {

/**
 * @brief Persistent per-port controller enable-list.
 *
 * Owns the "which composite device keys are permitted on each controller port"
 * state and its persistence to the ConsoleVariable store. Composite keys are
 * produced by ConnectedPhysicalDeviceManager and treated here as opaque strings.
 *
 * Two operating modes:
 *
 *  1. Legacy default — active until the user makes any explicit assignment
 *     toggle. All ports have empty enable-lists. Consumers should interpret
 *     this as "port 0 accepts every controller; ports 1..3 accept none" to
 *     match pre-libultraship#2 out-of-box behaviour.
 *  2. Strict — active as soon as any Assign / Unassign / Reset call has fired,
 *     locked in by the persisted `Configured` flag. Per-port enable-lists are
 *     authoritative; devices not in the enable-list are ignored for that port.
 *
 * The store is a pure data + persistence layer; it does not know about SDL
 * instance IDs, joystick paths, or hotplug events.
 *
 * Design doc: Plans/controller_port_persistence_plan.md.
 * Tracker: St3ph3nBr00ks/libultraship#2.
 */
class ControllerAssignmentStore {
  public:
    /** @brief Constructs the store and loads persisted state from CVars. */
    ControllerAssignmentStore();

    // ---------------------------------------------------------------
    // Query
    // ---------------------------------------------------------------

    /**
     * @brief Returns true if the given composite device key is on the
     *        enable-list for the given controller port.
     * @param portIndex Zero-based controller port index.
     * @param deviceKey Composite device key string.
     */
    bool PortHasDeviceKeyAssigned(uint8_t portIndex, const std::string& deviceKey) const;

    /**
     * @brief Returns true if the store is in strict mode (any port has
     *        assignments OR the user-configured flag is set).
     */
    bool AnyPortConfigured() const;

    /**
     * @brief Returns true iff the user has ever explicitly configured any port
     *        during any session past or present.
     */
    bool UserHasConfigured() const {
        return mUserHasConfigured;
    }

    // ---------------------------------------------------------------
    // Mutations
    // ---------------------------------------------------------------

    /**
     * @brief Adds a composite device key to the given port's enable-list.
     *
     * Also flips the sticky mUserHasConfigured flag. Callers should invoke
     * SaveToConfig() to persist. Callers should also invoke whatever downstream
     * refresh their consumer needs (e.g. rebuilding an ignore-list) — this
     * class does not know about downstream consumers.
     *
     * @param portIndex Zero-based controller port index.
     * @param deviceKey Composite device key string (empty strings rejected).
     */
    void AssignDeviceKeyToPort(uint8_t portIndex, const std::string& deviceKey);

    /**
     * @brief Removes a composite device key from the given port's enable-list.
     *
     * Also flips the sticky mUserHasConfigured flag, even if the key wasn't
     * present — the mere fact of a toggle counts as user configuration.
     *
     * @param portIndex Zero-based controller port index.
     * @param deviceKey Composite device key string.
     */
    void UnassignDeviceKeyFromPort(uint8_t portIndex, const std::string& deviceKey);

    /**
     * @brief Clears every port's enable-list AND the user-configured flag.
     *
     * After Reset + SaveToConfig, next launch is indistinguishable from a
     * fresh install (legacy default mode). Persisted CVars are cleared.
     */
    void ResetAllAssignments();

    /**
     * @brief On first user interaction, snapshots the currently connected
     *        devices into port 0's enable-list so a subsequent uncheck-of-one
     *        leaves the others intact.
     *
     * No-op once any port is configured or mUserHasConfigured is set.
     *
     * @param connectedDeviceKeys Composite device keys currently connected;
     *                            supplied by the caller because this class
     *                            does not know about SDL.
     */
    void SeedFromConnectedDevicesIfUnconfigured(const std::vector<std::string>& connectedDeviceKeys);

    // ---------------------------------------------------------------
    // Persistence
    // ---------------------------------------------------------------

    /** @brief Loads state from CVars; called from the constructor. */
    void LoadFromConfig();

    /** @brief Serialises state to CVars and flushes to disk. */
    void SaveToConfig();

  private:
    std::unordered_map<uint8_t, std::unordered_set<std::string>> mEnabledDeviceKeysByPort;
    bool mUserHasConfigured = false;
};

} // namespace Ship
