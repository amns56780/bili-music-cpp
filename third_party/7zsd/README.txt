7-Zip SFX module (7zSD.sfx), version 9.20.
Taken from 7-Zip Extra 9.20 (https://www.7-zip.org/a/7z920_extra.7z).

7-Zip is licensed under the GNU LGPL. The SFX modules may be freely used to
create self-extracting installers, including commercial ones. See the 7-Zip
license: https://www.7-zip.org/license.txt

This module (installer variant) supports an embedded config:

    ;!@Install@!UTF-8!
    Title="..."
    RunProgram="install.bat"
    GUIMode="2"
    ;!@InstallEnd@!

setup.exe is built by concatenating: 7zSD.sfx + config.txt + payload.7z
The payload archive must use a codec this 9.20 module understands; the
packaging script therefore writes LZMA1 (py7zr FILTER_LZMA), not LZMA2.
