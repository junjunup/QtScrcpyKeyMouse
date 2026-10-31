; Inno Setup 6 script: packs the folder made by publish_for_win.bat into a setup.exe
;   iscc /DSourceDir=<publish dir> /DAppVersion=1.0.0 /DOutputDir=<dir> ci\win\installer.iss

#ifndef SourceDir
  #error Pass /DSourceDir=<publish dir>
#endif
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define AppName "QtScrcpy KeyMouse"
#define AppExe "QtScrcpy.exe"

[Setup]
AppId={{6E3C2B7A-4D0F-4E8B-9C51-2A7F3D9B1E64}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=QtScrcpy (self build)
DefaultDirName={autopf}\QtScrcpyKeyMouse
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; per-user install by default, no admin prompt
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=QtScrcpyKeyMouse-Setup-{#AppVersion}
SetupIconFile=..\..\QtScrcpy\res\QtScrcpy.ico
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
#if FileExists(AddBackslash(CompilerPath) + "Languages\ChineseSimplified.isl")
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
#endif
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; the bundled adb server keeps adb.exe locked
Filename: "{app}\adb.exe"; Parameters: "kill-server"; Flags: runhidden skipifdoesntexist; RunOnceId: "KillAdb"

[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  // upgrading over a running adb server would fail to replace adb.exe
  if FileExists(ExpandConstant('{app}\adb.exe')) then
    Exec(ExpandConstant('{app}\adb.exe'), 'kill-server', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := '';
end;
