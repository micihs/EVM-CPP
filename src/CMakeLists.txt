include(LibraryTools)

hunter_add_package(intx)
find_package(intx CONFIG REQUIRED)

add_library(evm
    ${include_dir}/evm/evm.h
    advanced_analysis.cpp
    advanced_analysis.hpp
    advanced_execution.cpp
    advanced_execution.hpp
    advanced_instructions.cpp
    baseline.cpp
    baseline.hpp
    baseline_instruction_table.cpp
    baseline_instruction_table.hpp
    eof.cpp
    eof.hpp    
    instructions.hpp
    instructions_calls.cpp
    instructions_storage.cpp
    instructions_traits.hpp
    instructions_xmacro.hpp
    opcodes_helpers.h
    tracing.cpp
    tracing.hpp
    vm.cpp
    vm.hpp
)

target_link_libraries(evm PUBLIC evmc::evmc intx::intx PRIVATE evmc::instructions ethash::keccak)
target_include_directories(evm PUBLIC
    $<BUILD_INTERFACE:${include_dir}>$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(CABLE_COMPILER_GNULIKE)
    target_compile_options(
        evm PRIVATE
        -fno-exceptions
        $<$<CXX_COMPILER_ID:GNU>:-Wstack-usage=2500>
    )
    if(NOT SANITIZE MATCHES undefined)
        target_compile_options(evm PRIVATE -fno-rtti)
    endif()
endif()
set_target_properties(
    evm
    PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_SOVERSION}
)

if(NOT SANITIZE)
    target_link_options(evmo PRIVATE $<$<PLATFORM_ID:Linux>:LINKER:--no-undefined>)
endif()

set_source_files_properties(vm.cpp PROPERTIES COMPILE_DEFINITIONS PROJECT_VERSION="${PROJECT_VERSION}")

add_standalone_library(evmo)