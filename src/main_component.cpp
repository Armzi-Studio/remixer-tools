#include "app/main_component.hpp"
#include <iostream>

namespace remixer::desktop {

MainComponent::MainComponent()
    : state_(AppState::get_instance())
{
    state_.initialize_engine();
}

MainComponent::~MainComponent() {
#if defined(REMIXER_USE_JUCE)
    shutdownAudio();
#endif
}

void MainComponent::paint() {
    // Render modern dark-themed background with neon accents
    std::cout << "[UI Render] Painting Main Window (Modern Dark Theme)...\n";
}

void MainComponent::resized() {
    // Layout subcomponents (Navigation Header, Content View, Master Deck)
}

#if defined(REMIXER_USE_JUCE)
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    if (auto* engine = state_.get_engine()) {
        engine->initialize();
    }
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    auto* engine = state_.get_engine();
    if (!engine || !state_.is_playing()) {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    // Wrap JUCE AudioBuffer into Remixer C-ABI rmx_audio_buffer_t
    auto* juceBuffer = bufferToFill.buffer;
    float* channelPointers[2] = {
        juceBuffer->getWritePointer(0, bufferToFill.startSample),
        juceBuffer->getNumChannels() > 1 ? juceBuffer->getWritePointer(1, bufferToFill.startSample) : nullptr
    };

    rmx_audio_buffer_t rmx_buf{};
    rmx_buf.channels = channelPointers;
    rmx_buf.num_channels = static_cast<uint32_t>(juceBuffer->getNumChannels());
    rmx_buf.num_frames = static_cast<uint32_t>(bufferToFill.numSamples);
    rmx_buf.sample_rate = 48000;

    engine->process_block(&rmx_buf, &rmx_buf);
}

void MainComponent::releaseResources() {
    if (auto* engine = state_.get_engine()) {
        engine->stop();
    }
}
#endif

} // namespace remixer::desktop
