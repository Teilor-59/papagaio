# Motor de análise: C++ puro, sem JUCE, para rodar igual no app e na ferramenta offline.
add_library(papagaio_motor STATIC
    src/AnalisadorDeEspectro.cpp
    src/AnalisadorDuplo.cpp
    src/Bandas.cpp
    src/MonitorDeDrift.cpp
    src/DetectorMicrofonia.cpp
    src/Fft.cpp
    src/IgualAudibilidade.cpp
    src/LeitorWav.cpp
    src/MapaDeRisco.cpp
    src/MedidorDeNivel.cpp)
target_include_directories(papagaio_motor PUBLIC include)
target_compile_features(papagaio_motor PUBLIC cxx_std_17)
