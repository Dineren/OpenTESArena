#ifndef ENTITY_ANIMATION_INSTANCE_H
#define ENTITY_ANIMATION_INSTANCE_H

#include <array>
#include <string>
#include <vector>

#include "EntityAnimationDefinition.h"
#include "EntityAnimationLibrary.h"
#include "EntityAnimationUtils.h"
#include "../Media/TextureUtils.h"

#include "components/utilities/BufferView.h"

// Instance-specific animation data, references a shared animation definition.

class EntityAnimationInstance
{
public:
	class Keyframe
	{
	private:
		ImageID imageID;
	public:
		Keyframe(ImageID imageID);

		int getImageID() const;
	};

	class KeyframeList
	{
	private:
		std::vector<Keyframe> keyframes;
	public:
		int getKeyframeCount() const;
		const Keyframe &getKeyframe(int index) const;

		void addKeyframe(Keyframe &&keyframe);
		void clearKeyframes();
	};

	class State
	{
	private:
		std::vector<KeyframeList> keyframeLists;
	public:
		int getKeyframeListCount() const;
		const KeyframeList &getKeyframeList(int index) const;

		void addKeyframeList(KeyframeList &&keyframeList);
		void clearKeyframeLists();
	};
private:
	std::vector<State> states;
	double currentSeconds; // Seconds through current state.
	int stateIndex; // Active state, also usable with animation definition states.
	EntityAnimID animID; // Animation definition handle.

	// @todo: other fancy stuff, like discriminated union for what kind of animation instance;
	// NPC weapon ID, townsperson generated texture ID, etc..
public:
	EntityAnimationInstance();

	int getStateCount() const;
	const State &getState(int index) const;
	double getCurrentSeconds() const;
	int getStateIndex() const;
	EntityAnimID getAnimID() const;

	void addState(State &&state);
	void clearStates();

	// Sets the active state index shared between this instance and its definition.
	void setStateIndex(int index);

	// Sets the entity animation definition ID used by this instance.
	void setAnimID(EntityAnimID animID);

	void reset();
	void resetTime();

	// Animates the instance by delta time and loops if the total seconds is exceeded.
	void tick(double dt, double totalSeconds, bool looping);
};

#endif
