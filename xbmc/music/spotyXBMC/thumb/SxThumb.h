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

#ifndef SXTHUMB_H_
#define SXTHUMB_H_

#include "../DllLibspotify.h"
#include <string>

namespace addon_music_spotify {

  using namespace std;

  class ThumbStore;
  class SxThumb {
  public:

    static void SP_CALLCONV cb_imageLoaded(sp_image *image, void *userdata);
    void thumbLoaded(sp_image *image);

    bool isLoaded() {
      return m_isLoaded;
    }

    void addRef() {
      m_references++;
    }
    int getReferencesCount() {
      return m_references;
    }

    string getPath() {
      return m_file;
    }

    friend class ThumbStore;

  private:
    SxThumb(sp_image* image, string path);
    virtual ~SxThumb();

    void rmRef() {
      m_references--;
    }

    sp_image *m_image;
    bool m_isLoaded;
    bool m_imageIsFromCache;
    string m_file;
    int m_references;

  protected:
    DllLibspotify *m_dll;
  };

} /* namespace addon_music_spotify */
#endif /* SXTHUMB_H_ */
