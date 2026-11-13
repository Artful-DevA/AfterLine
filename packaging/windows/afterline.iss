; Inno Setup script for the AfterLine installer. Built by the Windows workflow:
;   iscc /DAppVersion=0.2.3 /DSourceDir=..\..\dist\AfterLine packaging\windows\afterline.iss
; Installs for the current user (no administrator rights needed) unless the
; person chooses "all users".

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\AfterLine"
#endif

[Setup]
AppId={{6E2F9C41-8B3A-4D57-9A1E-AF7E2B0C5D13}
AppName=AfterLine
AppVersion={#AppVersion}
AppVerName=AfterLine {#AppVersion}
AppPublisher=AfterLine
AppPublisherURL=https://afterline-linux.netlify.app
AppSupportURL=https://github.com/Artful-DevA/AfterLine/issues
AppUpdatesURL=https://afterline-linux.netlify.app
DefaultDirName={autopf}\AfterLine
DefaultGroupName=AfterLine
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
LicenseFile={#SourceDir}\LICENSE
SetupIconFile=..\..\resources\windows\afterline.ico
UninstallDisplayIcon={app}\bin\afterline.exe
OutputDir=..\..\dist
OutputBaseFilename=AfterLine-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\AfterLine"; Filename: "{app}\bin\afterline.exe"
Name: "{autodesktop}\AfterLine"; Filename: "{app}\bin\afterline.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\afterline.exe"; Description: "{cm:LaunchProgram,AfterLine}"; Flags: nowait postinstall skipifsilent
