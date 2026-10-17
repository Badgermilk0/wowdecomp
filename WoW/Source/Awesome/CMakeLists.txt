file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(Awesome STATIC
    ${PRIVATE_SOURCES}
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
