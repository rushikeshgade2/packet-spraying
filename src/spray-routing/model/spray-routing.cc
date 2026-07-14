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

TypeId
SprayRouting::GetTypeId ()
{
    static TypeId tid = TypeId ("ns3::SprayRouting")
        .SetParent<Ipv4RoutingProtocol> ()
        .SetGroupName ("SprayRouting")
        .AddConstructor<SprayRouting> ();
    return tid;
}

SprayRouting::SprayRouting ()
{
    m_rand = CreateObject<UniformRandomVariable> ();
}

SprayRouting::~SprayRouting () = default;

int64_t
SprayRouting::AssignStreams (int64_t stream)
{
    m_rand->SetStream (stream);
    return 1;
}

// -- Route management -------------------------------------------------
void
SprayRouting::AddRoute (Ipv4Address dest, Ipv4Mask mask,
                        Ipv4Address gateway, uint32_t interface,
                        uint32_t metric)
{
    NS_LOG_FUNCTION (this << dest << mask << gateway << interface << metric);
    m_routes.push_back ({dest.CombineMask (mask), mask, gateway, interface, metric});
}

// -- 5-tuple hash (FNV-1a + avalanche) --------------------------------
uint32_t
SprayRouting::HashFlow (const FlowTuple &ft)
{
    uint32_t h = 2166136261u;                  // FNV offset basis

    auto mix8 = [&h] (uint8_t b) {
        h ^= b;
        h *= 16777619u;                        // FNV prime
    };
    auto mix16 = [&mix8] (uint16_t v) {
        mix8 (static_cast<uint8_t> (v & 0xFF));
        mix8 (static_cast<uint8_t> ((v >> 8) & 0xFF));
    };
    auto mix32 = [&mix8] (uint32_t v) {
        mix8 (static_cast<uint8_t> (v & 0xFF));
        mix8 (static_cast<uint8_t> ((v >>  8) & 0xFF));
        mix8 (static_cast<uint8_t> ((v >> 16) & 0xFF));
        mix8 (static_cast<uint8_t> ((v >> 24) & 0xFF));
    };

    mix32 (ft.src.Get ());
    mix32 (ft.dst.Get ());
    mix16 (ft.sport);
    mix16 (ft.dport);
    mix8  (ft.proto);

    // Final avalanche so that neighbouring IPs/ports land far apart.
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// -- Pull ports out of the transport header ---------------------------
void
SprayRouting::ExtractPorts (Ptr<const Packet> p, uint8_t proto,
                            uint16_t &sport, uint16_t &dport)
{
    sport = 0;
    dport = 0;
    if (!p)
    {
        return;
    }

    if (proto == 6)          // TCP
    {
        TcpHeader th;
        if (p->PeekHeader (th) > 0)
        {
            sport = th.GetSourcePort ();
            dport = th.GetDestinationPort ();
        }
    }
    else if (proto == 17)    // UDP
    {
        UdpHeader uh;
        if (p->PeekHeader (uh) > 0)
        {
            sport = uh.GetSourcePort ();
            dport = uh.GetDestinationPort ();
        }
    }
}

// -- Route lookup -----------------------------------------------------
Ptr<Ipv4Route>
SprayRouting::LookupRoute (const FlowTuple &ft, bool isElephant) const
{
    NS_LOG_FUNCTION (this << ft.dst << isElephant);

    // Longest-prefix match: collect every route with the longest match.
    uint32_t longestPrefix = 0;
    std::vector<const RouteEntry *> candidates;

    for (const auto &r : m_routes)
    {
        if (ft.dst.CombineMask (r.mask) == r.dest)
        {
            uint32_t pfxLen = r.mask.GetPrefixLength ();
            if (pfxLen > longestPrefix)
            {
                longestPrefix = pfxLen;
                candidates.clear ();
            }
            if (pfxLen == longestPrefix)
            {
                candidates.push_back (&r);
            }
        }
    }

    if (candidates.empty ())
    {
        NS_LOG_WARN ("No route for " << ft.dst);
        return nullptr;
    }

    // Keep only the lowest-metric routes: that is the equal-cost set.
    uint32_t minMetric = std::numeric_limits<uint32_t>::max ();
    for (const auto *c : candidates)
    {
        minMetric = std::min (minMetric, c->metric);
    }
    std::vector<const RouteEntry *> ecmpSet;
    for (const auto *c : candidates)
    {
        if (c->metric == minMetric)
        {
            ecmpSet.push_back (c);
        }
    }

    const RouteEntry *selected = nullptr;

    if (isElephant && m_sprayElephantOnly && ecmpSet.size () > 1)
    {
        // ---- PACKET SPRAYING: uniform random path, per packet ----
        uint32_t idx = m_rand->GetInteger (0, ecmpSet.size () - 1);
        selected = ecmpSet[idx];
        NS_LOG_INFO ("Spray: random path " << idx << "/" << ecmpSet.size ()
                     << " -> " << ft.dst);
    }
    else
    {
        // ---- ECMP: 5-tuple hash, stable for the lifetime of the flow ----
        uint32_t h = HashFlow (ft);
        selected = ecmpSet[h % ecmpSet.size ()];
        NS_LOG_INFO ("ECMP hash: path " << (h % ecmpSet.size ()) << "/"
                     << ecmpSet.size () << " -> " << ft.dst);
    }

    Ptr<Ipv4Route> route = Create<Ipv4Route> ();
    route->SetDestination (ft.dst);
    route->SetGateway (selected->gateway);
    route->SetOutputDevice (m_ipv4->GetNetDevice (selected->interface));
    route->SetSource (m_ipv4->GetAddress (selected->interface, 0).GetLocal ());
    return route;
}

// -- RouteOutput: packet originated by a local application ------------
Ptr<Ipv4Route>
SprayRouting::RouteOutput (Ptr<Packet> p, const Ipv4Header &header,
                           Ptr<NetDevice> /* oif */,
                           Socket::SocketErrno &sockerr)
{
    NS_LOG_FUNCTION (this << header.GetDestination ());

    if (IsLocalAddress (header.GetDestination ()))
    {
        sockerr = Socket::ERROR_NOTERROR;
        return nullptr;                 // let the stack loop it back
    }

    FlowTuple ft;
    ft.src   = header.GetSource ();
    ft.dst   = header.GetDestination ();
    ft.proto = header.GetProtocol ();
    ExtractPorts (p, ft.proto, ft.sport, ft.dport);

    bool isElephant = (header.GetTos () != 0);
    if (p && !isElephant)
    {
        ElephantTag tag;
        isElephant = p->PeekPacketTag (tag);
    }

    Ptr<Ipv4Route> route = LookupRoute (ft, isElephant);
    if (!route)
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }
    sockerr = Socket::ERROR_NOTERROR;
    return route;
}

// -- RouteInput: packet arriving to be forwarded ----------------------
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

    if (IsLocalAddress (dest))
    {
        lcb (p, header, m_ipv4->GetInterfaceForDevice (idev));
        return true;
    }

    if (dest.IsMulticast ())
    {
        return false;
    }

    FlowTuple ft;
    ft.src   = header.GetSource ();
    ft.dst   = dest;
    ft.proto = header.GetProtocol ();
    ExtractPorts (p, ft.proto, ft.sport, ft.dport);

    bool isElephant = (header.GetTos () != 0);
    if (!isElephant)
    {
        ElephantTag tag;
        Ptr<Packet> copy = p->Copy ();
        isElephant = copy->PeekPacketTag (tag);
    }

    Ptr<Ipv4Route> route = LookupRoute (ft, isElephant);
    if (route)
    {
        ucb (route, p, header);
        return true;
    }

    ecb (p, header, Socket::ERROR_NOROUTETOHOST);
    return false;
}

// -- Helpers ----------------------------------------------------------
bool
SprayRouting::IsLocalAddress (Ipv4Address addr) const
{
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces (); ++i)
    {
        for (uint32_t j = 0; j < m_ipv4->GetNAddresses (i); ++j)
        {
            if (m_ipv4->GetAddress (i, j).GetLocal () == addr)
            {
                return true;
            }
        }
    }
    return false;
}

void SprayRouting::NotifyInterfaceUp   (uint32_t)                       {}
void SprayRouting::NotifyInterfaceDown (uint32_t)                       {}
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
    for (const auto &r : m_routes)
    {
        os << "  " << r.dest << "/" << r.mask.GetPrefixLength ()
           << " via " << r.gateway
           << " dev " << r.interface
           << " metric " << r.metric << "\n";
    }
}

} // namespace ns3