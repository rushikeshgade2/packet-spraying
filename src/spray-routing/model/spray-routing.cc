#include "spray-routing.h"
#include "elephant-tag.h"

#include "ns3/log.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-route.h"
#include "ns3/net-device.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/socket.h"
#include "ns3/packet.h"

#include <algorithm>
#include <limits>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SprayRouting");
NS_OBJECT_ENSURE_REGISTERED (SprayRouting);

// ── TypeId ───────────────────────────────────────────────────────────
TypeId
SprayRouting::GetTypeId ()
{
    static TypeId tid = TypeId ("ns3::SprayRouting")
        .SetParent<Ipv4RoutingProtocol> ()
        .SetGroupName ("SprayRouting")
        .AddConstructor<SprayRouting> ();
    return tid;
}

SprayRouting::SprayRouting ()  = default;
SprayRouting::~SprayRouting () = default;

// ── Route management ─────────────────────────────────────────────────
void
SprayRouting::AddRoute (Ipv4Address dest, Ipv4Mask mask,
                        Ipv4Address gateway, uint32_t interface,
                        uint32_t metric)
{
    NS_LOG_FUNCTION (this << dest << mask << gateway << interface << metric);
    m_routes.push_back ({dest.CombineMask(mask), mask, gateway, interface, metric});
}

// ── Internal lookup ──────────────────────────────────────────────────
Ptr<Ipv4Route>
SprayRouting::LookupRoute (Ipv4Address dest, bool isElephant,
                            Ptr<NetDevice> /* oif */) const
{
    NS_LOG_FUNCTION (this << dest << isElephant);

    // ── Longest-prefix match: collect all routes that match ──────────
    uint32_t longestPrefix = 0;
    std::vector<const RouteEntry *> candidates;

    for (const auto &r : m_routes) {
        if (dest.CombineMask (r.mask) == r.dest) {
            uint32_t pfxLen = r.mask.GetPrefixLength ();
            if (pfxLen > longestPrefix) {
                longestPrefix = pfxLen;
                candidates.clear ();
            }
            if (pfxLen == longestPrefix) {
                candidates.push_back (&r);
            }
        }
    }

    if (candidates.empty ()) {
        NS_LOG_WARN ("No route for " << dest);
        return nullptr;
    }

    // ── Keep only lowest-metric routes in the ECMP set ───────────────
    uint32_t minMetric = std::numeric_limits<uint32_t>::max ();
    for (const auto *c : candidates) {
        minMetric = std::min (minMetric, c->metric);
    }
    std::vector<const RouteEntry *> ecmpSet;
    for (const auto *c : candidates) {
        if (c->metric == minMetric) ecmpSet.push_back (c);
    }

    // ── Select route ─────────────────────────────────────────────────
    const RouteEntry *selected = nullptr;
    if (isElephant && m_sprayElephantOnly && ecmpSet.size () > 1) {
        // Round-robin per destination
        uint32_t &ctr = m_rrCounter[dest];
        selected = ecmpSet[ctr % ecmpSet.size ()];
        ++ctr;
        NS_LOG_INFO ("Elephant spray: selected path " << (ctr-1) % ecmpSet.size ()
                     << " of " << ecmpSet.size () << " to " << dest);
    } else {
        // Hash-based (stable per destination — approximates per-flow hash)
        uint32_t h = (dest.Get () * 2654435761u) >> 16;
        selected = ecmpSet[h % ecmpSet.size ()];
    }

    // ── Build Ipv4Route ───────────────────────────────────────────────
    Ptr<Ipv4Route> route = Create<Ipv4Route> ();
    route->SetDestination (dest);
    route->SetGateway (selected->gateway);
    route->SetOutputDevice (m_ipv4->GetNetDevice (selected->interface));
    route->SetSource (m_ipv4->GetAddress (selected->interface, 0).GetLocal ());
    return route;
}

// ── RouteOutput (called by local applications) ────────────────────────
Ptr<Ipv4Route>
SprayRouting::RouteOutput (Ptr<Packet> p, const Ipv4Header &header,
                            Ptr<NetDevice> oif,
                            Socket::SocketErrno &sockerr)
{
    NS_LOG_FUNCTION (this << header.GetDestination ());

    // Local delivery?
    if (IsLocalAddress (header.GetDestination ())) {
        // Let the stack handle it — return nullptr and no error
        sockerr = Socket::ERROR_NOTERROR;
        return nullptr;
    }

    // Is this packet tagged as elephant?
    bool isElephant = false;
    if (p) {
        ElephantTag tag;
        isElephant = p->PeekPacketTag (tag);
    }

    Ptr<Ipv4Route> route = LookupRoute (header.GetDestination (), isElephant, oif);
    if (!route) {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }
    sockerr = Socket::ERROR_NOTERROR;
    return route;
}

// ── RouteInput (called by stack when a packet arrives for forwarding) ─
bool
SprayRouting::RouteInput (Ptr<const Packet> p, const Ipv4Header &header,
                           Ptr<const NetDevice> idev,
                           const UnicastForwardCallback   &ucb,
                           const MulticastForwardCallback & /*mcb*/,
                           const LocalDeliverCallback     &lcb,
                           const ErrorCallback            &ecb)
{
    NS_LOG_FUNCTION (this << header.GetDestination ());

    Ipv4Address dest = header.GetDestination ();

    // ── Local delivery ───────────────────────────────────────────────
    if (IsLocalAddress (dest)) {
        lcb (p, header, m_ipv4->GetInterfaceForDevice (idev));
        return true;
    }

    // ── Loopback / multicast: not handled here ────────────────────────
    if (dest.IsMulticast ()) {
        return false;
    }

    // ── Forward ──────────────────────────────────────────────────────
    bool isElephant = false;
    {
        ElephantTag tag;
        // PeekPacketTag works on const Packet via copy
        Ptr<Packet> copy = p->Copy ();
        isElephant = copy->PeekPacketTag (tag);
    }

    Ptr<Ipv4Route> route = LookupRoute (dest, isElephant);
    if (route) {
        ucb (route, p, header);
        return true;
    }

    ecb (p, header, Socket::ERROR_NOROUTETOHOST);
    return false;
}

// ── Helpers ──────────────────────────────────────────────────────────
bool
SprayRouting::IsLocalAddress (Ipv4Address addr) const
{
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces (); ++i) {
        for (uint32_t j = 0; j < m_ipv4->GetNAddresses (i); ++j) {
            if (m_ipv4->GetAddress (i, j).GetLocal () == addr) return true;
        }
    }
    return false;
}

// ── Ipv4RoutingProtocol notification stubs ────────────────────────────
void SprayRouting::NotifyInterfaceUp   (uint32_t)                   {}
void SprayRouting::NotifyInterfaceDown (uint32_t)                   {}
void SprayRouting::NotifyAddAddress    (uint32_t, Ipv4InterfaceAddress) {}
void SprayRouting::NotifyRemoveAddress (uint32_t, Ipv4InterfaceAddress) {}

void
SprayRouting::SetIpv4 (Ptr<Ipv4> ipv4)
{
    m_ipv4 = ipv4;
}

void
SprayRouting::PrintRoutingTable (Ptr<OutputStreamWrapper> stream,
                                  Time::Unit /* unit */) const
{
    std::ostream &os = *stream->GetStream ();
    os << "SprayRouting table (" << m_routes.size () << " entries):\n";
    for (const auto &r : m_routes) {
        os << "  " << r.dest << "/" << r.mask.GetPrefixLength ()
           << " via " << r.gateway
           << " dev " << r.interface
           << " metric " << r.metric << "\n";
    }
}

} // namespace ns3
