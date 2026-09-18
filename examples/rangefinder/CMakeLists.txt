cmake_minimum_required(VERSION 3.16)
get_filename_component(ORCA_SENSOR_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${ORCA_SENSOR_SDK_DIR}/cmake/OrcaSensorVersion.cmake")
set(ORCA_SENSOR_PROVIDER_SDK_VERSION "1.0.0")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(OrcaRangefinderProvider VERSION "${ORCA_SENSOR_SDK_VERSION}" LANGUAGES CXX)
endif()
include("${ORCA_SENSOR_SDK_DIR}/cmake/OrcaSensorTool.cmake")
file(GLOB_RECURSE contract_profiles CONFIGURE_DEPENDS "${ORCA_SENSOR_SDK_DIR}/compatibility/*.json")
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(generated_header "${generated_dir}/rangefinder_input.h")
set(provider_dir "${CMAKE_BINARY_DIR}/providers/rangefinder")
configure_file("${ORCA_SENSOR_SDK_DIR}/include/orca_sensor_version.h.in"
    "${generated_dir}/orca_sensor_version.h" @ONLY)
add_custom_command(
    OUTPUT "${generated_header}" "${provider_dir}/provider.json"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_dir}"
    COMMAND ${ORCA_SENSOR_TOOL_COMMAND} contract
            "${CMAKE_CURRENT_SOURCE_DIR}/contract.json" "${generated_header}"
            --model "${CMAKE_CURRENT_SOURCE_DIR}/model.xml" --mount-site sensor_base
            --sdk-version "${ORCA_SENSOR_PROVIDER_SDK_VERSION}"
    COMMAND ${ORCA_SENSOR_TOOL_COMMAND} provider_manifest
            "${CMAKE_CURRENT_SOURCE_DIR}/provider.json" "${provider_dir}/provider.json"
            --sdk-version "${ORCA_SENSOR_PROVIDER_SDK_VERSION}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/contract.json" "${CMAKE_CURRENT_SOURCE_DIR}/provider.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/model.xml" "${ORCA_SENSOR_SDK_DIR}/VERSION"
            ${ORCA_SENSOR_TOOL_DEPENDENCIES} "${ORCA_SENSOR_SDK_DIR}/cmake/OrcaSensorTool.cmake" ${contract_profiles}
    VERBATIM)
add_library(orca_rangefinder SHARED rangefinder.cpp "${generated_header}" "${provider_dir}/provider.json")
target_compile_features(orca_rangefinder PRIVATE cxx_std_17)
target_include_directories(orca_rangefinder PRIVATE "${ORCA_SENSOR_SDK_DIR}/include" "${generated_dir}")
set_target_properties(orca_rangefinder PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
    LIBRARY_OUTPUT_DIRECTORY "${provider_dir}" RUNTIME_OUTPUT_DIRECTORY "${provider_dir}")
if(MSVC)
    target_compile_options(orca_rangefinder PRIVATE /W4 /EHsc)
else()
    target_compile_options(orca_rangefinder PRIVATE -Wall -Wextra -Wpedantic)
endif()
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(orca_rangefinder PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${config} "${provider_dir}" RUNTIME_OUTPUT_DIRECTORY_${config} "${provider_dir}")
endforeach()
file(MAKE_DIRECTORY "${provider_dir}")
configure_file(contract.json "${provider_dir}/contract.json" COPYONLY)
configure_file(model.xml "${provider_dir}/model.xml" COPYONLY)
configure_file(README.md "${provider_dir}/README.md" COPYONLY)
configure_file("${ORCA_SENSOR_PUBLIC_LICENSE}" "${provider_dir}/LICENSE" COPYONLY)
