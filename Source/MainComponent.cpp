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
	setSize(IPongView::kGridWidth, IPongView::kGridHeight);
	pong = IPongView::Create();
	this->startTimer(40);
}

MainComponent::~MainComponent()
{
}

//==============================================================================
void MainComponent::paint (Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    //g.fillAll (getLookAndFeel().findColour (ResizableWindow::backgroundColourId));

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

