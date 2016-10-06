#include "decoder/encoder_decoder.h"

#include <vector>
#include <yaml-cpp/yaml.h>

#include "common/threadpool.h"

#include "dl4mt/dl4mt.h"

#include "decoder/god.h"
#include "decoder/loader.h"
#include "decoder/scorer.h"
#include "decoder/sentence.h"

#include "mblas/matrix.h"

mblas::Matrix& EncoderDecoderState::GetStates() {
  return states_;
}

mblas::Matrix& EncoderDecoderState::GetEmbeddings() {
  return embeddings_;
}

const mblas::Matrix& EncoderDecoderState::GetStates() const {
  return states_;
}

const mblas::Matrix& EncoderDecoderState::GetEmbeddings() const {
  return embeddings_;
}

using EDState = EncoderDecoderState;

EncoderDecoder::EncoderDecoder(const std::string& name,
                               const YAML::Node& config,
                               size_t tab,
                               const Weights& model)
  : Scorer(name, config, tab),
    model_(model),
    encoder_(new Encoder(model_)),
    decoder_(new Decoder(model_))
{}

void EncoderDecoder::Score(const State& in,
                           Prob& prob,
                           State& out) {
  const EDState& edIn = in.get<EDState>();
  EDState& edOut = out.get<EDState>();

  decoder_->MakeStep(edOut.GetStates(), prob,
                     edIn.GetStates(), edIn.GetEmbeddings(),
                     SourceContext_);
}

State* EncoderDecoder::NewState() {
  return new EDState();
}

void EncoderDecoder::BeginSentenceState(State& state) {
  EDState& edState = state.get<EDState>();
  decoder_->EmptyState(edState.GetStates(), SourceContext_, 1);
  decoder_->EmptyEmbedding(edState.GetEmbeddings(), 1);
}

void EncoderDecoder::SetSource(const Sentence& source) {
  encoder_->GetContext(source.GetWords(tab_),
                        SourceContext_);
}

void EncoderDecoder::AssembleBeamState(const State& in,
                                       const Beam& beam,
                                       State& out) {
  std::vector<size_t> beamWords;
  std::vector<size_t> beamStateIds;
  for(auto h : beam) {
      beamWords.push_back(h->GetWord());
      beamStateIds.push_back(h->GetPrevStateIndex());
  }

  const EDState& edIn = in.get<EDState>();
  EDState& edOut = out.get<EDState>();

  edOut.GetStates() = mblas::Assemble<mblas::byRow, mblas::Matrix>(edIn.GetStates(), beamStateIds);
  decoder_->Lookup(edOut.GetEmbeddings(), beamWords);
}

void EncoderDecoder::GetAttention(mblas::Matrix& Attention) {
  decoder_->GetAttention(Attention);
}

size_t EncoderDecoder::GetVocabSize() const {
  return decoder_->GetVocabSize();
}

void EncoderDecoder::Filter(const std::vector<size_t>& filterIds) {
  decoder_->Filter(filterIds);
}

Encoder& EncoderDecoder::GetEncoder() {
  return *encoder_;
}

Decoder& EncoderDecoder::GetDecoder() {
  return *decoder_;
}



EncoderDecoderLoader::EncoderDecoderLoader(const std::string name,
                                           const YAML::Node& config)
  : Loader(name, config) {}

void EncoderDecoderLoader::Load() {
  std::string path = Get<std::string>("path");
  auto devices = God::Get<std::vector<size_t>>("devices");
  ThreadPool devicePool(devices.size());
  weights_.resize(devices.size());

  size_t i = 0;
  for(auto d : devices) {
    devicePool.enqueue([i, d, &path, this] {
      LOG(info) << "Loading model " << path;
      weights_[i].reset(new Weights(path, d));
    });
    ++i;
  }
}

ScorerPtr EncoderDecoderLoader::NewScorer(const size_t taskId) {
  size_t i = taskId % weights_.size();
  size_t d = weights_[i]->GetDevice();
  size_t tab = Has("tab") ? Get<size_t>("tab") : 0;
  return ScorerPtr(new EncoderDecoder(name_, config_,
                                      tab, *weights_[i]));
}
