cmake_minimum_required(VERSION 3.16)
include(cmake/OrcaSensorVersion.cmake)
project(OrcaSensorSDK VERSION "${ORCA_SENSOR_SDK_VERSION}" LANGUAGES C CXX)
configure_file(include/orca_sensor_version.h.in generated/orca_sensor_version.h @ONLY)
set(ORCA_SENSOR_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
include(cmake/OrcaSensorTool.cmake)

option(ORCA_SENSOR_BUILD_EXAMPLE "Build the four standalone providers and native acceptance programs" ON)
include(CTest)

if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "The bundled native acceptance programs require a matching native SDK build")
endif()
set(ORCA_SENSOR_HOST_LIBRARY
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/${ORCA_SENSOR_TOOL_PLATFORM}/${CMAKE_SHARED_LIBRARY_PREFIX}orca_sensor_host${CMAKE_SHARED_LIBRARY_SUFFIX}"
    CACHE FILEPATH "Precompiled Orca Sensor Host shared library")
if(NOT EXISTS "${ORCA_SENSOR_HOST_LIBRARY}" OR IS_DIRECTORY "${ORCA_SENSOR_HOST_LIBRARY}")
    message(FATAL_ERROR "Matching precompiled Orca Sensor Host is missing: ${ORCA_SENSOR_HOST_LIBRARY}")
endif()
# Keep acceptance executables and their runtime library together. Host itself is
# maintained and built separately; the vendor SDK never compiles Host sources.
get_filename_component(orca_host_filename "${ORCA_SENSOR_HOST_LIBRARY}" NAME)
configure_file("${ORCA_SENSOR_HOST_LIBRARY}" "${CMAKE_BINARY_DIR}/${orca_host_filename}" COPYONLY)
add_library(orca_sensor_host SHARED IMPORTED GLOBAL)
set_target_properties(orca_sensor_host PROPERTIES
    IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/${orca_host_filename}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include;${CMAKE_CURRENT_BINARY_DIR}/generated")
if(WIN32)
    set(ORCA_SENSOR_HOST_IMPLIB
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/${ORCA_SENSOR_TOOL_PLATFORM}/orca_sensor_host.lib"
        CACHE FILEPATH "Import library accompanying the Windows Host DLL")
    if(NOT EXISTS "${ORCA_SENSOR_HOST_IMPLIB}")
        message(FATAL_ERROR "Windows native acceptance requires the Host import library: ${ORCA_SENSOR_HOST_IMPLIB}")
    endif()
    set_property(TARGET orca_sensor_host PROPERTY IMPORTED_IMPLIB "${ORCA_SENSOR_HOST_IMPLIB}")
endif()

if(ORCA_SENSOR_BUILD_EXAMPLE)
    add_subdirectory(examples/touch_grid)
    add_subdirectory(examples/seven_pad)
    add_subdirectory(examples/contact_grid)
    add_subdirectory(examples/rangefinder)
    add_executable(orca_touch_grid_smoke examples/acceptance/smoke.cpp)
    target_compile_features(orca_touch_grid_smoke PRIVATE cxx_std_17)
    target_link_libraries(orca_touch_grid_smoke PRIVATE orca_sensor_host)
    target_include_directories(orca_touch_grid_smoke PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/examples/touch_grid/generated")
    add_dependencies(orca_touch_grid_smoke orca_touch_grid)
    add_executable(orca_seven_pad_smoke examples/acceptance/seven_pad_smoke.cpp)
    target_compile_features(orca_seven_pad_smoke PRIVATE cxx_std_17)
    target_link_libraries(orca_seven_pad_smoke PRIVATE orca_sensor_host)
    target_include_directories(orca_seven_pad_smoke PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/examples/seven_pad/generated")
    add_dependencies(orca_seven_pad_smoke orca_seven_pad)
    add_executable(orca_site_providers_smoke examples/acceptance/site_providers_smoke.cpp)
    target_compile_features(orca_site_providers_smoke PRIVATE cxx_std_17)
    target_link_libraries(orca_site_providers_smoke PRIVATE orca_sensor_host)
    target_include_directories(orca_site_providers_smoke PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/examples/contact_grid/generated"
        "${CMAKE_CURRENT_BINARY_DIR}/examples/rangefinder/generated")
    add_dependencies(orca_site_providers_smoke orca_contact_grid orca_rangefinder)
    set_target_properties(orca_touch_grid_smoke orca_seven_pad_smoke orca_site_providers_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        BUILD_RPATH_USE_ORIGIN TRUE)
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(orca_touch_grid_smoke orca_seven_pad_smoke orca_site_providers_smoke PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}")
    endforeach()
    if(BUILD_TESTING)
        add_test(NAME orca_touch_grid_smoke
            COMMAND orca_touch_grid_smoke $<TARGET_FILE:orca_touch_grid>)
        add_test(NAME orca_seven_pad_smoke
            COMMAND orca_seven_pad_smoke $<TARGET_FILE:orca_seven_pad>)
        add_test(NAME orca_site_providers_smoke
            COMMAND orca_site_providers_smoke
                $<TARGET_FILE:orca_contact_grid> $<TARGET_FILE:orca_rangefinder>)
    endif()
endif()

include(GNUInstallDirs)
if(WIN32)
    install(FILES "${ORCA_SENSOR_HOST_LIBRARY}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${ORCA_SENSOR_HOST_IMPLIB}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
else()
    install(FILES "${ORCA_SENSOR_HOST_LIBRARY}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} PATTERN "*.in" EXCLUDE)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/orca_sensor_version.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/PUBLIC_SDK_LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/ORCA_BINARY_LICENSE.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/ORCA_BINARY_LICENSE_SOURCE.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/ORCA_BINARY_LICENSE_ADDENDUM.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/OrcaSensorSDK")
