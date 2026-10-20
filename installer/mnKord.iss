; Windows installer for the mnKord VST3 (the Standalone is not included).
; Build it with Inno Setup 6 from a Release build, giving the version on the command line:
;   ISCC.exe /DMyAppVersion=1.4.0 installer\mnKord.iss
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
; The user guide and the licenses live in Common Files, next to the shared VST3 folder; there is
; nothing to choose, so there is no folder page. UsePreviousAppDir=no moves an install from an
; older version (which used Program Files\MNAudio\mnKord) to the new folder.
DefaultDirName={commoncf64}\MNAudio\mnKord
DisableDirPage=yes
UsePreviousAppDir=no
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
; A DAW that has the plugin loaded keeps mnKord.vst3 locked. Setup checks the files it is about to
; replace (the default filter doesn't know .vst3) and asks to close the programs that use them.
CloseApplications=yes
CloseApplicationsFilter=*.vst3,*.exe,*.dll
RestartApplications=no

[InstallDelete]
; Earlier versions put the guide and the licenses into Program Files\MNAudio\mnKord.
Type: filesandordirs; Name: "{autopf}\MNAudio\mnKord"
Type: dirifempty; Name: "{autopf}\MNAudio"

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

[Code]
const
  // Setup records the installed version under this key (the AppId above plus "_is1").
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{6F0A3C52-9B1E-4D7A-8E64-2C5B1A7D9F30}_is1';

var
  PreviousVersion: String;

// Takes the number before the first dot off S and returns it.
function TakeVersionPart(var S: String): Integer;
var
  P: Integer;
begin
  P := Pos('.', S);
  if P = 0 then
  begin
    Result := StrToIntDef(S, 0);
    S := '';
  end
  else
  begin
    Result := StrToIntDef(Copy(S, 1, P - 1), 0);
    S := Copy(S, P + 1, Length(S));
  end;
end;

// -1 if A is older than B, 0 if they are the same, 1 if A is newer (x.y.z numbers).
function CompareVersions(A, B: String): Integer;
var
  I, X, Y: Integer;
begin
  Result := 0;
  for I := 1 to 3 do
  begin
    X := TakeVersionPart(A);
    Y := TakeVersionPart(B);
    if X <> Y then
    begin
      if X < Y then
        Result := -1
      else
        Result := 1;
      Exit;
    end;
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := True;

  if not RegQueryStringValue(HKLM, UninstallKey, 'DisplayVersion', PreviousVersion) then
    PreviousVersion := '';

  // Going back to an older version asks first (a silent install says no).
  if (PreviousVersion <> '') and (CompareVersions(PreviousVersion, '{#MyAppVersion}') > 0) then
    Result := SuppressibleMsgBox(
      'A newer version of mnKord (v' + PreviousVersion + ') is installed. Setup is about to install the older v{#MyAppVersion} over it.'
        + #13#10 + #13#10 + 'Do you want to go back to the older version?',
      mbConfirmation, MB_YESNO, IDNO) = IDYES;
end;

// The "Ready to install" page says what is about to happen to the installed version.
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  What: String;
begin
  if PreviousVersion = '' then
    What := 'New installation: mnKord v{#MyAppVersion}'
  else if CompareVersions(PreviousVersion, '{#MyAppVersion}') < 0 then
    What := 'Upgrade: mnKord v' + PreviousVersion + ' -> v{#MyAppVersion}'
  else if CompareVersions(PreviousVersion, '{#MyAppVersion}') = 0 then
    What := 'Reinstall: mnKord v{#MyAppVersion}'
  else
    What := 'Downgrade: mnKord v' + PreviousVersion + ' -> v{#MyAppVersion}';

  Result := What + NewLine + NewLine +
    'Plugin (VST3):' + NewLine + Space + ExpandConstant('{commoncf64}\VST3\mnKord.vst3') + NewLine + NewLine +
    'User guide and licenses:' + NewLine + Space + ExpandConstant('{app}');

  if PreviousVersion <> '' then
    Result := Result + NewLine + NewLine +
      'Close your DAW first if it has mnKord loaded; Setup offers to close it.';
end;
