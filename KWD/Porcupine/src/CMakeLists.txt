add_definitions("-DACSDK_LOG_MODULE=PorcupineKeyWordDetector")
add_library(PORCUPINE SHARED
    PorcupineKeyWordDetector.cpp)

target_include_directories(PORCUPINE PUBLIC
	"${PORCUPINE_JSON_INCLUDE_DIR}"
	"${PORCUPINE_KEY_WORD_DETECTOR_INCLUDE_DIR}"
	"${KWD_SOURCE_DIR}/include"
	"${PORCUPINE_SOURCE_DIR}/include")

add_library(pv_porcupine STATIC IMPORTED)
set_target_properties(pv_porcupine PROPERTIES
	IMPORTED_LOCATION "${PORCUPINE_KEY_WORD_DETECTOR_LIB_PATH}/libpv_porcupine.a"
	INTERFACE_INCLUDE_DIRECTORIES "${PORCUPINE_KEY_WORD_DETECTOR_INCLUDE_DIR}"
)

target_link_libraries(PORCUPINE KWD AVSCommon pv_porcupine)

# install target
asdk_install()