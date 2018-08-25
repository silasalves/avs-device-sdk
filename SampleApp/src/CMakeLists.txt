set(SampleApp_SOURCES)
list(APPEND SampleApp_SOURCES 
    ConnectionObserver.cpp
    ConsolePrinter.cpp
    GuiRenderer.cpp
    InteractionManager.cpp
    KeywordObserver.cpp
    PortAudioMicrophoneWrapper.cpp
    UIManager.cpp
    UserInputManager.cpp
    SampleApplication.cpp
    main.cpp)

IF (HAS_EXTERNAL_MEDIA_PLAYER_ADAPTERS)
    file(GLOB_RECURSE SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/ExternalMediaAdapterRegistration/*.cpp)
    foreach(myfile ${SRC_FILE})
       list(APPEND SampleApp_SOURCES ${myfile})
    endforeach(myfile)
ENDIF()

add_executable(SampleApp ${SampleApp_SOURCES})

# add_library(pv_porcupine STATIC IMPORTED)
# set_target_properties(pv_porcupine PROPERTIES
#   IMPORTED_LOCATION "/home/silas/workspace/Porcupine/lib/linux/x86_64/libpv_porcupine.a"
#   INTERFACE_INCLUDE_DIRECTORIES "/home/silas/workspace/Porcupine/include"
# )
# target_link_libraries(SampleApp pv_porcupine)

target_include_directories(SampleApp PUBLIC 
    "${SampleApp_SOURCE_DIR}/include"
    "${MediaPlayer_SOURCE_DIR}/include"
    "${AudioResources_SOURCE_DIR}/include"
    "${RegistrationManager_SOURCE_DIR}/include"
    "${ESP_SOURCE_DIR}/include"
    "${PORTAUDIO_INCLUDE_DIR}")
    # "/home/silas/workspace/Porcupine/include")

target_link_libraries(SampleApp 
    DefaultClient
    CapabilitiesDelegate
    CBLAuthDelegate
    MediaPlayer
    SQLiteStorage
    ESP
    "${PORTAUDIO_LIB_PATH}")

if(KWD)
    target_link_libraries(SampleApp KeywordDetectorProvider)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(SampleApp
        "-framework CoreAudio" 
        "-framework AudioToolbox" 
        "-framework AudioUnit" 
        "-framework CoreServices" 
        "-framework Carbon")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_link_libraries(SampleApp
      rt m pthread asound)
endif()

