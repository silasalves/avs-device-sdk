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

#ifndef ALEXA_CLIENT_SDK_KWD_PORCUPINE_INCLUDE_PORCUPINE_PORCUPINEKEYWORDDETECTOR_H_
#define ALEXA_CLIENT_SDK_KWD_PORCUPINE_INCLUDE_PORCUPINE_PORCUPINEKEYWORDDETECTOR_H_

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <AVSCommon/Utils/AudioFormat.h>
#include <AVSCommon/AVS/AudioInputStream.h>
#include <AVSCommon/SDKInterfaces/KeyWordObserverInterface.h>
#include <AVSCommon/SDKInterfaces/KeyWordDetectorStateObserverInterface.h>

#include "KWD/AbstractKeywordDetector.h"
#include <pv_porcupine.h>
#include <vector>

namespace alexaClientSDK {
namespace kwd {

class PorcupineKeyWordDetector : public AbstractKeywordDetector {
public:
    /**
     * Creates a @c PorcupineKeyWordDetector.
     *
     * @param stream The stream of audio data. This should be mono (1 channel) and formatted in LPCM encoded with
     * 16 bits per sample and a sample rate of 16 kHz. Additionally, the data should be in little endian format.
     * @param audioFormat The format of the audio data located within the stream.
     * @param keyWordObservers The observers to notify of keyword detections.
     * @param keyWordDetectorStateObservers The observers to notify of state changes in the engine.
     * @param configFilePath The path to the configuration file.
     * @return A new @c PorcupineKeyWordDetector, or @c nullptr if the operation failed.
     * @see https://github.com/Picovoice/Porcupine for more information.
     */
    static std::unique_ptr<PorcupineKeyWordDetector> create(
        std::shared_ptr<avsCommon::avs::AudioInputStream> stream,
        avsCommon::utils::AudioFormat audioFormat,
        std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::KeyWordObserverInterface>> keyWordObservers,
        std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::KeyWordDetectorStateObserverInterface>> keyWordDetectorStateObservers,
        const std::string& configFilePath,
        std::chrono::milliseconds msToPushPerIteration = std::chrono::milliseconds(20));

    /**
     * Destructor.
     */
    ~PorcupineKeyWordDetector() override;

private:
    /**
     * Constructor.
     *
     * @param stream The stream of audio data. This should be mono (1 channel) and formatted in LPCM encoded with
     * 16 bits per sample and a sample rate of 16 kHz. Additionally, the data should be in little endian format.
     * @param audioFormat The format of the audio data located within the stream.
     * @param keyWordObservers The observers to notify of keyword detections.
     * @param keyWordDetectorStateObservers The observers to notify of state changes in the engine.
     * @param confPath The absolute path to `porcupine_params.pv`.
     * @param keyword The keyword being used (e.g. "Alexa" or "Vancouver").
     * @param keywordPath The absolute path to the keyword model (usually a `*.ppn` file, such as `alexa_linux.ppn`)
     * @param sensitivity Enables trading miss rate for false alarm. It is a floating number within [0, 1].
     * @param msToPushPerIteration The amount of data in milliseconds to push to Porcupine at a time. Smaller sizes will
     * lead to less delay but more CPU usage. Additionally, larger amounts of data fed into the engine per iteration
     * might lead longer delays before receiving keyword detection events. This has been defaulted to 20 milliseconds
     * as it is a good trade off between CPU usage and recognition delay.
     * @see https://github.com/Picovoice/Porcupine for more information regarding @c confPath, @c keywordPath
     * and @c sensitivity.
     */
    PorcupineKeyWordDetector(
        std::shared_ptr<avsCommon::avs::AudioInputStream> stream,
        avsCommon::utils::AudioFormat audioFormat,
        std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::KeyWordObserverInterface>> keyWordObservers,
        std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::KeyWordDetectorStateObserverInterface>> keyWordDetectorStateObservers,
        std::string confPath,
        std::string keyword,
        std::string keywordPath,
        float sensitivity,
        std::chrono::milliseconds msToPushPerIteration = std::chrono::milliseconds(20));

    /**
     * Initializes the stream reader and kicks off a thread to read data from the stream. This function should only be
     * called once with each new @c PorcupineKeyWordDetector.
     *
     * @param audioFormat The format of the audio data located within the stream.
     * @return @c true if the engine was initialized properly and @c false otherwise.
     */
    bool init(avsCommon::utils::AudioFormat audioFormat);

    /**
     * Checks to see if an @c avsCommon::utils::AudioFormat is compatible with Porcupine.
     *
     * @param audioFormat The audio format to check.
     * @return @c true if the audio format is compatible and @c false otherwise.
     */
    bool isAudioFormatCompatibleWithPorcupine(avsCommon::utils::AudioFormat audioFormat);

    /// The main function that reads data and feeds it into the engine.
    void detectionLoop();

    /// Indicates whether the internal main loop should keep running.
    std::atomic<bool> m_isShuttingDown;

    /// The stream of audio data.
    const std::shared_ptr<avsCommon::avs::AudioInputStream> m_stream;

    /// The reader that will be used to read audio data from the stream.
    std::shared_ptr<avsCommon::avs::AudioInputStream::Reader> m_streamReader;

    /// Internal thread that reads audio from the buffer and feeds it to the Porcupine engine.
    std::thread m_detectionThread;

    /// The Porcupine engine instantiation.
    pv_porcupine_object_t *m_porcupine;

    /// Buffer for Porcupine.
    std::vector<int16_t> m_buffer;

    /// Keyword being detected
    std::string porcupineKeyword;

    /**
     * The max number of samples to push into the underlying engine per iteration. This will be determined based on the
     * sampling rate of the audio data passed in.
     */
    const size_t m_maxSamplesPerPush;

};

}  // namespace kwd
}  // namespace alexaClientSDK

#endif  // ALEXA_CLIENT_SDK_KWD_PORCUPINE_INCLUDE_PORCUPINE_PORCUPINEKEYWORDDETECTOR_H_
