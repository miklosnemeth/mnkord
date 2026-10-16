; Windows installer for the mnKord VST3 (the Standalone is not included).
; Build it with Inno Setup 6 from a Release build, giving the version on the command line:
;   ISCC.exe /DMyAppVersion=1.0.0 installer\mnKord.iss
; The result is build\installer\mnKord-<version>-win-x64-setup.exe (see README "Installer").

#ifndef MyAppVersion
  #error Pass the version: ISCC.exe /DMyAppVersion=x.y.z installer\mnKord.iss
#endif

#define MyAppName "mnKord"

[Setup]
AppId={{6F0A3C52-9B1E-4D7A-8E64-2C5B1A7D9F30}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=MNAudio
AppPublisherURL=https://github.com/miklosnemeth/mnkord
AppSupportURL=https://github.com/miklosnemeth/mnkord/issues
DefaultDirName={autopf}\MNAudio\mnKord
DefaultGroupName=MNAudio
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\build\installer
OutputBaseFilename=mnKord-{#MyAppVersion}-win-x64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName} {#MyAppVersion}
WizardStyle=modern

[Messages]
; The plugin goes to the shared VST3 folder (fixed, below); this page only chooses where the
; guide and the licenses go, so the page says so.
SelectDirDesc=Where should the documentation and licenses be installed?
SelectDirLabel3=The %1 plugin (VST3) is installed into the shared VST3 folder (Common Files\VST3), where your DAW looks for it. Setup will put the user guide and the licenses into the following folder.

[Files]
; The plugin goes to the standard VST3 folder every host scans.
Source: "..\build\mnKord_artefacts\Release\VST3\mnKord.vst3\*"; DestDir: "{commoncf64}\VST3\mnKord.vst3"; \
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
