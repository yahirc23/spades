//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#ifndef HAMMER_VALIDHKMERGENERATOR_HPP_
#define HAMMER_VALIDHKMERGENERATOR_HPP_

#include <deque>
#include <string>
#include <vector>

#include "HSeq.hpp"
#include "io/reads/single_read.hpp"

#include <cmath>
#include <cstdint>

template <size_t kK>
class ValidHKMerGenerator {
 public:
  /**
   * @param read Read to generate k-mers from.
   * @param bad_quality_threshold  This class virtually cuts
   * nucleotides with quality lower the threshold from the ends of the
   * read.
   */
  // FIXME: Switch to delegating ctor.
  explicit ValidHKMerGenerator(const io::SingleRead &read,
                               unsigned bad_quality_threshold = 5) {
    Reset(read.GetSequenceString().data(), read.GetQualityString().data(),
          read.GetSequenceString().size(), bad_quality_threshold);
  }

  /**
   * @param seq sequence to generate k-mers from.
   * @param qual quality string
   * @param bad_quality_threshold  This class virtually cuts
   * nucleotides with quality lower the threshold from the ends of the
   * read.
   */
  explicit ValidHKMerGenerator(const char *seq, const char *qual, size_t len,
                               unsigned bad_quality_threshold = 5) {
    Reset(seq, qual, len, bad_quality_threshold);
  }

  ValidHKMerGenerator()
      : kmer_(),
        seq_(0),
        qual_(0),
        pos_(-1),
        nlen_(-1),
        end_(-1),
        len_(0),
        correct_probability_(0),
        bad_quality_threshold_(5),
        has_more_(false),
        first_(true) {}

  void Reset(const char *seq, const char *qual, size_t len,
             unsigned bad_quality_threshold = 5) {
    kmer_ = hammer::HSeq<kK>();
    seq_ = seq;
    qual_ = qual;
    pos_ = -1;
    nlen_ = -1;
    end_ = -1;
    len_ = len;
    correct_probability_ = 0.0;
    bad_quality_threshold_ = bad_quality_threshold;
    has_more_ = true;
    first_ = true;
    last_ = false;
    probs_.resize(0);
    runlens_.resize(0);
    length = 0;

    TrimBadQuality();
    Next();
  }

  /**
   * @result true if Next() succeed while generating new k-mer, false
   * otherwise.
   */
  bool HasMore() const { return has_more_; }

  /**
   * @result last k-mer generated by Next().
   */
  const hammer::HSeq<kK> &kmer() const { return kmer_; }

  /**
   * @result last k-mer position in initial read.
   */
  size_t pos() const { return pos_; }

  size_t nlen() const { return nlen_; }

  /**
   * @result number of nucleotides trimmed from left end
   */
  size_t trimmed_left() const { return beg_; }

  /**
   * @result number of nucleotides trimmed from right end
   */
  size_t trimmed_right() const { return len_ - end_; }

  /**
   * @result probability that last generated k-mer is correct.
   */
  double correct_probability() const {
    return exp(correct_probability_ / length);
  }

  /**
   * This functions reads next k-mer from the read and sets hasmore to
   * if succeeded. You can access k-mer read with kmer().
   */
  void Next();

 private:
  void TrimBadQuality();

  double Prob(unsigned qual) {
    return max(1 - pow(10.0, -(qual / 10.0)),
               1e-40);  //(qual < 3 ? 0.25 : 1 - pow(10.0, -(qual / 10.0)));
                        //     return Globals::quality_probs[qual];
  }

  unsigned GetQual(size_t pos) {
    if (pos >= len_) {
      return 1;
    } else {
      return qual_[pos];
    }
  }

  hammer::HSeq<kK> kmer_;
  const char *seq_;
  const char *qual_;
  size_t pos_;
  size_t nlen_;
  size_t length = 0;
  size_t beg_;
  size_t end_;
  size_t len_;
  double correct_probability_;
  unsigned bad_quality_threshold_;
  bool has_more_;
  bool first_;
  bool last_;
  std::deque<double> probs_;
  std::deque<double> runlens_;

  // Disallow copy and assign
  ValidHKMerGenerator(const ValidHKMerGenerator &) = delete;
  void operator=(const ValidHKMerGenerator &) = delete;
};

template <size_t kK>
void ValidHKMerGenerator<kK>::TrimBadQuality() {
  pos_ = 0;
  if (qual_)
    for (; pos_ < len_; ++pos_) {
      if (GetQual(pos_) >= bad_quality_threshold_) break;
    }
  beg_ = pos_;
  end_ = len_;
  if (qual_)
    for (; end_ > pos_; --end_) {
      if (GetQual(end_ - 1) >= bad_quality_threshold_) break;
    }
}

template <size_t kK>
void ValidHKMerGenerator<kK>::Next() {
  if (last_) {
    has_more_ = false;
    return;
  }

  size_t toadd = (first_ ? kK : 1);
  char pnucl = -1;
  double cprob = 0.0;
  double len = 0.0;
  nlen_ = 0;
  // Build the flow-space kmer looking over homopolymer streches.
  while (toadd) {
    // If we went past the end, then there are no new kmers anymore.
    // The current one might be incomplete but we yield it anyway
    // because one hk-mer can't have much influence on the consensus.
    if (pos_ >= end_) {
      last_ = true;
      if (toadd > 0) {
        has_more_ = false;
      }
      return;
    }

    // Check, whether the current nucl is good (not 'N')
    char cnucl = seq_[pos_ + nlen_];
    if (!is_nucl(cnucl)) {
      toadd = kK;
      pnucl = -1;
      pos_ += nlen_ + 1;
      nlen_ = 0;
      len = 0;
      length = 0;
      correct_probability_ = 0.0;
      probs_.resize(0);
      runlens_.resize(0);
      continue;
    }
    if (qual_) {
      cprob += log(Prob(GetQual(pos_ + nlen_)));
      ++len;
    }

    // If current nucl differs from previous nucl then either we're starting the
    // k-mer or just finished the homopolymer run.
    if (cnucl != pnucl) {
      // If previous nucl was valid then finish the current homopolymer run
      if (pnucl != -1) {
        toadd -= 1;

        correct_probability_ += cprob;
        length += len;

        if (probs_.size() == kK) {
          correct_probability_ -= probs_[0];
          length -= runlens_[0];
          probs_.pop_front();
          runlens_.pop_front();
        }

        probs_.push_back(cprob);
        runlens_.push_back(len);
        cprob = 0.0;
        len = 0;
      }
      pnucl = cnucl;
    }

    // If we have something to add to flowspace kmer - do it now.
    if (toadd) {
      kmer_ <<= cnucl;
      nlen_ += 1;
    }
  }

  pos_ += nlen_;
  first_ = false;
}
#endif  // HAMMER_VALIDHKMERGENERATOR_HPP__
