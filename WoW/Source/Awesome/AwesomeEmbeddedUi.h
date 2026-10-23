#ifndef WOW_SOURCE_AWESOME_AWESOMEEMBEDDEDUI_H
#define WOW_SOURCE_AWESOME_AWESOMEEMBEDDEDUI_H

#include <cstddef>

// A file of WoW/Source/Awesome/Interface, built into the client by EmbedInterface.cmake. The path is relative to
// Interface with backslashes ("AwesomeCVar\Core.lua"); the data has a terminating zero after m_size bytes.
struct AwesomeEmbeddedFile {
    const char* m_path;
    const unsigned char* m_data;
    size_t m_size;
};

// generated: AwesomeEmbeddedUiFiles.cpp in the build folder
extern const AwesomeEmbeddedFile g_awesomeEmbeddedFiles[];
extern const size_t g_awesomeEmbeddedFileCount;

#endif
