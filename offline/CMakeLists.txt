add_executable(papagaio-offline
    main.cpp
    Relatorio.cpp)
target_link_libraries(papagaio-offline PRIVATE papagaio_motor)
