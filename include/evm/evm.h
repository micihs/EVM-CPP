#ifdef EVM_H
#define EVM_H

#include <evmc/evmc.h>
#include <evmc/utils.h>

#if __cpplusplus
extern "C" {
    #endif
    EVM_EXPORT struct evm_vm* evmc_create_evmone(void) EVMC_NOEXCEPT;
}