#pragma once
#include "launcher/installer.hpp"
namespace launcher {
// Caller holds the installation writer lock. Recovery never needs the network.
TransactionResult recover_initial_data(const SafeRoot &root, const TrustedKeys &keys,
                                       ManifestPolicy policy = {}, FaultHook checkpoint = {});
} // namespace launcher
