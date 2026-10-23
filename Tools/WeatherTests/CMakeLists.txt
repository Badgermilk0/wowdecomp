# MODIFIED: weather overhaul. Unit tests for WoW/Source/WorldClient/WeatherModel, which has no engine dependencies.
add_executable(WeatherTests
    WeatherTests.cpp
    ${CMAKE_SOURCE_DIR}/WoW/Source/WorldClient/WeatherModel.cpp
)

target_include_directories(WeatherTests
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

set_target_properties(WeatherTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/Win
)

foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set_target_properties(WeatherTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_SOURCE_DIR}/bin/Win)
endforeach ()

add_test(NAME WeatherModel COMMAND WeatherTests)
