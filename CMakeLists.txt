cmake_minimum_required(VERSION 3.22)
project(Papagaio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# O runtime do C++ vai dentro do executável: o Papagaio.exe roda sozinho, sem o Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(motor)
add_subdirectory(offline)

option(PAPAGAIO_APP "Compila o app desktop (precisa do JUCE em externos/JUCE)" ON)
if(PAPAGAIO_APP)
    add_subdirectory(externos/JUCE EXCLUDE_FROM_ALL)
    add_subdirectory(app)
endif()
