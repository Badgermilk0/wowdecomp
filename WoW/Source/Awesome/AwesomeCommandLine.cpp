#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include <shellapi.h>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/Glue/CharacterSelection.h"
#include "WoW/Source/Glue/CharacterSelectionDisplay.h"

// Auto login from the command line:
//   Wow.exe -login "LOGIN" -password "PASSWORD" -realmlist "REALMLIST" -realmname "REALMNAME" -character "NAME"

static std::vector<std::string> s_arguments;

// "-name", "--name" or "/name" -> "name", anything else -> nullptr
static const char* FlagName(const std::string& argument) {
    const char* name = argument.c_str();

    if (*name != '-' && *name != '/') {
        return nullptr;
    }

    name++;

    if (*name == '-') {
        name++;
    }

    return name;
}

static const std::string* GetParam(const char* flag) {
    for (size_t i = 1; i + 1 < s_arguments.size(); i++) {
        const char* name = FlagName(s_arguments[i]);

        if (name && !strcmp(name, flag)) {
            return &s_arguments[i + 1];
        }
    }

    return nullptr;
}

static void SetCVarFromParam(const char* flag, const char* cvarName) {
    const std::string* value = GetParam(flag);

    if (!value) {
        return;
    }

    CVar* cvar = CVar::Lookup(cvarName);

    if (cvar) {
        cvar->Set(value->c_str(), true, false, false, true);
    }
}

void AwesomeCommandLineInitialize() {
    int32_t count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);

    if (!arguments) {
        return;
    }

    for (int32_t i = 0; i < count; i++) {
        char buffer[1024];
        AwesomeWideToUtf8(arguments[i], buffer, sizeof(buffer));
        s_arguments.push_back(buffer);
    }

    LocalFree(arguments);
}

void AwesomeCommandLineOnGlueXMLLoaded() {
    static bool s_done = false;

    if (s_done) {
        return;
    }

    s_done = true;

    SetCVarFromParam("realmlist", "realmList");
    SetCVarFromParam("realmname", "realmName");

    const std::string* login = GetParam("login");
    const std::string* password = GetParam("password");

    if (login && password) {
        // LoginServerLogin takes a writable password buffer
        std::vector<char> passwordBuffer(password->begin(), password->end());
        passwordBuffer.push_back('\0');
        CGlueMgr::LoginServerLogin(login->c_str(), passwordBuffer.data());
        SecureZeroMemory(passwordBuffer.data(), passwordBuffer.size());
    }
}

void AwesomeCommandLineOnCharacterListUpdated() {
    static bool s_done = false;

    if (s_done) {
        return;
    }

    const std::string* character = GetParam("character");

    if (!character) {
        return;
    }

    for (uint32_t i = 0; i < CCharacterSelection::s_characterList.Count(); i++) {
        if (*character == CCharacterSelection::s_characterList[i].m_characterInfo.m_name) {
            s_done = true;
            CCharacterSelection::s_selectionIndex = static_cast<int32_t>(i);
            CGlueMgr::EnterWorld();
            return;
        }
    }
}
