cmake_minimum_required(VERSION 3.30.0)
project(brcm-timer.device VERSION 0.9.0)

# "make install" gathers the products in build/install/: devs/ the device for DEVS:, sdk/ what a developer needs, tools/ the test programs
# for the Amiga
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/build/install CACHE PATH "Install prefix" FORCE)
endif()

include(cmake/verstring.cmake)
include(cmake/sfdc.cmake)

get_verstring(VERSTRING)
sfdc(sfd/brcmtimer_lib.sfd)

# gic400.library, in the ROM of Emu68: its SFD and libraries/gic400.h come from the submodule
sfdc(gic400.library/sfd/gic400.sfd)
target_include_directories(gic400 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/gic400.library/include)

add_executable(brcm-timer.device
    src/main.c
    src/init.c
    src/open.c
    src/close.c
    src/expunge.c
    src/beginio.c
    src/abortio.c
    src/clock.c
    src/queue.c
    src/btim_addtime.c
    src/btim_subtime.c
    src/btim_cmptime.c
    src/btim_readeclock.c
    src/btim_getsystime.c
    src/end.c
)

# -fno-tree-loop-distribute-patterns: GCC may turn a loop that copies or clears into a call to memcpy() or memset(), and there is no C library
set(BTIM_OPT "-Os" CACHE STRING "optimisation level of the device")
target_compile_options(brcm-timer.device PRIVATE ${BTIM_OPT} -m68040 -msmall-code -fomit-frame-pointer -Wall -Wno-pointer-sign -fno-tree-loop-distribute-patterns)
# One place for the version: project(), above. The id string, lib_Version and lib_Revision all come from it
target_compile_definitions(brcm-timer.device PRIVATE
    VERSION_STRING="${VERSTRING}"
    BTIM_VERSION=${PROJECT_VERSION_MAJOR}
    BTIM_REVISION=${PROJECT_VERSION_MINOR}
)

target_include_directories(brcm-timer.device PRIVATE include src)
target_link_options(brcm-timer.device PRIVATE -ffreestanding -nostdlib -nostartfiles -s -Wl,-e,__start)
target_link_libraries(brcm-timer.device brcmtimer gic400)

# The conformance and performance test against timer.device (CLI, no C library): -O2, at -Os GCC may call memcpy() for the larger initialisers
add_executable(brcmtimer-stress
    testcases/startup.c
    testcases/stress.c
)
target_compile_options(brcmtimer-stress PRIVATE -O2 -m68040 -fomit-frame-pointer -Wall -Wno-pointer-sign -Wno-main -fno-tree-loop-distribute-patterns)
target_include_directories(brcmtimer-stress PRIVATE include)
target_link_options(brcmtimer-stress PRIVATE -ffreestanding -m68040 -nostdlib -nostartfiles -Wl,-e,__start)
target_link_libraries(brcmtimer-stress amiga brcmtimer)

# The example of the SDK: built as a client of the device builds it, with the startup of the C library
add_executable(brcmtimer-example examples/brcmtimer-example.c)
target_compile_options(brcmtimer-example PRIVATE -noixemul -O2 -m68040 -Wall -Wno-pointer-sign)
target_include_directories(brcmtimer-example PRIVATE include)
target_link_options(brcmtimer-example PRIVATE -noixemul -s)
target_link_libraries(brcmtimer-example brcmtimer)

install(TARGETS brcm-timer.device DESTINATION devs)
install(TARGETS brcmtimer-stress brcmtimer-example DESTINATION tools)
install(FILES examples/brcmtimer-example.c DESTINATION sdk/examples)
install(FILES sfd/brcmtimer_lib.sfd DESTINATION sdk/sfd)
install(FILES Autodocs/brcmtimer.doc Autodocs/brcmtimer.md DESTINATION sdk/Autodocs)
install(FILES include/devices/brcmtimer.h DESTINATION sdk/include/devices)
# gic400.library is generated next to brcmtimer for the build: it has its own SDK
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/ DESTINATION sdk/include PATTERN "gic400*" EXCLUDE)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/FD/ DESTINATION sdk/FD PATTERN "gic400*" EXCLUDE)
