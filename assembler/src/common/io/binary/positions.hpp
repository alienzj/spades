//***************************************************************************
//* Copyright (c) 2018 Saint Petersburg State University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#pragma once

#include "io/id_mapper.hpp"
#include "io_base.hpp"
#include "assembly_graph/handlers/edges_position_handler.hpp"

namespace io {

inline SaveFile &operator<<(SaveFile &file, const Range &range) {
    return (file << range.start_pos << range.end_pos);
}

template<typename Graph>
class EdgePositionsIO : public IOSingle<typename omnigraph::EdgesPositionHandler<Graph>> {
public:
    typedef omnigraph::EdgesPositionHandler<Graph> Type;
    typedef IdMapper<typename Graph::EdgeId> Mapper;
    EdgePositionsIO(const Mapper &mapper)
            : IOSingle<Type>("edge positions", ".pos"), mapper_(mapper) {
    }

private:
    void SaveImpl(SaveFile &file, const Type &edge_pos) override {
        for (auto it = edge_pos.g().ConstEdgeBegin(); !it.IsEnd(); ++it) {
            auto pos_it = edge_pos.GetEdgePositions(*it);
            file << (*it).int_id() << pos_it.size();
            for (const auto &i : pos_it) {
                file << i.contigId << i.mr.initial_range << i.mr.mapped_range;
            }
        }
    }

    void LoadImpl(LoadFile &file, Type &edge_pos) override {
        VERIFY(!edge_pos.IsAttached());
        edge_pos.Attach();
        size_t e;
        while (file >> e) { //Read until the end
            auto eid = mapper_[e];
            auto info_count = file.Read<size_t>();
            while (info_count--) {
                auto contig = file.Read<std::string>();
                size_t pos[4];
                file >> pos;
                edge_pos.AddEdgePosition(eid, contig, pos[0], pos[1], pos[2], pos[3]);
            }
        }
    };

    const Mapper &mapper_;
};

}
