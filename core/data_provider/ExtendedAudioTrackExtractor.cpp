#include "ExtendedAudioTrackExtractor.h"

#include <array>
#include <iostream>
#include <vrs/RecordReaders.h>
#include <vrs/helpers/Endian.h>
#include <vrs/helpers/FileMacros.h>
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

constexpr uint32_t kWavHeaderSize = 44;
constexpr uint32_t kWave64HeaderSize = 64; // Example size, you will need to define the correct header size

} // namespace

int ExtendedAudioTrackExtractor::createWavFile(const std::string& wavFilePath, const vrs::AudioContentBlockSpec& audioBlock, vrs::DiskFile& outFile, bool wav64) {
  IF_ERROR_RETURN(outFile.create(wavFilePath));

  array<uint8_t, kWavHeaderSize> fileHeader{};
  if (wav64) {
    writeHeader(fileHeader.data() + 0, htobe32(0x52494646)); // 'RIFF' in big endian
    writeHeader(fileHeader.data() + 4, htole32(0)); // Placeholder for size
    writeHeader(fileHeader.data() + 8, htobe32(0x57415645)); // 'WAVE' in big endian
    // Add necessary headers for Wave64 format
  } else {
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
  }

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

ExtendedAudioTrackExtractor::ExtendedAudioTrackExtractor(const string& folderPath, StreamId id, uint32_t& counter, bool wav64)
    : folderPath_{folderPath}, id_{id}, cumulativeOutputAudioFileCount_{counter}, currentAudioContentBlockSpec_{AudioFormat::UNDEFINED}, wav64_{wav64} {}

ExtendedAudioTrackExtractor::~ExtendedAudioTrackExtractor() {
  closeWavFile(currentWavFile_);
}

bool ExtendedAudioTrackExtractor::onAudioRead(const vrs::CurrentRecord& record, size_t, const vrs::ContentBlock& audioBlock) {
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

  const int64_t kMaxWavFileSize = wav64_ ? (1LL << 40) : (1LL << 32); // Adjust for WAV64
  if (!currentAudioContentBlockSpec_.isCompatibleWith(audioBlockSpec) || currentWavFile_.getPos() + audio_.size() >= kMaxWavFileSize) {
    closeWavFile(currentWavFile_);

    string path = fmt::format("{}/{}-{:04}-{:.3f}.{}", folderPath_, id_.getNumericName(), streamOutputAudioFileCount_, record.timestamp, wav64_ ? "w64" : "wav");
    cout << "Writing " << path << endl;
    int status = createWavFile(path, audioBlockSpec, currentWavFile_, wav64_);
    if (status != 0) {
      cout << "Failed to create wav file: " << path << endl;
      return false;
    }

    currentAudioContentBlockSpec_ = audioBlockSpec;
    cumulativeOutputAudioFileCount_++;
    streamOutputAudioFileCount_++;
    segmentStartTimestamp_ = record.timestamp;
    segmentSamplesCount_ = 0;
  }

  int status = writeWavAudioData(currentWavFile_, audioBlockSpec, audio_);
  if (status != 0) {
    cout << "Failed to write audio data." << endl;
    return false;
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

string extractAudioTrack(FilteredFileReader& filteredReader, const std::string& filePath, bool wav64) {
  const string wavFilePath = filePath + (helpers::endsWith(filePath, ".wav") ? "" : ".wav");
  const string jsonFilePath = wavFilePath + ".json";
  JDocument doc;
  JsonWrapper json{doc};
  string folderPath = os::getParentFolder(wavFilePath);
  if (folderPath.length() > 0) {
    if (!os::pathExists(folderPath)) {
      int status = os::makeDirectories(folderPath);
      if (status != 0) {
        json.addMember("status", "No audio track found.");
        return vrs::helpers::failure(doc, jsonFilePath);
      }
    }
    if (!os::isDir(folderPath)) {
      json.addMember("status", fmt::format("Can't write output files at {}, because something is there...", folderPath));
      return vrs::helpers::failure(doc, jsonFilePath);
    }
  }
  bool stop = false;
  unique_ptr<ExtendedAudioTrackExtractor> audioExtractor;
  StreamId streamId;
  for (auto id : filteredReader.filter.streams) {
    if (filteredReader.reader.mightContainAudio(id)) {
      if (audioExtractor) {
        json.addMember("status", "Multiple audio tracks found.");
        return vrs::helpers::failure(doc, jsonFilePath);
      }
      streamId = id;
      audioExtractor = make_unique<ExtendedAudioTrackExtractor>(wavFilePath, stop, wav64);
      filteredReader.reader.setStreamPlayer(id, audioExtractor.get());
    }
  }
  if (!audioExtractor) {
    json.addMember("status", "No audio track found.");
    return vrs::helpers::failure(doc, jsonFilePath);
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
  return vrs::helpers::writeJson(
      jsonFilePath,
      audioExtractor->getSummary(
          filteredReader.getPathOrUri(),
          streamId,
          filteredReader.reader.getFlavor(streamId),
          firstImageTime,
          lastImageTime),
      true);
}
