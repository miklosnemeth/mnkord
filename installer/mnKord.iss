; Windows installer for the mnKord VST3 (the Standalone is not included).
; Build it with Inno Setup 6 from a Release build, giving the version on the command line:
;   ISCC.exe /DMyAppVersion=0.1.0 installer\mnKord.iss
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
AppPublisherURL=https://github.com/miklosnemeth/mnKord
AppSupportURL=https://github.com/miklosnemeth/mnKord/issues
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
