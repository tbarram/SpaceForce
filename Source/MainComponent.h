/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#pragma once
#include "../JuceLibraryCode/JuceHeader.h"

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent   : public AudioAppComponent,
						public AsyncUpdater,
						public Timer
						//public ApplicationCommandTarget
{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent();

    //==============================================================================
    virtual void paint(Graphics&) override;
    virtual void resized() override;
	virtual void timerCallback() override;
	virtual void handleAsyncUpdate() override;
	
	// consume all key presses
	virtual bool keyPressed (const KeyPress& key) override { return true; }
	
	void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
	void getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill) override;
	void releaseResources() override;
	
	void MusicCallback();
	void RotaryCallback(int32_t val);
	
	/*
	ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
	void getAllCommands (Array<CommandID>& commands) override;
	void getCommandInfo (CommandID commandID, ApplicationCommandInfo& result) override;
	bool perform (const InvocationInfo& info) override;*/

private:
    //==============================================================================
    // Your private member variables go here...
	LookAndFeel_V2 l;


   // JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
