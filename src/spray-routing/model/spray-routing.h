#ifndef SPRAY_ROUTING_H
#define SPRAY_ROUTING_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4.h"
#include "ns3/output-stream-wrapper.h"

#include <map>
#include <vector>

namespace ns3 {

/**
 * \brief Multi-path routing protocol implementing per-packet packet spraying.
 *
 * Routing policy
 * --------------
 *  - Mouse flows  : one nexthop is selected via a 5-tuple hash (stable per flow,
 *                   like ECMP).
 *  - Elephant flows: every packet is forwarded on the NEXT equal-cost route in a
 *                   round-robin schedule, spreading load across all paths.
 *
 * Elephant detection: the presence of an ns3::ElephantTag on the packet.
 *
 * Route installation
 * ------------------
 * Routes are added explicitly via AddRoute().  Multiple calls with the same
 * (dest, mask) but different gateways create an ECMP set for that prefix.
 */
class SprayRouting : public Ipv4RoutingProtocol
{
public:
    static TypeId GetTypeId ();
    SprayRouting ();
    ~SprayRouting () override;

    // ── Route management ──────────────────────────────────────────────
    /**
     * Add a unicast route.
     * \param dest      Destination network address
     * \param mask      Destination network mask
     * \param gateway   Next-hop IP (use Ipv4Address("0.0.0.0") for directly connected)
     * \param interface Outgoing interface index on this node
     * \param metric    Route metric (lower = preferred; used to pick ECMP set)
     */
    void AddRoute (Ipv4Address dest, Ipv4Mask mask,
                   Ipv4Address gateway, uint32_t interface,
                   uint32_t metric = 1);

    void SetMode (bool sprayElephantOnly) { m_sprayElephantOnly = sprayElephantOnly; }

    // ── Ipv4RoutingProtocol interface ─────────────────────────────────
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

    Ptr<Ipv4>               m_ipv4;
    std::vector<RouteEntry> m_routes;
    bool                    m_sprayElephantOnly{true};

    // Per-destination round-robin counter (used for spray)
    mutable std::map<Ipv4Address, uint32_t> m_rrCounter;

    // ── Internal helpers ──────────────────────────────────────────────
    Ptr<Ipv4Route> LookupRoute (Ipv4Address dest, bool isElephant,
                                Ptr<NetDevice> oif = nullptr) const;

    bool IsLocalAddress (Ipv4Address addr) const;
    Ipv4Address GetLocalSourceAddress (uint32_t iface) const;

    static uint32_t HashFlow (Ipv4Address src, Ipv4Address dst,
                               uint16_t sport, uint16_t dport);
};

} // namespace ns3
#endif // SPRAY_ROUTING_H
