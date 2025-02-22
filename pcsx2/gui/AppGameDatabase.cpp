/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2020  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"

#include "App.h"
#include "AppGameDatabase.h"
#include "GameIndex.h"

AppGameDatabase& AppGameDatabase::Load()
{
	std::string game_index(reinterpret_cast<const char*>(&GameIndex_yaml), GameIndex_yaml_len);
	std::istringstream stream(game_index);

	if (!this->initDatabase(stream))
	{
		log_cb(RETRO_LOG_ERROR, "[GameDB] Database could not be loaded successfully\n");
		return *this;
	}

	log_cb(RETRO_LOG_INFO, "[GameDB] %d games on record\n", this->numGames()
		);

	return *this;
}

AppGameDatabase* Pcsx2App::GetGameDatabase()
{
	pxAppResources& res(GetResourceCache());

	ScopedLock lock(m_mtx_LoadingGameDB);
	if (!res.GameDB)
	{
		res.GameDB = std::make_unique<AppGameDatabase>();
		res.GameDB->Load();
	}
	return res.GameDB.get();
}

IGameDatabase* AppHost_GetGameDatabase()
{
	return wxGetApp().GetGameDatabase();
}
