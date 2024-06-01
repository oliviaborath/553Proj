#pragma once

#include <vrs/DiskFile.h>
#include <vrs/RecordFormatStreamPlayer.h>
#include <vrs/utils/FilteredFileReader.h>
#include <vrs/StreamId.h>
#include <vector>

namespace projectaria::tools::data_provider {

class ExtendedAudioTrackExtractor : public vrs::RecordFormatStreamPlayer {
 public:
  ExtendedAudioTrackExtractor(const std::string& wavFilePath, vrs::StreamId id, uint32_t& counter, bool wav64, bool& stop);
  ~ExtendedAudioTrackExtractor() override;

  bool onAudioRead(const vrs::CurrentRecord& record, size_t, const vrs::ContentBlock& audioBlock) override;
  bool onUnsupportedBlock(const vrs::CurrentRecord& record, size_t, const vrs::ContentBlock& cb) override;

  std::string getSummary(
      const std::string& vrsFilePath,
      vrs::StreamId streamId,
      const std::string& streamFlavor,
      double firstImageTime,
      double lastImageTime);

  static int createWavFile(const std::string& wavFilePath, const vrs::AudioContentBlockSpec& audioBlock, vrs::DiskFile& outFile, bool wav64);
  static int writeWavAudioData(vrs::DiskFile& inFile, const vrs::AudioContentBlockSpec& audioBlock, const std::vector<uint8_t>& audio);
  static int closeWavFile(vrs::DiskFile& inFile);

 protected:
  bool stop(const std::string& reason);

  // path to the output wav file
  std::string wavFilePath_;
  // flag set to true when an error occured, and file decoding should probably stop
  bool& stop_;

  // device id & instance for the stream we are operating on
  vrs::StreamId id_;
  // used to sum up the total number of audio files written out across all
  // streams (caller provides a reference so this class can add to it)
  uint32_t& cumulativeOutputAudioFileCount_;
  // count of audio files written out in this specific stream
  uint32_t streamOutputAudioFileCount_ = 0;
  double segmentStartTimestamp_ = 0;
  uint64_t segmentSamplesCount_ = 0;
  bool wav64_;

  // used to track compatibility of successive audio blocks within a stream;
  // if format changes, we close the wav file and start a new one
  vrs::AudioContentBlockSpec fileAudioSpec_;
   // output stream of wav file currently being written
  vrs::DiskFile currentWavFile_;
  // temp audio buffer to hold segment of audio to be written to file
  std::vector<uint8_t> audio_;

  // Error status describing what happened, or the empty string if nothing lethal is reported
  std::string status_;
  // For validation: start timestamp of the audio segment
  double audioStartTimestamp_ = 0;
  // For validation: count of audio samples previously processed since the start of the segment
  uint64_t audioSampleCount_ = 0;

  double firstAudioRecordTimestamp_ = -1;
  double lastAudioRecordTimestamp_ = -1;
  double firstAudioRecordDuration_ = -1;
  double lastAudioRecordDuration_ = -1;
  double minMidAudioRecordDuration_ = -1;
  double maxMidAudioRecordDuration_ = -1;
  double minAudioRecordGap_ = -1;
  double maxAudioRecordGap_ = -1;

  int32_t lastRecordSampleCount_ = 0;

  // To guess if an audio record's timestamp is close to the timestamp of the first audio sample,
  // we accumulate differences between expectations and reality, so "less is better".
  // Sum of weights: audio block duration against gap to next audio record's timestamp
  uint64_t firstSampleTimestampTotal_ = 0;
  // Sum of weights: audio block duration against gap to previous audio record's timestamp
  uint64_t pastLastSampleTimestampTotal_ = 0;

  uint32_t audioRecordMissCount_ = 0;
  std::string firstAudioBlockSpec_;
};

std::string extractAudioTrack(vrs::utils::FilteredFileReader& filteredReader, const std::string& filePath, bool wav64);

} // namespace projectaria::tools::data_provider
