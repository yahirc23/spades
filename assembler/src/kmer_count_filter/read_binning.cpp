//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#include "annotation.hpp"
#include "simple_tools.hpp"
#include "logger/log_writers.hpp"

#include "graphio.hpp"
#include "io/osequencestream.hpp"

void create_console_logger() {
    logging::logger *log = logging::create_logger("", logging::L_INFO);
    log->add_writer(std::make_shared<logging::console_writer>());
    logging::attach_logger(log);
}

namespace debruijn_graph {

class ContigBinner {
    const conj_graph_pack& gp_;
    EdgeAnnotation edge_annotation_;

    map<bin_id, std::shared_ptr<io::PairedOutputSequenceStream>> out_streams_;

public:
    ContigBinner(const conj_graph_pack& gp, const vector<bin_id>& bins_of_interest) :
                     gp_(gp),
                     edge_annotation_(gp, bins_of_interest) {
    }

    void Init(const string& output_dir, io::SingleStream& contigs, AnnotationStream& annotation_stream) {
        edge_annotation_.Fill(contigs, annotation_stream);
        for (bin_id bin : edge_annotation_.interesting_bins()) {
            string out_prefix = output_dir + ToString(bin);
            out_streams_.insert(make_pair(bin,
                                          make_shared<io::PairedOutputSequenceStream>(out_prefix + "_1.fasta",
                                                                                      out_prefix + "_2.fasta")));
        }
    }

    void Run(io::PairedStream& paired_reads) {
        io::PairedRead paired_read;
        while (!paired_reads.eof()) {
            paired_reads >> paired_read;
            set<bin_id> bins;
            insert_all(bins, edge_annotation_.RelevantBins(paired_read.first()));
            insert_all(bins, edge_annotation_.RelevantBins(paired_read.second()));
            for (auto bin : bins) {
                (*(out_streams_[bin])) << paired_read;
            }
        }
    }

    void close() {
        out_streams_.clear();
    }
};

}

//todo make it take dataset info
int main(int argc, char** argv) {
    using namespace debruijn_graph;

    if (argc < 9) {
        cout << "Usage: read_binning <K> <saves path> <contigs path> <contigs binning info> "
                "<left reads> <right reads> <output prefix> (<bins of interest>)+"  << endl;
        exit(1);
    }

    TmpFolderFixture("tmp");
    create_console_logger();
    size_t k = lexical_cast<size_t>(argv[1]);
    string saves_path = argv[2];
    string contigs_path = argv[3];
    string contigs_binning_path = argv[4];
    string left_reads = argv[5];
    string right_reads = argv[6];
    string out_prefix = argv[7];

    std::vector<bin_id> bins_of_interest;
    for (int i = 8; i < argc; ++i) {
        bins_of_interest.push_back(argv[i]);
    }

    conj_graph_pack gp(k, "tmp", 0);
    INFO("Load graph from " << saves_path);
    graphio::ScanGraphPack(saves_path, gp);

    ContigBinner binner(gp, bins_of_interest);

    auto contigs_stream_ptr = io::EasyStream(contigs_path, false);
    AnnotationStream binning_stream(contigs_binning_path);

    binner.Init(out_prefix, *contigs_stream_ptr, binning_stream);

    auto paired_stream = io::PairedEasyStream(left_reads, right_reads, false, 0);
    binner.Run(*paired_stream);
    binner.close();
    return 0;
}
