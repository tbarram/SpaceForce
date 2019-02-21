/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#include "MainComponent.h"
#include "SpaceForce.h"
#include <random>
#include <algorithm>

std::random_device rd;
std::mt19937 randomizer(rd());


struct SongInfo
{
	std::string mName;
	int32_t mPosition; // seconds
	int32_t mDuration; // seconds
	File mFile;
	AudioFormatReader* mReader;
	AudioFormatReaderSource* mSource;
	double mGain;
};

namespace
{
	IPongViewPtr pong = nullptr;
	
	Slider* rotarySlider = nullptr;
	LookAndFeel* laf = nullptr;

	AudioFormatManager formatManager;
	std::unique_ptr<AudioFormatReaderSource> readerSource;
	AudioTransportSource transportSource;

	std::vector<SongInfo> musicVector;
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
	
	std::vector<SongInfo> sMusicHistory;
	double kGainFactor = 1.0;
	
	const std::string kLocalMusicFolder = "../../../../Music/";
	const String kMusicFolder =
		File::getSpecialLocation(File::currentApplicationFile).getFullPathName() +
		"/Contents/Resources/Music/";
	
	//==============================================================================
	void AddSong(std::string name, int32_t pos, int32_t dur, std::string fileName, double gain = 1.0)
	{
		File file(kMusicFolder + fileName);
		if (!file.exists())
			file = File(kLocalMusicFolder + fileName);
		
		if (file.exists())
		{
			AudioFormatReader* reader = formatManager.createReaderFor(file);
			AudioFormatReaderSource* source = new AudioFormatReaderSource(reader, true);
			musicVector.push_back({name, pos, dur, file, reader, source, gain});
		}
	}
}

//==============================================================================
int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }

//==============================================================================
void MainComponent::MusicCallback()
{
	if (musicVector.size() <= 0)
		return;
	
	// update index
	musicIndex = ((musicIndex + 1) % musicVector.size());
	
	SongInfo& song = musicVector[musicIndex];
	sMusicHistory.push_back(song);
	
	if (song.mReader)
	{
		transportSource.setSource(song.mSource, 0, nullptr, song.mReader->sampleRate);
		this->prepareToPlay(512, song.mReader->sampleRate);
		transportSource.setPosition(song.mPosition);
		kGainFactor = song.mGain;
		transportSource.start();
	}
	
	pong->SetSongName(song.mName);
	
	// schedule the next one based on the duration specified
	// this will cancel the current scheduled event and re-start it
	const int64_t fileLengthSeconds = song.mReader->lengthInSamples * song.mReader->sampleRate;
	const int32_t secondsUntilNext = (const int32_t)min64(fileLengthSeconds, song.mDuration);
	musicTimer->startTimer(secondsUntilNext * 1000);
}

//==============================================================================
void MainComponent::RotaryCallback(int32_t val)
{
	laf->setColour(Slider::rotarySliderFillColourId, pong->ColorForScore(val));
	laf->setColour(Slider::thumbColourId, Colours::mediumslateblue);
	rotarySlider->setValue(val);
	rotarySlider->setVisible(val > 0);
	//printf("RotaryCallback val: %d\n", val);
}

//==============================================================================
MainComponent::MainComponent()
{
	triggerAsyncUpdate();
	pong = IPongView::Create();
	setSize(pong->GetGridWidth(), pong->GetGridHeight());
	this->startTimer(pong->GetRefreshrateMS());
	srand((int32_t)time(nullptr));
	
	rotarySlider = new Slider(Slider::Rotary, Slider::NoTextBox);
	this->addAndMakeVisible(rotarySlider);
	
	laf = new LookAndFeel_V2();
	rotarySlider->setLookAndFeel(laf);
	rotarySlider->setRange(0, 5000);
	//rotarySlider->setSkewFactor(0.5);
	laf->setColour(Slider::rotarySliderOutlineColourId, Colours::black);
	
	this->RotaryCallback(0);
	
	const int32_t sliderWidth = 140;
	const int32_t sliderHeight = 140;
	const int32_t sliderY = 240;
	rotarySlider->setBounds(pong->GetGridWidth()/2 - sliderWidth/2, sliderY, sliderWidth, sliderHeight);
	
	pong->InstallRotaryCallback([this](int32_t val) { this->RotaryCallback(val); });
	pong->InstallMusicCallback([this]() { this->MusicCallback(); }); //

	// audio
	setAudioChannels(0, 2);
	formatManager.registerBasicFormats();
	
	const int32_t gDuration = 30;
	
	// populate the music list
	AddSong("Heaven Or Las Vegas", 104, gDuration, "HeavenOrLasVegas.mp3");
	AddSong("Anthems", 43, 50, "Anthems.m4a");
	AddSong("Roygbiv", 0, 60, "Roygbiv.mp3");
	AddSong("In Particular", 20, gDuration, "InParticular.mp3");
	AddSong("Someone's Daughter", 26, 70, "SomeonesDaughter.mp3");
	AddSong("All I Need", 0, 70, "AllINeed.mp3");
	AddSong("We Are the People", 79, gDuration, "WeAreThePeople.mp3", 0);
	AddSong("Heavy Lifting", 26, gDuration, "HeavyLifting.mp3", 0.7);
	AddSong("Heavy Lifting", 140, gDuration, "HeavyLifting.mp3");
	AddSong("Round And Round", 106, 40, "RoundAndRound.mp3");
	AddSong("The Funeral", 60, gDuration, "TheFuneral.mp3", 0.7);
	AddSong("Furr", 0, 40, "Furr.m4a");
	AddSong("Babymaker", 50, 40, "Babymaker.m4a");
	AddSong("Fall In Love", gDuration, 40, "FallInLove.m4a", 0.7);
	AddSong("Too Young", 0, 68, "TooYoung.mp3");
	//AddSong("Girl U Want", 50, 37, "GirlUWant.mp3");
	AddSong("Living In America", 25, 30, "LivingInAmerica.mp3", 0.7);
	AddSong("Play My Song", 17, 37, "PlayMySong.mp3");
	AddSong("A Skull, A Suitcase, And A Long Red Bottle Of Wine", 80, gDuration, "SkullSuitcase.mp3");
	AddSong("Winchester", 0, 37, "Winchester.mp3");
	AddSong("To Turn You On", 128, 32, "ToTurnYouOn.mp3");
	AddSong("Ladytron", 0, gDuration, "Ladytron_.mp3");
	AddSong("Inspiration Information", 0, 40, "InspirationInformation.mp3");
	AddSong("Here's Where the Story Ends", 0, gDuration, "StoryEnds.mp3");
	AddSong("Big Sur", 4, gDuration, "BigSur.mp3");
	AddSong("Islands", 4, gDuration, "Islands.mp3");
	AddSong("Lions", 0, gDuration, "Lions.mp3");
	AddSong("Shadowban", 0, 50, "Shadowban.wav"); // make me mp3
	AddSong("Seven", 30, gDuration, "Seven.mp3");
	AddSong("Millenium", 0, gDuration, "Millenium.mp3");
	AddSong("Try", 0, gDuration, "Try.mp3");
	AddSong("Ride", 122, 40, "Ride.wav"); // make me mp3
	AddSong("Solar", 0, 60, "Solar.mp3");
	AddSong("Sunday", 75, 40, "Sunday.aiff"); // make me mp3
	
	// shuffle the vector
	std::shuffle(musicVector.begin(), musicVector.end(), randomizer);
	
	// start the music
	//musicTimer = new MusicTimer(this);
	//this->MusicCallback();
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
    g.setFont(Font(16.0f));
    g.setColour(Colours::lawngreen);
    //g.drawText ("Space Force", getLocalBounds(), Justification::centred, true);
	
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


