# MODIFIED: modern water and the smoother sky. The sky model, the ripple pass on a WARP device, and (with a folder of
# the stock water .bls files as argument) the translated water shaders compiled.
add_executable(WaterTests
    WaterTests.cpp
    ${CMAKE_SOURCE_DIR}/WoW/Source/WorldClient/SkyModel.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Source/Gx/CGxDeviceD3d11/D3d11Dx9Shader.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Source/Gx/CGxDeviceD3d11/D3d11WaterHlsl.cpp
)

target_include_directories(WaterTests
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(WaterTests
    PRIVATE
        d3d11
        d3dcompiler
        dxguid
)

set_target_properties(WaterTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/Win
)

foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set_target_properties(WaterTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_SOURCE_DIR}/bin/Win)
endforeach ()

add_test(NAME WaterSky COMMAND WaterTests)
