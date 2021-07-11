@echo off

mkdir .\build
pushd .\build
cl -FC -Zi c:\projects\teachme_studio\win32_main.cpp user32.lib gdi32.lib dsound.lib
popd