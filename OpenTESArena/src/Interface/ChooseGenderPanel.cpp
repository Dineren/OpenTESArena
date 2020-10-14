#include "SDL.h"

#include "ChooseGenderPanel.h"
#include "ChooseNamePanel.h"
#include "ChooseRacePanel.h"
#include "CursorAlignment.h"
#include "RichTextString.h"
#include "TextAlignment.h"
#include "TextBox.h"
#include "../Assets/ExeData.h"
#include "../Game/Game.h"
#include "../Game/Options.h"
#include "../Math/Vector2.h"
#include "../Media/Color.h"
#include "../Media/FontLibrary.h"
#include "../Media/FontName.h"
#include "../Media/PaletteFile.h"
#include "../Media/PaletteName.h"
#include "../Media/TextureFile.h"
#include "../Media/TextureManager.h"
#include "../Media/TextureName.h"
#include "../Rendering/Renderer.h"
#include "../Rendering/Surface.h"

ChooseGenderPanel::ChooseGenderPanel(Game &game)
	: Panel(game)
{
	this->parchment = Texture::generate(Texture::PatternType::Parchment, 180, 40,
		game.getTextureManager(), game.getRenderer());

	this->genderTextBox = [&game]()
	{
		const Int2 center(Renderer::ORIGINAL_WIDTH / 2, 80);

		const auto &exeData = game.getBinaryAssetLibrary().getExeData();
		const std::string &text = exeData.charCreation.chooseGender;

		const auto &fontLibrary = game.getFontLibrary();
		const RichTextString richText(
			text,
			FontName::A,
			Color(48, 12, 12),
			TextAlignment::Center,
			fontLibrary);

		return std::make_unique<TextBox>(center, richText, fontLibrary, game.getRenderer());
	}();

	this->maleTextBox = [&game]()
	{
		const Int2 center(Renderer::ORIGINAL_WIDTH / 2, 120);

		const auto &exeData = game.getBinaryAssetLibrary().getExeData();
		const std::string &text = exeData.charCreation.chooseGenderMale;

		const auto &fontLibrary = game.getFontLibrary();
		const RichTextString richText(
			text,
			FontName::A,
			Color(48, 12, 12),
			TextAlignment::Center,
			fontLibrary);

		return std::make_unique<TextBox>(center, richText, fontLibrary, game.getRenderer());
	}();

	this->femaleTextBox = [&game]()
	{
		const Int2 center(Renderer::ORIGINAL_WIDTH / 2, 160);

		const auto &exeData = game.getBinaryAssetLibrary().getExeData();
		const std::string &text = exeData.charCreation.chooseGenderFemale;

		const auto &fontLibrary = game.getFontLibrary();
		const RichTextString richText(
			text,
			FontName::A,
			Color(48, 12, 12),
			TextAlignment::Center,
			fontLibrary);

		return std::make_unique<TextBox>(center, richText, fontLibrary, game.getRenderer());
	}();

	this->backToNameButton = []()
	{
		auto function = [](Game &game)
		{
			game.setPanel<ChooseNamePanel>(game);
		};

		return Button<Game&>(function);
	}();

	this->maleButton = []()
	{
		const Int2 center(Renderer::ORIGINAL_WIDTH / 2, 120);
		auto function = [](Game &game)
		{
			const bool male = true;
			auto &charCreationState = game.getCharacterCreationState();
			charCreationState.setGender(male);

			game.setPanel<ChooseRacePanel>(game);
		};

		return Button<Game&>(center, 175, 35, function);
	}();

	this->femaleButton = []()
	{
		const Int2 center(Renderer::ORIGINAL_WIDTH / 2, 160);
		auto function = [](Game &game)
		{
			const bool male = false;
			auto &charCreationState = game.getCharacterCreationState();
			charCreationState.setGender(male);

			game.setPanel<ChooseRacePanel>(game);
		};

		return Button<Game&>(center, 175, 35, function);
	}();
}

Panel::CursorData ChooseGenderPanel::getCurrentCursor() const
{
	return this->getDefaultCursor();
}

void ChooseGenderPanel::handleEvent(const SDL_Event &e)
{
	const auto &inputManager = this->getGame().getInputManager();
	bool escapePressed = inputManager.keyPressed(e, SDLK_ESCAPE);

	if (escapePressed)
	{
		this->backToNameButton.click(this->getGame());
	}

	bool leftClick = inputManager.mouseButtonPressed(e, SDL_BUTTON_LEFT);

	if (leftClick)
	{
		const Int2 mousePosition = inputManager.getMousePosition();
		const Int2 mouseOriginalPoint = this->getGame().getRenderer()
			.nativeToOriginal(mousePosition);

		if (this->maleButton.contains(mouseOriginalPoint))
		{
			this->maleButton.click(this->getGame());
		}
		else if (this->femaleButton.contains(mouseOriginalPoint))
		{
			this->femaleButton.click(this->getGame());
		}
	}
}

void ChooseGenderPanel::render(Renderer &renderer)
{
	// Clear full screen.
	renderer.clear();

	// Draw background.
	const auto &textureManager = this->getGame().getTextureManager();
	const TextureID backgroundTextureID = this->getTextureID(
		TextureName::CharacterCreation, PaletteName::BuiltIn);
	const TextureRef backgroundTexture = textureManager.getTextureRef(backgroundTextureID);
	renderer.drawOriginal(backgroundTexture.get());

	// Draw parchments: title, male, and female.
	const int parchmentX = (Renderer::ORIGINAL_WIDTH / 2) -
		(this->parchment.getWidth() / 2);
	const int parchmentY = (Renderer::ORIGINAL_HEIGHT / 2) -
		(this->parchment.getHeight() / 2);
	renderer.drawOriginal(this->parchment, parchmentX, parchmentY - 20);
	renderer.drawOriginal(this->parchment, parchmentX, parchmentY + 20);
	renderer.drawOriginal(this->parchment, parchmentX, parchmentY + 60);

	// Draw text: title, male, and female.
	renderer.drawOriginal(this->genderTextBox->getTexture(),
		this->genderTextBox->getX(), this->genderTextBox->getY());
	renderer.drawOriginal(this->maleTextBox->getTexture(),
		this->maleTextBox->getX(), this->maleTextBox->getY());
	renderer.drawOriginal(this->femaleTextBox->getTexture(),
		this->femaleTextBox->getX(), this->femaleTextBox->getY());
}
