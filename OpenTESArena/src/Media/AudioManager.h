#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <memory>
#include <string>

// This class manages what sounds and music are played by OpenAL Soft.

class AudioManagerImpl;
class Options;

class AudioManager
{
private:
	std::unique_ptr<AudioManagerImpl> pImpl;
public:
	AudioManager();
	~AudioManager();

    void init(double musicVolume, double soundVolume, int maxChannels, 
		const std::string &midiConfig);

	static const double MIN_VOLUME;
	static const double MAX_VOLUME;

	double getMusicVolume() const;
	double getSoundVolume() const;

	// Plays a music file. All music should loop until changed.
	void playMusic(const std::string &filename);

	// Plays a sound file. All sounds should play once.
	void playSound(const std::string &filename);

	// Stops the music.
	void stopMusic();

	// Stops all sounds.
	void stopSound();

	// Sets the music volume. Percent must be between 0.0 and 1.0.
	void setMusicVolume(double percent);

	// Sets the sound volume. Percent must be between 0.0 and 1.0.
	void setSoundVolume(double percent);

	// Updates any state not handled by a background thread, such as resetting 
	// the sources of finished sounds.
	void update();
};

#endif
