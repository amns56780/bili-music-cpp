bilimusic desktop (C++/Dear ImGui) - portable build
====================================================

Run:
  double-click bilimusic.exe  (or bilimusic.bat)

The VLC playback core is bundled in the vlc\ folder - you do NOT need to
install VLC separately. If vlc\ is missing, the app falls back to a system
installed VLC (C:\Program Files\VideoLAN\VLC).

Data (shared with the Java version):
  %USERPROFILE%\.bilimusic\settings.json   - settings (volume, window, cache range)
  %USERPROFILE%\.bilimusic\playlists.json  - playlists (Java schema)
  %USERPROFILE%\.bilimusic\cache\          - full-song playback cache
  %USERPROFILE%\.bilimusic\downloads\      - downloaded songs

Install / uninstall:
  bilimusic-setup.exe  (self-extracting installer)
    default dir: %LOCALAPPDATA%\Programs\bilimusic
  uninstall: Settings - Apps, or uninstall.bat in the install folder
