# One provider build description serves the internal tree and the public SDK.
if(NOT DEFINED ORCA_SENSOR_PUBLIC_LICENSE)
    set(ORCA_SENSOR_PUBLIC_LICENSE "${ORCA_SENSOR_SDK_DIR}/licenses/PUBLIC_SDK_LICENSE.txt")
endif()
if(DEFINED ORCA_SENSOR_INTERNAL_TOOL_COMMAND)
    set(ORCA_SENSOR_TOOL_COMMAND ${ORCA_SENSOR_INTERNAL_TOOL_COMMAND})
    set(ORCA_SENSOR_TOOL_DEPENDENCIES ${ORCA_SENSOR_INTERNAL_TOOL_DEPENDENCIES})
    return()
endif()

# The SDK ships a self-contained code generator, not implementation sources.
string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" orca_tool_system)
if(orca_tool_system STREQUAL "darwin")
    set(orca_tool_system "macos")
endif()
string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" orca_tool_arch)
if(orca_tool_arch MATCHES "^(amd64|x64)$")
    set(orca_tool_arch "x86_64")
elseif(orca_tool_arch STREQUAL "arm64")
    set(orca_tool_arch "aarch64")
endif()
set(ORCA_SENSOR_TOOL_PLATFORM "${orca_tool_system}-${orca_tool_arch}")
set(orca_tool_suffix "")
if(CMAKE_HOST_WIN32)
    set(orca_tool_suffix ".exe")
endif()
set(ORCA_SENSOR_TOOL
    "${ORCA_SENSOR_SDK_DIR}/bin/${ORCA_SENSOR_TOOL_PLATFORM}/orca-sensor-tool${orca_tool_suffix}"
    CACHE FILEPATH "Matching Orca Sensor SDK contract-tool executable")
if(NOT EXISTS "${ORCA_SENSOR_TOOL}" OR IS_DIRECTORY "${ORCA_SENSOR_TOOL}")
    message(FATAL_ERROR
        "Orca Sensor SDK has no contract tool for ${ORCA_SENSOR_TOOL_PLATFORM}. "
        "Use a supported platform with the bundled bin/ and lib/ directories intact. "
        "This affects provider SDK builds only, not ordinary OrcaGym use.")
endif()
set(ORCA_SENSOR_TOOL_COMMAND "${ORCA_SENSOR_TOOL}")
set(ORCA_SENSOR_TOOL_DEPENDENCIES "${ORCA_SENSOR_TOOL}")
