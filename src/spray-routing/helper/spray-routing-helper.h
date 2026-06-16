#ifndef SPRAY_ROUTING_HELPER_H
#define SPRAY_ROUTING_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "../model/spray-routing.h"

namespace ns3 {

/**
 * \brief Helper to install SprayRouting on a set of nodes.
 *
 * Usage:
 *   SprayRoutingHelper helper;
 *   InternetStackHelper internet;
 *   internet.SetRoutingHelper(helper);
 *   internet.Install(nodes);
 *
 *   // Then add routes manually:
 *   Ptr<SprayRouting> r = helper.GetRouting<SprayRouting>(node->GetObject<Ipv4>());
 *   r->AddRoute(...);
 */
class SprayRoutingHelper : public Ipv4RoutingHelper
{
public:
    SprayRoutingHelper ();
    SprayRoutingHelper *Copy () const override;
    Ptr<Ipv4RoutingProtocol> Create (Ptr<Node> node) const override;

    /** Retrieve the SprayRouting instance from an Ipv4 object. */
    template <class T>
    Ptr<T> GetRouting (Ptr<Ipv4> ipv4) const;

    void SetSprayElephantOnly (bool v) { m_sprayElephantOnly = v; }

private:
    bool m_sprayElephantOnly{true};
};

// ── Template implementation ───────────────────────────────────────────
template <class T>
Ptr<T>
SprayRoutingHelper::GetRouting (Ptr<Ipv4> ipv4) const
{
    Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol ();
    if (proto) {
        Ptr<T> t = DynamicCast<T> (proto);
        if (t) return t;
    }
    return nullptr;
}

} // namespace ns3
#endif // SPRAY_ROUTING_HELPER_H
