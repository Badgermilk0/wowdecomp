file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

# MODIFIED: built-in AwesomeCVar and Flash. The files under Interface/ become byte arrays in a generated source,
# made again whenever one of them changes
file(GLOB_RECURSE EMBEDDED_INTERFACE CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Interface/*.toc" "${CMAKE_CURRENT_SOURCE_DIR}/Interface/*.lua" "${CMAKE_CURRENT_SOURCE_DIR}/Interface/*.xml")
set(EMBEDDED_INTERFACE_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/AwesomeEmbeddedUiFiles.cpp")

add_custom_command(
    OUTPUT "${EMBEDDED_INTERFACE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}/Interface" "-DOUT=${EMBEDDED_INTERFACE_SOURCE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/EmbedInterface.cmake"
    DEPENDS ${EMBEDDED_INTERFACE} "${CMAKE_CURRENT_SOURCE_DIR}/EmbedInterface.cmake"
    COMMENT "Embedding the built-in interface files"
    VERBATIM
)

add_library(Awesome STATIC
    ${PRIVATE_SOURCES}
    "${EMBEDDED_INTERFACE_SOURCE}"
)

target_include_directories(Awesome
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Awesome
    PRIVATE
        ole32
        oleaut32
        uuid
        sapi
        d3dcompiler
        freetype
        msdfgen
        unordered_dense
        stb
)

# MSDF and the other C++23 code ported from awesome_wotlk; /permissive keeps the decomp headers compiling
set_target_properties(Awesome PROPERTIES CXX_STANDARD 23)
target_compile_options(Awesome PRIVATE /permissive /utf-8)

# the MSDF files that never include a decomp header stay conforming: some std::ranges concepts fail under /permissive
set_source_files_properties(
    Msdf/MSDFCache.cpp
    Msdf/MSDFFaceSweep.cpp
    Msdf/MSDFManager.cpp
    Msdf/MSDFPregen.cpp
    Msdf/MsdfFreetype.cpp
    PROPERTIES COMPILE_OPTIONS "/permissive-"
)
