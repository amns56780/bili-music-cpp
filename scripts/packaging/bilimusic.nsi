; bilimusic NSIS installer (Modern UI 2)
;
; Build with:
;   makensis.exe /DPAYLOAD="<staging dir>" /DOUTFILE="<setup.exe>" ^
;                /DICON="<bilimusic.ico>" /DAPPVERSION="1.0.0" bilimusic.nsi
;
; Payload layout (staged by scripts/package.ps1):
;   bilimusic.exe, bilimusic.ico, assets\, vlc\, readme.txt, bilimusic.bat

Unicode true

!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef PAYLOAD
  !error "PAYLOAD is not defined"
!endif
!ifndef OUTFILE
  !error "OUTFILE is not defined"
!endif
!ifndef ICON
  !error "ICON is not defined"
!endif
!ifndef APPVERSION
  !define APPVERSION "1.0.0"
!endif

!define APPNAME "bilimusic"
!define APPEXE "bilimusic.exe"
!define COMPANY "amns56780"
!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\bilimusic"
!define UNINSTALLER "uninstall.exe"
!define APPURL "https://github.com/amns56780/bili-music-cpp"

!ifndef BITMAPDIR
  !define BITMAPDIR "."
!endif

Name "${APPNAME} ${APPVERSION}"
BrandingText "${APPNAME} ${APPVERSION}  ·  Bilibili 音乐播放器"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\bilimusic"
InstallDirRegKey HKCU "${REGKEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 64
SetDatablockOptimize on
CRCCheck on
XPStyle on

VIProductVersion "1.0.0.0"
VIAddVersionKey /LANG=2052 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=2052 "ProductVersion" "${APPVERSION}.0"
VIAddVersionKey /LANG=2052 "FileVersion" "${APPVERSION}.0"
VIAddVersionKey /LANG=2052 "FileDescription" "${APPNAME} 安装程序"
VIAddVersionKey /LANG=2052 "CompanyName" "${COMPANY}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Copyright (C) 2026 ${COMPANY}"
VIAddVersionKey /LANG=2052 "OriginalFilename" "bilimusic-setup.exe"

Icon "${ICON}"
UninstallIcon "${ICON}"

!define MUI_ABORTWARNING
!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${BITMAPDIR}\installer-header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${BITMAPDIR}\installer-welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${BITMAPDIR}\installer-welcome.bmp"
!define MUI_WELCOMEPAGE_TITLE "欢迎安装 ${APPNAME} ${APPVERSION}"
!define MUI_WELCOMEPAGE_TEXT "这是一个原生 C++ 的 Bilibili 音乐播放器。$\r$\n$\r$\n安装包已内置 VLC 播放内核，无需另外安装 VLC；歌单与登录信息与 Java 版共用 %USERPROFILE%\.bilimusic。$\r$\n$\r$\n点击「下一步」继续。"
!define MUI_DIRECTORYPAGE_TEXT_TOP "请选择 ${APPNAME} 的安装位置。$\r$\n默认安装到当前用户目录，不需要管理员权限。"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APPEXE}"
!define MUI_FINISHPAGE_RUN_TEXT "启动 ${APPNAME}"
!define MUI_FINISHPAGE_LINK "项目主页"
!define MUI_FINISHPAGE_LINK_LOCATION "${APPURL}"
!define MUI_FINISHPAGE_NOREBOOTSUPPORT
!define MUI_UNFINISHPAGE_NOAUTOCLOSE

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "SimpChinese"

Section "bilimusic（必需）" SEC_MAIN
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD}\*"
  WriteUninstaller "$INSTDIR\${UNINSTALLER}"

  CreateDirectory "$SMPROGRAMS\bilimusic"
  CreateShortcut "$SMPROGRAMS\bilimusic\bilimusic.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\bilimusic.ico" 0
  CreateShortcut "$SMPROGRAMS\bilimusic\卸载 bilimusic.lnk" "$INSTDIR\${UNINSTALLER}"

  WriteRegStr HKCU "${REGKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${REGKEY}" "DisplayVersion" "${APPVERSION}"
  WriteRegStr HKCU "${REGKEY}" "Publisher" "${COMPANY}"
  WriteRegStr HKCU "${REGKEY}" "DisplayIcon" "$INSTDIR\bilimusic.ico"
  WriteRegStr HKCU "${REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${REGKEY}" "UninstallString" '"$INSTDIR\${UNINSTALLER}"'
  WriteRegStr HKCU "${REGKEY}" "QuietUninstallString" '"$INSTDIR\${UNINSTALLER}" /S'
  WriteRegDWORD HKCU "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${REGKEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${REGKEY}" "EstimatedSize" "$0"
SectionEnd

Section "创建桌面快捷方式" SEC_DESKTOP
  CreateShortcut "$DESKTOP\bilimusic.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\bilimusic.ico" 0
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\bilimusic.lnk"
  Delete "$SMPROGRAMS\bilimusic\bilimusic.lnk"
  Delete "$SMPROGRAMS\bilimusic\卸载 bilimusic.lnk"
  RMDir "$SMPROGRAMS\bilimusic"

  DeleteRegKey HKCU "${REGKEY}"

  IfSilent skipDataPrompt
  MessageBox MB_YESNO|MB_ICONQUESTION "是否同时删除歌单与登录信息（%USERPROFILE%\.bilimusic）？$\r$\n$\r$\n选择「否」会保留歌单数据，以后重装还能继续用。" IDNO +2
  RMDir /r "$PROFILE\.bilimusic"
  skipDataPrompt:

  RMDir /r "$INSTDIR"
  ; 卸载器自身在运行时无法删除自己：写一个临时 bat，等本进程退出后删掉整个目录
  FileOpen $9 "$TEMP\bilimusic-uninstall.bat" w
  FileWrite $9 "@echo off$\r$\n"
  FileWrite $9 "ping -n 3 127.0.0.1 >nul$\r$\n"
  FileWrite $9 "rd /s /q $\"$INSTDIR$\"$\r$\n"
  FileWrite $9 "del $\"$TEMP\bilimusic-uninstall.bat$\"$\r$\n"
  FileClose $9
  ExecShell "open" "$TEMP\bilimusic-uninstall.bat" "" SW_HIDE
SectionEnd
