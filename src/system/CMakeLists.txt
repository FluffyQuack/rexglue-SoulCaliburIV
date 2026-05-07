# rexruntime: System emulation + kernel exports merged into one SHARED library.
# Process singletons (Runtime::instance_, thread_state_, current_xthread_tls_)
# require single-DLL hosting. Aliases preserve consumer compat for namespaced
# rex:: refs; plain rexsystem/rexkernel direct refs were updated to rexruntime.

set(REXSYSTEM_SOURCES
    runtime.cpp
    kernel_state.cpp
    xmemory.cpp
    kernel_module.cpp
    user_module.cpp
    xthread.cpp
    xevent.cpp
    xfile.cpp
    xmutant.cpp
    xsemaphore.cpp
    xtimer.cpp
    xiocompletion.cpp
    xnotifylistener.cpp
    xenumerator.cpp
    xsocket.cpp
    xsymboliclink.cpp
    xobject.cpp
    xmodule.cpp
    shared_library.cpp
    guest_path.cpp

    # Utilities
    util/object_table.cpp
    util/native_list.cpp
    util/xdbf_utils.cpp
    util/gameinfo_utils.cpp

    # XAM services
    xam/app_manager.cpp
    xam/content_manager.cpp
    xam/user_profile.cpp

    # Formerly rexruntime: module loading, CPU execution, thread state
    ppc_types.cpp
    elf_module.cpp
    entry_table.cpp
    export_resolver.cpp
    lzx.cpp
    mmio_handler.cpp
    map_parser.cpp
    module.cpp
    function_dispatcher.cpp
    thread.cpp
    thread_state.cpp
    xex_module.cpp
)

add_library(rexruntime SHARED ${REXSYSTEM_SOURCES})
add_library(rex::runtime ALIAS rexruntime)
add_library(rex::system ALIAS rexruntime)
add_library(rex::kernel ALIAS rexruntime)

# Auto-export every function symbol on MSVC, including hundreds of __imp__*
# kernel hooks defined in src/kernel/. Linux uses default symbol visibility,
# which already exports everything.
set_target_properties(rexruntime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

target_include_directories(rexruntime PUBLIC
    $<BUILD_INTERFACE:${REXGLUE_ROOT}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(rexruntime PRIVATE
    ${REXGLUE_ROOT}/thirdparty
)

target_link_libraries(rexruntime
    PUBLIC
        rexcore
        rexfilesystem
        simde
    PRIVATE
        aes128
        mspack
)

if(WIN32)
    target_link_libraries(rexruntime PRIVATE ws2_32)
endif()

# Propagate graphics backend selection
if(REXGLUE_USE_D3D12)
    target_compile_definitions(rexruntime PUBLIC REX_HAS_D3D12=1)
endif()
if(REXGLUE_USE_VULKAN)
    target_compile_definitions(rexruntime PUBLIC REX_HAS_VULKAN=1)
endif()
