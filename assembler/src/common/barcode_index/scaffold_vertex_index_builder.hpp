#pragma once
#include "scaffold_vertex_index.hpp"
#include "barcode_info_extractor.hpp"

namespace barcode_index {

    template <class EdgeEntryT>
    class AbstractScaffoldVertexEntryExtractor {
     public:
        virtual EdgeEntryT ExtractEntry(const path_extend::scaffold_graph::ScaffoldVertex &vertex) const = 0;
    };

    class SimpleScaffoldVertexEntryExtractor: public AbstractScaffoldVertexEntryExtractor<SimpleVertexEntry> {
     public:
        typedef typename path_extend::scaffold_graph::EdgeIdVertex EdgeIdVertex;
        typedef typename path_extend::scaffold_graph::PathVertex PathVertex;
     private:
        const debruijn_graph::Graph &g_;
        const FrameBarcodeIndexInfoExtractor& barcode_extractor_;
        const size_t tail_threshold_;
        const size_t count_threshold_;
        const size_t length_threshold_;

     public:
        SimpleScaffoldVertexEntryExtractor(const Graph &g_,
                                           const FrameBarcodeIndexInfoExtractor &barcode_extractor_,
                                           const size_t tail_threshold_,
                                           const size_t count_threshold_,
                                           const size_t length_threshold_)
            : g_(g_),
              barcode_extractor_(barcode_extractor_),
              tail_threshold_(tail_threshold_),
              count_threshold_(count_threshold_),
              length_threshold_(length_threshold_) {}

        SimpleVertexEntry ExtractEntry(const path_extend::scaffold_graph::ScaffoldVertex &vertex) const override {
            auto inner_vertex = vertex.getInnerVertex();

            SimpleVertexEntry empty;
            auto type = vertex.getType();
            switch (type) {
                case path_extend::scaffold_graph::ScaffoldVertexT::Edge: {
                    auto edge_vertex = std::static_pointer_cast<EdgeIdVertex>(inner_vertex);
                    return ExtractEntryInner(edge_vertex);
                }
                case path_extend::scaffold_graph::ScaffoldVertexT::Path: {
                    auto path_vertex = std::static_pointer_cast<PathVertex>(inner_vertex);
                    return ExtractEntryInner(path_vertex);
                }
            }
            WARN("ScaffoldVertex of unknown type");
            return empty;
        }

     private:
        SimpleVertexEntry ExtractEntryInner(shared_ptr<EdgeIdVertex> simple_edge_vertex) const {
            SimpleVertexEntry result;
            TRACE("Extracting entry from edge");
            const auto& entry = barcode_extractor_.GetBarcodesFromHead(simple_edge_vertex->get(), count_threshold_, tail_threshold_);
            std::copy(entry.begin(), entry.end(), std::inserter(result, result.end()));
            return result;
        }

        //fixme optimize later
        SimpleVertexEntry ExtractEntryInner(shared_ptr<PathVertex> path_vertex) const {
            TRACE("Extracting entry from path");
            size_t current_prefix = 0;
            path_extend::BidirectionalPath* path = path_vertex->get();
            size_t path_size = path->Size();
            SimpleVertexEntry result;
            const size_t global_count_threshold = 5;
            std::unordered_map<BarcodeId, size_t> barcode_to_count;
            for (size_t i = 0; i < path_size and current_prefix <= tail_threshold_; ++i) {
                EdgeId current_edge = path->At(i);
                if (g_.length(current_edge) < length_threshold_) {
                    current_prefix += g_.length(current_edge);
                    continue;
                }
                size_t current_tail = tail_threshold_ - current_prefix;
                TRACE("Current tail: " << current_tail);
                const auto &current_entry = barcode_extractor_.GetBarcodesAndCountsFromHead(current_edge, count_threshold_,current_tail);
                for (const auto& barcode_and_reads: current_entry) {
                    barcode_to_count[barcode_and_reads.first] += barcode_and_reads.second;
                }
                TRACE("Current entry size: " << barcode_to_count.size());
                current_prefix += g_.length(current_edge);
            }
            for (const auto& barcode_and_count: barcode_to_count) {
                if (barcode_and_count.second >= global_count_threshold) {
                    result.insert(barcode_and_count.first);
                }
            }
            TRACE("Result size: " << result.size());
            return result;
        }
    };

    template <class EdgeEntryT>
    class ScaffoldVertexIndexBuilder {
        typedef path_extend::scaffold_graph::ScaffoldVertex ScaffoldVertex;

        const Graph& g_;
        shared_ptr<AbstractScaffoldVertexEntryExtractor<EdgeEntryT>> vertex_entry_extractor_;
        shared_ptr<ScaffoldVertexIndex<EdgeEntryT>> index_;
        size_t max_threads_;

     public:
        ScaffoldVertexIndexBuilder(const Graph &g_,
                                   shared_ptr<AbstractScaffoldVertexEntryExtractor<EdgeEntryT>> vertex_entry_extractor_,
                                   size_t max_threads)
            : g_(g_), vertex_entry_extractor_(vertex_entry_extractor_),
              index_(std::make_shared<ScaffoldVertexIndex<EdgeEntryT>>(g_)), max_threads_(max_threads) {}

        template <class ContainerT>
        shared_ptr<ScaffoldVertexIndex<EdgeEntryT>> GetConstructedIndex(const ContainerT& vertex_container) {

            //todo make parallel using iterator chunks
            INFO("Constructing long edge index in " << max_threads_ << " threads");
//            size_t counter = 0;
//            size_t block_size = vertex_container.size() / 10;
            for (const auto& vertex: vertex_container)
            {
                auto entry = vertex_entry_extractor_->ExtractEntry(vertex);
                TRACE("Entry size: " << entry.size());
                {
                    index_->InsertEntry(vertex, std::move(entry));
//                    ++counter;
                }
//                if (counter % block_size == 0) {
//                    INFO("Processed " << counter << " edges out of " << vertex_container.size());
//                }
            }
            INFO("Constructed long edge index");
            return index_;
        }
    };

    class SimpleScaffoldVertexIndexBuilderHelper {
     public:
        typedef path_extend::scaffold_graph::ScaffoldVertex ScaffoldVertex;

        template <class ContainerT>
        shared_ptr<SimpleScaffoldVertexIndex> ConstructScaffoldVertexIndex(const Graph& g_,
                                                                           const FrameBarcodeIndexInfoExtractor& extractor,
                                                                           size_t tail_threshold,
                                                                           size_t count_threshold,
                                                                           size_t length_threshold,
                                                                           size_t max_threads,
                                                                           const ContainerT& vertex_container) {
            INFO("Building simple long edge barcode index with parameters");
            INFO("Tail threshold: " << tail_threshold);
            INFO("Count threshold: " << count_threshold);
            INFO("Length threshold: " << length_threshold);
            auto entry_extractor = make_shared<SimpleScaffoldVertexEntryExtractor>(g_, extractor, tail_threshold,
                                                                                       count_threshold, length_threshold);
            ScaffoldVertexIndexBuilder<SimpleVertexEntry> builder(g_, entry_extractor, max_threads);
            return builder.GetConstructedIndex(vertex_container);
        }
    };

}