; NSIS installer for KSipPhone (Windows). Built by .github/workflows/windows.yml:
;   makensis -DVERSION=x.y.z -DSRCDIR=<bundle dir> ksipphone.nsi
Unicode true
!include "MUI2.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!define APPNAME "KSipPhone"
!define APPKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\KSipPhone"
!define CAPKEY "Software\KSipPhone\Capabilities"

Name "${APPNAME} ${VERSION}"
OutFile "KSipPhone-${VERSION}-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\KSipPhone"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!define MUI_ICON "ksipphone.ico"
!define MUI_UNICON "ksipphone.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\ksipphone.exe"
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "German"

Section "KSipPhone" SecMain
  SectionIn RO
  ; stop a running instance (it keeps baresip.exe open)
  nsExec::Exec 'taskkill /IM ksipphone.exe /F'
  nsExec::Exec 'taskkill /IM baresip.exe /F'
  SetOutPath "$INSTDIR"
  File /r "${SRCDIR}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortCut "$SMPROGRAMS\KSipPhone.lnk" "$INSTDIR\ksipphone.exe"

  WriteRegStr HKCU "${APPKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${APPKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${APPKEY}" "Publisher" "gongsearch"
  WriteRegStr HKCU "${APPKEY}" "URLInfoAbout" "https://github.com/gongsearch/ksipphone"
  WriteRegStr HKCU "${APPKEY}" "DisplayIcon" "$INSTDIR\ksipphone.exe"
  WriteRegStr HKCU "${APPKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "${APPKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${APPKEY}" "NoRepair" 1
SectionEnd

Section "tel:, sip: and callto: links" SecLinks
  ; ProgID + capabilities, so KSipPhone appears under Settings > Apps > Default apps
  WriteRegStr HKCU "Software\Classes\KSipPhone.URL" "" "KSipPhone call link"
  WriteRegStr HKCU "Software\Classes\KSipPhone.URL" "URL Protocol" ""
  WriteRegStr HKCU "Software\Classes\KSipPhone.URL\DefaultIcon" "" "$INSTDIR\ksipphone.exe,0"
  WriteRegStr HKCU "Software\Classes\KSipPhone.URL\shell\open\command" "" '"$INSTDIR\ksipphone.exe" "%1"'
  WriteRegStr HKCU "${CAPKEY}" "ApplicationName" "KSipPhone"
  WriteRegStr HKCU "${CAPKEY}" "ApplicationDescription" "SIP phone"
  !macro _Proto P
    WriteRegStr HKCU "${CAPKEY}\URLAssociations" "${P}" "KSipPhone.URL"
    ; direct class as well (used if the user never picked another default)
    WriteRegStr HKCU "Software\Classes\${P}" "" "URL:${P}"
    WriteRegStr HKCU "Software\Classes\${P}" "URL Protocol" ""
    WriteRegStr HKCU "Software\Classes\${P}\shell\open\command" "" '"$INSTDIR\ksipphone.exe" "%1"'
  !macroend
  !insertmacro _Proto "tel"
  !insertmacro _Proto "sip"
  !insertmacro _Proto "sips"
  !insertmacro _Proto "callto"
  WriteRegStr HKCU "Software\RegisteredApplications" "KSipPhone" "${CAPKEY}"
SectionEnd

Section "Start with Windows (in the tray)" SecAutostart
  CreateShortCut "$SMSTARTUP\KSipPhone.lnk" "$INSTDIR\ksipphone.exe" "--minimized"
SectionEnd

Section "Uninstall"
  nsExec::Exec 'taskkill /IM ksipphone.exe /F'
  nsExec::Exec 'taskkill /IM baresip.exe /F'
  Delete "$SMPROGRAMS\KSipPhone.lnk"
  Delete "$SMSTARTUP\KSipPhone.lnk"
  DeleteRegKey HKCU "Software\Classes\KSipPhone.URL"
  !macro _UnProto P
    ReadRegStr $0 HKCU "Software\Classes\${P}\shell\open\command" ""
    StrCmp $0 '"$INSTDIR\ksipphone.exe" "%1"' 0 +2
      DeleteRegKey HKCU "Software\Classes\${P}"
  !macroend
  !insertmacro _UnProto "tel"
  !insertmacro _UnProto "sip"
  !insertmacro _UnProto "sips"
  !insertmacro _UnProto "callto"
  DeleteRegKey HKCU "Software\KSipPhone"
  DeleteRegValue HKCU "Software\RegisteredApplications" "KSipPhone"
  DeleteRegKey HKCU "${APPKEY}"
  RMDir /r "$INSTDIR"
SectionEnd
