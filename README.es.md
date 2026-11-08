# Vega Vocal

Cadena de voz completa en un solo plugin VST3 (JUCE 9, C++20).

Entrada → Afinación → De-esser → Compresor → Saturación → Delay + Reverb → Salida

## Compilar

Requiere CMake y Visual Studio 2022 Build Tools. `external/JUCE` es un clon de JUCE 9.0.4 y no se versiona.

```
git clone --branch 9.0.4 --depth 1 https://github.com/juce-framework/JUCE.git external/JUCE
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target VegaVocal_VST3 vocal_core_tests vocal_host_tests --parallel
```

El plugin queda en `build/VegaVocal_artefacts/Release/VST3/Vega Vocal.vst3`. Para instalarlo, cópialo a `C:\Program Files\Common Files\VST3`.

## Instalador

```
cmake --build build --config Release --target VegaVocalSetup
```

Crea `build/Release/Instalar Vega Vocal.exe`, un solo archivo con el plugin dentro. Para compartirlo, comprímelo en un zip junto con `installer/LEEME.txt`.

## Pruebas

```
build\Release\vocal_core_tests.exe
build\vocal_host_tests_artefacts\Release\vocal_host_tests.exe captura.png "build\VegaVocal_artefacts\Release\VST3\Vega Vocal.vst3"
```

`vocal_core_tests` mide el audio a 44.1, 48 y 96 kHz. `vocal_host_tests` prueba el plugin completo, carga el `.vst3` real y guarda una captura de la interfaz.

## Archivos

- `Source/VocalCore.h`: todo el procesamiento de audio, sin dependencia de JUCE.
- `Source/PluginProcessor.*`: parámetros, presets, estado y bypass.
- `Source/PluginEditor.*`: interfaz.

## Licencia de JUCE

JUCE se usa bajo AGPLv3 o bajo su licencia comercial. Revisa sus condiciones antes de vender o distribuir el plugin.
