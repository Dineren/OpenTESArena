#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "CommonUiView.h"
#include "FastTravelSubPanel.h"
#include "GameWorldPanel.h"
#include "ProvinceMapPanel.h"
#include "ProvinceMapUiController.h"
#include "ProvinceMapUiView.h"
#include "WorldMapPanel.h"
#include "../Game/Game.h"
#include "../Input/InputActionName.h"
#include "../UI/CursorData.h"
#include "../UI/FontLibrary.h"
#include "../UI/Surface.h"
#include "../UI/TextRenderUtils.h"
#include "../WorldMap/ArenaLocationUtils.h"

#include "components/debug/Debug.h"
#include "components/utilities/String.h"

void ProvinceMapPanel::LocationTextureRefGroup::init(UiTextureID textureID, UiTextureID playerCurrentTextureID,
	UiTextureID travelDestinationTextureID, Renderer &renderer)
{
	this->textureRef.init(textureID, renderer);
	this->playerCurrentTextureRef.init(playerCurrentTextureID, renderer);
	this->travelDestinationTextureRef.init(travelDestinationTextureID, renderer);
}

ProvinceMapPanel::ProvinceMapPanel(Game &game)
	: Panel(game) { }

bool ProvinceMapPanel::init(int provinceID)
{
	auto &game = this->getGame();
	auto &renderer = game.getRenderer();
	const auto &fontLibrary = FontLibrary::getInstance();
	const TextBox::InitInfo hoveredLocationTextBoxInitInfo = ProvinceMapUiView::getHoveredLocationTextBoxInitInfo(fontLibrary);
	if (!this->hoveredLocationTextBox.init(hoveredLocationTextBoxInitInfo, renderer))
	{
		DebugLogError("Couldn't init hovered location text box.");
		return false;
	}

	this->searchButton = []()
	{
		const Rect &clickArea = ProvinceMapUiView::SearchButtonRect;
		return Button<Game&, ProvinceMapPanel&, int>(
			clickArea.getLeft(),
			clickArea.getTop(),
			clickArea.getWidth(),
			clickArea.getHeight(),
			ProvinceMapUiController::onSearchButtonSelected);
	}();

	this->travelButton = []()
	{
		const Rect &clickArea = ProvinceMapUiView::TravelButtonRect;
		return Button<Game&, ProvinceMapPanel&>(
			clickArea.getLeft(),
			clickArea.getTop(),
			clickArea.getWidth(),
			clickArea.getHeight(),
			ProvinceMapUiController::onTravelButtonSelected);
	}();

	this->backToWorldMapButton = []()
	{
		const Rect &clickArea = ProvinceMapUiView::BackToWorldMapRect;
		return Button<Game&>(
			clickArea.getLeft(),
			clickArea.getTop(),
			clickArea.getWidth(),
			clickArea.getHeight(),
			ProvinceMapUiController::onBackToWorldMapButtonSelected);
	}();

	// Use fullscreen button proxy to determine what was clicked since there is button overlap.
	this->addButtonProxy(MouseButtonType::Left,
		Rect(0, 0, ArenaRenderUtils::SCREEN_WIDTH, ArenaRenderUtils::SCREEN_HEIGHT),
		[this, &game]()
	{
		const auto &inputManager = game.getInputManager();
		const Int2 mousePosition = inputManager.getMousePosition();
		const Int2 classicPosition = game.getRenderer().nativeToOriginal(mousePosition);

		if (this->searchButton.contains(classicPosition))
		{
			this->searchButton.click(game, *this, this->provinceID);
		}
		else if (this->travelButton.contains(classicPosition))
		{
			this->travelButton.click(game, *this);
		}
		else if (this->backToWorldMapButton.contains(classicPosition))
		{
			this->backToWorldMapButton.click(game);
		}
		else
		{
			// The closest location to the cursor was clicked. See if it can be set as the
			// travel destination (depending on whether the player is already there).
			this->trySelectLocation(this->hoveredLocationID);
		}
	});

	this->addInputActionListener(InputActionName::Back,
		[this, &game](const InputActionCallbackValues &values)
	{
		if (values.performed)
		{
			this->backToWorldMapButton.click(game);
		}
	});

	this->addMouseMotionListener([this](Game &game, int dx, int dy)
	{
		const auto &renderer = game.getRenderer();
		const auto &inputManager = game.getInputManager();
		const Int2 mousePosition = inputManager.getMousePosition();
		const Int2 originalPosition = renderer.nativeToOriginal(mousePosition);
		this->updateHoveredLocationID(originalPosition);
	});

	auto &textureManager = game.getTextureManager();
	const auto &binaryAssetLibrary = BinaryAssetLibrary::getInstance();
	const UiTextureID backgroundTextureID = ProvinceMapUiView::allocBackgroundTexture(provinceID, binaryAssetLibrary, textureManager, renderer);
	this->backgroundTextureRef.init(backgroundTextureID, renderer);
	this->addDrawCall(
		this->backgroundTextureRef.get(),
		Int2::Zero,
		Int2(ArenaRenderUtils::SCREEN_WIDTH, ArenaRenderUtils::SCREEN_HEIGHT),
		PivotType::TopLeft);

	initLocationIconUI(provinceID);

	UiDrawCall::TextureFunc hoveredLocationTextureFunc = [this]()
	{
		return this->hoveredLocationTextBox.getTextureID();
	};

	UiDrawCall::PositionFunc hoveredLocationPositionFunc = [this, &game]()
	{
		auto &gameState = game.getGameState();
		const WorldMapDefinition &worldMapDef = gameState.getWorldMapDefinition();
		const ProvinceDefinition &provinceDef = worldMapDef.getProvinceDef(this->provinceID);
		const LocationDefinition &locationDef = provinceDef.getLocationDef(this->hoveredLocationID);

		const Int2 locationCenter = ProvinceMapUiView::getLocationCenterPoint(game, this->provinceID, this->hoveredLocationID);
		const Int2 textBoxCenter = locationCenter - Int2(0, 10);

		// Can't use the text box dimensions with clamping since it's allocated for worst-case location name now.
		const auto &fontLibrary = FontLibrary::getInstance();
		const std::string &fontName = ProvinceMapUiView::LocationFontName;
		int fontDefIndex;
		if (!fontLibrary.tryGetDefinitionIndex(fontName.c_str(), &fontDefIndex))
		{
			DebugCrash("Couldn't get hovered location font name \"" + fontName + "\".");
		}

		const FontDefinition &fontDef = fontLibrary.getDefinition(fontDefIndex);

		const std::string locationName = ProvinceMapUiModel::getLocationName(game, this->provinceID, this->hoveredLocationID);
		TextRenderUtils::TextShadowInfo shadowInfo;
		shadowInfo.init(ProvinceMapUiView::LocationTextShadowOffsetX, ProvinceMapUiView::LocationTextShadowOffsetY,
			ProvinceMapUiView::LocationTextShadowColor);
		const TextRenderUtils::TextureGenInfo textureGenInfo = TextRenderUtils::makeTextureGenInfo(locationName, fontDef, shadowInfo);

		// Clamp to screen edges, with some extra space on the left and right (note this clamped position
		// is for the TopLeft pivot type).
		const Rect textBoxRect(textBoxCenter, textureGenInfo.width, textureGenInfo.height);
		const Int2 clampedCenter = ProvinceMapUiView::getLocationTextClampedCenter(textBoxRect);
		return clampedCenter;
	};

	UiDrawCall::SizeFunc hoveredLocationSizeFunc = [this]()
	{
		const Rect &hoveredLocationTextBoxRect = this->hoveredLocationTextBox.getRect();
		return Int2(hoveredLocationTextBoxRect.getWidth(), hoveredLocationTextBoxRect.getHeight());
	};

	UiDrawCall::PivotFunc hoveredLocationPivotFunc = []()
	{
		return PivotType::Middle;
	};

	UiDrawCall::ActiveFunc hoveredLocationActiveFunc = [this]()
	{
		return !this->isPaused();
	};

	this->addDrawCall(
		hoveredLocationTextureFunc,
		hoveredLocationPositionFunc,
		hoveredLocationSizeFunc,
		hoveredLocationPivotFunc,
		hoveredLocationActiveFunc);

	const UiTextureID cursorTextureID = CommonUiView::allocDefaultCursorTexture(textureManager, renderer);
	this->cursorTextureRef.init(cursorTextureID, renderer);
	this->addCursorDrawCall(this->cursorTextureRef.get(), PivotType::TopLeft, hoveredLocationActiveFunc);

	this->blinkState.init(ProvinceMapUiView::BlinkPeriodSeconds, true);
	this->provinceID = provinceID;
	this->hoveredLocationID = -1;

	const auto &inputManager = game.getInputManager();
	const Int2 mousePosition = inputManager.getMousePosition();
	const Int2 originalPosition = renderer.nativeToOriginal(mousePosition);
	this->updateHoveredLocationID(originalPosition);

	return true;
}

void ProvinceMapPanel::initLocationIconUI(int provinceID)
{
	auto &game = this->getGame();
	auto &textureManager = game.getTextureManager();
	auto &renderer = game.getRenderer();
	const auto &binaryAssetLibrary = BinaryAssetLibrary::getInstance();

	// Location icon textures.
	const TextureAsset backgroundPaletteTextureAsset = ProvinceMapUiView::getBackgroundPaletteTextureAsset(provinceID, binaryAssetLibrary);
	this->cityStateTextureRefs.init(
		ProvinceMapUiView::allocCityStateIconTexture(ProvinceMapUiView::HighlightType::None, backgroundPaletteTextureAsset, textureManager, renderer),
		ProvinceMapUiView::allocCityStateIconTexture(ProvinceMapUiView::HighlightType::PlayerLocation, backgroundPaletteTextureAsset, textureManager, renderer),
		ProvinceMapUiView::allocCityStateIconTexture(ProvinceMapUiView::HighlightType::TravelDestination, backgroundPaletteTextureAsset, textureManager, renderer),
		renderer);
	this->townTextureRefs.init(
		ProvinceMapUiView::allocTownIconTexture(ProvinceMapUiView::HighlightType::None, backgroundPaletteTextureAsset, textureManager, renderer),
		ProvinceMapUiView::allocTownIconTexture(ProvinceMapUiView::HighlightType::PlayerLocation, backgroundPaletteTextureAsset, textureManager, renderer),
		ProvinceMapUiView::allocTownIconTexture(ProvinceMapUiView::HighlightType::TravelDestination, backgroundPaletteTextureAsset, textureManager, renderer),
		renderer);
	this->villageTextureRefs.init(
		ProvinceMapUiView::allocVillageIconTexture(ProvinceMapUiView::HighlightType::None, backgroundPaletteTextureAsset, textureManager, renderer),
		ProvinceMapUiView::allocVillageIconTexture(ProvinceMapUiView::HighlightType::PlayerLocation, backgroundPaletteTextureAsset, textureManager, renderer),
		ProvinceMapUiView::allocVillageIconTexture(ProvinceMapUiView::HighlightType::TravelDestination, backgroundPaletteTextureAsset, textureManager, renderer),
		renderer);
	this->dungeonTextureRefs.init(
		ProvinceMapUiView::allocDungeonIconTexture(ProvinceMapUiView::HighlightType::None, backgroundPaletteTextureAsset, textureManager, renderer),
		ProvinceMapUiView::allocDungeonIconTexture(ProvinceMapUiView::HighlightType::PlayerLocation, backgroundPaletteTextureAsset, textureManager, renderer),
		ProvinceMapUiView::allocDungeonIconTexture(ProvinceMapUiView::HighlightType::TravelDestination, backgroundPaletteTextureAsset, textureManager, renderer),
		renderer);

	if (ProvinceMapUiView::provinceHasStaffDungeonIcon(provinceID))
	{
		this->staffDungeonTextureRefs.init(
			ProvinceMapUiView::allocStaffDungeonIconTexture(provinceID, ProvinceMapUiView::HighlightType::None, backgroundPaletteTextureAsset, textureManager, renderer),
			ProvinceMapUiView::allocStaffDungeonIconTexture(provinceID, ProvinceMapUiView::HighlightType::PlayerLocation, backgroundPaletteTextureAsset, textureManager, renderer),
			ProvinceMapUiView::allocStaffDungeonIconTexture(provinceID, ProvinceMapUiView::HighlightType::TravelDestination, backgroundPaletteTextureAsset, textureManager, renderer),
			renderer);
	}

	auto &gameState = game.getGameState();
	const WorldMapInstance &worldMapInst = gameState.getWorldMapInstance();
	const ProvinceInstance &provinceInst = worldMapInst.getProvinceInstance(provinceID);
	const int provinceDefIndex = provinceInst.getProvinceDefIndex();
	const WorldMapDefinition &worldMapDef = gameState.getWorldMapDefinition();
	const ProvinceDefinition &provinceDef = worldMapDef.getProvinceDef(provinceDefIndex);
	const ProvinceDefinition &playerProvinceDef = gameState.getProvinceDefinition();
	for (int i = 0; i < provinceInst.getLocationCount(); i++)
	{
		const LocationInstance &locationInst = provinceInst.getLocationInstance(i);
		if (locationInst.isVisible())
		{
			const int locationDefIndex = locationInst.getLocationDefIndex();
			UiDrawCall::TextureFunc baseTextureFunc = [this, &provinceDef, locationDefIndex]()
			{
				const LocationDefinition &locationDef = provinceDef.getLocationDef(locationDefIndex);
				const LocationTextureRefGroup *textureRefGroupPtr = [this, &locationDef]() -> const LocationTextureRefGroup*
				{
					const LocationDefinition::Type locationDefType = locationDef.getType();
					if (locationDefType == LocationDefinition::Type::City)
					{
						const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
						const ArenaTypes::CityType cityType = cityDef.type;
						switch (cityType)
						{
						case ArenaTypes::CityType::CityState:
							return &this->cityStateTextureRefs;
						case ArenaTypes::CityType::Town:
							return &this->townTextureRefs;
						case ArenaTypes::CityType::Village:
							return &this->villageTextureRefs;
						default:
							DebugCrash("Unhandled city type \"" + std::to_string(static_cast<int>(cityType)) + "\".");
							return nullptr;
						}
					}
					else if (locationDefType == LocationDefinition::Type::Dungeon)
					{
						return &this->dungeonTextureRefs;
					}
					else if (locationDefType == LocationDefinition::Type::MainQuestDungeon)
					{
						const LocationDefinition::MainQuestDungeonDefinition &mainQuestDungeonDef = locationDef.getMainQuestDungeonDefinition();
						const LocationDefinition::MainQuestDungeonDefinition::Type mainQuestDungeonType = mainQuestDungeonDef.type;
						switch (mainQuestDungeonType)
						{
						case LocationDefinition::MainQuestDungeonDefinition::Type::Start:
						case LocationDefinition::MainQuestDungeonDefinition::Type::Map:
							return &this->dungeonTextureRefs;
						case LocationDefinition::MainQuestDungeonDefinition::Type::Staff:
							return &this->staffDungeonTextureRefs;
						default:
							DebugCrash("Unhandled main quest dungeon type \"" + std::to_string(static_cast<int>(mainQuestDungeonType)) + "\".");
							return nullptr;
						}
					}
					else
					{
						DebugCrash("Unhandled location definition type \"" + std::to_string(static_cast<int>(locationDefType)) + "\".");
						return nullptr;
					}
				}();

				DebugAssert(textureRefGroupPtr != nullptr);
				return textureRefGroupPtr->textureRef.get();
			};

			UiDrawCall::TextureFunc highlightTextureFunc = [this, provinceID, &gameState, &provinceInst, &provinceDef, &playerProvinceDef, i, locationDefIndex]()
			{
				const LocationDefinition &locationDef = provinceDef.getLocationDef(locationDefIndex);
				const ProvinceMapUiView::HighlightType highlightType = [this, provinceID, &gameState, &provinceDef, &playerProvinceDef, i, &locationDef]()
				{
					const LocationDefinition &playerLocationDef = gameState.getLocationDefinition();
					if (provinceDef.matches(playerProvinceDef) && locationDef.matches(playerLocationDef))
					{
						return ProvinceMapUiView::HighlightType::PlayerLocation;
					}
					else
					{
						// If there is a currently-selected destination in this province, draw its blinking highlight
						// if within the "blink on" interval.
						const ProvinceMapUiModel::TravelData *travelDataPtr = gameState.getTravelData();
						if (travelDataPtr != nullptr)
						{
							const ProvinceMapUiModel::TravelData &travelData = *travelDataPtr;
							if ((travelData.provinceID == provinceID) && (travelData.locationID == i))
							{
								// See if the blink period percent lies within the "on" percent. Use less-than to compare them
								// so the on-state appears before the off-state.
								if (this->blinkState.getPercent() < ProvinceMapUiView::BlinkPeriodPercentOn)
								{
									return ProvinceMapUiView::HighlightType::TravelDestination;
								}
							}
						}
					}

					return ProvinceMapUiView::HighlightType::None;
				}();

				const LocationTextureRefGroup *textureRefGroupPtr = [this, &locationDef]() -> const LocationTextureRefGroup*
				{
					const LocationDefinition::Type locationDefType = locationDef.getType();
					if (locationDefType == LocationDefinition::Type::City)
					{
						const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
						const ArenaTypes::CityType cityType = cityDef.type;
						switch (cityType)
						{
						case ArenaTypes::CityType::CityState:
							return &this->cityStateTextureRefs;
						case ArenaTypes::CityType::Town:
							return &this->townTextureRefs;
						case ArenaTypes::CityType::Village:
							return &this->villageTextureRefs;
						default:
							DebugCrash("Unhandled city type \"" + std::to_string(static_cast<int>(cityType)) + "\".");
							return nullptr;
						}
					}
					else if (locationDefType == LocationDefinition::Type::Dungeon)
					{
						return &this->dungeonTextureRefs;
					}
					else if (locationDefType == LocationDefinition::Type::MainQuestDungeon)
					{
						const LocationDefinition::MainQuestDungeonDefinition &mainQuestDungeonDef = locationDef.getMainQuestDungeonDefinition();
						const LocationDefinition::MainQuestDungeonDefinition::Type mainQuestDungeonType = mainQuestDungeonDef.type;
						switch (mainQuestDungeonType)
						{
						case LocationDefinition::MainQuestDungeonDefinition::Type::Start:
						case LocationDefinition::MainQuestDungeonDefinition::Type::Map:
							return &this->dungeonTextureRefs;
						case LocationDefinition::MainQuestDungeonDefinition::Type::Staff:
							return &this->staffDungeonTextureRefs;
						default:
							DebugCrash("Unhandled main quest dungeon type \"" + std::to_string(static_cast<int>(mainQuestDungeonType)) + "\".");
							return nullptr;
						}
					}
					else
					{
						DebugCrash("Unhandled location definition type \"" + std::to_string(static_cast<int>(locationDefType)) + "\".");
						return nullptr;
					}
				}();

				DebugAssert(textureRefGroupPtr != nullptr);

				switch (highlightType)
				{
				case ProvinceMapUiView::HighlightType::None:
					return textureRefGroupPtr->textureRef.get();
				case ProvinceMapUiView::HighlightType::PlayerLocation:
					return textureRefGroupPtr->playerCurrentTextureRef.get();
				case ProvinceMapUiView::HighlightType::TravelDestination:
					return textureRefGroupPtr->travelDestinationTextureRef.get();
				default:
					DebugUnhandledReturnMsg(UiTextureID, std::to_string(static_cast<int>(highlightType)));
				}
			};

			const LocationDefinition &locationDef = provinceDef.getLocationDef(locationDefIndex);
			const std::optional<Int2> baseTextureDims = renderer.tryGetUiTextureDims(baseTextureFunc());
			DebugAssert(baseTextureDims.has_value());

			const Int2 iconCenter(locationDef.getScreenX(), locationDef.getScreenY());
			constexpr PivotType pivotType = PivotType::Middle;
			this->addDrawCall(
				baseTextureFunc,
				iconCenter,
				*baseTextureDims,
				pivotType);

			UiDrawCall::ActiveFunc highlightActiveFunc = [this, provinceID, &gameState, &provinceInst, &provinceDef, &playerProvinceDef, i, locationDefIndex]()
			{
				const LocationDefinition &locationDef = provinceDef.getLocationDef(locationDefIndex);
				const LocationDefinition &playerLocationDef = gameState.getLocationDefinition();
				if (provinceDef.matches(playerProvinceDef) && locationDef.matches(playerLocationDef))
				{
					return true;
				}
				else
				{
					// If there is a currently-selected destination in this province, draw its blinking highlight
					// if within the "blink on" interval.
					const ProvinceMapUiModel::TravelData *travelDataPtr = gameState.getTravelData();
					if (travelDataPtr != nullptr)
					{
						const ProvinceMapUiModel::TravelData &travelData = *travelDataPtr;
						if ((travelData.provinceID == provinceID) && (travelData.locationID == i))
						{
							// See if the blink period percent lies within the "on" percent. Use less-than to compare them
							// so the on-state appears before the off-state.
							if (this->blinkState.getPercent() < ProvinceMapUiView::BlinkPeriodPercentOn)
							{
								return true;
							}
						}
					}
				}

				return false;
			};

			UiDrawCall::SizeFunc highlightSizeFunc = [&renderer, highlightTextureFunc]()
			{
				const std::optional<Int2> highlightTextureDims = renderer.tryGetUiTextureDims(highlightTextureFunc());
				DebugAssert(highlightTextureDims.has_value());
				return *highlightTextureDims;
			};

			this->addDrawCall(
				highlightTextureFunc,
				[iconCenter]() { return iconCenter; },
				highlightSizeFunc,
				[pivotType]() { return pivotType; },
				highlightActiveFunc);
		}
	}
}

void ProvinceMapPanel::trySelectLocation(int selectedLocationID)
{
	auto &game = this->getGame();
	auto &gameState = game.getGameState();
	const auto &binaryAssetLibrary = BinaryAssetLibrary::getInstance();

	const WorldMapDefinition &worldMapDef = gameState.getWorldMapDefinition();
	const ProvinceDefinition &currentProvinceDef = gameState.getProvinceDefinition();
	const LocationDefinition &currentLocationDef = gameState.getLocationDefinition();

	const ProvinceDefinition &selectedProvinceDef = worldMapDef.getProvinceDef(this->provinceID);
	const LocationDefinition &selectedLocationDef = selectedProvinceDef.getLocationDef(selectedLocationID);

	// Only continue if the selected location is not the player's current location.
	const bool matchesPlayerLocation = selectedProvinceDef.matches(currentProvinceDef) &&
		selectedLocationDef.matches(currentLocationDef);

	if (!matchesPlayerLocation)
	{
		// Set the travel data for the selected location and reset the blink timer.
		const Date &currentDate = gameState.getDate();

		// Use a copy of the RNG so displaying the travel pop-up multiple times doesn't
		// cause different day amounts.
		ArenaRandom tempRandom = gameState.getRandom();

		auto makeGlobalPoint = [](const LocationDefinition &locationDef, const ProvinceDefinition &provinceDef)
		{
			const Int2 localPoint(locationDef.getScreenX(), locationDef.getScreenY());
			return ArenaLocationUtils::getGlobalPoint(localPoint, provinceDef.getGlobalRect());
		};

		const Int2 srcGlobalPoint = makeGlobalPoint(currentLocationDef, currentProvinceDef);
		const Int2 dstGlobalPoint = makeGlobalPoint(selectedLocationDef, selectedProvinceDef);
		const int travelDays = ArenaLocationUtils::getTravelDays(srcGlobalPoint, dstGlobalPoint,
			currentDate.getMonth(), gameState.getWeathersArray(), tempRandom, binaryAssetLibrary);

		// Set selected map location.
		gameState.setTravelData(ProvinceMapUiModel::TravelData(selectedLocationID, this->provinceID, travelDays));

		this->blinkState.reset();

		// Create pop-up travel dialog.
		const std::string travelText = ProvinceMapUiModel::makeTravelText(game, this->provinceID,
			currentLocationDef, currentProvinceDef, selectedLocationID);
		std::unique_ptr<Panel> textPopUp = ProvinceMapUiModel::makeTextPopUp(game, travelText);
		game.pushSubPanel(std::move(textPopUp));
	}
	else
	{
		// Cannot travel to the player's current location. Create an error pop-up.
		const std::string &currentLocationName = [&gameState, &currentLocationDef]() -> const std::string&
		{
			const LocationInstance &currentLocationInst = gameState.getLocationInstance();
			return currentLocationInst.getName(currentLocationDef);
		}();

		const std::string errorText = ProvinceMapUiModel::makeAlreadyAtLocationText(game, currentLocationName);
		std::unique_ptr<Panel> textPopUp = ProvinceMapUiModel::makeTextPopUp(game, errorText);
		game.pushSubPanel(std::move(textPopUp));
	}
}

void ProvinceMapPanel::updateHoveredLocationID(const Int2 &originalPosition)
{
	std::optional<Int2> closestPosition;

	// Lambda for comparing distances of two location points to the mouse position.
	auto closerThanCurrentClosest = [&originalPosition, &closestPosition](const Int2 &point)
	{
		if (!closestPosition.has_value())
		{
			return true;
		}

		const Int2 diff = point - originalPosition;
		const Int2 closestDiff = *closestPosition - originalPosition;
		// @todo: change to distance squared
		const double distance = std::sqrt((diff.x * diff.x) + (diff.y * diff.y));
		const double closestDistance = std::sqrt((closestDiff.x * closestDiff.x) + (closestDiff.y * closestDiff.y));
		return distance < closestDistance;
	};

	// Look through all visible locations to find the one closest to the mouse.
	std::optional<int> closestIndex;
	auto &game = this->getGame();
	auto &gameState = game.getGameState();
	const auto &binaryAssetLibrary = BinaryAssetLibrary::getInstance();

	const WorldMapInstance &worldMapInst = gameState.getWorldMapInstance();
	const ProvinceInstance &provinceInst = worldMapInst.getProvinceInstance(this->provinceID);
	const int provinceDefIndex = provinceInst.getProvinceDefIndex();
	const WorldMapDefinition &worldMapDef = gameState.getWorldMapDefinition();
	const ProvinceDefinition &provinceDef = worldMapDef.getProvinceDef(provinceDefIndex);

	for (int i = 0; i < provinceInst.getLocationCount(); i++)
	{
		const LocationInstance &locationInst = provinceInst.getLocationInstance(i);
		if (locationInst.isVisible())
		{
			const int locationDefIndex = locationInst.getLocationDefIndex();
			const LocationDefinition &locationDef = provinceDef.getLocationDef(locationDefIndex);
			const Int2 point(locationDef.getScreenX(), locationDef.getScreenY());

			if (closerThanCurrentClosest(point))
			{
				closestPosition = point;
				closestIndex = i;
			}
		}
	}

	DebugAssertMsg(closestIndex.has_value(), "No closest location ID found.");

	if (this->hoveredLocationID != *closestIndex)
	{
		this->hoveredLocationID = *closestIndex;

		const std::string locationName = ProvinceMapUiModel::getLocationName(game, this->provinceID, this->hoveredLocationID);
		this->hoveredLocationTextBox.setText(locationName);
	}
}

void ProvinceMapPanel::onPauseChanged(bool paused)
{
	Panel::onPauseChanged(paused);

	if (!paused)
	{
		// Make sure the hovered location matches where the pointer is now since mouse motion events
		// aren't processed while this panel is paused.
		auto &game = this->getGame();
		const auto &renderer = game.getRenderer();
		const auto &inputManager = game.getInputManager();
		const Int2 mousePosition = inputManager.getMousePosition();
		const Int2 originalPosition = renderer.nativeToOriginal(mousePosition);
		this->updateHoveredLocationID(originalPosition);
	}
}

void ProvinceMapPanel::tick(double dt)
{
	const auto &gameState = this->getGame().getGameState();
	if (gameState.getTravelData() != nullptr)
	{
		this->blinkState.update(dt);
	}
}

void ProvinceMapPanel::handleFastTravel()
{
	// Switch to world map and push fast travel sub-panel on top of it.
	auto &game = this->getGame();
	game.pushSubPanel<FastTravelSubPanel>();
	game.setPanel<WorldMapPanel>();
}
