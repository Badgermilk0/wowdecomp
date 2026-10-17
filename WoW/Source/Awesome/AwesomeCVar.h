#ifndef WOW_SOURCE_AWESOME_AWESOMECVAR_H
#define WOW_SOURCE_AWESOME_AWESOMECVAR_H

#include <cstddef>
#include <cstdint>

struct CVar;

// A CVar with a typed, clamped mirror of its value. The engine stores every CVar as a string; the callback parses
// each set, clamps it to [min, max] (an out-of-range value is replaced by the clamped one) and runs onChange.
class AwesomeCVar {
    public:
    typedef void (*ChangeCallback)(AwesomeCVar* cvar, bool changed);

    const char* m_name;
    const char* m_help;
    bool m_isFloat;
    float m_init;
    float m_min;
    float m_max;
    // optional upper bound evaluated on every set, replaces m_max (ttsVoice: the installed voice count)
    int32_t (*m_dynamicMax)();
    // printf format the clamped value is written back with
    const char* m_format;
    // runs on every engine set; changed is false when the value equals the last applied one (true the first time)
    ChangeCallback m_onChange;

    CVar* m_cvar;
    int32_t m_intValue;
    float m_floatValue;
    bool m_applied;

    static AwesomeCVar Int(const char* name, int32_t init, int32_t min, int32_t max, ChangeCallback onChange = nullptr);
    static AwesomeCVar Float(const char* name, float init, float min, float max, ChangeCallback onChange = nullptr, const char* format = "%.2f");

    // registers with the engine; the engine runs the callback with the saved value (Config.wtf) on registration
    void Register();
    // re-applies the current string, firing onChange with changed = false
    void Refresh();
    // sets the value as if typed into the console
    void Set(const char* value);

    private:
    static bool Callback(CVar* cvar, const char* oldValue, const char* newValue, void* arg);
    bool Apply(const char* value);
};

#endif
