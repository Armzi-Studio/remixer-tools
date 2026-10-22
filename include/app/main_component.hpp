#pragma once

#include "app/app_state.hpp"

#if defined(REMIXER_USE_JUCE)
    #include <juce_gui_basics/juce_gui_basics.h>
    #include <juce_audio_utils/juce_audio_utils.h>
    using ComponentBase = juce::AudioAppComponent;
#else
    // Lightweight C++ Native GUI Mock for build without full JUCE framework
    class ComponentBase {
    public:
        virtual ~ComponentBase() = default;
        virtual void resized() {}
        virtual void paint() {}
    };
#endif

namespace remixer::desktop {

class MainComponent : public ComponentBase {
public:
    MainComponent();
    ~MainComponent() override;

    void paint() override;
    void resized() override;

#if defined(REMIXER_USE_JUCE)
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
#endif

private:
    AppState& state_;
};

} // namespace remixer::desktop
