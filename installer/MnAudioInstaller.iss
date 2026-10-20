; The part of the Windows installers that mnKord and mnArpee share.
;
; This file is the same in both repositories (installer/MnAudioInstaller.iss): change it in one, copy it to
; the other. Each installer script defines these before including it:
;
;   MyAppName      "mnKord"        the product name, also the name of the .vst3 bundle
;   MyAppGuid      "6F0A3C52-..."  the AppId (a GUID, without braces); the old version is found through it
;   MyAppVersion   "1.5.0"         given by the build (ISCC /DMyAppVersion=x.y.z)
;   MyRepo         "mnkord"        the name of the repository, for the links
;   MyDocsLabel    "..."           (optional) what the install folder holds, for the "Ready to install" page
;
; and has its own AppId=, AppName=, AppVersion=, OutputDir=, [Files] and [Icons].
;
; What it does:
;  - the plugin goes to Common Files\VST3\MNAudio\<name>.vst3, the documents to Common Files\MNAudio\<name>
;  - a DAW that has the plugin loaded is asked to close (the check covers .vst3 files)
;  - the version found on the computer is shown on the "Ready to install" page; going back to an older
;    version asks first (a silent install says no)
;  - the old version is uninstalled before the new one is installed, and the folders earlier installers
;    used are removed

#ifndef MyDocsLabel
  #define MyDocsLabel "Licenses and readme"
#endif
#define MyVst3Dest "{commoncf64}\VST3\MNAudio\" + MyAppName + ".vst3"

[Setup]
AppPublisher=MNAudio
AppPublisherURL=https://github.com/miklosnemeth/{#MyRepo}
AppSupportURL=https://github.com/miklosnemeth/{#MyRepo}/issues
; The documents live in Common Files, next to the shared VST3 folder; there is nothing to choose, so there
; is no folder page. UsePreviousAppDir=no moves an install from an older version to this folder.
DefaultDirName={commoncf64}\MNAudio\{#MyAppName}
DisableDirPage=yes
UsePreviousAppDir=no
DefaultGroupName=MNAudio
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-win-x64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName} {#MyAppVersion}
WizardStyle=modern
; A DAW that has the plugin loaded keeps the .vst3 file locked. Setup checks the files it is about to
; replace (the default filter doesn't know .vst3) and asks to close the programs that use them.
CloseApplications=yes
CloseApplicationsFilter=*.vst3,*.exe,*.dll
RestartApplications=no

[InstallDelete]
; What earlier installers put elsewhere: the documents in Program Files, the plugin directly in the VST3 folder.
Type: filesandordirs; Name: "{autopf}\MNAudio\{#MyAppName}"
Type: dirifempty; Name: "{autopf}\MNAudio"
Type: filesandordirs; Name: "{commoncf64}\VST3\{#MyAppName}.vst3"

[Code]
// An older (or the same) version on this computer is found through the uninstall entry of Setup
// (the same AppId).
var
  OldVersion: String;
  OldUninstaller: String;

function UninstallKey(): String;
begin
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{' + '{#MyAppGuid}' + '}_is1';
end;

// Takes the number before the first dot off Version and returns it.
function NextPart(var Version: String): Integer;
var
  Dot: Integer;
begin
  Dot := Pos('.', Version);
  if Dot = 0 then
  begin
    Result := StrToIntDef(Version, 0);
    Version := '';
  end
  else
  begin
    Result := StrToIntDef(Copy(Version, 1, Dot - 1), 0);
    Version := Copy(Version, Dot + 1, Length(Version));
  end;
end;

// -1, 0 or 1 when A is older than, the same as or newer than B.
function CompareVersions(A, B: String): Integer;
var
  X, Y: Integer;
begin
  Result := 0;
  while (Result = 0) and ((A <> '') or (B <> '')) do
  begin
    X := NextPart(A);
    Y := NextPart(B);
    if X < Y then
      Result := -1
    else if X > Y then
      Result := 1;
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  OldVersion := '';
  OldUninstaller := '';

  if RegQueryStringValue(HKLM, UninstallKey(), 'DisplayVersion', OldVersion) then
  begin
    if RegQueryStringValue(HKLM, UninstallKey(), 'UninstallString', OldUninstaller) then
      OldUninstaller := RemoveQuotes(OldUninstaller);

    // Going back to an older version asks first (a silent install says no).
    if CompareVersions(OldVersion, '{#MyAppVersion}') > 0 then
      Result := SuppressibleMsgBox(
        'A newer version of {#MyAppName} (v' + OldVersion + ') is installed. Setup is about to install the older v{#MyAppVersion} over it.'
          + #13#10 + #13#10 + 'Do you want to go back to the older version?',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES;
  end;
end;

// The "Ready to install" page says what is about to happen to the installed version.
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  What: String;
begin
  if OldVersion = '' then
    What := 'New installation: {#MyAppName} v{#MyAppVersion}'
  else if CompareVersions(OldVersion, '{#MyAppVersion}') < 0 then
    What := 'Upgrade: {#MyAppName} v' + OldVersion + ' -> v{#MyAppVersion}'
  else if CompareVersions(OldVersion, '{#MyAppVersion}') = 0 then
    What := 'Reinstall: {#MyAppName} v{#MyAppVersion}'
  else
    What := 'Downgrade: {#MyAppName} v' + OldVersion + ' -> v{#MyAppVersion}';

  if OldVersion <> '' then
    What := What + NewLine + Space + 'The installed version is removed first.';

  Result := What + NewLine + NewLine +
    'Plugin (VST3):' + NewLine + Space + ExpandConstant('{#MyVst3Dest}') + NewLine + NewLine +
    '{#MyDocsLabel}:' + NewLine + Space + ExpandConstant('{app}');

  if OldVersion <> '' then
    Result := Result + NewLine + NewLine +
      'Close your DAW first if it has {#MyAppName} loaded; Setup offers to close it.';
end;

// The old version is uninstalled before the new one goes in, so no file of it is left behind.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode, Waited: Integer;
begin
  Result := '';
  if (OldVersion = '') or (OldUninstaller = '') or not FileExists(OldUninstaller) then
    Exit;

  Log('Running the uninstaller of version ' + OldVersion + ': ' + OldUninstaller);
  if Exec(OldUninstaller, '/VERYSILENT /NORESTART /SUPPRESSMSGBOXES', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Log('The old uninstaller returned ' + IntToStr(ResultCode));
    // Setup's uninstaller finishes in a second process: wait until its entry is gone (at most 20 s).
    Waited := 0;
    while RegKeyExists(HKLM, UninstallKey()) and (Waited < 20000) do
    begin
      Sleep(250);
      Waited := Waited + 250;
    end;
  end
  else
    // The files are replaced anyway.
    Log('The old uninstaller could not be started: ' + SysErrorMessage(ResultCode));
end;
