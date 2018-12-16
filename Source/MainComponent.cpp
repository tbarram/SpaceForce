/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#include "MainComponent.h"
#include "UPongView.h"
#include <random>
#include <algorithm>

std::random_device rd;
std::mt19937 randomizer(rd());

// position and duration are in seconds
// name, position, duration, file, reader, gainFactor
using MusicInfo = std::tuple<std::string, int32_t, int32_t, File, double>;

namespace
{
	IPongViewPtr pong = nullptr;

	AudioFormatManager formatManager;
	std::unique_ptr<AudioFormatReaderSource> readerSource;
	AudioTransportSource transportSource;

	std::vector<MusicInfo> musicVector;
	int32_t musicIndex = -1; // this starts us at 0
	
	class MusicTimer : public Timer
	{
	public:
		MusicTimer(MainComponent* main) : mMain(main) {}
		virtual void timerCallback() override { mMain->MusicCallback(); }
		
	private:
		MainComponent* mMain;
	};
	
	MusicTimer* musicTimer = nullptr;
	
	std::vector<MusicInfo> sMusicHistory;
	double kGainFactor = 1.0;
	const std::string kMusicFolder = "../../../../Music/";
	
	//==============================================================================
	void AddSong(std::string name, int32_t pos, int32_t dur, std::string fileName, double gain = 1.0)
	{
		musicVector.push_back({name, pos, dur, File(kMusicFolder + fileName), 1.0});
	}
}

//==============================================================================
void MainComponent::MusicCallback()
{
	// update index
	musicIndex = ((musicIndex + 1) % musicVector.size());
	
	// random no repeat
	if (musicIndex == 0)
		std::shuffle(musicVector.begin(), musicVector.end(), randomizer);
	
	MusicInfo& song = musicVector[musicIndex];
	sMusicHistory.push_back(song);
	
	// decomposition declarations - cool
	const auto [name, position, duration, file, kGainFactor] = song;
	
	auto* reader = formatManager.createReaderFor(file);
	if (reader)
	{
		std::unique_ptr<AudioFormatReaderSource> source(new AudioFormatReaderSource(reader, true));
		transportSource.setSource(source.get(), 0, nullptr, reader->sampleRate);                                                                                       // [13]
		readerSource.reset(source.release());
		this->prepareToPlay(512, reader->sampleRate);
		transportSource.setPosition(position);
		transportSource.start();
	}
	
	pong->SetSongName(name);
	
	// schedule the next one based on the duration specified
	// this will cancel the current scheduled event and re-start it
	musicTimer->startTimer(duration * 1000);
}

//==============================================================================
MainComponent::MainComponent()
{
	triggerAsyncUpdate();
	pong = IPongView::Create();
	setSize(pong->GetGridWidth(), pong->GetGridHeight());
	this->startTimer(pong->GetRefreshrateMS());
	srand((int32_t)time(nullptr));
	
	pong->InstallMusicCallback([this]() { this->MusicCallback(); });

	// audio
	setAudioChannels(0, 2);
	
	// populate the music list
	AddSong("Heaven Or Las Vegas", 104, 30, "HeavenOrLasVegas.mp3");
	AddSong("Anthems", 43, 30, "Anthems.m4a");
	AddSong("Roygbiv", 0, 60, "Roygbiv.mp3");
	AddSong("In Particular", 20, 30, "InParticular.mp3");
	AddSong("Someone's Daughter", 26, 70, "SomeonesDaughter.mp3");
	AddSong("All I Need", 0, 70, "AllINeed.mp3");
	AddSong("We Are the People", 79, 30, "WeAreThePeople.mp3");
	AddSong("Heavy Lifting", 26, 30, "HeavyLifting.mp3", 0.7);
	AddSong("Heavy Lifting", 140, 30, "HeavyLifting.mp3");
	AddSong("Round And Round", 106, 30, "RoundAndRound.mp3");
	AddSong("The Funeral", 60, 30, "TheFuneral.mp3", 0.7);
	AddSong("Furr", 0, 40, "Furr.m4a");
	AddSong("Babymaker", 50, 40, "Babymaker.m4a");
	AddSong("Fall In Love", 30, 40, "FallInLove.m4a", 0.7);
	AddSong("Too Young", 0, 68, "TooYoung.mp3");
	AddSong("Girl U Want", 50, 37, "GirlUWant.mp3");
	AddSong("Living In America", 25, 30, "LivingInAmerica.mp3", 0.7);
	AddSong("Play My Song", 17, 37, "PlayMySong.mp3");
	AddSong("A Skull, A Suitcase, And A Long Red Bottle Of Wine", 80, 30, "SkullSuitcase.mp3");
	AddSong("Winchester", 0, 37, "Winchester.mp3");
	AddSong("To Turn You On", 128, 32, "ToTurnYouOn.mp3");
	AddSong("Ladytron", 81, 30, "Ladytron.mp3");
	AddSong("Inspiration Information", 0, 40, "InspirationInformation.mp3");
	AddSong("Here's Where the Story Ends", 0, 30, "StoryEnds.mp3");
	AddSong("Big Sur", 4, 30, "BigSur.mp3");
	AddSong("Islands", 4, 30, "Islands.mp3");
	AddSong("Lions", 0, 30, "Lions.mp3");
	AddSong("Shadowban", 0, 30, "Shadowban.wav"); // make me mp3
	AddSong("Seven", 30, 30, "Seven.mp3");
	AddSong("Millenium", 0, 30, "Millenium.mp3");
	AddSong("Try", 0, 30, "Try.mp3");
	AddSong("Ride", 122, 40, "Ride.wav"); // make me mp3
	AddSong("Solar", 0, 30, "Solar.mp3");
	AddSong("Sunday", 75, 40, "Sunday.aiff"); // make me mp3
	
	formatManager.registerBasicFormats();
	
	// start the music
	musicTimer = new MusicTimer(this);
	this->MusicCallback();
}

//==============================================================================
MainComponent::~MainComponent()
{
	pong.reset();
	transportSource.stop();
	shutdownAudio();
	delete musicTimer;
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

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
	transportSource.prepareToPlay (samplesPerBlockExpected, sampleRate);
}

//==============================================================================
/*void MainComponent::getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill)
{
	if (readerSource.get() == nullptr)
	{
		bufferToFill.clearActiveBufferRegion();
		return;
	}
	
	transportSource.getNextAudioBlock(bufferToFill);
}*/

//==============================================================================
void MainComponent::getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill)
{
	transportSource.getNextAudioBlock(bufferToFill);
	
	if (kGainFactor != 1.0)
	{
		for (auto channel = 0; channel < bufferToFill.buffer->getNumChannels(); ++channel)
		{
			auto* buffer = bufferToFill.buffer->getWritePointer (channel, bufferToFill.startSample);
			for (auto sample = 0; sample < bufferToFill.numSamples; ++sample)
				buffer[sample] *= kGainFactor;
		}
	}
}

//==============================================================================
void MainComponent::releaseResources()
{
	transportSource.releaseResources();
}


