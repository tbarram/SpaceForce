/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#include "MainComponent.h"
#include "UPongView.h"

IPongViewPtr pong = nullptr;




//==============================================================================
MainComponent::MainComponent()
{
	triggerAsyncUpdate();
	pong = IPongView::Create();
	setSize(pong->GetGridWidth(), pong->GetGridHeight());
	this->startTimer(pong->GetRefreshrateMS());
}

MainComponent::~MainComponent()
{
	pong.reset();
}

//==============================================================================
void MainComponent::paint (Graphics& g)
{
    g.setFont (Font (16.0f));
    g.setColour (Colours::lawngreen);
    g.drawText ("Space Force", getLocalBounds(), Justification::centred, true);
	
	pong->Draw(g);
}

//==============================================================================
void MainComponent::resized()
{
    // This is called when the MainComponent is resized.
    // If you add any child components, this is where you should
    // update their positions.
}

//==============================================================================
void MainComponent::timerCallback()
{
	this->repaint();
}

//==============================================================================
void MainComponent::handleAsyncUpdate()
{
	// cant do this in ctor since window isnt visible yet
	setWantsKeyboardFocus(true);
	grabKeyboardFocus();
}

