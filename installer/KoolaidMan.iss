; Koolaid Man - Windows installer (Inno Setup 6)
; Built in CI:  ISCC.exe /DSourceDir=<folder with the built plug-in> /DAppVersion=x.y.z installer\KoolaidMan.iss

#ifndef SourceDir
  #define SourceDir "..\dist"
#endif
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{7E1C9A52-4B7D-4E0B-9C1D-6B3A5F2E8D41}
AppName=Koolaid Man
AppVersion={#AppVersion}
AppPublisher=Koolaid Man
DefaultDirName={autopf}\Koolaid Man
DefaultGroupName=Koolaid Man
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=KoolaidMan-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=Koolaid Man (Beat-Drop Creator)
CloseApplications=yes
; a plug-in's code file ends in .vst3 - include it so Setup notices a DAW (or the plug-in
; scanner) holding it open and offers to close it, instead of failing with "Access is denied"
CloseApplicationsFilter=*.exe,*.dll,*.vst3
RestartApplications=no

[Types]
Name: "full"; Description: "VST3 plug-in + standalone app"
Name: "plugin"; Description: "VST3 plug-in only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Cubase and other DAWs)"; Types: full plugin custom
Name: "standalone"; Description: "Standalone app (no DAW needed)"; Types: full

[Files]
; restartreplace: if the plug-in is still locked, replace it at the next reboot instead of failing
Source: "{#SourceDir}\Koolaid Man.vst3\*"; DestDir: "{commoncf64}\VST3\Koolaid Man.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion restartreplace uninsrestartdelete; Components: vst3
Source: "{#SourceDir}\Koolaid Man.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete; Components: standalone
Source: "{#SourceDir}\INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Koolaid Man"; Filename: "{app}\Koolaid Man.exe"; Components: standalone
Name: "{group}\Koolaid Man - Quick Start"; Filename: "{app}\INSTALL.txt"
Name: "{group}\Uninstall Koolaid Man"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\INSTALL.txt"; Description: "Read the quick start"; Flags: postinstall shellexec skipifsilent unchecked

[Messages]
SetupAppRunningError=Koolaid Man is open (in your DAW or the standalone app).%n%nClose Cubase / your DAW and the Koolaid Man app, then click OK.
FinishedLabel=Koolaid Man is installed.%n%nIn Cubase: rescan plug-ins if needed (Studio > VST Plug-in Manager), then put Koolaid Man on an Insert slot - it's under Tools.
