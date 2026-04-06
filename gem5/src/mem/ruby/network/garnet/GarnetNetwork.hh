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


#ifndef __MEM_RUBY_NETWORK_GARNET_0_GARNETNETWORK_HH__
#define __MEM_RUBY_NETWORK_GARNET_0_GARNETNETWORK_HH__

#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <unordered_map>
#include <random>

#include "mem/ruby/network/Network.hh"
#include "mem/ruby/network/fault_model/FaultModel.hh"
#include "mem/ruby/network/garnet/CommonTypes.hh"
#include "params/GarnetNetwork.hh"

namespace gem5
{

namespace ruby
{

class FaultModel;
class NetDest;

namespace garnet
{

class NetworkInterface;
class Router;
class NetworkLink;
class NetworkBridge;
class CreditLink;

// Per-router process-variation data (computed once at init, never changes)
struct RouterPVData {
    double delta_vth;   // ΔVth = rand + sys component (Volts)
    double delta_leff;  // ΔLeff = rand + sys component (metres)
    double isub;        // precomputed I_sub (A); used in HCI MTTF
};

class GarnetNetwork : public Network
{
  public:
    typedef GarnetNetworkParams Params;
    GarnetNetwork(const Params &p);
    ~GarnetNetwork();

    void init();

    const char *garnetVersion = "3.0";

    // Configuration (set externally)

    // for 2D topology
    int getNumRows() const { return m_num_rows; }
    int getNumCols() { return m_num_cols; }

    // for network
    uint32_t getNiFlitSize() const { return m_ni_flit_size; }
    uint32_t getBuffersPerDataVC() { return m_buffers_per_data_vc; }
    uint32_t getBuffersPerCtrlVC() { return m_buffers_per_ctrl_vc; }
    int getRoutingAlgorithm() const { return m_routing_algorithm; }

    // RL accessors
    Router*      getRouter(int id)      { return m_routers[id]; }
    int          getLinkRLIndex(NetworkLink* lnk) const;

    // RL agent interface
    double getRLEpsilon()   const { return m_rl_epsilon; }
    Cycles getRLWarmup()    const { return m_rl_warmup_cycles; }
    std::vector<std::vector<std::vector<double>>>& getLARETheta() { return m_lare_theta; }

    // Reproducible epsilon-greedy sampling via member RNG
    double sampleRandom() { return m_real_dist(m_rng); }
    int    sampleAction() { return m_action_dist(m_rng); }

    bool isFaultModelEnabled() const { return m_enable_fault_model; }
    FaultModel* fault_model;


    // Internal configuration
    bool isVNetOrdered(int vnet) const { return m_ordered[vnet]; }
    VNET_type
    get_vnet_type(int vnet)
    {
        return m_vnet_type[vnet];
    }
    int getNumRouters();
    int get_router_id(int ni, int vnet);


    // Methods used by Topology to setup the network
    void makeExtOutLink(SwitchID src, NodeID dest, BasicLink* link,
                     std::vector<NetDest>& routing_table_entry);
    void makeExtInLink(NodeID src, SwitchID dest, BasicLink* link,
                    std::vector<NetDest>& routing_table_entry);
    void makeInternalLink(SwitchID src, SwitchID dest, BasicLink* link,
                          std::vector<NetDest>& routing_table_entry,
                          PortDirection src_outport_dirn,
                          PortDirection dest_inport_dirn);

    bool functionalRead(Packet *pkt, WriteMask &mask);
    //! Function for performing a functional write. The return value
    //! indicates the number of messages that were written.
    uint32_t functionalWrite(Packet *pkt);

    // Stats
    void collateStats();
    void regStats();
    void resetStats();
    void print(std::ostream& out) const;

    // increment counters
    void increment_injected_packets(int vnet) { m_packets_injected[vnet]++; }
    void increment_received_packets(int vnet) { m_packets_received[vnet]++; }

    void
    increment_packet_network_latency(Tick latency, int vnet)
    {
        m_packet_network_latency[vnet] += latency;
    }

    void
    increment_packet_queueing_latency(Tick latency, int vnet)
    {
        m_packet_queueing_latency[vnet] += latency;
    }

    void increment_injected_flits(int vnet) { m_flits_injected[vnet]++; }
    void increment_received_flits(int vnet) { m_flits_received[vnet]++; }

    void
    increment_flit_network_latency(Tick latency, int vnet)
    {
        m_flit_network_latency[vnet] += latency;
    }

    void
    increment_flit_queueing_latency(Tick latency, int vnet)
    {
        m_flit_queueing_latency[vnet] += latency;
    }

    void
    increment_total_hops(int hops)
    {
        m_total_hops += hops;
    }

    void update_traffic_distribution(RouteInfo route);
    int getNextPacketID() { return m_next_packet_id++; }

    // Periodic wear/temperature state updates (called from RoutingUnit)
    void maybeUpdateWearoutState(Cycles cur);

    // MTTF computation helpers
    double getRawEMMTTF(int rl_link_idx, int dst_router_id) const;
    double getRawHCIMTTF(int router_id) const;
    int    getCongBin(int router_id, int outport) const;  // sums over all vnets
    int    getWearBin(double mttf_norm) const;
    double computeCWeight(double x, double x_max, double x_min) const;

    // LARE coefficient persistence
    void saveLARETheta() const;
    void loadLARETheta();

    // LARE Q-value: dot product theta[router_id][outport_idx] · features
    double computeQLARE(int router_id, int outport_idx,
                        const std::vector<double>& features) const;

    // AF output at simulation end
    void writeMTTFReport() const;

    // LARE dimensionality constant
    static const int LARE_NUM_FEATURES = 9;

    // RL hyperparameter constants
    static constexpr double RL_ALPHA     = 0.1;
    static constexpr double RL_GAMMA     = 0.95;
    static constexpr double RL_W_WEAR    = 0.7;
    static constexpr double RL_W_LAT     = 0.15;
    static constexpr double RL_W_BALANCE = 0.15;
    static constexpr double RL_CLO_W     = 8.0;
    static constexpr double RL_ALPHA_MIN = 1e-6;

    // Failure-model constants
    static constexpr double EA_EM    = 0.9;    // eV
    static constexpr double EA_HCI   = 0.3;    // eV
    static constexpr double HCI_N    = 1.5;
    static constexpr double KB       = 8.617e-5; // eV/K

    // I_sub technology constants
    static constexpr double ISUB_VGS      = 1.0;
    static constexpr double ISUB_VDS      = 1.0;
    static constexpr double ISUB_ECR      = 4e6;
    static constexpr double ISUB_TOX      = 1.2e-9;
    static constexpr double ISUB_XJ       = 40e-9;
    static constexpr double ISUB_C2       = 1.0;
    static constexpr double ISUB_IDS      = 1e-4;
    static constexpr double ISUB_PHI_I_EV = 3.7;   // eV (q cancels)
    static constexpr double ISUB_LAMBDA_E = 9e-9;
    static constexpr double ISUB_VTH0     = 0.179;
    static constexpr double ISUB_LEFF0    = 14.4e-9;

    // Process-variation parameters
    static constexpr double PV_SIGMA_VTH_RAND  = 0.063;
    static constexpr double PV_SIGMA_VTH_SYS   = 0.063;
    static constexpr double PV_SIGMA_LEFF_RAND = 0.032;
    static constexpr double PV_SIGMA_LEFF_SYS  = 0.032;
    static constexpr double PV_PHI             = 0.5;

    // Temperature map constants
    static constexpr double TEMP_MIN_K  = 338.0;
    static constexpr double TEMP_MAX_K  = 358.0;
    static constexpr double TEMP_MEAN_K = 348.0;
    static constexpr double TEMP_STD_K  = 3.3;  // 3σ ≈ ±10K → [338,358]K

  protected:
    // Configuration
    int m_num_rows;
    int m_num_cols;
    uint32_t m_ni_flit_size;
    uint32_t m_max_vcs_per_vnet;
    uint32_t m_buffers_per_ctrl_vc;
    uint32_t m_buffers_per_data_vc;
    int m_routing_algorithm;
    bool m_enable_fault_model;

    // Statistical variables
    statistics::Vector m_packets_received;
    statistics::Vector m_packets_injected;
    statistics::Vector m_packet_network_latency;
    statistics::Vector m_packet_queueing_latency;

    statistics::Formula m_avg_packet_vnet_latency;
    statistics::Formula m_avg_packet_vqueue_latency;
    statistics::Formula m_avg_packet_network_latency;
    statistics::Formula m_avg_packet_queueing_latency;
    statistics::Formula m_avg_packet_latency;

    statistics::Vector m_flits_received;
    statistics::Vector m_flits_injected;
    statistics::Vector m_flit_network_latency;
    statistics::Vector m_flit_queueing_latency;

    statistics::Formula m_avg_flit_vnet_latency;
    statistics::Formula m_avg_flit_vqueue_latency;
    statistics::Formula m_avg_flit_network_latency;
    statistics::Formula m_avg_flit_queueing_latency;
    statistics::Formula m_avg_flit_latency;

    statistics::Scalar m_total_ext_in_link_utilization;
    statistics::Scalar m_total_ext_out_link_utilization;
    statistics::Scalar m_total_int_link_utilization;
    statistics::Scalar m_average_link_utilization;
    statistics::Vector m_average_vc_load;

    statistics::Scalar  m_total_hops;
    statistics::Formula m_avg_hops;

    std::vector<std::vector<statistics::Scalar *>> m_data_traffic_distribution;
    std::vector<std::vector<statistics::Scalar *>> m_ctrl_traffic_distribution;

  private:
    GarnetNetwork(const GarnetNetwork& obj);
    GarnetNetwork& operator=(const GarnetNetwork& obj);

    std::vector<VNET_type > m_vnet_type;
    std::vector<Router *> m_routers;   // All Routers in Network
    std::vector<NetworkLink *> m_networklinks; // All flit links in the network
    std::vector<NetworkBridge *> m_networkbridges; // All network bridges
    std::vector<CreditLink *> m_creditlinks; // All credit links in the network
    std::vector<NetworkInterface *> m_nis;   // All NI's in Network
    int m_next_packet_id; // static vairable for packet id allocation

    // RL / wear-aware routing state
    std::vector<RouterPVData>  m_router_pv;
    std::vector<double>        m_router_temp_K;
    std::vector<double>        m_router_util;       // windowed (per 1000 cyc)
    std::vector<double>        m_router_util_prev;  // crossbar snapshot
    std::vector<double>        m_link_util;         // windowed INT_ link util
    std::vector<double>        m_link_util_prev;    // raw util snapshot

    std::vector<NetworkLink*>              m_int_links;           // INT_ links only
    std::vector<int>                       m_int_link_src_router; // src router per INT_ link
    std::vector<int>                       m_int_link_dst_router; // dst router per INT_ link
    std::unordered_map<NetworkLink*, int>  m_link_ptr_to_rl_idx;  // link* -> index in m_int_links

    std::vector<double> m_chol_L; // N×N Cholesky L (row-major) for spatial correlation

    Cycles      m_last_util_update;
    Cycles      m_last_temp_update;
    double      m_rl_epsilon;
    Cycles      m_rl_warmup_cycles;
    std::string m_lare_theta_file;
    std::string m_mttf_output_file;

    // [router_id][outport_idx][feature_idx], initialized to 1.0
    // Second dim sized to max outports across all routers (≤5 for an 8×8 mesh)
    std::vector<std::vector<std::vector<double>>> m_lare_theta;

    // Seeded RNG for epsilon-greedy sampling (persists across routing decisions)
    std::mt19937                         m_rng;
    std::uniform_real_distribution<double> m_real_dist;
    std::uniform_int_distribution<int>     m_action_dist;
};

inline std::ostream&
operator<<(std::ostream& out, const GarnetNetwork& obj)
{
    obj.print(out);
    out << std::flush;
    return out;
}

} // namespace garnet
} // namespace ruby
} // namespace gem5

#endif //__MEM_RUBY_NETWORK_GARNET_0_GARNETNETWORK_HH__
