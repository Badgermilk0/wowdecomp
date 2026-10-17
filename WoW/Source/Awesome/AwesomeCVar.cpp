#include "WoW/Source/Awesome/AwesomeCVar.h"
#include <cstdio>
#include <cstdlib>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"

AwesomeCVar AwesomeCVar::Int(const char* name, int32_t init, int32_t min, int32_t max, ChangeCallback onChange) {
    AwesomeCVar cvar = {};
    cvar.m_name = name;
    cvar.m_help = "";
    cvar.m_isFloat = false;
    cvar.m_init = static_cast<float>(init);
    cvar.m_min = static_cast<float>(min);
    cvar.m_max = static_cast<float>(max);
    cvar.m_format = "%d";
    cvar.m_onChange = onChange;
    cvar.m_intValue = init;
    cvar.m_floatValue = static_cast<float>(init);
    return cvar;
}

AwesomeCVar AwesomeCVar::Float(const char* name, float init, float min, float max, ChangeCallback onChange, const char* format) {
    AwesomeCVar cvar = {};
    cvar.m_name = name;
    cvar.m_help = "";
    cvar.m_isFloat = true;
    cvar.m_init = init;
    cvar.m_min = min;
    cvar.m_max = max;
    cvar.m_format = format;
    cvar.m_onChange = onChange;
    cvar.m_intValue = static_cast<int32_t>(init);
    cvar.m_floatValue = init;
    return cvar;
}

void AwesomeCVar::Register() {
    char init[32];

    if (this->m_isFloat) {
        snprintf(init, sizeof(init), "%g", static_cast<double>(this->m_init));
    } else {
        snprintf(init, sizeof(init), "%d", static_cast<int32_t>(this->m_init));
    }

    // category 0 like the original, so the values are saved to Config.wtf
    this->m_cvar = CVar::Register(this->m_name, this->m_help, 0, init, &AwesomeCVar::Callback, CATEGORY_DEBUG, false, this, false);
}

void AwesomeCVar::Refresh() {
    if (this->m_cvar && this->m_cvar->m_stringValue.m_str) {
        this->Apply(this->m_cvar->m_stringValue.m_str);
    }
}

void AwesomeCVar::Set(const char* value) {
    if (!this->m_cvar || !value) {
        return;
    }

    this->m_cvar->InternalSet(value, true, false, false, true);
    this->Apply(this->m_cvar->m_stringValue.m_str);
}

bool AwesomeCVar::Callback(CVar* cvar, const char* oldValue, const char* newValue, void* arg) {
    AwesomeCVar* self = static_cast<AwesomeCVar*>(arg);

    if (!self->m_cvar) {
        self->m_cvar = cvar;
    }

    return self->Apply(newValue);
}

bool AwesomeCVar::Apply(const char* value) {
    if (!value) {
        return false;
    }

    float max = this->m_dynamicMax ? static_cast<float>(this->m_dynamicMax()) : this->m_max;

    if (max < this->m_min) {
        max = this->m_min;
    }

    bool accepted = true;
    bool changed;

    if (this->m_isFloat) {
        float requested = static_cast<float>(atof(value));
        float clamped = requested < this->m_min ? this->m_min : (requested > max ? max : requested);

        if (clamped != requested && this->m_cvar) {
            // store the clamped value instead, the original set is rejected
            char buffer[32];
            snprintf(buffer, sizeof(buffer), this->m_format ? this->m_format : "%.2f", static_cast<double>(clamped));
            this->m_cvar->InternalSet(buffer, true, false, false, true);
            accepted = false;
        }

        changed = !this->m_applied || clamped != this->m_floatValue;
        this->m_floatValue = clamped;
        this->m_intValue = static_cast<int32_t>(clamped);
    } else {
        int32_t requested = atoi(value);
        int32_t imin = static_cast<int32_t>(this->m_min);
        int32_t imax = static_cast<int32_t>(max);
        int32_t clamped = requested < imin ? imin : (requested > imax ? imax : requested);

        if (clamped != requested && this->m_cvar) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", clamped);
            this->m_cvar->InternalSet(buffer, true, false, false, true);
            accepted = false;
        }

        changed = !this->m_applied || clamped != this->m_intValue;
        this->m_intValue = clamped;
        this->m_floatValue = static_cast<float>(clamped);
    }

    this->m_applied = true;

    if (this->m_onChange) {
        this->m_onChange(this, changed);
    }

    return accepted;
}
