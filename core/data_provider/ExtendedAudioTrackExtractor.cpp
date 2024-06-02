#include "ExtendedAudioTrackExtractor.h"

#include <array>
#include <iostream>
#include <vrs/RecordReaders.h>
#include <vrs/helpers/Endian.h>
#include <vrs/helpers/FileMacros.h>
#include <vrs/helpers/Rapidjson.hpp>
#include <vrs/helpers/Strings.h>
#include <vrs/os/Utils.h>
#include <fmt/format.h>

using namespace std;
using namespace vrs;
using namespace projectaria::tools::data_provider;

namespace {

template <class T>
inline void writeHeader(void* p, const T t) {
  memcpy(p, &t, sizeof(T));
}
} // namespace

constexpr uint32_t kWavHeaderSize = 44;
constexpr uint32_t kWave64HeaderSize = 64; // Adjusted for WAV64

namespace projectaria::tools::data_provider {

ExtendedAudioTrackExtractor::ExtendedAudioTrackExtractor(const string& wavFilePath, StreamId id, uint32_t& counter, bool& stop)
    : wavFilePath_{wavFilePath}, id_{id}, cumulativeOutputAudioFileCount_{counter}, fileAudioSpec_{AudioFormat::UNDEFINED}, stop_{stop} {}

ExtendedAudioTrackExtractor::~ExtendedAudioTrackExtractor() {
  closeWavFile(currentWavFile_);
}

bool ExtendedAudioTrackExtractor::onAudioRead(
    const vrs::CurrentRecord& record, 
    size_t, 
    const vrs::ContentBlock& audioBlock) {
  audio_.resize(audioBlock.getBlockSize());
  int readStatus = record.reader->read(audio_);
  if (readStatus != 0) {
    cout << "Failed to read audio data." << endl;
    return false;
  }

  const AudioContentBlockSpec& audioBlockSpec = audioBlock.audio();
  if (audioBlockSpec.getAudioFormat() != AudioFormat::PCM) {
    cout << "Skipping non-PCM audio block" << endl;
    return true;
  }

  const int64_t kMaxWavFileSize = 1LL << 40; // Adjusted for WAV64
  if (!fileAudioSpec_.isCompatibleWith(audioBlockSpec) || currentWavFile_.getPos() + audio_.size() >= kMaxWavFileSize) {
    closeWavFile(currentWavFile_);

    // string path = fmt::format("{}/{}-{:04}-{:.3f}.{}", wavFilePath_, id_.getNumericName(), streamOutputAudioFileCount_, record.timestamp, "w64");
    cout << "Writing " << wavFilePath_ << endl;
    int status = createWavFile(wavFilePath_, audioBlockSpec, currentWavFile_);
    if (status != 0) {
      cout << "Can't create wav file: " << status << endl;
      return false;
    }

    fileAudioSpec_ = audioBlockSpec;
    cumulativeOutputAudioFileCount_++;
    streamOutputAudioFileCount_++;
    segmentStartTimestamp_ = record.timestamp;
    segmentSamplesCount_ = 0;
  }

  int status = writeWavAudioData(currentWavFile_, audioBlockSpec, audio_);
  if (status != 0) {
    cout << "Can't write to wav file: " << status << endl;
    return false;
  }

  // Time/sample counting validation
  if (segmentSamplesCount_ > 0) {
    const double kMaxJitter = 0.01;
    double actualTime = record.timestamp - segmentStartTimestamp_;
    double expectedTime = static_cast<double>(segmentSamplesCount_) / fileAudioSpec_.getSampleRate();
    if (actualTime - expectedTime > kMaxJitter) {
      cout << "Audio block at " << actualTime << " ms late." << endl;
    } else if (expectedTime - actualTime > kMaxJitter) {
      cout << "Audio block at " << expectedTime << " ms, early." << endl;
    }
  } else {
    segmentStartTimestamp_ = record.timestamp;
  }
  segmentSamplesCount_ += audioBlock.audio().getSampleCount();

  return true;
}

bool ExtendedAudioTrackExtractor::onUnsupportedBlock(const vrs::CurrentRecord& record, size_t, const vrs::ContentBlock& cb) {
  if (cb.getContentType() == ContentType::AUDIO) {
      cout << "Audio block skipped for " << record.streamId.getName() << ", content: " << cb.asString() << endl;
  }
  return false;
}

std::string ExtendedAudioTrackExtractor::getSummary(
    const std::string& vrsFilePath,
    vrs::StreamId streamId,
    const std::string& streamFlavor,
    double firstImageTime,
    double lastImageTime) {
  JDocument doc;
  JsonWrapper json{doc};
  closeWavFile(currentWavFile_);
  json.addMember("input", vrsFilePath);
  json.addMember("output", wavFilePath_);
  json.addMember("stream_id", streamId.getNumericName());
  if (!streamFlavor.empty()) {
    json.addMember("stream_flavor", streamFlavor);
  }
  if (firstImageTime >= 0) {
    json.addMember("first_image_timestamp", firstImageTime);
  }
  if (lastImageTime >= 0) {
    json.addMember("last_image_timestamp", lastImageTime);
  }
  json.addMember("status", status_.empty() ? "success" : status_);
  if (status_.empty()) {
    if (firstAudioRecordTimestamp_ <= lastAudioRecordTimestamp_) {
      json.addMember("first_audio_record_timestamp", firstAudioRecordTimestamp_);
      json.addMember("last_audio_record_timestamp", lastAudioRecordTimestamp_);
    }
    if (firstAudioRecordDuration_ <= lastAudioRecordDuration_) {
      json.addMember("first_audio_record_duration", firstAudioRecordDuration_);
      json.addMember("last_audio_record_duration", lastAudioRecordDuration_);
    }
    if (minMidAudioRecordDuration_ <= maxMidAudioRecordDuration_) {
      json.addMember("min_mid_audio_record_duration", minMidAudioRecordDuration_);
      json.addMember("max_mid_audio_record_duration", maxMidAudioRecordDuration_);
    }
    if (minAudioRecordGap_ <= maxAudioRecordGap_) {
      json.addMember("min_audio_record_gap", minAudioRecordGap_);
      json.addMember("max_audio_record_gap_", maxAudioRecordGap_);
    }
    double totalDuration = 0;
    if (audioSampleCount_ > 0 && fileAudioSpec_.getSampleRate() > 0) {
      totalDuration = audioSampleCount_ * 1. / fileAudioSpec_.getSampleRate();
    }
    json.addMember("total_audio_duration", totalDuration);
    json.addMember("audio_record_miss_count", audioRecordMissCount_);
    double firstSampleRatio =
        static_cast<double>(firstSampleTimestampTotal_) / pastLastSampleTimestampTotal_;
    json.addMember("first_sample_timestamp_ratio", firstSampleRatio);
  }
  if (!firstAudioBlockSpec_.empty()) {
    json.addMember("audio_channel_count", fileAudioSpec_.getChannelCount());
    json.addMember("audio_sample_rate", fileAudioSpec_.getSampleRate());
    json.addMember("audio_sample_format", fileAudioSpec_.getSampleFormatAsString());
    json.addMember("first_audio_block_spec", firstAudioBlockSpec_);
  }
  return jDocumentToJsonStringPretty(doc);
}


int ExtendedAudioTrackExtractor::createWavFile(const std::string& wavFilePath, const vrs::AudioContentBlockSpec& audioBlock, vrs::DiskFile& outFile) {
  IF_ERROR_RETURN(outFile.create(wavFilePath));

  array<uint8_t, kWavHeaderSize> fileHeader{};

  writeHeader(fileHeader.data() + 0, htobe32(0x52494646)); // 'RIFF' in big endian
  writeHeader(fileHeader.data() + 4, htole32(0)); // Placeholder for size
  writeHeader(fileHeader.data() + 8, htobe32(0x57415645)); // 'WAVE' in big endian
  writeHeader(fileHeader.data() + 12, htobe32(0x666d7420)); // 'fmt' in big endian
  writeHeader(fileHeader.data() + 16, htole32(16)); // PCM header size

  uint16_t format = 1; // PCM format
  if (audioBlock.isIEEEFloat()) {
    format = 3;
  } else if (audioBlock.getSampleFormat() == AudioSampleFormat::A_LAW) {
    format = 6;
  } else if (audioBlock.getSampleFormat() == AudioSampleFormat::MU_LAW) {
    format = 7;
  }

  uint32_t bytesPerSample = (audioBlock.getBitsPerSample() + 7) / 8;
  writeHeader(fileHeader.data() + 20, htole16(format));
  writeHeader(fileHeader.data() + 22, htole16(audioBlock.getChannelCount()));
  writeHeader(fileHeader.data() + 24, htole32(audioBlock.getSampleRate()));
  writeHeader(fileHeader.data() + 28, htole32(audioBlock.getSampleRate() * audioBlock.getChannelCount() * bytesPerSample));
  writeHeader(fileHeader.data() + 32, htole16(audioBlock.getChannelCount() * bytesPerSample));
  writeHeader(fileHeader.data() + 34, htole16(audioBlock.getBitsPerSample()));
  writeHeader(fileHeader.data() + 36, htobe32(0x64617461)); // 'data' in big endian
  writeHeader(fileHeader.data() + 40, htole32(0)); // Placeholder for data size

  return outFile.write((char*)fileHeader.data(), fileHeader.size());
}

int ExtendedAudioTrackExtractor::writeWavAudioData(vrs::DiskFile& inFile, const vrs::AudioContentBlockSpec& audioBlock, const std::vector<uint8_t>& audio) {
  uint32_t srcOffset = 0;
  uint32_t bytesPerSampleBlock = (audioBlock.getBitsPerSample() + 7) / 8 * audioBlock.getChannelCount();
  uint32_t srcStride = audioBlock.getSampleBlockStride();
  uint32_t totalSamples = audioBlock.getSampleCount();
  for (uint32_t i = 0; i < totalSamples; ++i) {
    if (srcOffset >= (uint32_t)audio.size()) {
      cout << "Malformed audio block encountered, read past end of audio block" << endl;
      break;
    }

    IF_ERROR_RETURN(inFile.write((char*)audio.data() + srcOffset, bytesPerSampleBlock));
    srcOffset += srcStride;
  }
  return 0;
}

int ExtendedAudioTrackExtractor::closeWavFile(vrs::DiskFile& inFile) {
  if (!inFile.isOpened()) {
    return 0;
  }
  uint32_t totalAudioDataSize = inFile.getPos() - kWavHeaderSize;
  IF_ERROR_RETURN(inFile.setPos(4));
  uint32_t fileSize = htole32(36 + totalAudioDataSize);
  IF_ERROR_RETURN(inFile.write((char*)&fileSize, sizeof(fileSize)));
  IF_ERROR_RETURN(inFile.setPos(40));
  uint32_t dataSize = htole32(totalAudioDataSize);
  IF_ERROR_RETURN(inFile.write((char*)&dataSize, sizeof(dataSize)));
  return inFile.close();
}

bool ExtendedAudioTrackExtractor::stop(const string& reason) {
  status_ = reason;
  stop_ = true;
  return false;
}

string extractAudioTrack(vrs::utils::FilteredFileReader& filteredReader, const string& filePath) {
  const string wavFilePath = filePath + (helpers::endsWith(filePath,  ".w64") ? "" : ".w64" );
  const string jsonFilePath = wavFilePath + ".json";
  JDocument doc;
  JsonWrapper json{doc};
  string folderPath = os::getParentFolder(wavFilePath);
  if (folderPath.length() > 0) {
      if (!os::pathExists(folderPath)) {
          int status = os::makeDirectories(folderPath);
          if (status != 0) {
              json.addMember("status", "No audio track found.");
              return jDocumentToJsonStringPretty(doc);
          }
      }
      if (!os::isDir(folderPath)) {
          json.addMember(
              "status",
              fmt::format("Can't write output files at {}, because something is there...", folderPath));
          return jDocumentToJsonStringPretty(doc);
      }
  }
  bool stop = false;
  uint32_t counter = 0;
  unique_ptr<ExtendedAudioTrackExtractor> audioExtractor;
  StreamId streamId;
  for (auto id : filteredReader.filter.streams) {
      if (filteredReader.reader.mightContainAudio(id)) {
          if (audioExtractor) {
              json.addMember("status", "Multiple audio tracks found.");
              return jDocumentToJsonStringPretty(doc);
          }
          streamId = id;
          audioExtractor = make_unique<ExtendedAudioTrackExtractor>(wavFilePath, streamId, counter, stop);
          filteredReader.reader.setStreamPlayer(id, audioExtractor.get());
      }
  }
  if (!audioExtractor) {
      json.addMember("status", "No audio track found.");
      return jDocumentToJsonStringPretty(doc);
  }
  filteredReader.iterateSafe();
  double firstImageTime = -1;
  double lastImageTime = -1;
  for (auto id : filteredReader.filter.streams) {
      if (filteredReader.reader.mightContainImages(id)) {
          auto record = filteredReader.reader.getRecord(id, Record::Type::DATA, 0);
          if (record != nullptr && (firstImageTime < 0 || record->timestamp < firstImageTime)) {
              firstImageTime = record->timestamp;
          }
          record = filteredReader.reader.getLastRecord(id, Record::Type::DATA);
          if (record != nullptr && (lastImageTime < 0 || record->timestamp > lastImageTime)) {
              lastImageTime = record->timestamp;
          }
      }
  }
  return audioExtractor->getSummary(
      filteredReader.getPathOrUri(),
      streamId,
      filteredReader.reader.getFlavor(streamId),
      firstImageTime,
      lastImageTime);
}

} // namespace projectaria::tools::data_provider
