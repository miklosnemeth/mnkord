; Windows installer for the mnKord VST3 (the Standalone is not included).
; Build it with Inno Setup 6 from a Release build, giving the version on the command line:
;   ISCC.exe /DMyAppVersion=1.6.0 installer\mnKord.iss
; The result is build\installer\mnKord-<version>-win-x64-setup.exe (see README "Installer").
; What it shares with the mnArpee installer (the folders, closing the DAW, the old version) is in
; MnAudioInstaller.iss, the same file in both repositories.

#ifndef MyAppVersion
  #error Pass the version: ISCC.exe /DMyAppVersion=x.y.z installer\mnKord.iss
#endif

#define MyAppName "mnKord"
#define MyAppGuid "6F0A3C52-9B1E-4D7A-8E64-2C5B1A7D9F30"
#define MyRepo "mnkord"
#define MyDocsLabel "User guide and licenses"
#include "MnAudioInstaller.iss"

[Setup]
; The same GUID as MyAppGuid ({{ is the escape of a single brace). The old uninstaller is found with it.
AppId={{{#MyAppGuid}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
OutputDir=..\build\installer

[Files]
; The plugin goes to the standard VST3 folder every host scans, in a folder of its own for MNAudio.
Source: "..\build\mnKord_artefacts\Release\VST3\mnKord.vst3\*"; DestDir: "{#MyVst3Dest}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
; Licenses and the readme sit in the install folder.
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Resources\Fonts\Montserrat-OFL.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "..\Resources\Fonts\RobotoCondensed-LICENSE.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion

; The user guide (English and Hungarian), with Start menu shortcuts.
Source: "..\docs\mnKord-User-Guide.pdf"; DestDir: "{app}\Docs"; Flags: ignoreversion
Source: "..\docs\mnKord-Felhasznaloi-utmutato.pdf"; DestDir: "{app}\Docs"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MNAudio\mnKord user guide"; Filename: "{app}\Docs\mnKord-User-Guide.pdf"
Name: "{autoprograms}\MNAudio\mnKord felhasznaloi utmutato"; Filename: "{app}\Docs\mnKord-Felhasznaloi-utmutato.pdf"
