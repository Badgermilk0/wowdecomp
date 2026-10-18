#include "Engine/Source/Gxu/GxuFontFallback.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include "Engine/Source/Gxu/GxuFont.h"
#include "Engine/Source/Gxu/GxuFontFace.h"
#include "Engine/Source/Gxu/GxuFontMsdf.h"

namespace {
struct FallbackFace {
    std::string m_path;
    bool m_tried = false;
    FT_Face m_face = nullptr;
    // a game font: the engine's shared face
    HFACE m_handle = nullptr;
    // a disk font: the bytes FreeType reads from
    std::vector<uint8_t> m_bytes;
};

std::vector<FallbackFace> s_chain;

bool IsAbsolute(const std::string& path) {
    return (path.size() > 1 && path[1] == ':') || (path.size() > 1 && path[0] == '\\' && path[1] == '\\');
}

// %NAME% from the environment; an unknown name stays as written
std::string Expand(const std::string& path) {
    std::string out;

    for (size_t i = 0; i < path.size(); i++) {
        size_t close = path[i] == '%' ? path.find('%', i + 1) : std::string::npos;

        if (close == std::string::npos) {
            out += path[i];
            continue;
        }

        std::string name = path.substr(i + 1, close - i - 1);
        const char* value = std::getenv(name.c_str());

        if (value) {
            out += value;
        } else {
            out += path.substr(i, close - i + 1);
        }

        i = close;
    }

    return out;
}

void Release(FallbackFace& entry) {
    if (entry.m_handle) {
        FontFaceCloseHandle(entry.m_handle);
    } else if (entry.m_face) {
        if (g_gxuFontMsdf.m_faceDestroyed) {
            g_gxuFontMsdf.m_faceDestroyed(entry.m_face);
        }

        FT_Done_Face(entry.m_face);
    }

    entry.m_handle = nullptr;
    entry.m_face = nullptr;
    entry.m_bytes.clear();
    entry.m_bytes.shrink_to_fit();
}

void Load(FallbackFace& entry) {
    entry.m_tried = true;

    if (!g_FTLibrary) {
        return;
    }

    std::string path = Expand(entry.m_path);

    if (!IsAbsolute(path)) {
        entry.m_handle = FontFaceGetHandle(path.c_str(), g_FTLibrary);
        entry.m_face = entry.m_handle ? FontFaceGetFace(entry.m_handle) : nullptr;
        return;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file) {
        return;
    }

    std::streamsize size = file.tellg();

    if (size <= 0) {
        return;
    }

    entry.m_bytes.resize(static_cast<size_t>(size));
    file.seekg(0);

    if (!file.read(reinterpret_cast<char*>(entry.m_bytes.data()), size)) {
        entry.m_bytes.clear();
        return;
    }

    FT_Face face = nullptr;

    if (FT_New_Memory_Face(g_FTLibrary, entry.m_bytes.data(), static_cast<FT_Long>(entry.m_bytes.size()), 0, &face) || !face) {
        entry.m_bytes.clear();
        return;
    }

    if (FT_Select_Charmap(face, ft_encoding_unicode)) {
        FT_Done_Face(face);
        entry.m_bytes.clear();
        return;
    }

    entry.m_face = face;

    // the MSDF atlas generates the face's glyphs into the font that asked for them
    if (g_gxuFontMsdf.m_faceCreated) {
        g_gxuFontMsdf.m_faceCreated(face, entry.m_bytes.data(), static_cast<uint32_t>(entry.m_bytes.size()));
    }
}
}  // namespace

void GxuFontSetFallbackList(const char* list) {
    for (FallbackFace& entry : s_chain) {
        Release(entry);
    }

    s_chain.clear();

    std::string text = list ? list : "";
    size_t start = 0;

    while (start <= text.size()) {
        size_t end = text.find(';', start);

        if (end == std::string::npos) {
            end = text.size();
        }

        std::string path = text.substr(start, end - start);

        while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) {
            path.erase(path.begin());
        }

        while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) {
            path.pop_back();
        }

        if (!path.empty()) {
            s_chain.emplace_back();
            s_chain.back().m_path = path;
        }

        start = end + 1;
    }
}

FT_Face GxuFontFallbackFind(FT_Face primary, uint32_t code, bool load) {
    for (FallbackFace& entry : s_chain) {
        if (!entry.m_tried) {
            if (!load) {
                continue;
            }

            Load(entry);
        }

        if (entry.m_face && entry.m_face != primary && FT_Get_Char_Index(entry.m_face, code)) {
            return entry.m_face;
        }
    }

    return nullptr;
}

void GxuFontFallbackShutdown() {
    for (FallbackFace& entry : s_chain) {
        Release(entry);
        entry.m_tried = false;
    }
}
