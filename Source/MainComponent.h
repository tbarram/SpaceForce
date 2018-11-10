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
class MainComponent   : public Component,
						public AsyncUpdater,
						public Timer
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

private:
    //==============================================================================
    // Your private member variables go here...


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
