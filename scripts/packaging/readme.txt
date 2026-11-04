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

Installer:
  bilimusic-cpp-setup.exe is a modern NSIS wizard:
    - per-user install (no admin / UAC), default %LOCALAPPDATA%\Programs\bilimusic
    - Start Menu shortcut, optional desktop shortcut
    - "Apps & Features" uninstall entry; silent: /S (optionally /D=<dir>)
  To uninstall: Windows Settings - Apps, or uninstall.exe in the install folder.
