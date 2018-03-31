#include <unordered_map>

#include "MusicFile.h"
#include "MusicName.h"
#include "../World/WeatherType.h"

namespace std
{
	// Hash specializations, required until GCC 6.1.
	template <>
	struct hash<MusicName>
	{
		size_t operator()(const MusicName &x) const
		{
			return static_cast<size_t>(x);
		}
	};

	template <>
	struct hash<WeatherType>
	{
		size_t operator()(const WeatherType &x) const
		{
			return static_cast<size_t>(x);
		}
	};
}

namespace
{
	// Each MusicName has a corresponding filename. Interestingly, it seems Arena
	// has separate XFM files for FM synth output devices (OPL, as on Adlib and
	// Sound Blaster before the AWE32), while the corresponding XMI files are for
	// MT-32, MPU-401, and other General MIDI devices.
	// - Dungeon1 uses the .XFM version because the .XMI version is a duplicate
	//   of Dungeon5.
	const std::unordered_map<MusicName, std::string> MusicFilenames =
	{
		{ MusicName::ArabCityEnter, "ARABCITY.XMI" },
		{ MusicName::ArabTownEnter, "ARABTOWN.XMI" },
		{ MusicName::ArabVillageEnter, "ARAB_VLG.XMI" },
		{ MusicName::CityEnter, "CITY.XMI" },
		{ MusicName::Combat, "COMBAT.XMI" },
		{ MusicName::Credits, "CREDITS.XMI" },
		{ MusicName::Dungeon1, "DUNGEON1.XFM" },
		{ MusicName::Dungeon2, "DUNGEON2.XMI" },
		{ MusicName::Dungeon3, "DUNGEON3.XMI" },
		{ MusicName::Dungeon4, "DUNGEON4.XMI" },
		{ MusicName::Dungeon5, "DUNGEON5.XMI" },
		{ MusicName::Equipment, "EQUIPMNT.XMI" },
		{ MusicName::Evil, "EVIL.XMI" },
		{ MusicName::EvilIntro, "EVLINTRO.XMI" },
		{ MusicName::Magic, "MAGIC_2.XMI" },
		{ MusicName::Night, "NIGHT.XMI" },
		{ MusicName::Overcast, "OVERCAST.XMI" },
		{ MusicName::OverSnow, "OVERSNOW.XFM" },
		{ MusicName::Palace, "PALACE.XMI" },
		{ MusicName::PercIntro, "PERCNTRO.XMI" },
		{ MusicName::Raining, "RAINING.XMI" },
		{ MusicName::Sheet, "SHEET.XMI" },
		{ MusicName::Sneaking, "SNEAKING.XMI" },
		{ MusicName::Snowing, "SNOWING.XMI" },
		{ MusicName::Square, "SQUARE.XMI" },
		{ MusicName::SunnyDay, "SUNNYDAY.XFM" },
		{ MusicName::Swimming, "SWIMMING.XMI" },
		{ MusicName::Tavern, "TAVERN.XMI" },
		{ MusicName::Temple, "TEMPLE.XMI" },
		{ MusicName::TownEnter, "TOWN.XMI" },
		{ MusicName::VillageEnter, "VILLAGE.XMI" },
		{ MusicName::Vision, "VISION.XMI" },
		{ MusicName::WinGame, "WINGAME.XMI" }
	};

	// Mappings of weather types to music names.
	const std::unordered_map<WeatherType, MusicName> WeatherMusicNames =
	{
		{ WeatherType::Clear, MusicName::SunnyDay },
		{ WeatherType::Overcast, MusicName::Overcast },
		{ WeatherType::Rain, MusicName::Raining },
		{ WeatherType::Snow, MusicName::Snowing },
		{ WeatherType::SnowOvercast, MusicName::Snowing },
		{ WeatherType::Rain2, MusicName::Raining },
		{ WeatherType::Overcast2, MusicName::Overcast },
		{ WeatherType::SnowOvercast2, MusicName::Snowing }
	};
}

const std::string &MusicFile::fromName(MusicName musicName)
{
	const std::string &filename = MusicFilenames.at(musicName);
	return filename;
}

MusicName MusicFile::fromWeather(WeatherType weatherType)
{
	const MusicName musicName = WeatherMusicNames.at(weatherType);
	return musicName;
}
