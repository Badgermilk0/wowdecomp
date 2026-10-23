#ifndef WOW_SOURCE_AWESOME_AWESOMETHUNDERDSP_H
#define WOW_SOURCE_AWESOME_AWESOMETHUNDERDSP_H

// The thunder layers' FMOD chains (weather overhaul). FMOD only, no engine, so Tools/WeatherTests renders them
// through the client's own FMOD Ex offline (WeatherAudioTests).
//
// A chain is an FMOD channel group holding, in signal order: a 50 Hz and a 110 Hz bass band, an optional 3 kHz
// presence band, an optional lowpass, and a compressor whose make-up gain lifts the sound past what the channel
// volume (capped at 1) allows, with its threshold set so peaks stay about 1 dB under full scale.

#include <cstdint>
#include "Engine/Source/SoundEngine/fmodex/src/fmod.hpp"

struct ThunderDspChain {
    const char* m_name;
    float m_cutoff;       // lowpass in Hz, 0 = none
    float m_presence;     // gain at 3 kHz, 1 = none
    float m_bassShare;    // how much of the bass gain this layer takes
    float m_boostOffset;  // dB on top of the boost
    FMOD::ChannelGroup* m_group;
    FMOD::DSP* m_bassLow;
    FMOD::DSP* m_bassHigh;
    FMOD::DSP* m_presenceEq;
    FMOD::DSP* m_lowpass;
    FMOD::DSP* m_compressor;
};

// creates the group and its DSPs on first use; false if FMOD refused the group
bool ThunderDspEnsure(ThunderDspChain& chain, FMOD::System* system, float bass, float boostDb);
// bass: PARAMEQ gain at 50 Hz (1 flat, 3 the most), half of it at 110 Hz; boostDb: compressor make-up gain
void ThunderDspApply(ThunderDspChain& chain, float bass, float boostDb);
// moves channel into the chain (creating it with the channel's system) and plays it at pitch times its rate
bool ThunderDspAttach(ThunderDspChain& chain, FMOD::Channel* channel, float pitch, float bass, float boostDb);
void ThunderDspRelease(ThunderDspChain& chain);

#endif
