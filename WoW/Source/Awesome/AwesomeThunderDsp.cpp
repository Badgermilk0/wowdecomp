#include "WoW/Source/Awesome/AwesomeThunderDsp.h"
#include <cmath>

// as SE2::ConnectDspToChannelGroup: the DSP goes right after the group head, so the last one connected runs first
static FMOD::DSP* AddDsp(FMOD::System* system, FMOD::ChannelGroup* group, FMOD_DSP_TYPE type) {
    FMOD::DSP* dsp = nullptr;

    if (system->createDSPByType(type, &dsp) != FMOD_OK || !dsp) {
        return nullptr;
    }

    FMOD::DSP* head = nullptr;
    FMOD::DSP* output = nullptr;
    group->getDSPHead(&head);
    head->getOutput(0, &output);
    head->disconnectAll(false, true);
    output->addInput(dsp);
    dsp->addInput(head);
    dsp->setActive(true);
    return dsp;
}

void ThunderDspApply(ThunderDspChain& chain, float bass, float boostDb) {
    float gain = 1.0f + (bass - 1.0f) * chain.m_bassShare;
    float boost = boostDb + chain.m_boostOffset;

    if (boost < 0.0f) {
        boost = 0.0f;
    }

    if (chain.m_bassLow) {
        chain.m_bassLow->setParameter(FMOD_DSP_PARAMEQ_CENTER, 50.0f);
        chain.m_bassLow->setParameter(FMOD_DSP_PARAMEQ_BANDWIDTH, 1.5f);
        chain.m_bassLow->setParameter(FMOD_DSP_PARAMEQ_GAIN, gain);
    }

    if (chain.m_bassHigh) {
        chain.m_bassHigh->setParameter(FMOD_DSP_PARAMEQ_CENTER, 110.0f);
        chain.m_bassHigh->setParameter(FMOD_DSP_PARAMEQ_BANDWIDTH, 1.0f);
        chain.m_bassHigh->setParameter(FMOD_DSP_PARAMEQ_GAIN, 1.0f + (gain - 1.0f) * 0.5f);
    }

    if (chain.m_presenceEq) {
        chain.m_presenceEq->setParameter(FMOD_DSP_PARAMEQ_CENTER, 3000.0f);
        chain.m_presenceEq->setParameter(FMOD_DSP_PARAMEQ_BANDWIDTH, 2.0f);
        chain.m_presenceEq->setParameter(FMOD_DSP_PARAMEQ_GAIN, chain.m_presence);
    }

    if (chain.m_lowpass) {
        chain.m_lowpass->setParameter(FMOD_DSP_LOWPASS_SIMPLE_CUTOFF, chain.m_cutoff);
    }

    if (chain.m_compressor) {
        chain.m_compressor->setParameter(FMOD_DSP_COMPRESSOR_THRESHOLD, -boost - 1.0f);
        chain.m_compressor->setParameter(FMOD_DSP_COMPRESSOR_ATTACK, 10.0f);
        chain.m_compressor->setParameter(FMOD_DSP_COMPRESSOR_RELEASE, 400.0f);
        chain.m_compressor->setParameter(FMOD_DSP_COMPRESSOR_GAINMAKEUP, boost);
        // no boost: the compressor does nothing worth its cost
        chain.m_compressor->setBypass(boost <= 0.0f);
    }
}

bool ThunderDspEnsure(ThunderDspChain& chain, FMOD::System* system, float bass, float boostDb) {
    if (chain.m_group) {
        return true;
    }

    if (!system || system->createChannelGroup(chain.m_name, &chain.m_group) != FMOD_OK || !chain.m_group) {
        chain.m_group = nullptr;
        return false;
    }

    // signal order: bass 50 Hz, bass 110 Hz, presence, lowpass, compressor
    chain.m_compressor = AddDsp(system, chain.m_group, FMOD_DSP_TYPE_COMPRESSOR);
    chain.m_lowpass = chain.m_cutoff > 0.0f ? AddDsp(system, chain.m_group, FMOD_DSP_TYPE_LOWPASS_SIMPLE) : nullptr;
    chain.m_presenceEq = chain.m_presence != 1.0f ? AddDsp(system, chain.m_group, FMOD_DSP_TYPE_PARAMEQ) : nullptr;
    chain.m_bassHigh = AddDsp(system, chain.m_group, FMOD_DSP_TYPE_PARAMEQ);
    chain.m_bassLow = AddDsp(system, chain.m_group, FMOD_DSP_TYPE_PARAMEQ);
    ThunderDspApply(chain, bass, boostDb);
    return true;
}

bool ThunderDspAttach(ThunderDspChain& chain, FMOD::Channel* channel, float pitch, float bass, float boostDb) {
    FMOD::System* system = nullptr;

    if (!channel || channel->getSystemObject(&system) != FMOD_OK || !ThunderDspEnsure(chain, system, bass, boostDb)) {
        return false;
    }

    channel->setChannelGroup(chain.m_group);

    if (pitch > 0.1f && fabsf(pitch - 1.0f) > 0.001f) {
        float frequency = 0.0f;

        if (channel->getFrequency(&frequency) == FMOD_OK && frequency > 0.0f) {
            channel->setFrequency(frequency * pitch);
        }
    }

    return true;
}

void ThunderDspRelease(ThunderDspChain& chain) {
    FMOD::DSP** dsps[] = { &chain.m_bassLow, &chain.m_bassHigh, &chain.m_presenceEq, &chain.m_lowpass, &chain.m_compressor };

    for (FMOD::DSP** dsp : dsps) {
        if (*dsp) {
            (*dsp)->remove();
            (*dsp)->release();
            *dsp = nullptr;
        }
    }

    if (chain.m_group) {
        chain.m_group->release();
        chain.m_group = nullptr;
    }
}
