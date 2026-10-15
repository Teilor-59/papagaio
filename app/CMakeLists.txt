juce_add_gui_app(Papagaio
    PRODUCT_NAME "Papagaio"
    COMPANY_NAME "Teilor André Fianco"
    BUNDLE_ID "com.teilorfianco.papagaio"
    ICON_BIG "${CMAKE_SOURCE_DIR}/Identidade Visual/Icone.png"    # gerado com Papagaio.exe --exportar-icone
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "O Papagaio escuta o mic de medição para avisar de microfonia.")

# o logotipo vai dentro do executável
juce_add_binary_data(PapagaioDados SOURCES "${CMAKE_SOURCE_DIR}/Identidade Visual/Logotipo.png")

target_sources(Papagaio PRIVATE
    Main.cpp
    Aparencia.cpp
    Blocos.cpp
    Calibracao.cpp
    Mesa.cpp
    Componentes.cpp
    MotorAoVivo.cpp
    TelaPrincipal.cpp
    Vista.cpp)

target_compile_definitions(Papagaio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>   # só no Windows: cabeçalho ASIO do JUCE, sob a licença do SDK da Steinberg
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Papagaio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Papagaio,JUCE_VERSION>")

target_link_libraries(Papagaio
    PRIVATE
        papagaio_motor
        PapagaioDados
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
