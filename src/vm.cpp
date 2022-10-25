#include "vm.hpp"
#include "advanced_execution.hpp"
#include "baseline.hpp"
#include <evm/evm.h>
#include <cassert>
#include <iostream>

namespace evm
{
namespace
{
void destroy(evmc_vm* vm) noexcept
{
    assert(vm != nullptr);
    delete static_cast<VM*>(vm);
}

constexpr evmc_capabilities_flagset get_capabilities(evmc_vm* /*vm*/) noexcept
{
    return EVMC_CAPABILITY_EVM1;
}

evmc_set_option_result set_option(evmc_vm* c_vm, char const* c_name, char const* c_value) noexcept
{
    const auto name = (c_name != nullptr) ? std::string_view{c_name} : std::string_view{};
    const auto value = (c_value != nullptr) ? std::string_view{c_value} : std::string_view{};
    auto& vm = *static_cast<VM*>(c_vm);
    if (name == "advanced")
    {
        c_vm->execute = evm::advanced::execute;
        return EVMC_SET_OPTION_SUCCESS;
    }
    else if (name == "cgoto")
    {
#if EVM_CGOTO_SUPPORTED
        if (value == "no")
        {
            vm.cgoto = false;
            return EVMC_SET_OPTION_SUCCESS;
        }
        return EVMC_SET_OPTION_INVALID_VALUE;
#else
        return EVMC_SET_OPTION_INVALID_NAME;
#endif
    }
    else if (name == "trace")
    {
        vm.add_tracer(create_instruction_tracer(std::cerr));
        return EVMC_SET_OPTION_SUCCESS;
    }
    else if (name == "histogram")
    {
        vm.add_tracer(create_histogram_tracer(std::cerr));
        return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_NAME;
}
}

inline constexpr VM::VM() noexcept
  : evmc_vm{
        EVMC_ABI_VERSION,
        "evm",
        PROJECT_VERSION,
        evm::destroy,
        evm::baseline::execute,
        evm::get_capabilities,
        evmo::set_option,
    }
{}
}

extern "C" {
EVMC_EXPORT evmc_vm* evmc_create_evm() noexcept
{
    return new evm::VM{};
}
}