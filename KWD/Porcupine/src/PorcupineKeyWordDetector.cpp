/*
 * Copyright 2017-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Modifications copyright 2018 Silas Franco dos Reis Alves
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 * 
 * Modifications
 * 
 *     Modified the Kitt.ai interface to use the Porcupine engine instead.
 */

#include <memory>
#include <sstream>

#include <AVSCommon/Utils/Logger/Logger.h>
#include <AVSCommon/Utils/Memory/Memory.h>

#include "Porcupine/PorcupineKeyWordDetector.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace alexaClientSDK {
namespace kwd {

using namespace avsCommon;
using namespace avsCommon::avs;
using namespace avsCommon::sdkInterfaces;
using namespace avsCommon::utils;

using nlohmann::json;

static const std::string TAG("PorcupineKeyWordDetector");

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

/// The number of hertz per kilohertz.
static const size_t HERTZ_PER_KILOHERTZ = 1000;

/// The timeout to use for read calls to the SharedDataStream.
const std::chrono::milliseconds TIMEOUT_FOR_READ_CALLS = std::chrono::milliseconds(1000);

/// The Porcupine compatible audio encoding of LPCM.
static const avsCommon::utils::AudioFormat::Encoding PORCUPINE_COMPATIBLE_ENCODING =
    avsCommon::utils::AudioFormat::Encoding::LPCM;

/// The Porcupine compatible endianness which is little endian.
static const avsCommon::utils::AudioFormat::Endianness PORCUPINE_COMPATIBLE_ENDIANNESS =
    avsCommon::utils::AudioFormat::Endianness::LITTLE;


std::unique_ptr<PorcupineKeyWordDetector> PorcupineKeyWordDetector::create(
    std::shared_ptr<AudioInputStream> stream,
    AudioFormat audioFormat,
    std::unordered_set<std::shared_ptr<KeyWordObserverInterface>> keyWordObservers,
    std::unordered_set<std::shared_ptr<KeyWordDetectorStateObserverInterface>> keyWordDetectorStateObservers,
    const std::string& configFilePath,
    std::chrono::milliseconds msToPushPerIteration) {
    if (!stream) {
        ACSDK_ERROR(LX("createFailed").d("reason", "nullStream"));
        return nullptr;
    }
    // TODO: ACSDK-249 - Investigate cpu usage of converting bytes between endianness and if it's not too much, do it.
    if (isByteswappingRequired(audioFormat)) {
        ACSDK_ERROR(LX("createFailed").d("reason", "endianMismatch"));
        return nullptr;
    }

    // Extract the configurations from the json file.
    json porcupineConf;
    std::ifstream porcupineConfFile(configFilePath);
    porcupineConfFile >> porcupineConf;

    std::unique_ptr<PorcupineKeyWordDetector> detector(new PorcupineKeyWordDetector(
        stream,
        audioFormat,
        keyWordObservers,
        keyWordDetectorStateObservers,
        porcupineConf["parameters-filepath"],
        porcupineConf["keyword"],
        porcupineConf["keyword-filepath"],
        porcupineConf["sensitivity"],
        msToPushPerIteration));
    if (!detector->init(audioFormat)) {
        ACSDK_ERROR(LX("createFailed").d("reason", "initDetectorFailed"));
        return nullptr;
    }
    return detector;
}

PorcupineKeyWordDetector::~PorcupineKeyWordDetector() {
    m_isShuttingDown = true;
    if (m_detectionThread.joinable()) {
        m_detectionThread.join();
    }
}

PorcupineKeyWordDetector::PorcupineKeyWordDetector(
    std::shared_ptr<AudioInputStream> stream,
    avsCommon::utils::AudioFormat audioFormat,
    std::unordered_set<std::shared_ptr<KeyWordObserverInterface>> keyWordObservers,
    std::unordered_set<std::shared_ptr<KeyWordDetectorStateObserverInterface>> keyWordDetectorStateObservers,
    std::string confPath,
    std::string keyword,
    std::string keywordPath,
    float sensitivity,
    std::chrono::milliseconds msToPushPerIteration) :
        AbstractKeywordDetector(keyWordObservers, keyWordDetectorStateObservers),
        m_stream{stream},
        m_maxSamplesPerPush{(audioFormat.sampleRateHz / HERTZ_PER_KILOHERTZ) * msToPushPerIteration.count()} {
    
    const pv_status_t status = pv_porcupine_init(
        confPath.c_str(),
        keywordPath.c_str(),
        sensitivity,
        &m_porcupine);
    porcupineKeyword = keyword;

    if(status != PV_STATUS_SUCCESS) {
        ACSDK_ERROR(LX("PorcupineKeyWordDetectorFailed").d("reason", "Unknown"));
    }
}

bool PorcupineKeyWordDetector::init(avsCommon::utils::AudioFormat audioFormat) {
    if (!isAudioFormatCompatibleWithPorcupine(audioFormat)) {
        return false;
    }
    m_streamReader = m_stream->createReader(AudioInputStream::Reader::Policy::BLOCKING);
    if (!m_streamReader) {
        ACSDK_ERROR(LX("initFailed").d("reason", "createStreamReaderFailed"));
        return false;
    }
    m_isShuttingDown = false;
    m_detectionThread = std::thread(&PorcupineKeyWordDetector::detectionLoop, this);
    return true;
}

bool PorcupineKeyWordDetector::isAudioFormatCompatibleWithPorcupine(avsCommon::utils::AudioFormat audioFormat) {
    if (audioFormat.numChannels != 1) {
        ACSDK_ERROR(LX("isAudioFormatCompatibleWithPorcupineFailed")
                        .d("reason", "numChannelsMismatch")
                        .d("PorcupineNumChannels", 1)
                        .d("numChannels", audioFormat.numChannels));
        return false;
    }
    if (audioFormat.sampleRateHz != 16000) {
        ACSDK_ERROR(LX("isAudioFormatCompatibleWithPorcupineFailed")
                        .d("reason", "sampleRateMismatch")
                        .d("PorcupineSampleRate", 16000)
                        .d("sampleRate", audioFormat.sampleRateHz));
        return false;
    }
    if (audioFormat.sampleSizeInBits != 16) {
        ACSDK_ERROR(LX("isAudioFormatCompatibleWithPorcupineFailed")
                        .d("reason", "sampleSizeInBitsMismatch")
                        .d("PorcupineSampleSizeInBits", 16)
                        .d("sampleSizeInBits", audioFormat.sampleSizeInBits));
        return false;
    }
    if (audioFormat.endianness != PORCUPINE_COMPATIBLE_ENDIANNESS) {
        ACSDK_ERROR(LX("isAudioFormatCompatibleWithPorcupineFailed")
                        .d("reason", "endiannessMismatch")
                        .d("PorcupineEndianness", PORCUPINE_COMPATIBLE_ENDIANNESS)
                        .d("endianness", audioFormat.endianness));
        return false;
    }
    if (audioFormat.encoding != PORCUPINE_COMPATIBLE_ENCODING) {
        ACSDK_ERROR(LX("isAudioFormatCompatibleWithPorcupineFailed")
                        .d("reason", "encodingMismatch")
                        .d("PorcupineEncoding", PORCUPINE_COMPATIBLE_ENCODING)
                        .d("encoding", audioFormat.encoding));
        return false;
    }
    return true;
}

void PorcupineKeyWordDetector::detectionLoop() {
    notifyKeyWordDetectorStateObservers(KeyWordDetectorStateObserverInterface::KeyWordDetectorState::ACTIVE);
    int16_t audioDataToPush[m_maxSamplesPerPush];
    ssize_t wordsRead;
    while (!m_isShuttingDown) {
        bool didErrorOccur;
        wordsRead = readFromStream(
            m_streamReader, m_stream, audioDataToPush, m_maxSamplesPerPush, TIMEOUT_FOR_READ_CALLS, &didErrorOccur);
        if (didErrorOccur) {
            break;
        } else if (wordsRead > 0) {
            // Words were successfully read.
            notifyKeyWordDetectorStateObservers(KeyWordDetectorStateObserverInterface::KeyWordDetectorState::ACTIVE);

            // Append the new data to Porcupine's buffer.
            m_buffer.insert(m_buffer.end(), audioDataToPush, audioDataToPush + wordsRead);

            // If there is enough data on the buffer, let Porcupine do the detection, otherwise skip to let the buffer grow.
            if (m_buffer.size() > (size_t)pv_porcupine_frame_length()){
                bool result;
                const pv_status_t status = pv_porcupine_process(m_porcupine, &m_buffer[0], &result);
                switch(status){
                    case PV_STATUS_SUCCESS:
                        if (result){
                            notifyKeyWordObservers(
                                m_stream,
                                porcupineKeyword,
                                KeyWordObserverInterface::UNSPECIFIED_INDEX,
                                m_streamReader->tell());
                        }
                        // Remove the data porcupine just read from the buffer.
                        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + pv_porcupine_frame_length());
                        break;
                    case PV_STATUS_OUT_OF_MEMORY:
                        ACSDK_ERROR(LX("detectionLoopFailed").d("reason", "outOfMemory"));
                        notifyKeyWordDetectorStateObservers(
                            KeyWordDetectorStateObserverInterface::KeyWordDetectorState::ERROR);
                        didErrorOccur = true;
                        break;
                    case PV_STATUS_IO_ERROR:
                        ACSDK_ERROR(LX("detectionLoopFailed").d("reason", "IOError"));
                        notifyKeyWordDetectorStateObservers(
                            KeyWordDetectorStateObserverInterface::KeyWordDetectorState::ERROR);
                        didErrorOccur = true;
                        break;
                    case PV_STATUS_INVALID_ARGUMENT:
                        ACSDK_ERROR(LX("detectionLoopFailed").d("reason", "invalidArgument"));
                        notifyKeyWordDetectorStateObservers(
                            KeyWordDetectorStateObserverInterface::KeyWordDetectorState::ERROR);
                        didErrorOccur = true;
                        break;
                }
                
                if (didErrorOccur) {
                    break;
                }
            }
        }
    }
    m_streamReader->close();
}

}  // namespace kwd
}  // namespace alexaClientSDK
