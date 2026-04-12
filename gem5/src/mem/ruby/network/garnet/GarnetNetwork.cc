/*
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
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


#include "mem/ruby/network/garnet/GarnetNetwork.hh"

#include <cassert>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <random>

#include "base/cast.hh"
#include "base/compiler.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/common/NetDest.hh"
#include "mem/ruby/network/MessageBuffer.hh"
#include "mem/ruby/network/garnet/CommonTypes.hh"
#include "mem/ruby/network/garnet/CreditLink.hh"
#include "mem/ruby/network/garnet/GarnetLink.hh"
#include "mem/ruby/network/garnet/NetworkInterface.hh"
#include "mem/ruby/network/garnet/NetworkLink.hh"
#include "mem/ruby/network/garnet/OutputUnit.hh"
#include "mem/ruby/network/garnet/Router.hh"
#include "mem/ruby/system/RubySystem.hh"
#include "sim/core.hh"

namespace gem5
{

namespace ruby
{

namespace garnet
{

/*
 * GarnetNetwork sets up the routers and links and collects stats.
 * Default parameters (GarnetNetwork.py) can be overwritten from command line
 * (see configs/network/Network.py)
 */

GarnetNetwork::GarnetNetwork(const Params &p)
    : Network(p)
{
    m_num_rows = p.num_rows;
    m_ni_flit_size = p.ni_flit_size;
    m_max_vcs_per_vnet = 0;
    m_buffers_per_data_vc = p.buffers_per_data_vc;
    m_buffers_per_ctrl_vc = p.buffers_per_ctrl_vc;
    m_routing_algorithm = p.routing_algorithm;
    m_next_packet_id = 0;

    // RL initialisation from params
    m_rl_epsilon       = p.rl_epsilon;
    m_rl_warmup_cycles = Cycles(p.rl_warmup_cycles);
    m_lare_theta_file  = p.lare_theta_file;
    m_mttf_output_file = p.mttf_output_file;
    m_last_util_update = Cycles(0);
    m_last_temp_update = Cycles(0);

    // Seeded RNG for runtime epsilon-greedy sampling (separate from PV rng(42))
    m_rng        = std::mt19937(12345);
    m_real_dist  = std::uniform_real_distribution<double>(0.0, 1.0);
    m_action_dist = std::uniform_int_distribution<int>(0, 1);

    m_enable_fault_model = p.enable_fault_model;
    if (m_enable_fault_model)
        fault_model = p.fault_model;

    m_vnet_type.resize(m_virtual_networks);

    for (int i = 0 ; i < m_virtual_networks ; i++) {
        if (m_vnet_type_names[i] == "response")
            m_vnet_type[i] = DATA_VNET_; // carries data (and ctrl) packets
        else
            m_vnet_type[i] = CTRL_VNET_; // carries only ctrl packets
    }

    // record the routers
    for (std::vector<BasicRouter*>::const_iterator i =  p.routers.begin();
         i != p.routers.end(); ++i) {
        Router* router = safe_cast<Router*>(*i);
        m_routers.push_back(router);

        // initialize the router's network pointers
        router->init_net_ptr(this);
    }

    // record the network interfaces
    for (std::vector<ClockedObject*>::const_iterator i = p.netifs.begin();
         i != p.netifs.end(); ++i) {
        NetworkInterface *ni = safe_cast<NetworkInterface *>(*i);
        m_nis.push_back(ni);
        ni->init_net_ptr(this);
    }

    // Print Garnet version
    inform("Garnet version %s\n", garnetVersion);
}

GarnetNetwork::~GarnetNetwork()
{
    saveLARETheta();
    writeMTTFReport();
}

void
GarnetNetwork::init()
{
    Network::init();

    for (int i=0; i < m_nodes; i++) {
        m_nis[i]->addNode(m_toNetQueues[i], m_fromNetQueues[i]);
    }

    // The topology pointer should have already been initialized in the
    // parent network constructor
    assert(m_topology_ptr != NULL);
    m_topology_ptr->createLinks(this);

    // Initialize topology specific parameters
    if (getNumRows() > 0) {
        // Only for Mesh topology
        // m_num_rows and m_num_cols are only used for
        // implementing XY or custom routing in RoutingUnit.cc
        m_num_rows = getNumRows();
        m_num_cols = m_routers.size() / m_num_rows;
        assert(m_num_rows * m_num_cols == m_routers.size());
    } else {
        m_num_rows = -1;
        m_num_cols = -1;
    }

    // FaultModel: declare each router to the fault model
    if (isFaultModelEnabled()) {
        for (std::vector<Router*>::const_iterator i= m_routers.begin();
             i != m_routers.end(); ++i) {
            Router* router = safe_cast<Router*>(*i);
            [[maybe_unused]] int router_id =
                fault_model->declare_router(router->get_num_inports(),
                                            router->get_num_outports(),
                                            router->get_vc_per_vnet(),
                                            getBuffersPerDataVC(),
                                            getBuffersPerCtrlVC());
            assert(router_id == router->get_id());
            router->printAggregateFaultProbability(std::cout);
            router->printFaultVector(std::cout);
        }
    }

    // =====================================================================
    // Wear-Aware Infrastructure Initialisation
    // Initialised unconditionally whenever m_num_rows > 0 so that DOR
    // baseline runs and RL runs both produce identical per-router I_sub
    // values (fixed seed 42) — required for clean AF ratio cancellation.
    // (m_num_rows and m_num_cols are finalised above; createLinks has run)
    // =====================================================================
    if (m_num_rows > 0) {
        int N = (int)m_routers.size();
        assert(N > 0);

        // --- Resize runtime vectors ---
        m_router_pv.resize(N);
        m_router_temp_K.resize(N, TEMP_MEAN_K);
        m_router_util.assign(N, 0.0);
        m_router_util_prev.assign(N, 0.0);
        int NL = (int)m_int_links.size();
        m_link_util.assign(NL, 0.0);
        m_link_util_prev.assign(NL, 0.0);

        // --- Die-normalised router positions ---
        // Die spans [0,1]×[0,1].  Router (cx,ry) maps to
        // (cx/(nc-1), ry/(nr-1)).  Euclidean distance used directly as
        // "fraction of die length" (die_length = 1).
        std::vector<double> pos_x(N), pos_y(N);
        for (int i = 0; i < N; i++) {
            int cx = i % m_num_cols;
            int ry = i / m_num_cols;
            pos_x[i] = (m_num_cols > 1)
                        ? (double)cx / (m_num_cols - 1) : 0.0;
            pos_y[i] = (m_num_rows > 1)
                        ? (double)ry / (m_num_rows - 1) : 0.0;
        }

        // --- Spherical correlation matrix C[N×N] ---
        // C[i,j] = 1 - 1.5*(r/phi) + 0.5*(r/phi)^3  for r <= phi = 0.5
        //        = 0                                   otherwise
        std::vector<double> C(N * N, 0.0);
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                double dx = pos_x[i] - pos_x[j];
                double dy = pos_y[i] - pos_y[j];
                double r  = std::sqrt(dx*dx + dy*dy);
                if (r < PV_PHI) {
                    double t = r / PV_PHI;
                    C[i*N+j] = 1.0 - 1.5*t + 0.5*t*t*t;
                }
                // else C[i,j] = 0 (already initialised)
            }
        }

        // --- Cholesky L such that L L^T = C ---
        // L stored row-major in m_chol_L (size N×N).
        // Regularisation (+1e-12 on diagonal) prevents negative radicands.
        m_chol_L.assign(N * N, 0.0);
        for (int j = 0; j < N; j++) {
            double s = C[j*N+j];
            for (int k = 0; k < j; k++)
                s -= m_chol_L[j*N+k] * m_chol_L[j*N+k];
            m_chol_L[j*N+j] = std::sqrt(std::max(s, 0.0) + 1e-12);
            for (int i = j+1; i < N; i++) {
                double t = C[i*N+j];
                for (int k = 0; k < j; k++)
                    t -= m_chol_L[i*N+k] * m_chol_L[j*N+k];
                m_chol_L[i*N+j] = t / m_chol_L[j*N+j];
            }
        }

        // --- Generate PV: random + spatially-correlated systematic ---
        std::mt19937 rng(42); // fixed seed → reproducible across runs
        std::normal_distribution<double> nd(0.0, 1.0);

        // Independent random components
        std::vector<double> z_vth_r(N), z_leff_r(N);
        for (int i = 0; i < N; i++) {
            z_vth_r[i]  = nd(rng);
            z_leff_r[i] = nd(rng);
        }

        // Correlated systematic components: v = L * z
        std::vector<double> z_vth_s(N), z_leff_s(N);
        for (int i = 0; i < N; i++) {
            z_vth_s[i]  = nd(rng);
            z_leff_s[i] = nd(rng);
        }
        std::vector<double> vth_sys(N, 0.0), leff_sys(N, 0.0);
        for (int i = 0; i < N; i++) {
            for (int j = 0; j <= i; j++) {
                vth_sys[i]  += m_chol_L[i*N+j] * z_vth_s[j];
                leff_sys[i] += m_chol_L[i*N+j] * z_leff_s[j];
            }
        }

        // Absolute standard deviations
        const double sv_r = PV_SIGMA_VTH_RAND  * ISUB_VTH0;
        const double sv_s = PV_SIGMA_VTH_SYS   * ISUB_VTH0;
        const double sl_r = PV_SIGMA_LEFF_RAND * ISUB_LEFF0;
        const double sl_s = PV_SIGMA_LEFF_SYS  * ISUB_LEFF0;

        for (int i = 0; i < N; i++) {
            double dv = z_vth_r[i]  * sv_r + vth_sys[i]  * sv_s;
            double dl = z_leff_r[i] * sl_r + leff_sys[i] * sl_s;
            m_router_pv[i].delta_vth  = dv;
            m_router_pv[i].delta_leff = dl;

            // Clamp to physically valid range
            double vth  = std::max(0.01,
                          std::min(ISUB_VGS * 0.99, ISUB_VTH0  + dv));
            double leff = std::max(1e-10,
                          std::min(100e-9,           ISUB_LEFF0 + dl));

            // Precompute I_sub (BTBT model, q cancels in exponent)
            //   Vdsat = (Vgs-Vth)*Leff*Ecr / ((Vgs-Vth) + Leff*Ecr)
            //   Em    = (Vds - Vdsat) / sqrt(3*tox*xj)
            //   Isub  = C2 * Ids * exp(-phi_i_eV / (lambda_e * Em))
            // NOTE: Do NOT clamp Em to a physical value — that distorts the
            // model.  Instead apply a numerical floor only to prevent
            // divide-by-zero, then clamp the exponent to prevent underflow.
            double Vdsat = ((ISUB_VGS - vth) * leff * ISUB_ECR)
                         / ((ISUB_VGS - vth) + leff * ISUB_ECR);
            double Em = (ISUB_VDS - Vdsat)
                      / std::sqrt(3.0 * ISUB_TOX * ISUB_XJ);
            Em = std::max(Em, 1e-10);  // numerical floor only (not physical clamp)
            double exponent = -ISUB_PHI_I_EV / (ISUB_LAMBDA_E * Em);
            if (exponent < -700.0) exponent = -700.0;  // prevent exp underflow
            double isub = ISUB_C2 * ISUB_IDS * std::exp(exponent);
            m_router_pv[i].isub = std::max(1e-30, isub);
        }

        // --- Initial temperature map (spatially correlated) ---
        {
            std::vector<double> z_t(N);
            for (int i = 0; i < N; i++) z_t[i] = nd(rng);
            for (int i = 0; i < N; i++) {
                double corr = 0.0;
                for (int j = 0; j <= i; j++)
                    corr += m_chol_L[i*N+j] * z_t[j];
                m_router_temp_K[i] = std::max(TEMP_MIN_K,
                    std::min(TEMP_MAX_K, TEMP_MEAN_K + TEMP_STD_K * corr));
            }
        }

        // --- Allocate and (optionally) load LARE theta (RL runs only) ---
        if (m_routing_algorithm == (int)CUSTOM_) {
            int num_routers = (int)m_routers.size();
            // Size second dimension to max outports across all routers
            // (up to 5 in an 8×8 mesh: Local + E + W + N + S)
            int max_outports = 0;
            for (auto r : m_routers)
                max_outports = std::max(max_outports, r->get_num_outports());
            m_lare_theta.assign(num_routers,
                std::vector<std::vector<double>>(max_outports,
                    std::vector<double>(LARE_NUM_FEATURES, 1.0)));
            loadLARETheta();
        }
    }

    // Register exit callback to flush Q-table and MTTF report.
    // gem5 exits via Python (not C++ stack unwind), so the destructor is
    // unreliable; registerExitCallback guarantees these run at sim end.
    registerExitCallback([this]() {
        saveLARETheta();
        writeMTTFReport();
    });
}

/*
 * This function creates a link from the Network Interface (NI)
 * into the Network.
 * It creates a Network Link from the NI to a Router and a Credit Link from
 * the Router to the NI
*/

void
GarnetNetwork::makeExtInLink(NodeID global_src, SwitchID dest, BasicLink* link,
                             std::vector<NetDest>& routing_table_entry)
{
    NodeID local_src = getLocalNodeID(global_src);
    assert(local_src < m_nodes);

    GarnetExtLink* garnet_link = safe_cast<GarnetExtLink*>(link);

    // GarnetExtLink is bi-directional
    NetworkLink* net_link = garnet_link->m_network_links[LinkDirection_In];
    net_link->setType(EXT_IN_);
    CreditLink* credit_link = garnet_link->m_credit_links[LinkDirection_In];

    m_networklinks.push_back(net_link);
    m_creditlinks.push_back(credit_link);

    PortDirection dst_inport_dirn = "Local";

    m_max_vcs_per_vnet = std::max(m_max_vcs_per_vnet,
                             m_routers[dest]->get_vc_per_vnet());

    /*
     * We check if a bridge was enabled at any end of the link.
     * The bridge is enabled if either of clock domain
     * crossing (CDC) or Serializer-Deserializer(SerDes) unit is
     * enabled for the link at each end. The bridge encapsulates
     * the functionality for both CDC and SerDes and is a Consumer
     * object similiar to a NetworkLink.
     *
     * If a bridge was enabled we connect the NI and Routers to
     * bridge before connecting the link. Example, if an external
     * bridge is enabled, we would connect:
     * NI--->NetworkBridge--->GarnetExtLink---->Router
     */
    if (garnet_link->extBridgeEn) {
        DPRINTF(RubyNetwork, "Enable external bridge for %s\n",
            garnet_link->name());
        NetworkBridge *n_bridge = garnet_link->extNetBridge[LinkDirection_In];
        m_nis[local_src]->
        addOutPort(n_bridge,
                   garnet_link->extCredBridge[LinkDirection_In],
                   dest, m_routers[dest]->get_vc_per_vnet());
        m_networkbridges.push_back(n_bridge);
    } else {
        m_nis[local_src]->addOutPort(net_link, credit_link, dest,
            m_routers[dest]->get_vc_per_vnet());
    }

    if (garnet_link->intBridgeEn) {
        DPRINTF(RubyNetwork, "Enable internal bridge for %s\n",
            garnet_link->name());
        NetworkBridge *n_bridge = garnet_link->intNetBridge[LinkDirection_In];
        m_routers[dest]->
            addInPort(dst_inport_dirn,
                      n_bridge,
                      garnet_link->intCredBridge[LinkDirection_In]);
        m_networkbridges.push_back(n_bridge);
    } else {
        m_routers[dest]->addInPort(dst_inport_dirn, net_link, credit_link);
    }

}

/*
 * This function creates a link from the Network to a NI.
 * It creates a Network Link from a Router to the NI and
 * a Credit Link from NI to the Router
*/

void
GarnetNetwork::makeExtOutLink(SwitchID src, NodeID global_dest,
                              BasicLink* link,
                              std::vector<NetDest>& routing_table_entry)
{
    NodeID local_dest = getLocalNodeID(global_dest);
    assert(local_dest < m_nodes);
    assert(src < m_routers.size());
    assert(m_routers[src] != NULL);

    GarnetExtLink* garnet_link = safe_cast<GarnetExtLink*>(link);

    // GarnetExtLink is bi-directional
    NetworkLink* net_link = garnet_link->m_network_links[LinkDirection_Out];
    net_link->setType(EXT_OUT_);
    CreditLink* credit_link = garnet_link->m_credit_links[LinkDirection_Out];

    m_networklinks.push_back(net_link);
    m_creditlinks.push_back(credit_link);

    PortDirection src_outport_dirn = "Local";

    m_max_vcs_per_vnet = std::max(m_max_vcs_per_vnet,
                             m_routers[src]->get_vc_per_vnet());

    /*
     * We check if a bridge was enabled at any end of the link.
     * The bridge is enabled if either of clock domain
     * crossing (CDC) or Serializer-Deserializer(SerDes) unit is
     * enabled for the link at each end. The bridge encapsulates
     * the functionality for both CDC and SerDes and is a Consumer
     * object similiar to a NetworkLink.
     *
     * If a bridge was enabled we connect the NI and Routers to
     * bridge before connecting the link. Example, if an external
     * bridge is enabled, we would connect:
     * NI<---NetworkBridge<---GarnetExtLink<----Router
     */
    if (garnet_link->extBridgeEn) {
        DPRINTF(RubyNetwork, "Enable external bridge for %s\n",
            garnet_link->name());
        NetworkBridge *n_bridge = garnet_link->extNetBridge[LinkDirection_Out];
        m_nis[local_dest]->
            addInPort(n_bridge, garnet_link->extCredBridge[LinkDirection_Out]);
        m_networkbridges.push_back(n_bridge);
    } else {
        m_nis[local_dest]->addInPort(net_link, credit_link);
    }

    if (garnet_link->intBridgeEn) {
        DPRINTF(RubyNetwork, "Enable internal bridge for %s\n",
            garnet_link->name());
        NetworkBridge *n_bridge = garnet_link->intNetBridge[LinkDirection_Out];
        m_routers[src]->
            addOutPort(src_outport_dirn,
                       n_bridge,
                       routing_table_entry, link->m_weight,
                       garnet_link->intCredBridge[LinkDirection_Out],
                       m_routers[src]->get_vc_per_vnet());
        m_networkbridges.push_back(n_bridge);
    } else {
        m_routers[src]->
            addOutPort(src_outport_dirn, net_link,
                       routing_table_entry,
                       link->m_weight, credit_link,
                       m_routers[src]->get_vc_per_vnet());
    }
}

/*
 * This function creates an internal network link between two routers.
 * It adds both the network link and an opposite credit link.
*/

void
GarnetNetwork::makeInternalLink(SwitchID src, SwitchID dest, BasicLink* link,
                                std::vector<NetDest>& routing_table_entry,
                                PortDirection src_outport_dirn,
                                PortDirection dst_inport_dirn)
{
    GarnetIntLink* garnet_link = safe_cast<GarnetIntLink*>(link);

    // GarnetIntLink is unidirectional
    NetworkLink* net_link = garnet_link->m_network_link;
    net_link->setType(INT_);
    CreditLink* credit_link = garnet_link->m_credit_link;

    m_networklinks.push_back(net_link);
    m_creditlinks.push_back(credit_link);

    // Track INT_ links for RL wear model
    m_int_link_src_router.push_back((int)src);
    m_int_link_dst_router.push_back((int)dest);
    m_link_ptr_to_rl_idx[net_link] = (int)m_int_links.size();
    m_int_links.push_back(net_link);

    m_max_vcs_per_vnet = std::max(m_max_vcs_per_vnet,
                             std::max(m_routers[dest]->get_vc_per_vnet(),
                             m_routers[src]->get_vc_per_vnet()));

    /*
     * We check if a bridge was enabled at any end of the link.
     * The bridge is enabled if either of clock domain
     * crossing (CDC) or Serializer-Deserializer(SerDes) unit is
     * enabled for the link at each end. The bridge encapsulates
     * the functionality for both CDC and SerDes and is a Consumer
     * object similiar to a NetworkLink.
     *
     * If a bridge was enabled we connect the NI and Routers to
     * bridge before connecting the link. Example, if a source
     * bridge is enabled, we would connect:
     * Router--->NetworkBridge--->GarnetIntLink---->Router
     */
    if (garnet_link->dstBridgeEn) {
        DPRINTF(RubyNetwork, "Enable destination bridge for %s\n",
            garnet_link->name());
        NetworkBridge *n_bridge = garnet_link->dstNetBridge;
        m_routers[dest]->addInPort(dst_inport_dirn, n_bridge,
                                   garnet_link->dstCredBridge);
        m_networkbridges.push_back(n_bridge);
    } else {
        m_routers[dest]->addInPort(dst_inport_dirn, net_link, credit_link);
    }

    if (garnet_link->srcBridgeEn) {
        DPRINTF(RubyNetwork, "Enable source bridge for %s\n",
            garnet_link->name());
        NetworkBridge *n_bridge = garnet_link->srcNetBridge;
        m_routers[src]->
            addOutPort(src_outport_dirn, n_bridge,
                       routing_table_entry,
                       link->m_weight, garnet_link->srcCredBridge,
                       m_routers[dest]->get_vc_per_vnet());
        m_networkbridges.push_back(n_bridge);
    } else {
        m_routers[src]->addOutPort(src_outport_dirn, net_link,
                        routing_table_entry,
                        link->m_weight, credit_link,
                        m_routers[dest]->get_vc_per_vnet());
    }
}

// Total routers in the network
int
GarnetNetwork::getNumRouters()
{
    return m_routers.size();
}

int
GarnetNetwork::getLinkRLIndex(NetworkLink* lnk) const
{
    auto it = m_link_ptr_to_rl_idx.find(lnk);
    return (it != m_link_ptr_to_rl_idx.end()) ? it->second : -1;
}

void
GarnetNetwork::maybeUpdateWearoutState(Cycles cur)
{
    // ---- Utilisation snapshot every 1000 cycles ----
    if (cur - m_last_util_update >= Cycles(1000)) {
        // INT_ link windowed utilisation
        for (int i = 0; i < (int)m_int_links.size(); i++) {
            double now = (double)m_int_links[i]->getLinkUtilization();
            m_link_util[i]      = (now - m_link_util_prev[i]) / 1000.0;
            m_link_util_prev[i] = now;
        }
        // Router crossbar windowed utilisation
        for (int i = 0; i < (int)m_routers.size(); i++) {
            double now = m_routers[i]->getCrossbarActivity();
            m_router_util[i]      = (now - m_router_util_prev[i]) / 1000.0;
            m_router_util_prev[i] = now;
        }
        m_last_util_update = cur;
    }

    // ---- Temperature map regeneration every 10^7 cycles ----
    if (cur - m_last_temp_update >= Cycles(10000000)) {
        int N = (int)m_routers.size();
        // Seed with current cycle so each update gives different values
        std::mt19937 rng_t((uint64_t)cur ^ 0xdeadbeefULL);
        std::normal_distribution<double> nd(0.0, 1.0);
        std::vector<double> z(N);
        for (int i = 0; i < N; i++) z[i] = nd(rng_t);

        for (int i = 0; i < N; i++) {
            double corr = 0.0;
            for (int j = 0; j <= i; j++)
                corr += m_chol_L[i*N+j] * z[j];
            m_router_temp_K[i] = std::max(TEMP_MIN_K,
                std::min(TEMP_MAX_K, TEMP_MEAN_K + TEMP_STD_K * corr));
        }
        m_last_temp_update = cur;
    }
}

// EM MTTF proportional factor for an INT_ link.
// rl_link_idx: index into m_int_links / m_link_util
// dst_router_id: temperature is taken from the destination router
double
GarnetNetwork::getRawEMMTTF(int rl_link_idx, int dst_router_id) const
{
    double U = (rl_link_idx >= 0 && rl_link_idx < (int)m_link_util.size())
               ? m_link_util[rl_link_idx] : 0.0;
    double alpha = std::max(0.5 * U, RL_ALPHA_MIN);   // α_link = 0.5 * U
    double T     = m_router_temp_K[dst_router_id];
    return std::pow(alpha, -2.0) * std::exp(EA_EM / (KB * T));
}

// HCI MTTF proportional factor for a router (includes PV via I_sub).
double
GarnetNetwork::getRawHCIMTTF(int router_id) const
{
    double U     = m_router_util[router_id];
    double alpha = std::max(0.1 * U, RL_ALPHA_MIN);   // α_router = 0.1 * U
    double T     = m_router_temp_K[router_id];
    double isub  = m_router_pv[router_id].isub;
    return std::pow(isub, -HCI_N) / alpha
           * std::exp(EA_HCI / (KB * T));
}

// Congestion bin for a specific output port on a given router.
// Sums active VCs across ALL vnets for a full picture of port pressure.
// 0=LOW (<0.33), 1=MED (<0.67), 2=HIGH (>=0.67)
int
GarnetNetwork::getCongBin(int router_id, int outport) const
{
    Router* r          = m_routers[router_id];
    int     num_vnets  = r->get_num_vnets();
    int     vcs_per_vnet = r->get_vc_per_vnet();
    int     total_vcs  = num_vnets * vcs_per_vnet;
    if (total_vcs == 0) return 0;

    int active = 0;
    for (int vnet = 0; vnet < num_vnets; vnet++) {
        active += r->get_num_active_vcs(outport, vnet);
    }

    double ratio = (double)active / (double)total_vcs;
    if (ratio < 0.33) return 0;  // LOW
    if (ratio < 0.67) return 1;  // MED
    return 2;                     // HIGH
}

// Map a normalised MTTF value to a 3-bin wear level.
// GOOD=0 (>=0.67), FAIR=1 (>=0.33), POOR=2 (<0.33)
int
GarnetNetwork::getWearBin(double mttf_norm) const
{
    if (mttf_norm >= 0.67) return 0;
    if (mttf_norm >= 0.33) return 1;
    return 2;
}

// Clotho exponential weighting C(x) with w=8.
// x_max and x_min are the max/min normalised values for the pair.
// Returns value in [0,1]; if both equal, returns 1.0.
double
GarnetNetwork::computeCWeight(double x,
                               double x_max, double x_min) const
{
    if (x_max <= x_min) return 1.0;
    double w   = RL_CLO_W;
    double num = std::exp(w * (x - x_max) / (x_max - x_min))
               - std::exp(-w);
    double den = 1.0 - std::exp(-w);
    return num / den;
}

void
GarnetNetwork::saveLARETheta() const
{
    if (m_lare_theta_file.empty() || m_lare_theta.empty()) return;
    std::ofstream f(m_lare_theta_file, std::ios::binary);
    if (!f) {
        warn("GarnetNetwork: cannot open '%s' for LARE theta write\n",
             m_lare_theta_file.c_str());
        return;
    }
    // Header: epsilon for reference
    f.write(reinterpret_cast<const char*>(&m_rl_epsilon), sizeof(double));
    // Body: [num_routers][max_outports][LARE_NUM_FEATURES] doubles
    for (const auto& router_theta : m_lare_theta)
        for (const auto& outport_theta : router_theta)
            f.write(reinterpret_cast<const char*>(outport_theta.data()),
                    sizeof(double) * LARE_NUM_FEATURES);
    inform("GarnetNetwork: LARE theta saved to '%s'\n",
           m_lare_theta_file.c_str());
}

void
GarnetNetwork::loadLARETheta()
{
    if (m_lare_theta_file.empty()) return;
    std::ifstream f(m_lare_theta_file, std::ios::binary);
    if (!f) {
        inform("GarnetNetwork: LARE theta file '%s' not found, starting fresh\n",
               m_lare_theta_file.c_str());
        return;
    }
    double stored_eps;
    f.read(reinterpret_cast<char*>(&stored_eps), sizeof(double));
    for (auto& router_theta : m_lare_theta)
        for (auto& outport_theta : router_theta)
            f.read(reinterpret_cast<char*>(outport_theta.data()),
                   sizeof(double) * LARE_NUM_FEATURES);
    inform("GarnetNetwork: LARE theta loaded from '%s' "
           "(epsilon=%.3f from param)\n",
           m_lare_theta_file.c_str(), m_rl_epsilon);
}

double
GarnetNetwork::computeQLARE(int router_id, int outport_idx,
                             const std::vector<double>& features) const
{
    assert(router_id   < (int)m_lare_theta.size());
    assert(outport_idx < (int)m_lare_theta[router_id].size());
    const auto& theta = m_lare_theta[router_id][outport_idx];
    double q = 0.0;
    for (int j = 0; j < LARE_NUM_FEATURES; j++)
        q += theta[j] * features[j];
    return q;
}

// Writes per-component proportional MTTF values to the configured output file.
// Post-processing computes AF by comparing two runs (RL vs DOR).
// At simulation end m_link_util / m_router_util hold the most-recently
// snapshotted windowed values; these proxy the final utilisation rate.
void
GarnetNetwork::writeMTTFReport() const
{
    if (m_mttf_output_file.empty()) return;  // output disabled by user
    if (m_router_temp_K.empty())    return;  // mesh not initialized

    std::ofstream f(m_mttf_output_file);
    if (!f) {
        warn("GarnetNetwork: cannot write MTTF report to '%s'\n",
             m_mttf_output_file.c_str());
        return;
    }

    f << "# router_id  hci_mttf_proportional\n";
    for (int i = 0; i < (int)m_routers.size(); i++) {
        f << "router " << i << " " << getRawHCIMTTF(i) << "\n";
    }

    f << "# rl_link_idx  src_router  dst_router  em_mttf_proportional\n";
    for (int i = 0; i < (int)m_int_links.size(); i++) {
        int src = m_int_link_src_router[i];
        int dst = m_int_link_dst_router[i];
        f << "link " << i << " " << src << " " << dst << " "
          << getRawEMMTTF(i, dst) << "\n";
    }
}

double
GarnetNetwork::computeClothoWnorm(
    const std::vector<int>& path_router_ids,
    const std::vector<int>& path_link_rl_indices,
    double em_global_max, double em_global_min,
    double hci_global_max, double hci_global_min) const
{
    double wnorm = 0.0;
    int n = (int)path_router_ids.size();
    double em_norm_max  = 1.0;
    double em_norm_min  = (em_global_max > 0.0)
                          ? em_global_min / em_global_max : 0.0;
    double hci_norm_max = 1.0;
    double hci_norm_min = (hci_global_max > 0.0)
                          ? hci_global_min / hci_global_max : 0.0;

    for (int i = 0; i < n; ++i) {
        int rl  = path_link_rl_indices[i];
        int nxt = path_router_ids[i];

        double raw_em = getRawEMMTTF(rl, nxt);
        double em_n   = (em_global_max > 0.0) ? raw_em / em_global_max : 0.0;
        double c_em   = computeCWeight(em_n, em_norm_max, em_norm_min);

        double raw_hci = getRawHCIMTTF(nxt);
        double hci_n   = (hci_global_max > 0.0) ? raw_hci / hci_global_max : 0.0;
        double c_hci   = computeCWeight(hci_n, hci_norm_max, hci_norm_min);

        wnorm += c_em * em_n + c_hci * hci_n;
    }
    return wnorm;
}

int
GarnetNetwork::outportClothoGAR(int src_id, int dest_id,
                                 int num_cols, int num_rows) const
{
    int dx = (dest_id % num_cols) - (src_id % num_cols);
    int dy = (dest_id / num_cols) - (src_id / num_cols);
    int steps_h = std::abs(dx);
    int steps_v = std::abs(dy);
    int total   = steps_h + steps_v;

    std::string h_dirn = (dx > 0) ? "East"  : "West";
    std::string v_dirn = (dy > 0) ? "North" : "South";
    int h_step = (dx > 0) ?  1        : -1;
    int v_step = (dy > 0) ?  num_cols : -num_cols;

    struct PathInfo {
        std::vector<int> router_ids;
        std::vector<int> link_rls;
        int first_outport;
    };
    std::vector<PathInfo> paths;
    paths.reserve(256);

    double em_max  = 0.0, em_min  = std::numeric_limits<double>::max();
    double hci_max = 0.0, hci_min = std::numeric_limits<double>::max();

    for (int mask = 0; mask < (1 << total); ++mask) {
        if (__builtin_popcount(mask) != steps_h) continue;

        PathInfo pi;
        pi.router_ids.reserve(total);
        pi.link_rls.reserve(total);
        pi.first_outport = -1;

        int cur = src_id;
        bool valid = true;

        for (int bit = 0; bit < total && valid; ++bit) {
            bool is_h = (mask >> bit) & 1;
            const std::string& dirn = is_h ? h_dirn : v_dirn;
            int step = is_h ? h_step : v_step;

            Router* r = m_routers[cur];
            int outport_idx = r->get_outport_idx(dirn);
            if (outport_idx < 0) { valid = false; break; }

            NetworkLink* lnk = r->getOutputUnit(outport_idx)->get_out_link();
            int rl  = getLinkRLIndex(lnk);
            int nxt = cur + step;

            pi.router_ids.push_back(nxt);
            pi.link_rls.push_back(rl);
            if (bit == 0) pi.first_outport = outport_idx;

            double em  = getRawEMMTTF(rl, nxt);
            double hci = getRawHCIMTTF(nxt);
            em_max  = std::max(em_max,  em);
            em_min  = std::min(em_min,  em);
            hci_max = std::max(hci_max, hci);
            hci_min = std::min(hci_min, hci);

            cur = nxt;
        }

        if (valid) paths.push_back(std::move(pi));
    }

    if (paths.empty()) {
        Router* r = m_routers[src_id];
        int op = r->get_outport_idx(h_dirn);
        return (op >= 0) ? op : r->get_outport_idx(v_dirn);
    }

    double best_score = -std::numeric_limits<double>::max();
    int best_outport  = paths[0].first_outport;

    for (const auto& pi : paths) {
        double score = computeClothoWnorm(pi.router_ids, pi.link_rls,
                                          em_max, em_min,
                                          hci_max, hci_min);
        if (score > best_score) {
            best_score   = score;
            best_outport = pi.first_outport;
        }
    }
    return best_outport;
}

// Get ID of router connected to a NI.
int
GarnetNetwork::get_router_id(int global_ni, int vnet)
{
    NodeID local_ni = getLocalNodeID(global_ni);

    return m_nis[local_ni]->get_router_id(vnet);
}

void
GarnetNetwork::regStats()
{
    Network::regStats();

    // Packets
    m_packets_received
        .init(m_virtual_networks)
        .name(name() + ".packets_received")
        .flags(statistics::pdf | statistics::total | statistics::nozero |
            statistics::oneline)
        ;

    m_packets_injected
        .init(m_virtual_networks)
        .name(name() + ".packets_injected")
        .flags(statistics::pdf | statistics::total | statistics::nozero |
            statistics::oneline)
        ;

    m_packet_network_latency
        .init(m_virtual_networks)
        .name(name() + ".packet_network_latency")
        .flags(statistics::oneline)
        ;

    m_packet_queueing_latency
        .init(m_virtual_networks)
        .name(name() + ".packet_queueing_latency")
        .flags(statistics::oneline)
        ;

    for (int i = 0; i < m_virtual_networks; i++) {
        m_packets_received.subname(i, csprintf("vnet-%i", i));
        m_packets_injected.subname(i, csprintf("vnet-%i", i));
        m_packet_network_latency.subname(i, csprintf("vnet-%i", i));
        m_packet_queueing_latency.subname(i, csprintf("vnet-%i", i));
    }

    m_avg_packet_vnet_latency
        .name(name() + ".average_packet_vnet_latency")
        .flags(statistics::oneline);
    m_avg_packet_vnet_latency =
        m_packet_network_latency / m_packets_received;

    m_avg_packet_vqueue_latency
        .name(name() + ".average_packet_vqueue_latency")
        .flags(statistics::oneline);
    m_avg_packet_vqueue_latency =
        m_packet_queueing_latency / m_packets_received;

    m_avg_packet_network_latency
        .name(name() + ".average_packet_network_latency");
    m_avg_packet_network_latency =
        sum(m_packet_network_latency) / sum(m_packets_received);

    m_avg_packet_queueing_latency
        .name(name() + ".average_packet_queueing_latency");
    m_avg_packet_queueing_latency
        = sum(m_packet_queueing_latency) / sum(m_packets_received);

    m_avg_packet_latency
        .name(name() + ".average_packet_latency");
    m_avg_packet_latency
        = m_avg_packet_network_latency + m_avg_packet_queueing_latency;

    // Flits
    m_flits_received
        .init(m_virtual_networks)
        .name(name() + ".flits_received")
        .flags(statistics::pdf | statistics::total | statistics::nozero |
            statistics::oneline)
        ;

    m_flits_injected
        .init(m_virtual_networks)
        .name(name() + ".flits_injected")
        .flags(statistics::pdf | statistics::total | statistics::nozero |
            statistics::oneline)
        ;

    m_flit_network_latency
        .init(m_virtual_networks)
        .name(name() + ".flit_network_latency")
        .flags(statistics::oneline)
        ;

    m_flit_queueing_latency
        .init(m_virtual_networks)
        .name(name() + ".flit_queueing_latency")
        .flags(statistics::oneline)
        ;

    for (int i = 0; i < m_virtual_networks; i++) {
        m_flits_received.subname(i, csprintf("vnet-%i", i));
        m_flits_injected.subname(i, csprintf("vnet-%i", i));
        m_flit_network_latency.subname(i, csprintf("vnet-%i", i));
        m_flit_queueing_latency.subname(i, csprintf("vnet-%i", i));
    }

    m_avg_flit_vnet_latency
        .name(name() + ".average_flit_vnet_latency")
        .flags(statistics::oneline);
    m_avg_flit_vnet_latency = m_flit_network_latency / m_flits_received;

    m_avg_flit_vqueue_latency
        .name(name() + ".average_flit_vqueue_latency")
        .flags(statistics::oneline);
    m_avg_flit_vqueue_latency =
        m_flit_queueing_latency / m_flits_received;

    m_avg_flit_network_latency
        .name(name() + ".average_flit_network_latency");
    m_avg_flit_network_latency =
        sum(m_flit_network_latency) / sum(m_flits_received);

    m_avg_flit_queueing_latency
        .name(name() + ".average_flit_queueing_latency");
    m_avg_flit_queueing_latency =
        sum(m_flit_queueing_latency) / sum(m_flits_received);

    m_avg_flit_latency
        .name(name() + ".average_flit_latency");
    m_avg_flit_latency =
        m_avg_flit_network_latency + m_avg_flit_queueing_latency;


    // Hops
    m_avg_hops.name(name() + ".average_hops");
    m_avg_hops = m_total_hops / sum(m_flits_received);

    // Links
    m_total_ext_in_link_utilization
        .name(name() + ".ext_in_link_utilization");
    m_total_ext_out_link_utilization
        .name(name() + ".ext_out_link_utilization");
    m_total_int_link_utilization
        .name(name() + ".int_link_utilization");
    m_average_link_utilization
        .name(name() + ".avg_link_utilization");
    m_average_vc_load
        .init(m_virtual_networks * m_max_vcs_per_vnet)
        .name(name() + ".avg_vc_load")
        .flags(statistics::pdf | statistics::total | statistics::nozero |
            statistics::oneline)
        ;

    // Traffic distribution
    for (int source = 0; source < m_routers.size(); ++source) {
        m_data_traffic_distribution.push_back(
            std::vector<statistics::Scalar *>());
        m_ctrl_traffic_distribution.push_back(
            std::vector<statistics::Scalar *>());

        for (int dest = 0; dest < m_routers.size(); ++dest) {
            statistics::Scalar *data_packets = new statistics::Scalar();
            statistics::Scalar *ctrl_packets = new statistics::Scalar();

            data_packets->name(name() + ".data_traffic_distribution." + "n" +
                    std::to_string(source) + "." + "n" + std::to_string(dest));
            m_data_traffic_distribution[source].push_back(data_packets);

            ctrl_packets->name(name() + ".ctrl_traffic_distribution." + "n" +
                    std::to_string(source) + "." + "n" + std::to_string(dest));
            m_ctrl_traffic_distribution[source].push_back(ctrl_packets);
        }
    }
}

void
GarnetNetwork::collateStats()
{
    RubySystem *rs = params().ruby_system;
    double time_delta = double(curCycle() - rs->getStartCycle());

    for (int i = 0; i < m_networklinks.size(); i++) {
        link_type type = m_networklinks[i]->getType();
        int activity = m_networklinks[i]->getLinkUtilization();

        if (type == EXT_IN_)
            m_total_ext_in_link_utilization += activity;
        else if (type == EXT_OUT_)
            m_total_ext_out_link_utilization += activity;
        else if (type == INT_)
            m_total_int_link_utilization += activity;

        m_average_link_utilization +=
            (double(activity) / time_delta);

        std::vector<unsigned int> vc_load = m_networklinks[i]->getVcLoad();
        for (int j = 0; j < vc_load.size(); j++) {
            m_average_vc_load[j] += ((double)vc_load[j] / time_delta);
        }
    }

    // Ask the routers to collate their statistics
    for (int i = 0; i < m_routers.size(); i++) {
        m_routers[i]->collateStats();
    }
}

void
GarnetNetwork::resetStats()
{
    for (int i = 0; i < m_routers.size(); i++) {
        m_routers[i]->resetStats();
    }
    for (int i = 0; i < m_networklinks.size(); i++) {
        m_networklinks[i]->resetStats();
    }
    for (int i = 0; i < m_creditlinks.size(); i++) {
        m_creditlinks[i]->resetStats();
    }
}

void
GarnetNetwork::print(std::ostream& out) const
{
    out << "[GarnetNetwork]";
}

void
GarnetNetwork::update_traffic_distribution(RouteInfo route)
{
    int src_node = route.src_router;
    int dest_node = route.dest_router;
    int vnet = route.vnet;

    if (m_vnet_type[vnet] == DATA_VNET_)
        (*m_data_traffic_distribution[src_node][dest_node])++;
    else
        (*m_ctrl_traffic_distribution[src_node][dest_node])++;
}

bool
GarnetNetwork::functionalRead(Packet *pkt, WriteMask &mask)
{
    bool read = false;
    for (unsigned int i = 0; i < m_routers.size(); i++) {
        if (m_routers[i]->functionalRead(pkt, mask))
            read = true;
    }

    for (unsigned int i = 0; i < m_nis.size(); ++i) {
        if (m_nis[i]->functionalRead(pkt, mask))
            read = true;
    }

    for (unsigned int i = 0; i < m_networklinks.size(); ++i) {
        if (m_networklinks[i]->functionalRead(pkt, mask))
            read = true;
    }

    for (unsigned int i = 0; i < m_networkbridges.size(); ++i) {
        if (m_networkbridges[i]->functionalRead(pkt, mask))
            read = true;
    }

    return read;
}

uint32_t
GarnetNetwork::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;

    for (unsigned int i = 0; i < m_routers.size(); i++) {
        num_functional_writes += m_routers[i]->functionalWrite(pkt);
    }

    for (unsigned int i = 0; i < m_nis.size(); ++i) {
        num_functional_writes += m_nis[i]->functionalWrite(pkt);
    }

    for (unsigned int i = 0; i < m_networklinks.size(); ++i) {
        num_functional_writes += m_networklinks[i]->functionalWrite(pkt);
    }

    return num_functional_writes;
}

} // namespace garnet
} // namespace ruby
} // namespace gem5
