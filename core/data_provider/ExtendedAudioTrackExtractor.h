#pragma once

#include <vrs/DiskFile.h>
#include <vrs/RecordFormatStreamPlayer.h>
#include <vrs/StreamId.h>
#include <vector>

namespace projectaria::tools::data_provider {

class ExtendedAudioTrackExtractor : public vrs::RecordFormatStreamPlayer {
 public:
  ExtendedAudioTrackExtractor(const std::string& folderPath, vrs::StreamId id, uint32_t& counter, bool wav64);
  ~ExtendedAudioTrackExtractor() override;

  bool onAudioRead(const vrs::CurrentRecord& record, size_t, const vrs::ContentBlock& audioBlock) override;
  bool onUnsupportedBlock(const vrs::CurrentRecord& record, size_t, const vrs::ContentBlock& cb) override;

  static int createWavFile(const std::string& wavFilePath, const vrs::AudioContentBlockSpec& audioBlock, vrs::DiskFile& outFile, bool wav64);
  static int writeWavAudioData(vrs::DiskFile& inFile, const vrs::AudioContentBlockSpec& audioBlock, const std::vector<uint8_t>& audio);
  static int closeWavFile(vrs::DiskFile& inFile);

 private:
  std::string folderPath_;
  vrs::StreamId id_;
  uint32_t& cumulativeOutputAudioFileCount_;
  uint32_t streamOutputAudioFileCount_ = 0;
  vrs::AudioContentBlockSpec currentAudioContentBlockSpec_;
  vrs::DiskFile currentWavFile_;
  std::vector<uint8_t> audio_;
  double segmentStartTimestamp_ = 0;
  uint64_t segmentSamplesCount_ = 0;
  bool wav64_;
};

} // namespace projectaria::tools::data_provider
