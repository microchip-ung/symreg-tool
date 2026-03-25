cmake_minimum_required(VERSION 3.5)
project(symreg)

option(BUILD_LAN966X "Build LAN966x Symreg Tool" OFF)
option(BUILD_LAN969X "Build LAN969x Symreg Tool" OFF)
option(BUILD_SPARX5 "Build Sparx5 Symreg Tool" OFF)
option(BUILD_LAN9645X "Build LAN9645X Symreg Tool" OFF)

set(SDEV_DIR "sdev" CACHE STRING "Destination for Switchdev specific files")

#######################################################################

function(add_symreg_target SOC_NAME SDEV_DIR)
    add_executable(symreg_${SOC_NAME}
        src/symreg.c
        src/symreg_arg.c
        src/symreg_io.c
        src/symreg_util.c
        src/symreg_${SOC_NAME}.c
        ${SDEV_DIR}/symreg_spec_${SOC_NAME}.h
    )
    target_include_directories(symreg_${SOC_NAME} PRIVATE ${SDEV_DIR})
    string(TOUPPER "${SOC_NAME}" SOC_NAME_UPPER)
    target_compile_options(symreg_${SOC_NAME} PRIVATE -D${SOC_NAME_UPPER} -Wno-format-truncation)
    install(TARGETS symreg_${SOC_NAME} DESTINATION bin)
endfunction()

#######################################################################

if (BUILD_SPARX5)
    add_symreg_target(sparx5 ${SDEV_DIR})
endif()

if (BUILD_LAN966X)
    add_symreg_target(lan966x ${SDEV_DIR})
endif()

if (BUILD_LAN969X)
    add_symreg_target(lan969x ${SDEV_DIR})
endif()

if (BUILD_LAN9645X)
    add_symreg_target(lan9645x ${SDEV_DIR})
endif()

#######################################################################

add_executable(symreg src/symreg_detect.c)
install(TARGETS symreg DESTINATION bin)

#######################################################################
#  vim: set ts=4 sw=4 sts=4 tw=120 cc=120 et ft=cmake :
