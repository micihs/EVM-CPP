#pragma once

#include <evmc/evmc.h>
#include <evmc/utils.h>

namespace evm::advanced
{
struct AdvancedExecutionState;
struct AdvancedCodeAnalysis;

EVMC_EXPORT evmc_result execute(
    AdvancedExecutionState& state, const AdvancedCodeAnalysis& analysis) noexcept;
//should be EVMC compatible
evmc_result execute(evmc_vm* vm, const evmc_host_interface* host, evmc_host_context* ctx,
    evmc_revision rev, const evmc_message* msg, const uint8_t* code, size_t code_size) noexcept;
}