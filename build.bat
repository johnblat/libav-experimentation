cl.exe /Zi ^
/I "ffmpeg-master-latest-win64-gpl-shared/include" ^
/I "SDL2-2.30.8/include" ^
/Fe:build\ffmpeg-test.exe main.cpp ^
ffmpeg-master-latest-win64-gpl-shared\lib\avcodec.lib ^
ffmpeg-master-latest-win64-gpl-shared\lib\avformat.lib ^
ffmpeg-master-latest-win64-gpl-shared\lib\avutil.lib ^
ffmpeg-master-latest-win64-gpl-shared\lib\swscale.lib ^
SDL-release-2.30.8\VisualC\x64\Debug\SDL2.lib ^
SDL-release-2.30.8\VisualC\x64\Debug\SDL2main.lib ^
/link /SUBSYSTEM:WINDOWS Shell32.lib

:: SDL2-2.30.8\lib\x64\SDL2.lib ^
:: SDL2-2.30.8\lib\x64\SDL2main.lib ^

