//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

/*
 * graph_construction.hpp
 *
 *  Created on: Aug 12, 2011
 *      Author: sergey
 */
#pragma once

#include "pipeline/graph_pack.hpp"

#include "io/reads/io_helper.hpp"
#include "assembly_graph/core/graph.hpp"

#include "assembly_graph/construction/debruijn_graph_constructor.hpp"
#include "assembly_graph/construction/early_simplification.hpp"

#include "utils/perf/perfcounter.hpp"
#include "io/dataset_support/read_converter.hpp"

#include "assembly_graph/handlers/edges_position_handler.hpp"
#include "assembly_graph/graph_support/coverage_filling.hpp"
#include "utils/ph_map/storing_traits.hpp"
#include "assembly_graph/index/edge_index_builders.hpp"
#include "utils/parallel/openmp_wrapper.h"
#include "utils/extension_index/kmer_extension_index_builder.hpp"


namespace debruijn_graph {

template<class StoringType>
struct CoverageCollector {
};

template<>
struct CoverageCollector<utils::SimpleStoring> {
    template<class Info>
    static void CollectCoverage(Info edge_info) {
        edge_info.edge_id->IncCoverage(edge_info.count);
    }
};

template<>
struct CoverageCollector<utils::InvertableStoring> {
    template<class Info>
    static void CollectCoverage(Info edge_info) {
        edge_info.edge_id->IncCoverage(edge_info.count);
        edge_info.edge_id->conjugate()->IncCoverage(edge_info.count);
    }
};


template<class Index>
void FillCoverageFromIndex(const Index &index) {
    for (auto I = index.value_cbegin(), E = index.value_cend();
            I != E; ++I) {
        const auto& edge_info = *I;
        VERIFY(edge_info.offset != -1u);
//      VERIFY(edge_info.edge_id.get() != NULL);
        if(edge_info.offset != -1u) {
            CoverageCollector<typename Index::storing_type>::CollectCoverage(edge_info);
        }
    }
    DEBUG("Coverage counted");
}

template<class Graph, class Readers, class Index>
size_t ConstructGraphUsingOldIndex(Readers& streams, Graph& g,
        Index& index, io::SingleStreamPtr contigs_stream = io::SingleStreamPtr()) {
    INFO("Constructing DeBruijn graph");

    TRACE("Filling indices");
    size_t rl = 0;
    VERIFY_MSG(streams.size(), "No input streams specified");

    TRACE("... in parallel");
    typedef typename Index::InnerIndexT InnerIndex;
    typedef typename EdgeIndexHelper<InnerIndex>::CoverageFillingEdgeIndexBuilderT IndexBuilder;
    InnerIndex& debruijn = index.inner_index();
    //fixme hack
    rl = IndexBuilder().BuildIndexFromStream(debruijn, streams, (contigs_stream == 0) ? 0 : &(*contigs_stream));

    VERIFY(g.k() + 1== debruijn.k());
    // FIXME: output_dir here is damn ugly!

    TRACE("Filled indices");

    INFO("Condensing graph");
    DeBruijnGraphConstructor<Graph, InnerIndex> g_c(g, debruijn);
    TRACE("Constructor ok");
    VERIFY(!index.IsAttached());
    index.Attach();
    g_c.ConstructGraph(100, 10000, 1.2); // TODO: move magic constants to config
    INFO("Graph condensed");

    return rl;
}

template<class ExtensionIndex>
void EarlyClipTips(size_t k, const config::debruijn_config::construction& params, size_t rl, ExtensionIndex& ext) {
    if (params.early_tc.enable) {
        size_t length_bound = rl - k;
        if (params.early_tc.length_bound)
            length_bound = params.early_tc.length_bound.get();
        AlternativeEarlyTipClipper(ext, length_bound).ClipTips();
    }
}

template<class Graph, class Read, class Index>
void ConstructGraphUsingExtentionIndex(const config::debruijn_config::construction params,
                                       io::ReadStreamList<Read>& streams, Graph& g,
                                       Index& index, io::SingleStreamPtr contigs_stream = io::SingleStreamPtr()) {
    size_t k = g.k();
    INFO("Constructing DeBruijn graph for k=" << k);

    TRACE("Filling indices");
    VERIFY_MSG(streams.size(), "No input streams specified");

    TRACE("... in parallel");
    // FIXME: output_dir here is damn ugly!
    typedef utils::DeBruijnExtensionIndex<> ExtensionIndex;
    typedef typename utils::ExtensionIndexHelper<ExtensionIndex>::DeBruijnExtensionIndexBuilderT ExtensionIndexBuilder;
    ExtensionIndex ext((unsigned) k, index.inner_index().workdir());

    ExtensionIndexBuilder().BuildExtensionIndexFromStream(ext, streams, (contigs_stream == 0) ? 0 : &(*contigs_stream), params.read_buffer_size);

    //FIXME get rid of cfg::get
    VERIFY(cfg::get().ds.RL() > 0);
    EarlyClipTips(k, params, cfg::get().ds.RL(), ext);

    INFO("Condensing graph");
    VERIFY(!index.IsAttached());
    DeBruijnGraphExtentionConstructor<Graph> g_c(g, ext);
    g_c.ConstructGraph(params.keep_perfect_loops);

    INFO("Building index with from graph")
    //todo pass buffer size
    index.Refill();
    index.Attach();
}

template<class Graph, class Index, class Streams>
void ConstructGraph(const config::debruijn_config::construction &params,
                              Streams& streams, Graph& g,
                              Index& index, io::SingleStreamPtr contigs_stream = io::SingleStreamPtr()) {
    VERIFY(params.con_mode == config::construction_mode::extention);
    ConstructGraphUsingExtentionIndex(params, streams, g, index, contigs_stream);
//  ConstructGraphUsingOldIndex(k, streams, g, index, contigs_stream);
}

template<class Graph, class Index, class Streams>
void ConstructGraphWithCoverage(const config::debruijn_config::construction &params,
                                  Streams& streams, Graph& g,
                                  Index& index, FlankingCoverage<Graph>& flanking_cov,
                                  io::SingleStreamPtr contigs_stream = io::SingleStreamPtr()) {
    ConstructGraph(params, streams, g, index, contigs_stream);

    typedef typename Index::InnerIndex InnerIndex;
    typedef typename EdgeIndexHelper<InnerIndex>::CoverageAndGraphPositionFillingIndexBuilderT IndexBuilder;
    INFO("Filling coverage index")
    IndexBuilder().ParallelFillCoverage(index.inner_index(), streams);
    INFO("Filling coverage and flanking coverage from index");
    FillCoverageAndFlanking(index.inner_index(), g, flanking_cov);
}

}
