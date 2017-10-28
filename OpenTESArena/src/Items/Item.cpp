#include <cassert>

#include "ArtifactData.h"
#include "Item.h"

Item::Item(const ArtifactData *artifactData)
{
	this->artifactData = (artifactData == nullptr) ? nullptr : artifactData->clone();
}

Item::~Item()
{

}

const ArtifactData *Item::getArtifactData() const
{
	return this->artifactData.get();
}
