// copyright (c) 1990-2017 by Digidesign, Inc. All rights reserved.

/*****************************************************************************

	UPongView.h
	
	Ted Barram 4/29/17
*****************************************************************************/
#pragma once

#include <memory>
#include "../JuceLibraryCode/JuceHeader.h"


typedef std::shared_ptr<class IPongView> IPongViewPtr;

// IPongView
class IPongView
{
public:
	static IPongViewPtr Create();
	virtual void Draw(Graphics& g) {};
	virtual int32_t GetRefreshrateMS() { return 100; };
	virtual int32_t GetGridWidth() { return 0; };
	virtual int32_t GetGridHeight() { return 0; };
	virtual void SetSongName(std::string name) {};
	virtual void InstallMusicCallback(std::function<void()> f) {};
	virtual void InstallRotaryCallback(std::function<void(int32_t)> f) {};
	virtual void InstallHighScoreCallback(std::function<void(std::string)> f) {};
	virtual Colour ColorForScore(int32_t score) { return Colours::red; }
	virtual void SetHighScore(std::string score) {};
	virtual ~IPongView() {}
};
