// MODIFIED: weather overhaul. Renders the thunder chains (WoW/Source/Awesome/AwesomeThunderDsp) through the client's
// own FMOD Ex, offline (FMOD_OUTPUTTYPE_WAVWRITER_NRT), on a synthetic thunder clap, and measures what they do:
// louder overall, more bass, the pitch-down stretching the sound, flat settings leaving the low end alone.
//   cmake --build build/x64 --config Release --target WeatherAudioTests
//   ctest --test-dir build/x64 -C Release -R Weather   (or run bin/Win/WeatherAudioTests.exe)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "WoW/Source/Awesome/AwesomeThunderDsp.h"

static int32_t s_failures;
static int32_t s_checks;

#define CHECK(condition) Check((condition), #condition, __LINE__)

static void Check(bool condition, const char* text, int32_t line) {
    s_checks++;

    if (!condition) {
        s_failures++;
        printf("FAIL line %d: %s\n", line, text);
    }
}

static const int32_t SOURCE_RATE = 44100;

// 2.5 s mono 16-bit: a noise clap with a 55 Hz body, 5 ms attack, exponential decay
static std::vector<uint8_t> MakeClap() {
    const int32_t samples = SOURCE_RATE * 5 / 2;
    std::vector<uint8_t> wav(44 + samples * 2);
    uint32_t seed = 12345;

    auto put32 = [&](size_t at, uint32_t value) { memcpy(&wav[at], &value, 4); };
    auto put16 = [&](size_t at, uint16_t value) { memcpy(&wav[at], &value, 2); };
    memcpy(&wav[0], "RIFF", 4);
    put32(4, static_cast<uint32_t>(wav.size() - 8));
    memcpy(&wav[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 1);
    put32(24, SOURCE_RATE);
    put32(28, SOURCE_RATE * 2);
    put16(32, 2);
    put16(34, 16);
    memcpy(&wav[36], "data", 4);
    put32(40, samples * 2);

    for (int32_t i = 0; i < samples; i++) {
        float t = i / static_cast<float>(SOURCE_RATE);
        seed = seed * 1664525u + 1013904223u;
        float noise = ((seed >> 8) * (1.0f / 16777216.0f)) * 2.0f - 1.0f;
        float envelope = (t < 0.005f ? t / 0.005f : 1.0f) * expf(-t * 2.5f);
        float value = envelope * (0.45f * noise + 0.25f * sinf(6.2831853f * 55.0f * t));
        put16(44 + i * 2, static_cast<uint16_t>(static_cast<int16_t>(value * 32767.0f)));
    }

    return wav;
}

struct Measure {
    double m_rms = 0.0;
    double m_lowRms = 0.0;
    double m_peak = 0.0;
    double m_clipped = 0.0;   // share of samples at full scale
    double m_duration = 0.0;  // seconds until the last sample above -40 dBFS
};

static double Db(double ratio) {
    return 20.0 * log10(ratio > 1e-12 ? ratio : 1e-12);
}

static bool ReadWav(const char* path, std::vector<float>* mono, int32_t* rate) {
    FILE* file = fopen(path, "rb");

    if (!file) {
        return false;
    }

    std::vector<uint8_t> data;
    uint8_t buffer[65536];
    size_t read;

    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        data.insert(data.end(), buffer, buffer + read);
    }

    fclose(file);

    if (data.size() < 12 || memcmp(&data[0], "RIFF", 4) || memcmp(&data[8], "WAVE", 4)) {
        return false;
    }

    uint16_t format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    size_t at = 12;

    while (at + 8 <= data.size()) {
        uint32_t size;
        memcpy(&size, &data[at + 4], 4);
        const uint8_t* chunk = &data[at + 8];

        if (!memcmp(&data[at], "fmt ", 4)) {
            memcpy(&format, chunk, 2);
            memcpy(&channels, chunk + 2, 2);
            memcpy(rate, chunk + 4, 4);
            memcpy(&bits, chunk + 14, 2);
        } else if (!memcmp(&data[at], "data", 4)) {
            if (at + 8 + size > data.size()) {
                size = static_cast<uint32_t>(data.size() - at - 8);
            }

            size_t frameBytes = channels * bits / 8;

            for (size_t frame = 0; frameBytes && (frame + 1) * frameBytes <= size; frame++) {
                float sum = 0.0f;

                for (uint16_t c = 0; c < channels; c++) {
                    const uint8_t* sample = chunk + frame * frameBytes + c * bits / 8;

                    if (bits == 16) {
                        int16_t value;
                        memcpy(&value, sample, 2);
                        sum += value / 32768.0f;
                    } else if (bits == 32 && format == 3) {
                        float value;
                        memcpy(&value, sample, 4);
                        sum += value;
                    }
                }

                mono->push_back(sum / channels);
            }

            return true;
        }

        at += 8 + size + (size & 1);
    }

    return false;
}

static Measure Analyse(const std::vector<float>& mono, int32_t rate) {
    Measure measure;
    double sum = 0.0;
    double lowSum = 0.0;
    double low = 0.0;
    // one-pole lowpass at 150 Hz for the bass level
    double alpha = 1.0 - exp(-6.283185307 * 150.0 / rate);
    size_t last = 0;
    size_t clipped = 0;

    for (size_t i = 0; i < mono.size(); i++) {
        double x = mono[i];
        sum += x * x;
        low += alpha * (x - low);
        lowSum += low * low;
        measure.m_peak = fabs(x) > measure.m_peak ? fabs(x) : measure.m_peak;
        clipped += fabs(x) >= 0.999 ? 1 : 0;

        if (fabs(x) > 0.01) {
            last = i;
        }
    }

    size_t count = mono.size() ? mono.size() : 1;
    measure.m_rms = sqrt(sum / count);
    measure.m_lowRms = sqrt(lowSum / count);
    measure.m_clipped = clipped / static_cast<double>(count);
    measure.m_duration = last / static_cast<double>(rate);
    return measure;
}

// plays the clap at volume 0.5, through chain unless it is null, and measures the mix
static Measure Render(const std::vector<uint8_t>& clap, ThunderDspChain* chain, float pitch, float bass, float boost, const char* label) {
    Measure measure;
    char path[512];
    const char* temp = getenv("TEMP");
    snprintf(path, sizeof(path), "%s\\weather_audio_test_%s.wav", temp ? temp : ".", label);

    FMOD::System* system = nullptr;

    if (FMOD::System_Create(&system) != FMOD_OK) {
        printf("FAIL %s: System_Create\n", label);
        s_failures++;
        return measure;
    }

    system->setOutput(FMOD_OUTPUTTYPE_WAVWRITER_NRT);

    if (system->init(32, FMOD_INIT_STREAM_FROM_UPDATE, path) != FMOD_OK) {
        printf("FAIL %s: init\n", label);
        s_failures++;
        system->release();
        return measure;
    }

    FMOD_CREATESOUNDEXINFO info = {};
    info.cbsize = sizeof(info);
    info.length = static_cast<unsigned int>(clap.size());
    FMOD::Sound* sound = nullptr;
    FMOD::Channel* channel = nullptr;
    CHECK(system->createSound(reinterpret_cast<const char*>(clap.data()), FMOD_OPENMEMORY | FMOD_SOFTWARE | FMOD_2D | FMOD_CREATESAMPLE, &info, &sound) == FMOD_OK);
    CHECK(system->playSound(FMOD_CHANNEL_FREE, sound, true, &channel) == FMOD_OK);

    if (channel) {
        channel->setVolume(0.5f);

        if (chain) {
            CHECK(ThunderDspAttach(*chain, channel, pitch, bass, boost));
            CHECK(chain->m_group && chain->m_compressor && chain->m_bassLow && chain->m_bassHigh);
        }

        channel->setPaused(false);
    }

    // until the sound ends, then a second more for the tails
    bool playing = true;
    int32_t after = 0;

    for (int32_t i = 0; i < 4000 && after < 50; i++) {
        system->update();

        if (playing && channel && channel->isPlaying(&playing) != FMOD_OK) {
            playing = false;
        }

        after += playing ? 0 : 1;
    }

    if (chain) {
        ThunderDspRelease(*chain);
        CHECK(!chain->m_group && !chain->m_compressor);
    }

    sound->release();
    system->close();
    system->release();

    std::vector<float> mono;
    int32_t rate = 0;

    if (!ReadWav(path, &mono, &rate) || rate <= 0) {
        printf("FAIL %s: no output %s\n", label, path);
        s_failures++;
        return measure;
    }

    remove(path);
    measure = Analyse(mono, rate);
    printf("  %-14s rms %6.1f dB  bass %6.1f dB  peak %6.1f dB  clipped %.3f%%  %.2f s\n", label, Db(measure.m_rms), Db(measure.m_lowRms), Db(measure.m_peak), measure.m_clipped * 100.0, measure.m_duration);
    return measure;
}

int main() {
    std::vector<uint8_t> clap = MakeClap();

    ThunderDspChain crack = { "Crack", 0.0f, 1.6f, 0.8f, 0.0f };
    ThunderDspChain body = { "Body", 2600.0f, 1.0f, 1.0f, 0.0f };
    ThunderDspChain distant = { "Distant", 650.0f, 1.0f, 1.0f, -1.0f };

    // the client's defaults (weatherThunderBass 1.8, weatherThunderBoost 6) and the CVars' limits (3, 24)
    Measure dry = Render(clap, nullptr, 1.0f, 1.0f, 0.0f, "dry");
    Measure crackOut = Render(clap, &crack, 1.0f, 1.8f, 6.0f, "crack");
    Measure bodyOut = Render(clap, &body, 0.6f, 1.8f, 6.0f, "body pitch 0.6");
    Measure bodyFlat = Render(clap, &body, 1.0f, 1.0f, 0.0f, "body flat");
    Measure distantOut = Render(clap, &distant, 0.7f, 1.8f, 6.0f, "distant");
    Measure bassOnly = Render(clap, &body, 1.0f, 1.8f, 0.0f, "bass only");
    Measure boostOnly = Render(clap, &body, 1.0f, 1.0f, 6.0f, "boost only");
    Measure crackMax = Render(clap, &crack, 1.0f, 3.0f, 24.0f, "crack max");

    // louder than the channel volume alone could make it, with the bass lifted more than the rest
    CHECK(Db(crackOut.m_rms / dry.m_rms) >= 8.0);
    CHECK(Db(crackOut.m_lowRms / dry.m_lowRms) >= 10.0);
    CHECK(Db(crackOut.m_lowRms / dry.m_lowRms) > Db(crackOut.m_rms / dry.m_rms));
    CHECK(Db(bodyOut.m_rms / dry.m_rms) >= 6.0);
    CHECK(Db(bodyOut.m_lowRms / dry.m_lowRms) >= 10.0);
    // each half does its part: the bass bands and the compressor's make-up gain
    CHECK(Db(bassOnly.m_lowRms / dry.m_lowRms) >= 6.0);
    CHECK(Db(boostOnly.m_rms / dry.m_rms) >= 2.0);
    // the pitch-down plays the clap slower: longer, by about 1 / 0.6
    CHECK(bodyOut.m_duration > dry.m_duration * 1.4);
    // flat settings: the lowpass takes highs only, the low end stays put
    CHECK(fabs(Db(bodyFlat.m_lowRms / dry.m_lowRms)) <= 1.5);
    CHECK(bodyFlat.m_rms <= dry.m_rms * 1.05);
    // the distant roll is mostly bass
    CHECK(distantOut.m_lowRms / distantOut.m_rms > dry.m_lowRms / dry.m_rms);
    // the defaults never reach full scale; at the limits only a sliver of samples does
    CHECK(crackOut.m_clipped < 0.0005);
    CHECK(bodyOut.m_clipped < 0.0005);
    CHECK(distantOut.m_clipped < 0.0005);
    CHECK(crackMax.m_clipped < 0.02);

    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
