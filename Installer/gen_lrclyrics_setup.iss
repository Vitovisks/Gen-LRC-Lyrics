
#define MyAppName "Gen LRC Lyrics"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "Vitin"

[Setup]
AppId={{2302C553-9503-4FD7-8DE0-49D73BACD87D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={code:GetDefaultWacupDir}
DisableDirPage=no
DisableProgramGroupPage=yes
Uninstallable=yes
PrivilegesRequired=admin
OutputBaseFilename=Gen-LRC-Lyrics-Setup
SetupIconFile=gen_lrclyrics.ico
WizardImageFile=wizard_image.bmp
WizardSmallImageFile=wizard_small.bmp
Compression=lzma
SolidCompression=yes
AppComments=Plugin de letras sincronizadas para WACUP.
AppCopyright=Copyright (C) 2026 Vitin
UninstallDisplayName=Gen LRC Lyrics
VersionInfoVersion=1.0.0.0
VersionInfoDescription=Gen LRC Lyrics Installer
VersionInfoProductName=Gen LRC Lyrics
VersionInfoCompany=Vitin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Files]
Source: "..\gen_lrclyrics.dll"; DestDir: "{app}\Plugins"; Flags: ignoreversion

[Code]
function GetDefaultWacupDir(Param: string): string;
var
  Candidate: string;
begin
  Candidate := ExpandConstant('{pf32}\WACUP');
  if FileExists(AddBackslash(Candidate) + 'wacup.exe') then
  begin
    Result := Candidate;
    Exit;
  end;

  Candidate := ExpandConstant('{pf}\WACUP');
  if FileExists(AddBackslash(Candidate) + 'wacup.exe') then
  begin
    Result := Candidate;
    Exit;
  end;

  Candidate := 'C:\Program Files (x86)\WACUP';
  if FileExists(AddBackslash(Candidate) + 'wacup.exe') then
  begin
    Result := Candidate;
    Exit;
  end;

  Candidate := 'D:\Program Files (x86)\WACUP';
  if FileExists(AddBackslash(Candidate) + 'wacup.exe') then
  begin
    Result := Candidate;
    Exit;
  end;

  Candidate := 'D:\Arquivos de Programas (x86)\WACUP';
  if FileExists(AddBackslash(Candidate) + 'wacup.exe') then
  begin
    Result := Candidate;
    Exit;
  end;

  Candidate := 'E:\Program Files (x86)\WACUP';
  if FileExists(AddBackslash(Candidate) + 'wacup.exe') then
  begin
    Result := Candidate;
    Exit;
  end;

  Result := ExpandConstant('{pf32}\WACUP');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if CurPageID = wpSelectDir then
  begin
    if not FileExists(AddBackslash(WizardDirValue) + 'wacup.exe') then
    begin
      MsgBox(
        'A pasta selecionada nao parece ser uma instalacao do WACUP.' +
        Chr(13) + Chr(10) +
        'Selecione a pasta que contem o arquivo wacup.exe.',
        mbError, MB_OK
      );

      Result := False;
    end;
  end;
end;

[Run]
Filename: "{app}\wacup.exe"; Description: "Abrir WACUP"; Flags: postinstall nowait skipifsilent
