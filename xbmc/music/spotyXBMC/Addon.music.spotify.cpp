/*
 spotyxbmc2 - A project to integrate Spotify into XBMC
 Copyright (C) 2011  David Erenger

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 For contact with the author:
 david.erenger@gmail.com
 */
#include "../../utils/StdString.h"
#include "Addon.music.spotify.h"
#include <stdint.h>
#include "Utils.h"
#include "Logger.h"
#include "session/Session.h"
#include "playlist/StarredList.h"
#include "playlist/TopLists.h"
#include "playlist/PlaylistStore.h"
#include "playlist/SxPlaylist.h"
#include "artist/SxArtist.h"
#include "artist/ArtistStore.h"
#include "track/TrackStore.h"
#include "album/SxAlbum.h"
#include "track/SxTrack.h"
#include "album/AlbumStore.h"
#include "../tags/MusicInfoTag.h"
#include "../Album.h"
#include "../Artist.h"
#include "../../MediaSource.h"
#include "player/PlayerHandler.h"
#include "search/SearchHandler.h"
#include "radio/RadioHandler.h"
#include "SxSettings.h"

using namespace addon_music_spotify;
using namespace std;

Addon_music_spotify* g_spotify;

Addon_music_spotify::Addon_music_spotify() {
    m_dll = new DllLibspotify;
    if(!m_dll->Load()) {
        Logger::printOut("unable to load libspotify");
        return;
    }

    Session::getInstance()->enable();
}

Addon_music_spotify::~Addon_music_spotify() {
	Session::getInstance()->deInit();
    delete m_dll, m_dll = NULL;
	Logger::printOut("removing spotify addon");
}

bool Addon_music_spotify::enable(bool enable) {
	if (enable)
		return Session::getInstance()->enable();
	else
		Session::getInstance()->deInit();
}

bool Addon_music_spotify::isReady() {
	return Session::getInstance()->isReady();
}

bool Addon_music_spotify::GetPlaylists(CFileItemList& items) {
	if (isReady()) {
		PlaylistStore* ps = Session::getInstance()->getPlaylistStore();
		CMediaSource playlistShare;
		for (int i = 0; i < ps->getPlaylistCount(); i++) {
			if (!ps->getPlaylist(i)->isFolder() && ps->getPlaylist(i)->isLoaded()) {
				playlistShare.strPath = StringUtils::Format("musicdb://3/spotify:playlist:%i", i);
				const char* owner = ps->getPlaylist(i)->getOwnerName();
				if (owner != NULL)
					playlistShare.strName = StringUtils::Format("%s %s %s",
							ps->getPlaylist(i)->getName(), Settings::getInstance()->getByString().c_str(), owner);
				else
					playlistShare.strName = StringUtils::Format("%s", ps->getPlaylist(i)->getName());
				CFileItemPtr pItem(new CFileItem(playlistShare));
				SxThumb* thumb = ps->getPlaylist(i)->getThumb();
				if (thumb != NULL)
					pItem->SetArt("thumb",thumb->getPath());
				pItem->SetArt("fanart", Settings::getInstance()->getFanart());
				items.Add(pItem);
			}
		}
	}
	return true;
}

bool Addon_music_spotify::GetAlbums(CFileItemList& items, CStdString& path,
		CStdString artistName) {
	CURL url(path);
	CStdString uri = url.GetFileNameWithoutPath();
	Logger::printOut("Addon_music_spotify::GetAlbums");
	Logger::printOut(uri);
	if (uri.substr(0,14) == "spotify:artist") {
		return getArtistAlbums(items, uri);
	} else if (uri.substr(0,15) == "spotify:toplist") {
		return getTopListAlbums(items);
	} else if (uri.substr(0,13) == "spotify:track") {
        sp_link *spLink = m_dll->sp_link_create_from_string(uri.substr(0,uri.find('.')).c_str());
		if (!spLink)
			return false;
        sp_track *spTrack = m_dll->sp_link_as_track(spLink);
		if (spTrack) {
            sp_artist* spArtist = m_dll->sp_track_artist(spTrack, 0);
			return getArtistAlbums(items, spArtist);
		}
	} else if (uri.substr(0,13) == "spotify:album") {
		Logger::printOut("browsing artist from album");
		Logger::printOut(uri);
        sp_link *spLink = m_dll->sp_link_create_from_string(uri.substr(0,uri.find('#')).c_str());
		if (!spLink)
			return false;
        sp_album *spAlbum = m_dll->sp_link_as_album(spLink);
		if (spAlbum) {
            sp_artist* spArtist = m_dll->sp_album_artist(spAlbum);
			return getArtistAlbums(items, spArtist);
		}
	} else {
		return getAllAlbums(items, artistName);
	}
	return true;
}

bool Addon_music_spotify::getArtistAlbums(CFileItemList& items,
		sp_artist* spArtist) {
	SxArtist* artist = ArtistStore::getInstance()->getArtist(spArtist, true);
	if (!artist)
		return true;

	// Not pretty but we need to advance libspotify if we are loading the artist for the first time here (clicking "browse artist" from the context menu)

	while (!Session::getInstance()->lock())
		;
	while (!artist->isAlbumsLoaded()) {
		Session::getInstance()->processEvents();
	}
	Session::getInstance()->unlock();

	Logger::printOut(
			"get artist albums, done browsing, waiting for all albums to load");

//	while (!artist->isAlbumsLoaded()) {
//	}

	//add the similar artists item
	if (artist->getArtists().size() > 0) {
		CFileItemPtr pItem(new CFileItem(Settings::getInstance()->getSimilarArtistsString()));
		CStdString path;
        sp_link* link = m_dll->sp_link_create_from_artist(spArtist);
		char* uri = new char[256];
        m_dll->sp_link_as_string(link, uri, 256);
        m_dll->sp_link_release(link);
		path = StringUtils::Format("musicdb://1/%s/", uri);
		delete uri;
		pItem->SetPath(path);
		pItem->m_bIsFolder = true;
		items.Add(pItem);
		pItem->SetIconImage("DefaultMusicArtists.png");
		pItem->SetArt("fanart", Settings::getInstance()->getFanart());
	}

	//add the albums
	vector<SxAlbum*> albums = artist->getAlbums();
	for (int i = 0; i < albums.size(); i++) {
		items.Add(Utils::SxAlbumToItem(albums[i]));
	}
	return true;
}

bool Addon_music_spotify::getArtistAlbums(CFileItemList& items,
		CStdString& artistStr) {
	if (isReady()) {
        sp_link *spLink = m_dll->sp_link_create_from_string(artistStr);
		if (!spLink)
			return true;
        sp_artist *spArtist = m_dll->sp_link_as_artist(spLink);
		if (!spArtist)
			return true;

		return getArtistAlbums(items, spArtist);

        m_dll->sp_link_release(spLink);
        m_dll->sp_artist_release(spArtist);
	}
	return true;
}

bool Addon_music_spotify::getAllAlbums(CFileItemList& items,
		CStdString& artistStr) {

	Logger::printOut("get album");
	if (isReady()) {
		if (artistStr.empty()) {
			//load all starred albums
			PlaylistStore* ps = Session::getInstance()->getPlaylistStore();
			StarredList* sl = ps->getStarredList();
			if (sl == NULL)
				return true;
			if (!sl->isLoaded())
				return true;
			for (int i = 0; i < sl->getNumberOfAlbums(); i++) {
				SxAlbum* album = sl->getAlbum(i);
				//if its a multidisc we need to add them all
				int discNumber = album->getNumberOfDiscs();
				if (discNumber == 1)
					items.Add(Utils::SxAlbumToItem(sl->getAlbum(i)));
				else {
					while (discNumber > 0) {
						items.Add(Utils::SxAlbumToItem(sl->getAlbum(i), "", discNumber));
						discNumber--;
					}
				}
			}
		} else {
			//TODO do a small search for the artist and fetch the albums from the result

		}
	}
	return true;

}

bool Addon_music_spotify::GetTracks(CFileItemList& items, CStdString& path,
		CStdString artistName, int albumId) {
	Logger::printOut("get tracks");
	CURL url(path);
	CStdString uri = url.GetFileNameWithoutPath();
	//the path will look something like this "musicdb://2/spotify:artist:0LcJLqbBmaGUft1e9Mm8HV/-1/"
	//if we are trying to show all tracks for a spotify artist, we cant use the params becouse they are only integers.
	CURL url2(path.substr(0,path.size() - 3));
	CStdString artist = url2.GetFileNameWithoutPath();
	if (uri.substr(0,13) == "spotify:album") {
		return getAlbumTracks(items, uri);
	} else if (artist.substr(0,14) == "spotify:artist") {
		return getArtistTracks(items, artist);
	} else if (uri.substr(0,16) == "spotify:playlist") {
		uri.erase(0, 17);
		Logger::printOut(StringUtils::Format("get tracks: %s", uri.substr(0,16).c_str()));
		return getPlaylistTracks(items, atoi(uri));
	} else if (artist.substr(0,15) == "spotify:toplist") {
		return g_spotify->getTopListTracks(items);
	} else if (uri.substr(0,13) == "spotify:radio") {
		return getRadioTracks(items, atoi(uri.substr(uri.size()-1).c_str()));
	} else if (uri.substr(0,13) == "spotify:track") {
		return getAlbumTracksFromTrack(items, uri);
	} else if (albumId == -1) {
		return getAllTracks(items, artistName);
	}
	return true;
}
bool Addon_music_spotify::GetOneTrack(CFileItemList& items, CStdString& path) {
  Logger::printOut("get one track");
  CURL url(path);
  CStdString uri = url.GetFileNameWithoutPath();
  if (uri.substr(0,13) == "spotify:track") {
      if (isReady()) {
      sp_link *spLink = m_dll->sp_link_create_from_string(uri.substr(0,uri.find('.')).c_str());
      if (!spLink) return false;
      sp_track *spTrack = m_dll->sp_link_as_track(spLink);
      if (spTrack) {
        SxTrack* track = TrackStore::getInstance()->getTrack(spTrack);
        items.Add(Utils::SxTrackToItem(track));
        m_dll->sp_track_release(spTrack);
      }
      m_dll->sp_link_release(spLink);
    } 
  }
  return true;  
}

bool Addon_music_spotify::getAlbumTracksFromTrack(CFileItemList& items,
		CStdString& uri) {
	if (isReady()) {
        sp_link *spLink = m_dll->sp_link_create_from_string(uri.substr(0,uri.find('.')).c_str());
		if (!spLink)
			return false;
        sp_track *spTrack = m_dll->sp_link_as_track(spLink);
		if (spTrack) {
            sp_album* spAlbum = m_dll->sp_track_album(spTrack);
			if (spAlbum) {
				// TODO find out on what disc the track is if its a multidisc
				SxAlbum* album = AlbumStore::getInstance()->getAlbum(spAlbum, true);

				// this is NOT nice, might result in a race condition... fix later
				while (!album->isLoaded()) {
					Session::getInstance()->processEvents();
				}
				vector<SxTrack*> tracks = album->getTracks();
				for (int i = 0; i < tracks.size(); i++) {
					items.Add(Utils::SxTrackToItem(tracks[i]));
				}
			}
            m_dll->sp_track_release(spTrack);
		}
        m_dll->sp_link_release(spLink);
	}
}

bool Addon_music_spotify::getAlbumTracks(CFileItemList& items,
		CStdString& path) {
	if (isReady()) {
		//lets split the string to get the album uri and the disc number
		CStdString uri = path.substr(0,path.find('#'));
		CStdString discStr = path.substr(path.find('#') + 1);
		//Logger::printOut(discStr.c_str());
		int disc = atoi(discStr.c_str());

		//0 means its not a multidisc, so we need to change it to 1
		if (disc == 0)
			disc = 1;
        sp_link *spLink = m_dll->sp_link_create_from_string(uri);
        sp_album *spAlbum = m_dll->sp_link_as_album(spLink);
		SxAlbum* salbum = AlbumStore::getInstance()->getAlbum(spAlbum, true);
		vector<SxTrack*> tracks = salbum->getTracks();
		for (int i = 0; i < tracks.size(); i++) {
			if (disc == tracks[i]->getDisc())
				items.Add(Utils::SxTrackToItem(tracks[i]));
		}
	}
	return true;
}

bool Addon_music_spotify::getArtistTracks(CFileItemList& items,
		CStdString& path) {
	Logger::printOut("get artist tracks");
	if (isReady()) {
        sp_link *spLink = m_dll->sp_link_create_from_string(path);
		if (!spLink)
			return true;
        sp_artist *spArtist = m_dll->sp_link_as_artist(spLink);
		if (!spArtist)
			return true;
		SxArtist* artist = ArtistStore::getInstance()->getArtist(spArtist, true);
		if (!artist)
			return true;

		//if its the first call the artist might not be loaded yet, the artist will update the view when its ready
		artist->doLoadTracksAndAlbums();
		while (!artist->isTracksLoaded())
			;
		vector<SxTrack*> tracks = artist->getTracks();
		for (int i = 0; i < tracks.size(); i++) {
			items.Add(Utils::SxTrackToItem(tracks[i]));
		}
	}
	return true;
}

bool Addon_music_spotify::getAllTracks(CFileItemList& items, CStdString& path) {
	Logger::printOut("get tracks");
	Logger::printOut(path);
	if (isReady()) {
		if (path.empty()) {
			//load the starred tracks
			PlaylistStore* ps = Session::getInstance()->getPlaylistStore();
			StarredList* sl = ps->getStarredList();

			if (sl == NULL)
				return true;
			if (!sl->isLoaded())
				return true;

			for (int i = 0; i < sl->getNumberOfTracks(); i++) {
				items.Add(Utils::SxTrackToItem(sl->getTrack(i)));
			}
		}
	}
	return true;
}

bool Addon_music_spotify::getRadioTracks(CFileItemList& items, int radio) {
	Logger::printOut("get radio tracks");
	if (isReady()) {
		int lowestTrackNumber = RadioHandler::getInstance()->getLowestTrackNumber(
				radio);
		if (radio == 1 || radio == 2) {
			vector<SxTrack*> tracks = RadioHandler::getInstance()->getTracks(radio);
			for (int i = 0; i < tracks.size(); i++) {
				const CFileItemPtr pItem = Utils::SxTrackToItem(tracks[i], "",
						i + lowestTrackNumber + 1);
				CStdString path;
				path = StringUtils::Format("%s%s%i%s%i", pItem->GetPath().c_str(), "radio#", radio, "#",
						i + lowestTrackNumber);
				pItem->SetPath(path);
				items.Add(pItem);
			}
		}
		return true;
	}
	return false;
}

bool Addon_music_spotify::GetArtists(CFileItemList& items, CStdString& path) {
	CURL url(path);
	CStdString uri = url.GetFileNameWithoutPath();
	if (uri.substr(0,15) == "spotify:toplist") {
		getTopListArtists(items);
	} else if (uri.substr(0,14) == "spotify:artist") {
		getArtistSimilarArtists(items, uri);
	} else {
		getAllArtists(items);
	}
	return true;
}

bool Addon_music_spotify::getAllArtists(CFileItemList& items) {
	Logger::printOut("get starred artists");
	if (isReady()) {
		PlaylistStore* ps = Session::getInstance()->getPlaylistStore();
		StarredList* sl = ps->getStarredList();

		if (sl == NULL)
			return true;
		if (!sl->isLoaded())
			return true;

		for (int i = 0; i < sl->getNumberOfArtists(); i++) {
			items.Add(Utils::SxArtistToItem(sl->getArtist(i)));
		}
	}
	return true;
}

bool Addon_music_spotify::getArtistSimilarArtists(CFileItemList& items,
		CStdString uri) {
	Logger::printOut("get similar artists");
	if (isReady()) {
        sp_link *spLink = m_dll->sp_link_create_from_string(uri);
		if (!spLink)
			return true;
        sp_artist *spArtist = m_dll->sp_link_as_artist(spLink);
		if (!spArtist)
			return true;
		SxArtist* artist = ArtistStore::getInstance()->getArtist(spArtist, true);
		if (!artist)
			return true;

		//if its the first call the artist might not be loaded yet, the artist will update the view when its ready
		artist->doLoadTracksAndAlbums();
		while (!artist->isTracksLoaded())
			;
		vector<SxArtist*> artists = artist->getArtists();
		for (int i = 0; i < artists.size(); i++) {
			items.Add(Utils::SxArtistToItem(artists[i]));
		}
	}
	return true;
}

bool Addon_music_spotify::getPlaylistTracks(CFileItemList& items, int index) {
	Logger::printOut("get playlist tracks");
	if (isReady()) {
		PlaylistStore* ps = Session::getInstance()->getPlaylistStore();
		SxPlaylist* pl = ps->getPlaylist(index);

		for (int i = 0; i < pl->getNumberOfTracks(); i++) {
			items.Add(Utils::SxTrackToItem(pl->getTrack(i), "", i + 1));
		}
	}
	return true;
}

bool Addon_music_spotify::GetTopLists(CFileItemList& items) {
	if (isReady()) {
		Logger::printOut("get the toplist entry list");
		TopLists* topLists = Session::getInstance()->getTopLists();

		if (topLists == NULL || !topLists->isLoaded())
			return true;

		//add the tracks entry
		CFileItemPtr pItem(new CFileItem(Settings::getInstance()->getTopListTrackString()));
		CStdString path;
		path = StringUtils::Format("musicdb://2/spotify:toplist/-1/");
		pItem->SetPath(path);
		pItem->m_bIsFolder = true;
		items.Add(pItem);
		pItem->SetArt("fanart", Settings::getInstance()->getFanart());

		//add the album entry
		CFileItemPtr pItem2(new CFileItem(Settings::getInstance()->getTopListAlbumString()));
		path = StringUtils::Format("musicdb://2/spotify:toplist");
		pItem2->SetPath(path);
		pItem2->m_bIsFolder = true;
		items.Add(pItem2);
		pItem2->SetArt("fanart", Settings::getInstance()->getFanart());

		//add the artist entry
		CFileItemPtr pItem3(new CFileItem(Settings::getInstance()->getTopListArtistString()));
		path = StringUtils::Format("musicdb://1/spotify:toplist");
		pItem3->SetPath(path);
		pItem3->m_bIsFolder = true;
		items.Add(pItem3);
		pItem3->SetArt("fanart", Settings::getInstance()->getFanart());
	}
	return true;
}

bool Addon_music_spotify::GetCustomEntries(CFileItemList& items) {
	if (isReady()) {
		//add radio 1
		CStdString name;
		name = StringUtils::Format("%s%s", Settings::getInstance()->getRadioPrefixString().c_str(),
				Settings::getInstance()->getRadio1Name().c_str());
		CFileItemPtr pItem(new CFileItem(name));
		CStdString path;
		path = StringUtils::Format("musicdb://3/spotify:radio:1/");
		pItem->SetPath(path);
		pItem->m_bIsFolder = true;
		items.Add(pItem);
		pItem->SetArt("fanart", Settings::getInstance()->getFanart());

		//add radio 2
		name = StringUtils::Format("%s%s", Settings::getInstance()->getRadioPrefixString().c_str(),
				Settings::getInstance()->getRadio2Name().c_str());
		CFileItemPtr pItem2(new CFileItem(name));
		path = StringUtils::Format("musicdb://3/spotify:radio:2/");
		pItem2->SetPath(path);
		pItem2->m_bIsFolder = true;
		items.Add(pItem2);
		pItem2->SetArt("fanart", Settings::getInstance()->getFanart());

	}
	return true;
}

bool Addon_music_spotify::GetContextButtons(CFileItemPtr& item,
		CContextButtons &buttons) {
	if (isReady()) {
		CURL url(item->GetPath());
		CStdString uri = url.GetFileNameWithoutPath();
		//the path will look something like this "musicdb://2/spotify:artist:0LcJLqbBmaGUft1e9Mm8HV/-1/"
		//if we are trying to show all tracks for a spotify artist, we cant use the params becouse they are only integers.

		if (uri.substr(0,13) == "spotify:album") {
			uri = uri.substr(0,uri.find('#'));
            sp_link *spLink = m_dll->sp_link_create_from_string(uri);
            sp_album *spAlbum = m_dll->sp_link_as_album(spLink);
			SxAlbum* salbum = AlbumStore::getInstance()->getAlbum(spAlbum, true);
			if (salbum) {
				buttons.Add(
						CONTEXT_BUTTON_SPOTIFY_TOGGLE_STAR_ALBUM,
						salbum->isStarred() ?
								Settings::getInstance()->getUnstarAlbumString() :
								Settings::getInstance()->getStarAlbumString());
				AlbumStore::getInstance()->removeAlbum(salbum);
				buttons.Add(CONTEXT_BUTTON_SPOTIFY_BROWSE_ARTIST,
						Settings::getInstance()->getBrowseArtistString());
			}
            m_dll->sp_link_release(spLink);
            m_dll->sp_album_release(spAlbum);
		} else if (uri.substr(0,13) == "spotify:track") {
			uri = uri.substr(0,uri.find('.'));
			Logger::printOut(uri);
            sp_link *spLink = m_dll->sp_link_create_from_string(uri);
            sp_track* spTrack = m_dll->sp_link_as_track(spLink);
			buttons.Add(
					CONTEXT_BUTTON_SPOTIFY_TOGGLE_STAR_TRACK,
                    m_dll->sp_track_is_starred(Session::getInstance()->getSpSession(), spTrack) ?
							Settings::getInstance()->getUnstarTrackString() :
							Settings::getInstance()->getStarTrackString());
			buttons.Add(CONTEXT_BUTTON_SPOTIFY_BROWSE_ALBUM,
					Settings::getInstance()->getBrowseAlbumString());
			buttons.Add(CONTEXT_BUTTON_SPOTIFY_BROWSE_ARTIST,
					Settings::getInstance()->getBrowseArtistString());

			//this is unstable as it is now... find a solution later

			SxAlbum* salbum = AlbumStore::getInstance()->getAlbum(
                    m_dll->sp_track_album(spTrack), true);
			if (salbum) {
				while (!Session::getInstance()->lock())
					;
				while (!salbum->isLoaded()) {
					Session::getInstance()->processEvents();
				}
				Session::getInstance()->unlock();
				buttons.Add(
						CONTEXT_BUTTON_SPOTIFY_TOGGLE_STAR_ALBUM,
						salbum->isStarred() ?
								Settings::getInstance()->getUnstarAlbumString() :
								Settings::getInstance()->getStarAlbumString());
				AlbumStore::getInstance()->removeAlbum(salbum);
			}

            m_dll->sp_track_release(spTrack);
            m_dll->sp_link_release(spLink);
		}
	}
	return true;
}

bool Addon_music_spotify::ToggleStarTrack(CFileItemPtr& item) {
	if (isReady()) {
		CURL url(item->GetPath());
		CStdString uri = url.GetFileNameWithoutPath();
		uri = uri.substr(0,uri.find('.'));
		Logger::printOut(uri);
        sp_link *spLink = m_dll->sp_link_create_from_string(uri);
        sp_track* spTrack = m_dll->sp_link_as_track(spLink);
        m_dll->sp_track_set_starred(Session::getInstance()->getSpSession(), &spTrack, 1,
                !m_dll->sp_track_is_starred(Session::getInstance()->getSpSession(), spTrack));
        m_dll->sp_link_release(spLink);
        m_dll->sp_track_release(spTrack);
	}
	return true;
}

bool Addon_music_spotify::ToggleStarAlbum(CFileItemPtr& item) {
	if (isReady()) {
		Logger::printOut("toggle album star addon");
		CURL url(item->GetPath());
		CStdString uri = url.GetFileNameWithoutPath();

		sp_album *spAlbum = NULL;
		if (uri.substr(0,13) == "spotify:album") {
			uri = uri.substr(0,uri.find('#'));
            sp_link* spLink = m_dll->sp_link_create_from_string(uri);
            spAlbum = m_dll->sp_link_as_album(spLink);
            m_dll->sp_link_release(spLink);
		} else if (uri.substr(0,13) == "spotify:track") {
            sp_link *spLink = m_dll->sp_link_create_from_string(uri.substr(0,uri.find('.')).c_str());
			if (!spLink)
				return true;
            sp_track *spTrack = m_dll->sp_link_as_track(spLink);
            m_dll->sp_link_release(spLink);
			if (spTrack) {
                spAlbum = m_dll->sp_track_album(spTrack);
			}
            m_dll->sp_track_release(spTrack);
		} else {
			return true;
		}
		SxAlbum* salbum = AlbumStore::getInstance()->getAlbum(spAlbum, true);
		if (salbum){
			while (!Session::getInstance()->lock())
				;
			while (!salbum->isLoaded()) {
				Session::getInstance()->processEvents();
			}
			Session::getInstance()->unlock();
			salbum->toggleStar();
			AlbumStore::getInstance()->removeAlbum(salbum);
		}
        m_dll->sp_album_release(spAlbum);
	}
	return true;
}

bool Addon_music_spotify::getTopListArtists(CFileItemList& items) {
	Logger::printOut("get toplist artists");
	if (isReady()) {
		PlaylistStore* ps = Session::getInstance()->getPlaylistStore();
		TopLists* tl = ps->getTopLists();

		if (!tl->isArtistsLoaded())
			tl->reLoadArtists();

		while (!tl->isArtistsLoaded()) {
			//Session::getInstance()->processEvents();
		}

		vector<SxArtist*> artists = tl->getArtists();
		for (int i = 0; i < artists.size(); i++) {
			items.Add(Utils::SxArtistToItem(artists[i]));
		}

	}
	return true;
}

bool Addon_music_spotify::getTopListAlbums(CFileItemList& items) {
	Logger::printOut("get toplist albums");
	if (isReady()) {
		PlaylistStore* ps = Session::getInstance()->getPlaylistStore();
		TopLists* tl = ps->getTopLists();

		if (!tl->isAlbumsLoaded())
			tl->reLoadAlbums();

		while (!tl->isAlbumsLoaded()) {
			// Session::getInstance()->processEvents();
		}

		vector<SxAlbum*> albums = tl->getAlbums();
		for (int i = 0; i < albums.size(); i++) {
			items.Add(Utils::SxAlbumToItem(albums[i]));
		}
	}
	return true;
}

bool Addon_music_spotify::getTopListTracks(CFileItemList& items) {
	Logger::printOut("get toplist tracks");
	if (isReady()) {
		PlaylistStore* ps = Session::getInstance()->getPlaylistStore();
		TopLists* tl = ps->getTopLists();

		if (!tl->isTracksLoaded())
			tl->reLoadTracks();

		while (!tl->isTracksLoaded()) {
			//Session::getInstance()->processEvents();
		}

		vector<SxTrack*> tracks = tl->getTracks();
		for (int i = 0; i < tracks.size(); i++) {
			items.Add(Utils::SxTrackToItem(tracks[i], "", i + 1));
		}
	}
	return true;
}

bool Addon_music_spotify::Search(CStdString query, CFileItemList& items) {
//do the search, if we are already searching and are to fetch results this want do anything
	Logger::printOut("new search");
	if (isReady()) {
		if (!SearchHandler::getInstance()->search(query)) {
			CStdString albumPrefix;
			albumPrefix = StringUtils::Format("[%s] ", g_localizeStrings.Get(558).c_str());
			Logger::printOut("search fetch albums");
			vector<SxAlbum*> albums = SearchHandler::getInstance()->getAlbumResults();
			for (int i = 0; i < albums.size(); i++) {
				//if its a multidisc we need to add them all
				int discNumber = albums[i]->getNumberOfDiscs();
				if (discNumber == 1) {
					items.Add(Utils::SxAlbumToItem(albums[i], albumPrefix));
				} else {
					while (discNumber > 0) {
						items.Add(Utils::SxAlbumToItem(albums[i], albumPrefix, discNumber));
						discNumber--;
					}
				}
			}
			Logger::printOut("search fetch tracks");
			vector<SxTrack*> tracks = SearchHandler::getInstance()->getTrackResults();
			for (int i = 0; i < tracks.size(); i++)
				items.Add(Utils::SxTrackToItem(tracks[i]));

			CStdString artistPrefix;
			artistPrefix = StringUtils::Format("[%s] ", g_localizeStrings.Get(557).c_str());

			Logger::printOut("search fetch artists");
			vector<SxArtist*> artists =
					SearchHandler::getInstance()->getArtistResults();
			for (int i = 0; i < artists.size(); i++)
				items.Add(Utils::SxArtistToItem(artists[i], artistPrefix));
		}
	}
	return true;
}

ICodec* Addon_music_spotify::GetCodec() {
	return (ICodec*) PlayerHandler::getInstance()->getCodec();
}

