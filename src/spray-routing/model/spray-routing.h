#ifndef SPRAY_ROUTING_H
#define SPRAY_ROUTING_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/random-variable-stream.h"

#include <vector>

namespace ns3 {

/**
 * \brief Multi-path routing protocol implementing per-packet packet spraying.
 *
 * Routing policy
 * --------------
 *  - Mouse flows   : next hop chosen by a 5-TUPLE HASH (src IP, dst IP,
 *                    src port, dst port, protocol).  Deterministic per flow,
 *                    so every packet of a flow takes the same path and there
 *                    is no reordering.  This is what real ECMP hardware does.
 *  - Elephant flows: next hop chosen UNIFORMLY AT RANDOM, independently for
 *                    each packet.  Spreads load evenly across all equal-cost
 *                    paths in expectation, at the cost of packet reordering.
 *
 * Randomness comes from ns-3's UniformRandomVariable, so runs are reproducible
 * and controlled by RngSeedManager (the --seed argument).
 *
 * Elephant detection: IP TOS != 0, or presence of an ns3::ElephantTag.
 */
class SprayRouting : public Ipv4RoutingProtocol
{
public:
    static TypeId GetTypeId ();
    SprayRouting ();
    ~SprayRouting () override;

    /**
     * Add a unicast route.  Multiple calls with the same (dest, mask) and the
     * same metric form an equal-cost (ECMP) set for that prefix.
     */
    void AddRoute (Ipv4Address dest, Ipv4Mask mask,
                   Ipv4Address gateway, uint32_t interface,
                   uint32_t metric = 1);

    /** If true (default), only elephants are sprayed; mice always use the hash. */
    void SetMode (bool sprayElephantOnly) { m_sprayElephantOnly = sprayElephantOnly; }

    /** Fix the RNG stream so runs are reproducible. */
    int64_t AssignStreams (int64_t stream);

    // -- Ipv4RoutingProtocol interface --------------------------------
    Ptr<Ipv4Route> RouteOutput (Ptr<Packet> p, const Ipv4Header &header,
                                Ptr<NetDevice> oif,
                                Socket::SocketErrno &sockerr) override;

    bool RouteInput  (Ptr<const Packet> p, const Ipv4Header &header,
                      Ptr<const NetDevice> idev,
                      const UnicastForwardCallback   &ucb,
                      const MulticastForwardCallback &mcb,
                      const LocalDeliverCallback     &lcb,
                      const ErrorCallback            &ecb) override;

    void NotifyInterfaceUp   (uint32_t iface) override;
    void NotifyInterfaceDown (uint32_t iface) override;
    void NotifyAddAddress    (uint32_t iface, Ipv4InterfaceAddress addr) override;
    void NotifyRemoveAddress (uint32_t iface, Ipv4InterfaceAddress addr) override;
    void SetIpv4             (Ptr<Ipv4> ipv4) override;
    void PrintRoutingTable   (Ptr<OutputStreamWrapper> stream,
                              Time::Unit unit = Time::S) const override;

private:
    struct RouteEntry {
        Ipv4Address dest;
        Ipv4Mask    mask;
        Ipv4Address gateway;
        uint32_t    interface;
        uint32_t    metric;
    };

    /** The 5-tuple identifying a flow. */
    struct FlowTuple {
        Ipv4Address src;
        Ipv4Address dst;
        uint16_t    sport;
        uint16_t    dport;
        uint8_t     proto;
    };

    Ptr<Ipv4>                  m_ipv4;
    std::vector<RouteEntry>    m_routes;
    bool                       m_sprayElephantOnly{true};
    Ptr<UniformRandomVariable> m_rand;   // drives random spray

    Ptr<Ipv4Route> LookupRoute (const FlowTuple &ft, bool isElephant) const;
    bool           IsLocalAddress (Ipv4Address addr) const;

    /** Pull src/dst ports from the transport header (0 if unavailable). */
    static void     ExtractPorts (Ptr<const Packet> p, uint8_t proto,
                                  uint16_t &sport, uint16_t &dport);

    /** FNV-1a over the 5-tuple, plus a final avalanche mix. */
    static uint32_t HashFlow (const FlowTuple &ft);
};

} // namespace ns3
#endif // SPRAY_ROUTING_H