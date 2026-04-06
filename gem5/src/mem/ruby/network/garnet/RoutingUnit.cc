/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "mem/ruby/network/garnet/RoutingUnit.hh"

#include <algorithm>
#include <cmath>

#include "mem/ruby/network/garnet/NetworkLink.hh"
#include "mem/ruby/network/garnet/OutputUnit.hh"

#include "base/cast.hh"
#include "base/compiler.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet/InputUnit.hh"
#include "mem/ruby/network/garnet/Router.hh"
#include "mem/ruby/slicc_interface/Message.hh"

namespace gem5
{

namespace ruby
{

namespace garnet
{

RoutingUnit::RoutingUnit(Router *router)
{
    m_router = router;
    m_routing_table.clear();
    m_weight_table.clear();
}

void
RoutingUnit::addRoute(std::vector<NetDest>& routing_table_entry)
{
    if (routing_table_entry.size() > m_routing_table.size()) {
        m_routing_table.resize(routing_table_entry.size());
    }
    for (int v = 0; v < routing_table_entry.size(); v++) {
        m_routing_table[v].push_back(routing_table_entry[v]);
    }
}

void
RoutingUnit::addWeight(int link_weight)
{
    m_weight_table.push_back(link_weight);
}

bool
RoutingUnit::supportsVnet(int vnet, std::vector<int> sVnets)
{
    // If all vnets are supported, return true
    if (sVnets.size() == 0) {
        return true;
    }

    // Find the vnet in the vector, return true
    if (std::find(sVnets.begin(), sVnets.end(), vnet) != sVnets.end()) {
        return true;
    }

    // Not supported vnet
    return false;
}

/*
 * This is the default routing algorithm in garnet.
 * The routing table is populated during topology creation.
 * Routes can be biased via weight assignments in the topology file.
 * Correct weight assignments are critical to provide deadlock avoidance.
 */
int
RoutingUnit::lookupRoutingTable(int vnet, NetDest msg_destination)
{
    // First find all possible output link candidates
    // For ordered vnet, just choose the first
    // (to make sure different packets don't choose different routes)
    // For unordered vnet, randomly choose any of the links
    // To have a strict ordering between links, they should be given
    // different weights in the topology file

    int output_link = -1;
    int min_weight = INFINITE_;
    std::vector<int> output_link_candidates;
    int num_candidates = 0;

    // Identify the minimum weight among the candidate output links
    for (int link = 0; link < m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(
            m_routing_table[vnet][link])) {

        if (m_weight_table[link] <= min_weight)
            min_weight = m_weight_table[link];
        }
    }

    // Collect all candidate output links with this minimum weight
    for (int link = 0; link < m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(
            m_routing_table[vnet][link])) {

            if (m_weight_table[link] == min_weight) {
                num_candidates++;
                output_link_candidates.push_back(link);
            }
        }
    }

    if (output_link_candidates.size() == 0) {
        fatal("Fatal Error:: No Route exists from this Router.");
        exit(0);
    }

    // Randomly select any candidate output link
    int candidate = 0;
    if (!(m_router->get_net_ptr())->isVNetOrdered(vnet))
        candidate = rand() % num_candidates;

    output_link = output_link_candidates.at(candidate);
    return output_link;
}


void
RoutingUnit::addInDirection(PortDirection inport_dirn, int inport_idx)
{
    m_inports_dirn2idx[inport_dirn] = inport_idx;
    m_inports_idx2dirn[inport_idx]  = inport_dirn;
}

void
RoutingUnit::addOutDirection(PortDirection outport_dirn, int outport_idx)
{
    m_outports_dirn2idx[outport_dirn] = outport_idx;
    m_outports_idx2dirn[outport_idx]  = outport_dirn;
}

// outportCompute() is called by the InputUnit
// It calls the routing table by default.
// A template for adaptive topology-specific routing algorithm
// implementations using port directions rather than a static routing
// table is provided here.

int
RoutingUnit::outportCompute(RouteInfo route, int inport,
                            PortDirection inport_dirn)
{
    int outport = -1;

    if (route.dest_router == m_router->get_id()) {

        // Multiple NIs may be connected to this router,
        // all with output port direction = "Local"
        // Get exact outport id from table
        outport = lookupRoutingTable(route.vnet, route.net_dest);
        return outport;
    }

    // Routing Algorithm set in GarnetNetwork.py
    // Can be over-ridden from command line using --routing-algorithm = 1
    RoutingAlgorithm routing_algorithm =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();

    switch (routing_algorithm) {
        case TABLE_:  outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
        case XY_:     outport =
            outportComputeXY(route, inport, inport_dirn); break;
        // any custom algorithm
        case CUSTOM_: outport =
            outportComputeCustom(route, inport, inport_dirn); break;
        default: outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
    }

    assert(outport != -1);
    return outport;
}

// XY routing implemented using port directions
// Only for reference purpose in a Mesh
// By default Garnet uses the routing table
int
RoutingUnit::outportComputeXY(RouteInfo route,
                              int inport,
                              PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";

    GarnetNetwork* net = m_router->get_net_ptr();
    net->maybeUpdateWearoutState(m_router->curCycle());

    [[maybe_unused]] int num_rows = net->getNumRows();
    int num_cols = net->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = dest_id / num_cols;

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0));

    if (x_hops > 0) {
        if (x_dirn) {
            assert(inport_dirn == "Local" || inport_dirn == "West");
            outport_dirn = "East";
        } else {
            assert(inport_dirn == "Local" || inport_dirn == "East");
            outport_dirn = "West";
        }
    } else if (y_hops > 0) {
        if (y_dirn) {
            // "Local" or "South" or "West" or "East"
            assert(inport_dirn != "North");
            outport_dirn = "North";
        } else {
            // "Local" or "North" or "West" or "East"
            assert(inport_dirn != "South");
            outport_dirn = "South";
        }
    } else {
        // x_hops == 0 and y_hops == 0
        // this is not possible
        // already checked that in outportCompute() function
        panic("x_hops == y_hops == 0");
    }

    return m_outports_dirn2idx[outport_dirn];
}

// Template for implementing custom routing algorithm
// using port directions. (Example adaptive)
int
RoutingUnit::outportComputeCustom(RouteInfo route,
                                 int inport,
                                 PortDirection inport_dirn)
{
    GarnetNetwork* net = m_router->get_net_ptr();
    Cycles cur = m_router->curCycle();

    // ----------------------------------------------------------------
    // Step 1: periodic utilisation / temperature updates
    // ----------------------------------------------------------------
    net->maybeUpdateWearoutState(cur);

    // ----------------------------------------------------------------
    // Step 2: grid coordinates
    // ----------------------------------------------------------------
    int num_cols = net->getNumCols();
    int my_id    = m_router->get_id();
    int my_x     = my_id % num_cols;
    int my_y     = my_id / num_cols;
    int dest_id  = route.dest_router;
    int dest_x   = dest_id % num_cols;
    int dest_y   = dest_id / num_cols;
    int dx = dest_x - my_x;
    int dy = dest_y - my_y;

    // Deterministic dimension-order (x then y) next-hop selection.
    // This keeps fallback routing deadlock-safe without requiring XY's
    // strict inport-direction assertions, which may not hold after
    // earlier adaptive turns.
    auto dimOrderOutport = [&]() -> int {
        if (dx == 0 && dy == 0) {
            return m_outports_dirn2idx.at("Local");
        }
        if (dx != 0) {
            PortDirection dirn = (dx > 0) ? "East" : "West";
            return m_outports_dirn2idx.at(dirn);
        }
        PortDirection dirn = (dy > 0) ? "North" : "South";
        return m_outports_dirn2idx.at(dirn);
    };

    // ----------------------------------------------------------------
    // Step 3: warmup — fall back to deterministic dimension-order
    // ----------------------------------------------------------------
    if (cur < net->getRLWarmup()) {
        return dimOrderOutport();
    }

    // ----------------------------------------------------------------
    // Step 4: forced routing when only one dimension remains
    // ----------------------------------------------------------------
    if (dx == 0 && dy == 0) {
        return dimOrderOutport();
    }
    if (dx == 0) {
        return dimOrderOutport();
    }
    if (dy == 0) {
        return dimOrderOutport();
    }

    // ----------------------------------------------------------------
    // Step 5: escape VC check — VC 0 within its vnet uses deterministic
    // dimension-order routing.
    // ----------------------------------------------------------------
    int vcs_per_vnet = (int)m_router->get_vc_per_vnet();
    int local_vc     = route.vc % vcs_per_vnet;
    if (local_vc == 0) {
        return dimOrderOutport();
    }

    // ----------------------------------------------------------------
    // Step 6: candidate directions and neighbour IDs
    // ----------------------------------------------------------------
    PortDirection h_dirn   = (dx > 0) ? "East"  : "West";
    PortDirection v_dirn   = (dy > 0) ? "North" : "South";
    int h_next_id = my_id + ((dx > 0) ?  1         :  -1);
    int v_next_id = my_id + ((dy > 0) ?  num_cols  : -num_cols);

    int h_outport = m_outports_dirn2idx.at(h_dirn);
    int v_outport = m_outports_dirn2idx.at(v_dirn);

    // ----------------------------------------------------------------
    // Step 7: raw MTTF values for both candidates
    // ----------------------------------------------------------------
    NetworkLink* h_link = m_router->getOutputUnit(h_outport)->get_out_link();
    NetworkLink* v_link = m_router->getOutputUnit(v_outport)->get_out_link();
    int h_rl = net->getLinkRLIndex(h_link);
    int v_rl = net->getLinkRLIndex(v_link);

    double h_em  = net->getRawEMMTTF(h_rl, h_next_id);
    double h_hci = net->getRawHCIMTTF(h_next_id);
    double v_em  = net->getRawEMMTTF(v_rl, v_next_id);
    double v_hci = net->getRawHCIMTTF(v_next_id);

    double em_max   = std::max(h_em,  v_em);
    double hci_max  = std::max(h_hci, v_hci);
    double h_em_n   = (em_max  > 0.0) ? h_em  / em_max  : 1.0;
    double v_em_n   = (em_max  > 0.0) ? v_em  / em_max  : 1.0;
    double h_hci_n  = (hci_max > 0.0) ? h_hci / hci_max : 1.0;
    double v_hci_n  = (hci_max > 0.0) ? v_hci / hci_max : 1.0;

    // ----------------------------------------------------------------
    // Step 8: congestion bins for both outports
    // ----------------------------------------------------------------
    int cong_h = net->getCongBin(my_id, h_outport);
    int cong_v = net->getCongBin(my_id, v_outport);

    // ----------------------------------------------------------------
    // Step 9: encode current inport direction for f8
    // Local=0, West=1, East=2, South=3, North=4; divide by 4
    // ----------------------------------------------------------------
    double inport_enc = 0.0;
    if      (inport_dirn == "West")  inport_enc = 1.0 / 4.0;
    else if (inport_dirn == "East")  inport_enc = 2.0 / 4.0;
    else if (inport_dirn == "South") inport_enc = 3.0 / 4.0;
    else if (inport_dirn == "North") inport_enc = 4.0 / 4.0;

    // ----------------------------------------------------------------
    // Step 10: grid constants for normalization
    // ----------------------------------------------------------------
    int num_routers  = net->getNumRows() * net->getNumCols();
    double id_range  = (double)(num_routers - 1);
    double max_hops  = (double)(net->getNumRows() + net->getNumCols() - 2);

    int rem_hops = std::abs(dx) + std::abs(dy);

    // ----------------------------------------------------------------
    // Step 11: build feature vectors for current state
    // ----------------------------------------------------------------
    auto buildFeatures = [&](double em_n, double hci_n, int cong) {
        std::vector<double> f(GarnetNetwork::LARE_NUM_FEATURES);
        f[0] = 1.0;
        f[1] = (id_range > 0.0) ? (double)dest_id / id_range : 0.0;
        f[2] = (id_range > 0.0) ? (double)my_id   / id_range : 0.0;
        f[3] = (max_hops > 0.0) ? (double)route.hops_traversed / max_hops : 0.0;
        f[4] = (max_hops > 0.0) ? (double)rem_hops             / max_hops : 0.0;
        f[5] = em_n;
        f[6] = hci_n;
        f[7] = (double)cong / 2.0;
        f[8] = inport_enc;
        return f;
    };

    std::vector<double> feat_h = buildFeatures(h_em_n, h_hci_n, cong_h);
    std::vector<double> feat_v = buildFeatures(v_em_n, v_hci_n, cong_v);

    // ----------------------------------------------------------------
    // Step 12: compute Q-values and epsilon-greedy action selection
    // ----------------------------------------------------------------
    double q_h = net->computeQLARE(my_id, h_outport, feat_h);
    double q_v = net->computeQLARE(my_id, v_outport, feat_v);

    double eps = net->getRLEpsilon();
    int action;   // 0 = horizontal, 1 = vertical
    if (eps > 0.0 && net->sampleRandom() < eps) {
        action = net->sampleAction();
    } else {
        action = (q_h >= q_v) ? 0 : 1;
    }

    int           chosen_outport = (action == 0) ? h_outport : v_outport;
    int           chosen_next_id = (action == 0) ? h_next_id : v_next_id;
    double        chosen_em_n    = (action == 0) ? h_em_n    : v_em_n;
    double        chosen_hci_n   = (action == 0) ? h_hci_n   : v_hci_n;
    int           chosen_cong    = (action == 0) ? cong_h    : cong_v;
    double        chosen_q_s     = (action == 0) ? q_h       : q_v;
    PortDirection chosen_dirn    = (action == 0) ? h_dirn    : v_dirn;

    // ----------------------------------------------------------------
    // Step 13: reward
    // ----------------------------------------------------------------
    double c_em  = net->computeCWeight(chosen_em_n,
                       std::max(h_em_n, v_em_n), std::min(h_em_n, v_em_n));
    double c_hci = net->computeCWeight(chosen_hci_n,
                       std::max(h_hci_n, v_hci_n), std::min(h_hci_n, v_hci_n));
    double mttf_score = c_em * chosen_em_n + c_hci * chosen_hci_n;
    double cong_norm  = (double)chosen_cong / 2.0;

    double R = GarnetNetwork::RL_W_WEAR    * mttf_score
             - GarnetNetwork::RL_W_LAT     * cong_norm
             + GarnetNetwork::RL_W_BALANCE * (1.0 - cong_norm);

    // ----------------------------------------------------------------
    // Step 14: LARE coefficient update
    // ----------------------------------------------------------------
    if (eps > 0.0) {
        int  next_id  = chosen_next_id;
        int  nx       = next_id % num_cols;
        int  ny       = next_id / num_cols;
        int  ndx      = dest_x - nx;
        int  ndy      = dest_y - ny;
        bool terminal = (next_id == dest_id);
        bool forced   = !terminal && (ndx == 0 || ndy == 0);

        double q_sp = 0.0;

        if (!terminal && !forced) {
            PortDirection nh_dirn  = (ndx > 0) ? "East"  : "West";
            PortDirection nv_dirn  = (ndy > 0) ? "North" : "South";
            int nh_next_id = next_id + ((ndx > 0) ?  1        :  -1);
            int nv_next_id = next_id + ((ndy > 0) ?  num_cols : -num_cols);

            Router* nr  = net->getRouter(next_id);
            int nh_op   = nr->get_outport_idx(nh_dirn);
            int nv_op   = nr->get_outport_idx(nv_dirn);

            NetworkLink* nh_lnk = nr->getOutputUnit(nh_op)->get_out_link();
            NetworkLink* nv_lnk = nr->getOutputUnit(nv_op)->get_out_link();
            int nh_rl = net->getLinkRLIndex(nh_lnk);
            int nv_rl = net->getLinkRLIndex(nv_lnk);

            double nh_em  = net->getRawEMMTTF(nh_rl, nh_next_id);
            double nh_hci = net->getRawHCIMTTF(nh_next_id);
            double nv_em  = net->getRawEMMTTF(nv_rl, nv_next_id);
            double nv_hci = net->getRawHCIMTTF(nv_next_id);

            double nem_max  = std::max(nh_em,  nv_em);
            double nhci_max = std::max(nh_hci, nv_hci);
            double nh_em_n  = (nem_max  > 0.0) ? nh_em  / nem_max  : 1.0;
            double nv_em_n  = (nem_max  > 0.0) ? nv_em  / nem_max  : 1.0;
            double nh_hci_n = (nhci_max > 0.0) ? nh_hci / nhci_max : 1.0;
            double nv_hci_n = (nhci_max > 0.0) ? nv_hci / nhci_max : 1.0;

            int nc_h = net->getCongBin(next_id, nh_op);
            int nc_v = net->getCongBin(next_id, nv_op);

            int nrem_hops = std::abs(ndx) + std::abs(ndy);

            // Inport direction at next router = opposite of chosen direction
            double sp_inport_enc = 0.0;
            if      (chosen_dirn == "East")  sp_inport_enc = 1.0 / 4.0;  // arrived from West
            else if (chosen_dirn == "West")  sp_inport_enc = 2.0 / 4.0;  // arrived from East
            else if (chosen_dirn == "North") sp_inport_enc = 3.0 / 4.0;  // arrived from South
            else if (chosen_dirn == "South") sp_inport_enc = 4.0 / 4.0;  // arrived from North

            auto buildFeatSp = [&](double en, double hn, int cg) {
                std::vector<double> f(GarnetNetwork::LARE_NUM_FEATURES);
                f[0] = 1.0;
                f[1] = (id_range > 0.0) ? (double)dest_id  / id_range : 0.0;
                f[2] = (id_range > 0.0) ? (double)next_id  / id_range : 0.0;
                f[3] = (max_hops > 0.0)
                       ? (double)(route.hops_traversed + 1) / max_hops : 0.0;
                f[4] = (max_hops > 0.0) ? (double)nrem_hops / max_hops : 0.0;
                f[5] = en;
                f[6] = hn;
                f[7] = (double)cg / 2.0;
                f[8] = sp_inport_enc;
                return f;
            };

            std::vector<double> feat_nh = buildFeatSp(nh_em_n, nh_hci_n, nc_h);
            std::vector<double> feat_nv = buildFeatSp(nv_em_n, nv_hci_n, nc_v);

            double q_nh = net->computeQLARE(next_id, nh_op, feat_nh);
            double q_nv = net->computeQLARE(next_id, nv_op, feat_nv);
            q_sp = std::max(q_nh, q_nv);
        }

        double alpha    = GarnetNetwork::RL_ALPHA;
        double gamma    = GarnetNetwork::RL_GAMMA;
        double td_error = R + gamma * q_sp - chosen_q_s;

        auto& theta = net->getLARETheta()[my_id][chosen_outport];
        const auto& feat_chosen = (action == 0) ? feat_h : feat_v;
        for (int j = 0; j < GarnetNetwork::LARE_NUM_FEATURES; j++) {
            theta[j] += alpha * td_error * feat_chosen[j];
        }
    }

    return chosen_outport;
}

} // namespace garnet
} // namespace ruby
} // namespace gem5
