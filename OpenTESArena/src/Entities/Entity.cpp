#include <algorithm>

#include "Entity.h"
#include "EntityManager.h"
#include "EntityType.h"
#include "../Game/Game.h"
#include "../World/ChunkUtils.h"

Entity::Entity()
	: position(ChunkInt2::Zero, VoxelDouble2::Zero)
{
	this->id = EntityManager::NO_ID;
	this->defID = EntityManager::NO_DEF_ID;
	this->animInst.clear();
}

void Entity::init(EntityDefID defID, const EntityAnimationInstance &animInst)
{
	DebugAssert(this->id != EntityManager::NO_ID);
	this->defID = defID;
	this->animInst = animInst;
}

EntityID Entity::getID() const
{
	return this->id;
}

EntityDefID Entity::getDefinitionID() const
{
	return this->defID;
}

const CoordDouble2 &Entity::getPosition() const
{
	return this->position;
}

EntityAnimationInstance &Entity::getAnimInstance()
{
	return this->animInst;
}

const EntityAnimationInstance &Entity::getAnimInstance() const
{
	return this->animInst;
}

void Entity::getViewIndependentBBox2D(const EntityManager &entityManager,
	const EntityDefinitionLibrary &entityDefLibrary, CoordDouble2 *outMin, CoordDouble2 *outMax) const
{
	DebugAssert(this->defID != EntityManager::NO_DEF_ID);

	const EntityDefinition &entityDef = entityManager.getEntityDef(this->defID, entityDefLibrary);
	const EntityAnimationDefinition &animDef = entityDef.getAnimDef();

	// Get the largest width from the animation frames.
	double maxAnimWidth, dummyMaxAnimHeight;
	EntityUtils::getAnimationMaxDims(animDef, &maxAnimWidth, &dummyMaxAnimHeight);
	static_cast<void>(dummyMaxAnimHeight);

	const double halfMaxWidth = maxAnimWidth * 0.50;

	// Orient the bounding box so it is largest with respect to the grid. Recalculate the coordinates in case
	// the min and max are in different chunks.
	*outMin = ChunkUtils::recalculateCoord(
		this->position.chunk,
		VoxelDouble2(this->position.point.x - halfMaxWidth, this->position.point.y - halfMaxWidth));
	*outMax = ChunkUtils::recalculateCoord(
		this->position.chunk,
		VoxelDouble2(this->position.point.x + halfMaxWidth, this->position.point.y + halfMaxWidth));
}

void Entity::getViewIndependentBBox3D(double flatPosY, const EntityManager &entityManager,
	const EntityDefinitionLibrary &entityDefLibrary, CoordDouble3 *outMin, CoordDouble3 *outMax) const
{
	DebugAssert(this->defID != EntityManager::NO_DEF_ID);

	const EntityDefinition &entityDef = entityManager.getEntityDef(this->defID, entityDefLibrary);
	const EntityAnimationDefinition &animDef = entityDef.getAnimDef();

	// Get the largest width and height from the animation frames.
	double maxAnimWidth, maxAnimHeight;
	EntityUtils::getAnimationMaxDims(animDef, &maxAnimWidth, &maxAnimHeight);

	const double halfMaxWidth = maxAnimWidth * 0.50;

	// Orient the bounding box so it is largest with respect to the grid. Recalculate the coordinates in case
	// the min and max are in different chunks.
	const VoxelDouble3 minPoint(
		this->position.point.x - halfMaxWidth,
		flatPosY,
		this->position.point.y - halfMaxWidth);
	const VoxelDouble3 maxPoint(
		this->position.point.x + halfMaxWidth,
		flatPosY + maxAnimHeight,
		this->position.point.y + halfMaxWidth);
	*outMin = ChunkUtils::recalculateCoord(this->position.chunk, minPoint);
	*outMax = ChunkUtils::recalculateCoord(this->position.chunk, maxPoint);
}

void Entity::setID(EntityID id)
{
	this->id = id;
}

void Entity::setPosition(const CoordDouble2 &position, EntityManager &entityManager)
{
	this->position = position;
	entityManager.updateEntityChunk(this);
}

void Entity::reset()
{
	// Don't change the entity type -- the entity manager doesn't change an allocation's entity
	// group between lifetimes.
	this->id = EntityManager::NO_ID;
	this->defID = EntityManager::NO_DEF_ID;
	this->position = CoordDouble2(ChunkInt2::Zero, VoxelDouble2::Zero);
	this->animInst.clear();
}

void Entity::tick(Game &game, double dt)
{
	this->animInst.update(dt);
}
