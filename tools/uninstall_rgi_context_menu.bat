@ECHO OFF
SetLocal EnableExtensions

reg delete "HKCU\Software\Classes\SystemFileAssociations\.png\shell\RgImageConvertToRgi" /f >NUL 2>NUL
reg delete "HKCU\Software\Classes\SystemFileAssociations\.rgi\shell\RgImageConvertToPng" /f >NUL 2>NUL
reg delete "HKCU\Software\Classes\SystemFileAssociations\.rgi\shell\RgImageOpenInViewer" /f >NUL 2>NUL

ECHO Removed rg_image RGI context menu entries for the current Windows user.
EXIT /B 0
